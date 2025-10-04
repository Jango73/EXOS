
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    Internet Protocol version 4 (IPv4)

\************************************************************************/

#include "network/IPv4.h"
#include "ARPContext.h"
#include "Console.h"
#include "Device.h"
#include "Heap.h"
#include "ID.h"
#include "network/ARP.h"
#include "Log.h"
#include "Memory.h"
#include "System.h"
#include "Notification.h"
#include "Clock.h"
#include "NetworkChecksum.h"

/************************************************************************/
// Global variables

static U16 NextID = 1; // IPv4 packet identification counter

/************************************************************************/
// Configuration

#define IPV4_MAX_PROTOCOLS 256
#define IPV4_DEFAULT_TTL 64

/************************************************************************/
// Global state

LPIPV4_CONTEXT IPv4_GetContext(LPDEVICE Device) {
    LPIPV4_CONTEXT Context = NULL;

    if (Device == NULL) return NULL;

    LockMutex(&(Device->Mutex), INFINITY);
    Context = (LPIPV4_CONTEXT)GetDeviceContext(Device, KOID_IPV4);
    UnlockMutex(&(Device->Mutex));

    return Context;
}

/************************************************************************/

/**
 * @brief Calculates the IPv4 header checksum.
 *
 * @param Header Pointer to the IPv4 header.
 * @return Calculated checksum in network byte order.
 */
U16 IPv4_CalculateChecksum(IPV4_HEADER* Header) {
    U32 HeaderLength = (Header->VersionIHL & 0x0F) * 4;
    U16 SavedChecksum = Header->HeaderChecksum;
    Header->HeaderChecksum = 0;
    U16 ComputedChecksum = NetworkChecksum_Calculate((const U8*)Header, HeaderLength);
    Header->HeaderChecksum = SavedChecksum;
    return ComputedChecksum;
}

/************************************************************************/

/**
 * @brief Validates the IPv4 header checksum.
 *
 * @param Header Pointer to the IPv4 header.
 * @return 1 if checksum is valid, 0 otherwise.
 */
int IPv4_ValidateChecksum(IPV4_HEADER* Header) {
    U32 HeaderLength = (Header->VersionIHL & 0x0F) * 4;
    U16 ReceivedChecksum = Header->HeaderChecksum;
    Header->HeaderChecksum = 0;

    DEBUG(TEXT("[IPv4_ValidateChecksum] Validating IPv4 header checksum"));
    DEBUG(TEXT("[IPv4_ValidateChecksum] Header length: %u bytes, received checksum: %x"),
          HeaderLength, Ntohs(ReceivedChecksum));

    // Calculate checksum of entire header (including checksum field)
    U16 CalculatedChecksum = NetworkChecksum_Calculate((const U8*)Header, HeaderLength);

    // Compare calculated checksum with received checksum
    BOOL IsValid = (CalculatedChecksum == ReceivedChecksum);

    DEBUG(TEXT("[IPv4_ValidateChecksum] Calculated checksum: %x, valid: %s"),
          Ntohs(CalculatedChecksum), IsValid ? "YES" : "NO");

    Header->HeaderChecksum = ReceivedChecksum;

    return IsValid ? 1 : 0;
}

/************************************************************************/

/**
 * @brief Sends a raw Ethernet frame.
 *
 * @param Data   Pointer to frame data.
 * @param Length Frame length in bytes.
 * @return 1 on success, otherwise 0.
 */
static int IPv4_SendEthernetFrame(LPIPV4_CONTEXT Context, const U8* Data, U32 Length) {
    NETWORKSEND Send;
    LPDEVICE Device;
    int Result = 0;

    if (Context == NULL || Context->Device == NULL) return 0;

    Device = Context->Device;

    LockMutex(&(Device->Mutex), INFINITY);

    Send.Device = (LPPCI_DEVICE)Device;
    Send.Data = Data;
    Send.Length = Length;
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        SAFE_USE_VALID_ID(((LPPCI_DEVICE)Device)->Driver, KOID_DRIVER) {
            Result = (((LPPCI_DEVICE)Device)->Driver->Command(DF_NT_SEND, (U32)(LPVOID)&Send) == DF_ERROR_SUCCESS) ? 1 : 0;
        }
    }

    UnlockMutex(&(Device->Mutex));
    return Result;
}

/************************************************************************/
// Packet processing

/**
 * @brief Processes an incoming IPv4 packet.
 *
 * @param Packet      Pointer to the IPv4 header.
 * @param TotalLength Total packet length including header.
 */
static void IPv4_HandlePacket(LPIPV4_CONTEXT Context, const IPV4_HEADER* Packet, U32 TotalLength) {
    if (Context == NULL) return;
    U8 Version = (Packet->VersionIHL >> 4) & 0x0F;
    U8 IHL = Packet->VersionIHL & 0x0F;
    U16 PacketLength = Ntohs(Packet->TotalLength);
    U16 FlagsFragOffset = Ntohs(Packet->FlagsFragmentOffset);
    U32 HeaderLength = IHL * 4;

    // Debug: Log ALL incoming IPv4 packets
    U32 SrcIP = Ntohl(Packet->SourceAddress);
    U32 DstIP = Ntohl(Packet->DestinationAddress);
    DEBUG(TEXT("[IPv4_HandlePacket] Received: Src=%u.%u.%u.%u Dst=%u.%u.%u.%u Proto=%u Len=%u"),
                 (SrcIP >> 24) & 0xFF, (SrcIP >> 16) & 0xFF, (SrcIP >> 8) & 0xFF, SrcIP & 0xFF,
                 (DstIP >> 24) & 0xFF, (DstIP >> 16) & 0xFF, (DstIP >> 8) & 0xFF, DstIP & 0xFF,
                 Packet->Protocol, PacketLength);

    // Validate version
    if (Version != 4) {
        DEBUG(TEXT("[IPv4_HandlePacket] Invalid version: %u"), (U32)Version);
        return;
    }

    // Validate header length
    if (IHL < 5 || HeaderLength > TotalLength) {
        DEBUG(TEXT("[IPv4_HandlePacket] Invalid header length: IHL=%u"), (U32)IHL);
        return;
    }

    // Validate total length
    if (PacketLength > TotalLength || PacketLength < HeaderLength) {
        DEBUG(TEXT("[IPv4_HandlePacket] Invalid packet length: %u (frame=%u, hdr=%u)"),
                     (U32)PacketLength, TotalLength, HeaderLength);
        return;
    }

    // Validate checksum
    if (!IPv4_ValidateChecksum(Packet)) {
        DEBUG(TEXT("[IPv4_HandlePacket] Invalid checksum"));
        return;
    }

    // Decrement and check TTL
    if (Packet->TimeToLive <= 1) {
        DEBUG(TEXT("[IPv4_HandlePacket] TTL expired (TTL=%u)"), (U32)Packet->TimeToLive);
        return;
    }

    // Check if packet is for us (simple routing)
    // Accept packets destined to our IP or broadcast
    if (Packet->DestinationAddress != Context->LocalIPv4_Be &&
        Packet->DestinationAddress != Htonl(0xFFFFFFFF)) {
        // Not for us, drop (could implement forwarding here)
        U32 DstIP = Ntohl(Packet->DestinationAddress);
        DEBUG(TEXT("[IPv4_HandlePacket] Packet not for us: %u.%u.%u.%u"),
                     (DstIP >> 24) & 0xFF, (DstIP >> 16) & 0xFF,
                     (DstIP >> 8) & 0xFF, DstIP & 0xFF);
        return;
    }

    // Handle fragments (for now, only accept non-fragmented packets)
    if ((FlagsFragOffset & IPV4_FRAGMENT_OFFSET_MASK) != 0 ||
        (FlagsFragOffset & IPV4_FLAG_MORE_FRAGMENTS) != 0) {
        DEBUG(TEXT("[IPv4_HandlePacket] Fragmented packets not supported"));
        return;
    }

    // Extract payload
    U32 PayloadLength = PacketLength - HeaderLength;
    const U8* Payload = ((const U8*)Packet) + HeaderLength;

    // Dispatch to protocol handler
    IPv4_ProtocolHandler Handler = Context->ProtocolHandlers[Packet->Protocol];
    if (Handler) {
        Handler(Payload, PayloadLength, Packet->SourceAddress, Packet->DestinationAddress);
    } else {
        DEBUG(TEXT("[IPv4_HandlePacket] No handler for protocol %u"), (U32)Packet->Protocol);
    }
}

/************************************************************************/
// Public API

void IPv4_Initialize(LPDEVICE Device, U32 LocalIPv4_Be) {
    LPIPV4_CONTEXT Context;
    U32 i;

    if (Device == NULL) return;

    Context = (LPIPV4_CONTEXT)KernelHeapAlloc(sizeof(IPV4_CONTEXT));
    if (Context == NULL) return;

    Context->Device = Device;
    Context->LocalIPv4_Be = LocalIPv4_Be;
    Context->NetmaskBe = 0;
    Context->DefaultGatewayBe = 0;

    // Clear protocol handlers
    for (i = 0; i < IPV4_MAX_PROTOCOLS; i++) {
        Context->ProtocolHandlers[i] = NULL;
    }

    // Clear pending packets
    for (i = 0; i < IPV4_MAX_PENDING_PACKETS; i++) {
        Context->PendingPackets[i].IsValid = 0;
    }
    Context->ARPCallbackRegistered = 0;

    // Create notification context like ARP does
    Context->NotificationContext = Notification_CreateContext();

    LockMutex(&(Device->Mutex), INFINITY);
    SetDeviceContext(Device, KOID_IPV4, Context);
    UnlockMutex(&(Device->Mutex));

    U32 IP = Ntohl(LocalIPv4_Be);
    DEBUG(TEXT("[IPv4_Initialize] Initialized for %u.%u.%u.%u"),
                 (IP >> 24) & 0xFF, (IP >> 16) & 0xFF,
                 (IP >> 8) & 0xFF, IP & 0xFF);
}

/************************************************************************/

void IPv4_Destroy(LPDEVICE Device) {
    LPIPV4_CONTEXT Context;

    if (Device == NULL) return;

    Context = IPv4_GetContext(Device);

    LockMutex(&(Device->Mutex), INFINITY);
    RemoveDeviceContext(Device, KOID_IPV4);
    UnlockMutex(&(Device->Mutex));

    SAFE_USE(Context) {
        if (Context->NotificationContext) {
            Notification_DestroyContext(Context->NotificationContext);
            Context->NotificationContext = NULL;
        }
        KernelHeapFree(Context);
    }
}

/************************************************************************/

void IPv4_SetLocalAddress(LPDEVICE Device, U32 LocalIPv4_Be) {
    LPIPV4_CONTEXT Context;

    if (Device == NULL) return;

    Context = IPv4_GetContext(Device);
    if (Context == NULL) return;

    Context->LocalIPv4_Be = LocalIPv4_Be;

    U32 IP = Ntohl(LocalIPv4_Be);
    DEBUG(TEXT("[IPv4_SetLocalAddress] Local address set to %u.%u.%u.%u"),
                 (IP >> 24) & 0xFF, (IP >> 16) & 0xFF,
                 (IP >> 8) & 0xFF, IP & 0xFF);
}

/************************************************************************/

void IPv4_SetNetworkConfig(LPDEVICE Device, U32 LocalIPv4_Be, U32 NetmaskBe, U32 DefaultGatewayBe) {
    LPIPV4_CONTEXT Context;

    if (Device == NULL) return;

    Context = IPv4_GetContext(Device);
    if (Context == NULL) return;

    Context->LocalIPv4_Be = LocalIPv4_Be;
    Context->NetmaskBe = NetmaskBe;
    Context->DefaultGatewayBe = DefaultGatewayBe;

    ARP_SetLocalAddress(Device, LocalIPv4_Be);

    U32 IP = Ntohl(LocalIPv4_Be);
    U32 Mask = Ntohl(NetmaskBe);
    U32 Gateway = Ntohl(DefaultGatewayBe);
    DEBUG(TEXT("[IPv4_SetNetworkConfig] IP=%u.%u.%u.%u Mask=%u.%u.%u.%u Gateway=%u.%u.%u.%u"),
                 (IP >> 24) & 0xFF, (IP >> 16) & 0xFF, (IP >> 8) & 0xFF, IP & 0xFF,
                 (Mask >> 24) & 0xFF, (Mask >> 16) & 0xFF, (Mask >> 8) & 0xFF, Mask & 0xFF,
                 (Gateway >> 24) & 0xFF, (Gateway >> 16) & 0xFF, (Gateway >> 8) & 0xFF, Gateway & 0xFF);
}

/************************************************************************/

void IPv4_RegisterProtocolHandler(LPDEVICE Device, U8 Protocol, IPv4_ProtocolHandler Handler) {
    LPIPV4_CONTEXT Context;

    if (Device == NULL) return;

    Context = IPv4_GetContext(Device);
    if (Context == NULL) return;

    Context->ProtocolHandlers[Protocol] = Handler;
    DEBUG(TEXT("[IPv4_RegisterProtocolHandler] Registered handler for protocol %u"), (U32)Protocol);
}

/************************************************************************/

/**
 * @brief Sends an IPv4 packet.
 *
 * @param DestinationIP  Destination IPv4 address in big-endian.
 * @param Protocol       Protocol number.
 * @param Payload        Payload data.
 * @param PayloadLength  Payload length in bytes.
 * @return 1 on success, 0 on failure.
 */
int IPv4_Send(LPDEVICE Device, U32 DestinationIP, U8 Protocol, const U8* Payload, U32 PayloadLength) {
    LPIPV4_CONTEXT Context;
    U8 DestinationMAC[6];
    if (Device == NULL) return 0;

    Context = IPv4_GetContext(Device);
    if (Context == NULL) return 0;

    // Simple routing: use gateway for non-local addresses
    U32 NextHopIP = DestinationIP;
    if (Context->DefaultGatewayBe != 0 && Context->NetmaskBe != 0) {
        U32 LocalNetwork = Context->LocalIPv4_Be & Context->NetmaskBe;
        U32 DestNetwork = DestinationIP & Context->NetmaskBe;
        DEBUG(TEXT("[IPv4_Send] Routing check: LocalNetwork=%x, DestNetwork=%x, Gateway=%x"),
              Ntohl(LocalNetwork), Ntohl(DestNetwork), Ntohl(Context->DefaultGatewayBe));
        if (DestNetwork != LocalNetwork) {
            NextHopIP = Context->DefaultGatewayBe;
            DEBUG(TEXT("[IPv4_Send] Using gateway: NextHopIP=%x"), Ntohl(NextHopIP));
        } else {
            DEBUG(TEXT("[IPv4_Send] Using direct route: NextHopIP=%x"), Ntohl(NextHopIP));
        }
    }

    // Try immediate ARP resolution (non-blocking)
    if (ARP_Resolve(Device, NextHopIP, DestinationMAC)) {
        DEBUG(TEXT("[IPv4_Send] ARP resolved immediately, sending packet"));
    } else {
        // ARP resolution pending - queue the packet for later transmission
        U32 DstIP = Ntohl(DestinationIP);
        U32 NextHopIPHost = Ntohl(NextHopIP);
        DEBUG(TEXT("[IPv4_Send] ARP resolution pending for destination %u.%u.%u.%u (NextHop %u.%u.%u.%u) - queuing packet"),
                     (DstIP >> 24) & 0xFF, (DstIP >> 16) & 0xFF, (DstIP >> 8) & 0xFF, DstIP & 0xFF,
                     (NextHopIPHost >> 24) & 0xFF, (NextHopIPHost >> 16) & 0xFF, (NextHopIPHost >> 8) & 0xFF, NextHopIPHost & 0xFF);

        return IPv4_AddPendingPacket(Context, DestinationIP, NextHopIP, Protocol, Payload, PayloadLength) ? IPV4_SEND_PENDING : IPV4_SEND_FAILED;
    }

    // Calculate total frame size
    U32 IPv4HeaderSize = sizeof(IPV4_HEADER);
    U32 ETHERNET_HEADERSize = sizeof(ETHERNET_HEADER);
    U32 TotalFrameSize = ETHERNET_HEADERSize + IPv4HeaderSize + PayloadLength;

    // Allocate frame buffer (on stack for small packets)
    U8 FrameBuffer[1514]; // Typical Ethernet MTU
    if (TotalFrameSize > sizeof(FrameBuffer)) {
        DEBUG(TEXT("[IPv4_Send] Packet too large: %u bytes"), TotalFrameSize);
        return 0;
    }

    // Build Ethernet header
    ETHERNET_HEADER* EthHeader = (ETHERNET_HEADER*)FrameBuffer;
    MemoryCopy(EthHeader->Destination, DestinationMAC, 6);

    // Get our MAC address from network device
    NETWORKGETINFO GetInfo;
    NETWORKINFO NetInfo;
    BOOL MacRetrieved = FALSE;
    MemorySet(&GetInfo, 0, sizeof(GetInfo));
    MemorySet(&NetInfo, 0, sizeof(NetInfo));
    GetInfo.Device = (LPPCI_DEVICE)Device;
    GetInfo.Info = &NetInfo;

    LockMutex(&(Device->Mutex), INFINITY);

    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        SAFE_USE_VALID_ID(((LPPCI_DEVICE)Device)->Driver, KOID_DRIVER) {
            if (((LPPCI_DEVICE)Device)->Driver->Command(DF_NT_GETINFO, (U32)(LPVOID)&GetInfo) != DF_ERROR_SUCCESS) {
                DEBUG(TEXT("[IPv4_Send] Failed to get network info"));
                goto Out;
            }
            MacRetrieved = TRUE;
        } else {
            DEBUG(TEXT("[IPv4_Send] Invalid network device driver"));
            goto Out;
        }
    } else {
        DEBUG(TEXT("[IPv4_Send] Invalid network device"));
        goto Out;
    }

Out:
    UnlockMutex(&(Device->Mutex));
    if (!MacRetrieved) {
        return 0;
    }

    MemoryCopy(EthHeader->Source, NetInfo.MAC, 6);
    EthHeader->EthType = Htons(ETHTYPE_IPV4);

    // Debug: Show Ethernet header
    DEBUG(TEXT("[IPv4_Send] Ethernet Header: Dest=%02X:%02X:%02X:%02X:%02X:%02X Src=%02X:%02X:%02X:%02X:%02X:%02X EthType=0x%04X"),
          EthHeader->Destination[0], EthHeader->Destination[1], EthHeader->Destination[2],
          EthHeader->Destination[3], EthHeader->Destination[4], EthHeader->Destination[5],
          EthHeader->Source[0], EthHeader->Source[1], EthHeader->Source[2],
          EthHeader->Source[3], EthHeader->Source[4], EthHeader->Source[5],
          Ntohs(EthHeader->EthType));

    // Build IPv4 header
    IPV4_HEADER* IPv4Hdr = (IPV4_HEADER*)(FrameBuffer + ETHERNET_HEADERSize);
    MemorySet(IPv4Hdr, 0, sizeof(IPV4_HEADER));

    IPv4Hdr->VersionIHL = 0x45; // Version 4, IHL 5 (20 bytes)
    IPv4Hdr->TypeOfService = 0;
    IPv4Hdr->TotalLength = Htons((U16)(IPv4HeaderSize + PayloadLength));
    IPv4Hdr->Identification = Htons(NextID);
    NextID = (NextID == 0xFFFF) ? 1 : NextID + 1;
    IPv4Hdr->FlagsFragmentOffset = Htons(IPV4_FLAG_DONT_FRAGMENT); // Don't fragment
    IPv4Hdr->TimeToLive = IPV4_DEFAULT_TTL;
    IPv4Hdr->Protocol = Protocol;
    IPv4Hdr->HeaderChecksum = 0; // Will be calculated below
    IPv4Hdr->SourceAddress = Context->LocalIPv4_Be;
    IPv4Hdr->DestinationAddress = DestinationIP;

    // Calculate and set checksum
    IPv4Hdr->HeaderChecksum = IPv4_CalculateChecksum(IPv4Hdr);

    DEBUG(TEXT("[IPv4_Send] IPv4 Header: Src=%d.%d.%d.%d Dst=%d.%d.%d.%d Proto=%u Len=%u TTL=%u ID=%u Checksum=0x%04X"),
          (Ntohl(IPv4Hdr->SourceAddress) >> 24) & 0xFF, (Ntohl(IPv4Hdr->SourceAddress) >> 16) & 0xFF,
          (Ntohl(IPv4Hdr->SourceAddress) >> 8) & 0xFF, Ntohl(IPv4Hdr->SourceAddress) & 0xFF,
          (Ntohl(IPv4Hdr->DestinationAddress) >> 24) & 0xFF, (Ntohl(IPv4Hdr->DestinationAddress) >> 16) & 0xFF,
          (Ntohl(IPv4Hdr->DestinationAddress) >> 8) & 0xFF, Ntohl(IPv4Hdr->DestinationAddress) & 0xFF,
          IPv4Hdr->Protocol, Ntohs(IPv4Hdr->TotalLength), IPv4Hdr->TimeToLive,
          Ntohs(IPv4Hdr->Identification), Ntohs(IPv4Hdr->HeaderChecksum));

    DEBUG(TEXT("[IPv4_Send] Sending IPv4 packet: Src=%x Dst=%x Proto=%u Len=%u"),
          Ntohl(IPv4Hdr->SourceAddress), Ntohl(IPv4Hdr->DestinationAddress),
          IPv4Hdr->Protocol, Ntohs(IPv4Hdr->TotalLength));

    // Copy payload
    if (PayloadLength > 0) {
        MemoryCopy(FrameBuffer + ETHERNET_HEADERSize + IPv4HeaderSize, Payload, PayloadLength);
    }

    // Send frame
    U32 Result = IPv4_SendEthernetFrame(Context, FrameBuffer, TotalFrameSize);
    DEBUG(TEXT("[IPv4_Send] Frame send result: %u (TotalSize=%u)"), Result, TotalFrameSize);
    return Result > 0 ? IPV4_SEND_IMMEDIATE : IPV4_SEND_FAILED;
}

/************************************************************************/

/**
 * @brief Sends an IPv4 packet directly (assuming ARP is already resolved)
 * @param Context IPv4 context
 * @param DestinationIP Destination IP address in big-endian
 * @param NextHopIP Next hop IP address in big-endian
 * @param Protocol Protocol number
 * @param Payload Payload data
 * @param PayloadLength Payload length
 * @return 1 on success, 0 on failure
 */
static int IPv4_SendDirect(LPIPV4_CONTEXT Context, U32 DestinationIP, U32 NextHopIP, U8 Protocol, const U8* Payload, U32 PayloadLength) {
    // NextID is now global static variable
    U8 DestinationMAC[6];
    LPDEVICE Device;

    if (Context == NULL || Payload == NULL) return 0;

    Device = Context->Device;
    if (Device == NULL) return 0;

    // Resolve NextHop MAC address (should be in cache now)
    if (!ARP_Resolve(Device, NextHopIP, DestinationMAC)) {
        DEBUG(TEXT("[IPv4_SendDirect] ARP resolution failed for NextHop %x"), Ntohl(NextHopIP));
        return 0;
    }

    // Calculate total frame size
    U32 IPv4HeaderSize = sizeof(IPV4_HEADER);
    U32 ETHERNET_HEADERSize = sizeof(ETHERNET_HEADER);
    U32 TotalFrameSize = ETHERNET_HEADERSize + IPv4HeaderSize + PayloadLength;

    // Allocate frame buffer (on stack for small packets)
    U8 FrameBuffer[1514]; // Typical Ethernet MTU
    if (TotalFrameSize > sizeof(FrameBuffer)) {
        DEBUG(TEXT("[IPv4_SendDirect] Packet too large: %u bytes"), TotalFrameSize);
        return 0;
    }

    // Build Ethernet header
    ETHERNET_HEADER* EthHeader = (ETHERNET_HEADER*)FrameBuffer;
    MemoryCopy(EthHeader->Destination, DestinationMAC, 6);

    // Get our MAC address from network device
    NETWORKGETINFO GetInfo;
    NETWORKINFO NetInfo;
    BOOL MacRetrieved = FALSE;
    MemorySet(&GetInfo, 0, sizeof(GetInfo));
    MemorySet(&NetInfo, 0, sizeof(NetInfo));
    GetInfo.Device = (LPPCI_DEVICE)Device;
    GetInfo.Info = &NetInfo;

    LockMutex(&(Device->Mutex), INFINITY);

    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        SAFE_USE_VALID_ID(((LPPCI_DEVICE)Device)->Driver, KOID_DRIVER) {
            if (((LPPCI_DEVICE)Device)->Driver->Command(DF_NT_GETINFO, (U32)(LPVOID)&GetInfo) != DF_ERROR_SUCCESS) {
                DEBUG(TEXT("[IPv4_SendDirect] Failed to get network info"));
                goto Out;
            }
            MacRetrieved = TRUE;
        } else {
            DEBUG(TEXT("[IPv4_SendDirect] Invalid network device driver"));
            goto Out;
        }
    } else {
        DEBUG(TEXT("[IPv4_SendDirect] Invalid network device"));
        goto Out;
    }

Out:
    UnlockMutex(&(Device->Mutex));
    if (!MacRetrieved) {
        return 0;
    }

    MemoryCopy(EthHeader->Source, NetInfo.MAC, 6);
    EthHeader->EthType = Htons(ETHTYPE_IPV4);

    // Build IPv4 header
    IPV4_HEADER* IPv4Hdr = (IPV4_HEADER*)(FrameBuffer + ETHERNET_HEADERSize);
    MemorySet(IPv4Hdr, 0, sizeof(IPV4_HEADER));

    IPv4Hdr->VersionIHL = 0x45; // Version 4, IHL 5 (20 bytes)
    IPv4Hdr->TypeOfService = 0;
    IPv4Hdr->TotalLength = Htons((U16)(IPv4HeaderSize + PayloadLength));
    IPv4Hdr->Identification = Htons(NextID);
    NextID = (NextID == 0xFFFF) ? 1 : NextID + 1;
    IPv4Hdr->FlagsFragmentOffset = Htons(IPV4_FLAG_DONT_FRAGMENT); // Don't fragment
    IPv4Hdr->TimeToLive = IPV4_DEFAULT_TTL;
    IPv4Hdr->Protocol = Protocol;
    IPv4Hdr->HeaderChecksum = 0; // Will be calculated below
    IPv4Hdr->SourceAddress = Context->LocalIPv4_Be;
    IPv4Hdr->DestinationAddress = DestinationIP;

    // Calculate and set checksum
    IPv4Hdr->HeaderChecksum = IPv4_CalculateChecksum(IPv4Hdr);

    DEBUG(TEXT("[IPv4_SendDirect] Sending IPv4 packet: Src=%x Dst=%x Proto=%u Len=%u"),
          Ntohl(IPv4Hdr->SourceAddress), Ntohl(IPv4Hdr->DestinationAddress),
          IPv4Hdr->Protocol, Ntohs(IPv4Hdr->TotalLength));

    // Copy payload
    if (PayloadLength > 0) {
        MemoryCopy(FrameBuffer + ETHERNET_HEADERSize + IPv4HeaderSize, Payload, PayloadLength);
    }

    // Send frame
    U32 Result = IPv4_SendEthernetFrame(Context, FrameBuffer, TotalFrameSize);
    DEBUG(TEXT("[IPv4_SendDirect] Frame send result: %u (TotalSize=%u)"), Result, TotalFrameSize);
    return Result;
}

/************************************************************************/

/**
 * @brief Handles incoming Ethernet frames for IPv4.
 *
 * @param Frame  Pointer to Ethernet frame data.
 * @param Length Frame length in bytes.
 */
void IPv4_OnEthernetFrame(LPDEVICE Device, const U8* Frame, U32 Length) {
    LPIPV4_CONTEXT Context;

    DEBUG(TEXT("[IPv4_OnEthernetFrame] Entry Device=%x Frame=%x Length=%u"), (U32)Device, (U32)Frame, Length);

    if (Device == NULL || Frame == NULL) {
        DEBUG(TEXT("[IPv4_OnEthernetFrame] NULL parameters"));
        return;
    }

    Context = IPv4_GetContext(Device);
    if (Context == NULL) {
        DEBUG(TEXT("[IPv4_OnEthernetFrame] No IPv4 context for device"));
        return;
    }

    if (Length < sizeof(ETHERNET_HEADER)) {
        DEBUG(TEXT("[IPv4_OnEthernetFrame] Frame too short: %u"), Length);
        return;
    }

    const ETHERNET_HEADER* Ethernet = (const ETHERNET_HEADER*)Frame;
    U16 EthType = Ntohs(Ethernet->EthType);
    if (EthType != ETHTYPE_IPV4) return;

    if (Length < sizeof(ETHERNET_HEADER) + sizeof(IPV4_HEADER)) return;

    const IPV4_HEADER* IPv4Hdr = (const IPV4_HEADER*)(Frame + sizeof(ETHERNET_HEADER));
    U32 IPv4Length = Length - sizeof(ETHERNET_HEADER);

    IPv4_HandlePacket(Context, IPv4Hdr, IPv4Length);
}

/************************************************************************/

/**
 * @brief Callback function called when ARP resolution completes
 */
void IPv4_ARPResolvedCallback(LPNOTIFICATION_DATA NotificationData, LPVOID UserData) {
    LPIPV4_CONTEXT Context = (LPIPV4_CONTEXT)UserData;
    LPARP_RESOLVED_DATA ResolvedData;

    DEBUG(TEXT("[IPv4_ARPResolvedCallback] Entry: Context=%x NotificationData=%x"), (U32)Context, (U32)NotificationData);

    if (Context == NULL || NotificationData == NULL) {
        DEBUG(TEXT("[IPv4_ARPResolvedCallback] NULL parameter: Context=%x NotificationData=%x"), (U32)Context, (U32)NotificationData);
        return;
    }
    if (NotificationData->EventID != NOTIF_EVENT_ARP_RESOLVED) {
        DEBUG(TEXT("[IPv4_ARPResolvedCallback] Wrong event ID: %x"), NotificationData->EventID);
        return;
    }
    if (NotificationData->Data == NULL) {
        DEBUG(TEXT("[IPv4_ARPResolvedCallback] NULL data"));
        return;
    }

    ResolvedData = (LPARP_RESOLVED_DATA)NotificationData->Data;
    U32 ResolvedIP = ResolvedData->IPv4_Be;
    DEBUG(TEXT("[IPv4_ARPResolvedCallback] ARP resolved for IP %x, processing pending packets"), Ntohl(ResolvedIP));

    IPv4_ProcessPendingPackets(Context, ResolvedIP);
}

/************************************************************************/

/**
 * @brief Adds a packet to the pending packets queue
 */
int IPv4_AddPendingPacket(LPIPV4_CONTEXT Context, U32 DestinationIP, U32 NextHopIP, U8 Protocol, const U8* Payload, U32 PayloadLength) {
    U32 i;

    if (Context == NULL || Payload == NULL || PayloadLength == 0 || PayloadLength > 1500 || PayloadLength > (65535 - sizeof(IPV4_HEADER))) {
        DEBUG(TEXT("[IPv4_AddPendingPacket] Invalid parameters: PayloadLength=%u"), PayloadLength);
        return 0;
    }

    // Register ARP callback if not already done
    if (!Context->ARPCallbackRegistered) {
        if (ARP_RegisterNotification(Context->Device, NOTIF_EVENT_ARP_RESOLVED, IPv4_ARPResolvedCallback, Context)) {
            Context->ARPCallbackRegistered = 1;
            DEBUG(TEXT("[IPv4_AddPendingPacket] Registered ARP callback"));
        } else {
            DEBUG(TEXT("[IPv4_AddPendingPacket] Failed to register ARP callback"));
            return 0;
        }
    }

    // Find empty slot
    for (i = 0; i < IPV4_MAX_PENDING_PACKETS; i++) {
        if (!Context->PendingPackets[i].IsValid) {
            Context->PendingPackets[i].DestinationIP = DestinationIP;
            Context->PendingPackets[i].NextHopIP = NextHopIP;
            Context->PendingPackets[i].Protocol = Protocol;
            Context->PendingPackets[i].PayloadLength = PayloadLength;
            MemoryCopy(Context->PendingPackets[i].Payload, Payload, PayloadLength);
            Context->PendingPackets[i].IsValid = 1;

            DEBUG(TEXT("[IPv4_AddPendingPacket] Added pending packet %u: Dst=%x NextHop=%x Proto=%u Len=%u"),
                  i, Ntohl(DestinationIP), Ntohl(NextHopIP), Protocol, PayloadLength);
            return 1;
        }
    }

    DEBUG(TEXT("[IPv4_AddPendingPacket] No free slots for pending packet"));
    return 0;
}

/************************************************************************/

/**
 * @brief Processes pending packets for a resolved IP
 */
void IPv4_ProcessPendingPackets(LPIPV4_CONTEXT Context, U32 ResolvedIP) {
    U32 i;
    U32 ProcessedCount = 0;

    if (Context == NULL) return;

    for (i = 0; i < IPV4_MAX_PENDING_PACKETS; i++) {
        if (Context->PendingPackets[i].IsValid && Context->PendingPackets[i].NextHopIP == ResolvedIP) {
            DEBUG(TEXT("[IPv4_ProcessPendingPackets] Sending pending packet %u: Dst=%x Proto=%u Len=%u"),
                  i, Ntohl(Context->PendingPackets[i].DestinationIP),
                  Context->PendingPackets[i].Protocol, Context->PendingPackets[i].PayloadLength);

            // Verify ARP is still available before sending
            U8 VerifyMAC[6];
            if (!ARP_Resolve(Context->Device, Context->PendingPackets[i].NextHopIP, VerifyMAC)) {
                DEBUG(TEXT("[IPv4_ProcessPendingPackets] ARP expired for NextHop %x, keeping packet pending"),
                      Ntohl(Context->PendingPackets[i].NextHopIP));
                continue;
            }

            // Send the packet directly since ARP is verified
            DEBUG(TEXT("[IPv4_ProcessPendingPackets] Calling IPv4_SendDirect for pending packet"));
            int Result = IPv4_SendDirect(Context, Context->PendingPackets[i].DestinationIP,
                                        Context->PendingPackets[i].NextHopIP,
                                        Context->PendingPackets[i].Protocol,
                                        Context->PendingPackets[i].Payload,
                                        Context->PendingPackets[i].PayloadLength);

            DEBUG(TEXT("[IPv4_ProcessPendingPackets] Packet %u send result: %d"), i, Result);

            // Send notification only if packet was actually sent (not pending)
            if (Result == IPV4_SEND_IMMEDIATE && Context->NotificationContext) {
                IPV4_PACKET_SENT_DATA PacketSentData;
                PacketSentData.DestinationIP = Context->PendingPackets[i].DestinationIP;
                PacketSentData.Protocol = Context->PendingPackets[i].Protocol;
                PacketSentData.PayloadLength = Context->PendingPackets[i].PayloadLength;

                Notification_Send(Context->NotificationContext, NOTIF_EVENT_IPV4_PACKET_SENT,
                                 &PacketSentData, sizeof(PacketSentData));
            }

            // Mark as processed
            Context->PendingPackets[i].IsValid = 0;
            ProcessedCount++;
        }
    }

    DEBUG(TEXT("[IPv4_ProcessPendingPackets] Processed %u pending packets for IP %x"), ProcessedCount, Ntohl(ResolvedIP));
}

/************************************************************************/

/**
 * @brief Registers for IPv4 notifications on a specific device
 */
U32 IPv4_RegisterNotification(LPDEVICE Device, U32 EventID, NOTIFICATION_CALLBACK Callback, LPVOID UserData) {
    LPIPV4_CONTEXT Context = IPv4_GetContext(Device);
    if (!Context || !Context->NotificationContext) {
        return 0; // Not initialized yet
    }
    return Notification_Register(Context->NotificationContext, EventID, Callback, UserData);
}
