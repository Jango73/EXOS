
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
#include "console/Console.h"
#include "Device.h"
#include "Heap.h"
#include "ID.h"
#include "network/ARP.h"
#include "Log.h"
#include "Memory.h"
#include "System.h"
#include "utils/Notification.h"
#include "Clock.h"
#include "utils/NetworkChecksum.h"

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

    // Calculate checksum of entire header (including checksum field)
    U16 CalculatedChecksum = NetworkChecksum_Calculate((const U8*)Header, HeaderLength);

    // Compare calculated checksum with received checksum
    BOOL IsValid = (CalculatedChecksum == ReceivedChecksum);

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
static INT IPv4_SendEthernetFrame(LPIPV4_CONTEXT Context, const U8* Data, U32 Length) {
    if (Context == NULL) return 0;
    return Network_SendRawFrame(Context->Device, Data, Length);
}

/************************************************************************/

/**
 * @brief Read the source MAC address from the network device driver.
 *
 * @param Device Network device.
 * @param SourceMAC Receives the device MAC address.
 * @return TRUE on success.
 */
static BOOL IPv4_GetSourceMACAddress(LPDEVICE Device, U8 SourceMAC[6]) {
    NETWORK_GET_INFO GetInfo;
    NETWORKINFO NetInfo;
    BOOL MacRetrieved = FALSE;

    if (Device == NULL || SourceMAC == NULL) return FALSE;

    MemorySet(&GetInfo, 0, sizeof(GetInfo));
    MemorySet(&NetInfo, 0, sizeof(NetInfo));
    GetInfo.Device = (LPPCI_DEVICE)Device;
    GetInfo.Info = &NetInfo;

    LockMutex(&(Device->Mutex), INFINITY);

    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        SAFE_USE_VALID_ID(((LPPCI_DEVICE)Device)->Driver, KOID_DRIVER) {
            if (((LPPCI_DEVICE)Device)->Driver->Command(DF_NT_GETINFO, (UINT)(LPVOID)&GetInfo) != DF_RETURN_SUCCESS) {
                goto Out;
            }
            MacRetrieved = TRUE;
        } else {
            goto Out;
        }
    } else {
        goto Out;
    }

Out:
    UnlockMutex(&(Device->Mutex));

    if (!MacRetrieved) return FALSE;

    MemoryCopy(SourceMAC, NetInfo.MAC, 6);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Build and send one IPv4 Ethernet frame.
 *
 * @param Context IPv4 context.
 * @param DestinationIP Destination IPv4 address in big-endian.
 * @param DestinationMAC Destination MAC address.
 * @param Protocol IPv4 protocol number.
 * @param Payload Payload buffer.
 * @param PayloadLength Payload length in bytes.
 * @return Raw frame send result.
 */
static INT IPv4_SendResolvedPacket(
    LPIPV4_CONTEXT Context,
    U32 DestinationIP,
    const U8 DestinationMAC[6],
    U8 Protocol,
    const U8* Payload,
    U32 PayloadLength
) {
    U32 IPv4HeaderSize;
    U32 EthernetHeaderSize;
    U32 TotalFrameSize;
    U8 FrameBuffer[1514];
    U8 SourceMAC[6];
    ETHERNET_HEADER* EthernetHeader;
    IPV4_HEADER* IPv4Header;

    if (Context == NULL || DestinationMAC == NULL) return 0;
    if (Context->Device == NULL) return 0;
    if (Payload == NULL && PayloadLength > 0) return 0;

    IPv4HeaderSize = sizeof(IPV4_HEADER);
    EthernetHeaderSize = sizeof(ETHERNET_HEADER);
    TotalFrameSize = EthernetHeaderSize + IPv4HeaderSize + PayloadLength;
    if (TotalFrameSize > sizeof(FrameBuffer)) return 0;
    if (!IPv4_GetSourceMACAddress(Context->Device, SourceMAC)) return 0;

    EthernetHeader = (ETHERNET_HEADER*)FrameBuffer;
    MemoryCopy(EthernetHeader->Destination, DestinationMAC, 6);
    MemoryCopy(EthernetHeader->Source, SourceMAC, 6);
    EthernetHeader->EthType = Htons(ETHTYPE_IPV4);

    IPv4Header = (IPV4_HEADER*)(FrameBuffer + EthernetHeaderSize);
    MemorySet(IPv4Header, 0, sizeof(IPV4_HEADER));
    IPv4Header->VersionIHL = 0x45;
    IPv4Header->TypeOfService = 0;
    IPv4Header->TotalLength = Htons((U16)(IPv4HeaderSize + PayloadLength));
    IPv4Header->Identification = Htons(NextID);
    NextID = (NextID == 0xFFFF) ? 1 : NextID + 1;
    IPv4Header->FlagsFragmentOffset = Htons(IPV4_FLAG_DONT_FRAGMENT);
    IPv4Header->TimeToLive = IPV4_DEFAULT_TTL;
    IPv4Header->Protocol = Protocol;
    IPv4Header->HeaderChecksum = 0;
    IPv4Header->SourceAddress = Context->LocalIPv4_Be;
    IPv4Header->DestinationAddress = DestinationIP;
    IPv4Header->HeaderChecksum = IPv4_CalculateChecksum(IPv4Header);

    if (PayloadLength > 0) {
        MemoryCopy(FrameBuffer + EthernetHeaderSize + IPv4HeaderSize, Payload, PayloadLength);
    }

    return IPv4_SendEthernetFrame(Context, FrameBuffer, TotalFrameSize);
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
    UNUSED(SrcIP);
    UNUSED(DstIP);
    // Validate version
    if (Version != 4) {
        return;
    }

    // Validate header length
    if (IHL < 5 || HeaderLength > TotalLength) {
        return;
    }

    // Validate total length
    if (PacketLength > TotalLength || PacketLength < HeaderLength) {
        return;
    }

    // Validate checksum
    if (!IPv4_ValidateChecksum((IPV4_HEADER*)Packet)) {
        return;
    }

    // Decrement and check TTL
    if (Packet->TimeToLive <= 1) {
        return;
    }

    // Check if packet is for us (simple routing)
    // Accept packets destined to our IP or broadcast
    if (Packet->DestinationAddress != Context->LocalIPv4_Be &&
        Packet->DestinationAddress != Htonl(0xFFFFFFFF)) {
        // Not for us, drop (could implement forwarding here)
        U32 DstIP = Ntohl(Packet->DestinationAddress);
        UNUSED(DstIP);
        return;
    }

    // Handle fragments (for now, only accept non-fragmented packets)
    if ((FlagsFragOffset & IPV4_FRAGMENT_OFFSET_MASK) != 0 ||
        (FlagsFragOffset & IPV4_FLAG_MORE_FRAGMENTS) != 0) {
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
    UNUSED(IP);
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
    UNUSED(IP);
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
    UNUSED(IP);
    UNUSED(Mask);
    UNUSED(Gateway);
}

/************************************************************************/

/**
 * @brief Clears all pending IPv4 packets queued for ARP resolution.
 *
 * Used when network configuration changes to avoid sending packets using
 * stale routing information.
 *
 * @param Device Target network device.
 */
void IPv4_ClearPendingPackets(LPDEVICE Device) {
    LPIPV4_CONTEXT Context;
    U32 Index;

    if (Device == NULL) return;

    Context = IPv4_GetContext(Device);
    if (Context == NULL) return;

    for (Index = 0; Index < IPV4_MAX_PENDING_PACKETS; Index++) {
        Context->PendingPackets[Index].IsValid = 0;
    }

}

/************************************************************************/

void IPv4_RegisterProtocolHandler(LPDEVICE Device, U8 Protocol, IPv4_ProtocolHandler Handler) {
    LPIPV4_CONTEXT Context;

    if (Device == NULL) return;

    Context = IPv4_GetContext(Device);
    if (Context == NULL) return;

    Context->ProtocolHandlers[Protocol] = Handler;
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
        if (DestNetwork != LocalNetwork) {
            NextHopIP = Context->DefaultGatewayBe;
        } else {
        }
    }

    // Try immediate ARP resolution (non-blocking)
    if (ARP_Resolve(Device, NextHopIP, DestinationMAC)) {
    } else {
        // ARP resolution pending - queue the packet for later transmission
        U32 DstIP = Ntohl(DestinationIP);
        U32 NextHopIPHost = Ntohl(NextHopIP);
        UNUSED(DstIP);
        UNUSED(NextHopIPHost);
        return IPv4_AddPendingPacket(Context, DestinationIP, NextHopIP, Protocol, Payload, PayloadLength) ? IPV4_SEND_PENDING : IPV4_SEND_FAILED;
    }

    U32 Result = IPv4_SendResolvedPacket(Context, DestinationIP, DestinationMAC, Protocol, Payload, PayloadLength);
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
    U8 DestinationMAC[6];
    LPDEVICE Device;

    if (Context == NULL || Payload == NULL) return 0;

    Device = Context->Device;
    if (Device == NULL) return 0;

    // Resolve NextHop MAC address (should be in cache now)
    if (!ARP_Resolve(Device, NextHopIP, DestinationMAC)) {
        return 0;
    }

    return IPv4_SendResolvedPacket(Context, DestinationIP, DestinationMAC, Protocol, Payload, PayloadLength);
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

    if (Device == NULL || Frame == NULL) {
        return;
    }

    Context = IPv4_GetContext(Device);
    if (Context == NULL) {
        return;
    }

    if (Length < sizeof(ETHERNET_HEADER)) {
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

    if (Context == NULL || NotificationData == NULL) {
        return;
    }
    if (NotificationData->EventID != NOTIF_EVENT_ARP_RESOLVED) {
        return;
    }
    if (NotificationData->Data == NULL) {
        return;
    }

    ResolvedData = (LPARP_RESOLVED_DATA)NotificationData->Data;
    U32 ResolvedIP = ResolvedData->IPv4_Be;
    IPv4_ProcessPendingPackets(Context, ResolvedIP);
}

/************************************************************************/

/**
 * @brief Adds a packet to the pending packets queue
 */
int IPv4_AddPendingPacket(LPIPV4_CONTEXT Context, U32 DestinationIP, U32 NextHopIP, U8 Protocol, const U8* Payload, U32 PayloadLength) {
    U32 i;

    if (Context == NULL || Payload == NULL || PayloadLength == 0 || PayloadLength > 1500 || PayloadLength > (65535 - sizeof(IPV4_HEADER))) {
        return 0;
    }

    // Register ARP callback if not already done
    if (!Context->ARPCallbackRegistered) {
        if (ARP_RegisterNotification(Context->Device, NOTIF_EVENT_ARP_RESOLVED, IPv4_ARPResolvedCallback, Context)) {
            Context->ARPCallbackRegistered = 1;
        } else {
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
            return 1;
        }
    }

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
            // Verify ARP is still available before sending
            U8 VerifyMAC[6];
            if (!ARP_Resolve(Context->Device, Context->PendingPackets[i].NextHopIP, VerifyMAC)) {
                continue;
            }

            // Send the packet directly since ARP is verified
            int Result = IPv4_SendDirect(Context, Context->PendingPackets[i].DestinationIP,
                                        Context->PendingPackets[i].NextHopIP,
                                        Context->PendingPackets[i].Protocol,
                                        Context->PendingPackets[i].Payload,
                                        Context->PendingPackets[i].PayloadLength);

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
