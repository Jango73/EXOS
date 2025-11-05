
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


    Address Resolution Protocol (ARP)

\************************************************************************/

#include "network/ARP.h"
#include "ARPContext.h"
#include "Device.h"
#include "Heap.h"
#include "ID.h"
#include "Log.h"
#include "Memory.h"
#include "network/Network.h"
#include "System.h"
#include "utils/Notification.h"
#include "utils/AdaptiveDelay.h"

/************************************************************************/
// Helper functions

LPARP_CONTEXT ARP_GetContext(LPDEVICE Device) {
    LPARP_CONTEXT Context = NULL;

    if (Device == NULL) return NULL;

    LockMutex(&(Device->Mutex), INFINITY);
    Context = (LPARP_CONTEXT)GetDeviceContext(Device, KOID_ARP);
    UnlockMutex(&(Device->Mutex));

    return Context;
}

/************************************************************************/
// Utilities

/**
 * @brief Vérifie si une adresse MAC est valide (unicast, non nulle, non broadcast).
 *
 * @param MacAddress Adresse MAC à valider (tableau de 6 octets).
 * @return 1 si valide, 0 sinon.
 */
static int IsValidMacAddress(const U8 MacAddress[6]) {
    if (MacAddress == NULL) {
        DEBUG(TEXT("[IsValidMacAddress] NULL address"));
        return 0;
    }

    // Vérifier si l'adresse est nulle (00:00:00:00:00:00)
    if (MacAddress[0] == 0 && MacAddress[1] == 0 && MacAddress[2] == 0 &&
        MacAddress[3] == 0 && MacAddress[4] == 0 && MacAddress[5] == 0) {
        DEBUG(TEXT("[IsValidMacAddress] Zero address"));
        return 0;
    }

    // Vérifier si l'adresse est broadcast (FF:FF:FF:FF:FF:FF)
    if (MacAddress[0] == 0xFF && MacAddress[1] == 0xFF && MacAddress[2] == 0xFF &&
        MacAddress[3] == 0xFF && MacAddress[4] == 0xFF && MacAddress[5] == 0xFF) {
        DEBUG(TEXT("[IsValidMacAddress] Broadcast address"));
        return 0;
    }

    // Vérifier si l'adresse est multidiffusion (bit I/G à 1 dans le premier octet)
    if (MacAddress[0] & 0x01) {
        DEBUG(TEXT("[IsValidMacAddress] Multicast address"));
        return 0;
    }

    return 1;
}

/************************************************************************/

/**
 * @brief Copies a MAC address.
 *
 * @param Destination Buffer receiving the address.
 * @param Source      Source MAC address.
 */

static void MacCopy(U8 Destination[6], const U8 Source[6]) { MemoryCopy(Destination, Source, 6); }

/************************************************************************/

/**
 * @brief Searches the ARP cache for an IPv4 address.
 *
 * @param IPv4_Be IPv4 address in big-endian.
 * @return Matching cache entry or NULL.
 */

static LPARP_CACHE_ENTRY ArpLookup(LPARP_CONTEXT Context, U32 IPv4_Be) {
    U32 Index;
    if (Context == NULL) return NULL;
    DEBUG(TEXT("[ArpLookup] Searching for IP %x"), Ntohl(IPv4_Be));
    for (Index = 0; Index < ARP_CACHE_SIZE; Index++) {
        if (Context->Cache[Index].IPv4_Be == IPv4_Be) {
            DEBUG(TEXT("[ArpLookup] Found entry %u: IsValid=%u IsProbing=%u IPv4=%x"),
                  Index, Context->Cache[Index].IsValid, Context->Cache[Index].IsProbing, Ntohl(Context->Cache[Index].IPv4_Be));
            return &Context->Cache[Index];
        }
    }
    DEBUG(TEXT("[ArpLookup] No entry found for IP %x"), Ntohl(IPv4_Be));
    return NULL;
}

/************************************************************************/

/**
 * @brief Allocates a cache slot for an IPv4 address.
 *
 * Finds an empty slot or evicts the entry with the smallest TTL.
 *
 * @param IPv4_Be IPv4 address in big-endian.
 * @return Pointer to the selected cache entry.
 */

static LPARP_CACHE_ENTRY ArpAllocateSlot(LPARP_CONTEXT Context, U32 IPv4_Be) {
    U32 Index, Victim = 0;

    if (Context == NULL) return NULL;

    // Handle edge case: if cache size is 0, cannot allocate any slot
    if (ARP_CACHE_SIZE == 0) return NULL;

    /* First check if there's already a probing entry for this IP */
    for (Index = 0; Index < ARP_CACHE_SIZE; Index++) {
        if (Context->Cache[Index].IPv4_Be == IPv4_Be && Context->Cache[Index].IsProbing) {
            DEBUG(TEXT("[ArpAllocateSlot] Found existing probing entry for IP %x at index %u"), Ntohl(IPv4_Be), Index);
            return &Context->Cache[Index];
        }
    }

    /* Find an empty slot */
    for (Index = 0; Index < ARP_CACHE_SIZE; Index++) {
        if (!Context->Cache[Index].IsValid) {
            Victim = Index;
            break;
        }
    }

    /* Otherwise evict the entry with the smallest TTL */
    if (Index == ARP_CACHE_SIZE) {
        U32 MinTtl = 0xFFFFFFFFU;
        for (Index = 0; Index < ARP_CACHE_SIZE; Index++) {
            if (Context->Cache[Index].TimeToLive < MinTtl) {
                MinTtl = Context->Cache[Index].TimeToLive;
                Victim = Index;
            }
        }
    }

    Context->Cache[Victim].IPv4_Be = IPv4_Be;
    Context->Cache[Victim].IsValid = 0;
    Context->Cache[Victim].IsProbing = 0;
    Context->Cache[Victim].TimeToLive = 0;
    return &Context->Cache[Victim];
}

/************************************************************************/

/**
 * @brief Updates the ARP cache with a resolved address.
 *
 * Creates or refreshes an entry and sets its TTL.
 *
 * @param IPv4_Be    IPv4 address in big-endian.
 * @param MacAddress Corresponding MAC address.
 */

static void ArpCacheUpdate(LPARP_CONTEXT Context, U32 IPv4_Be, const U8 MacAddress[6]) {
    LPARP_CACHE_ENTRY Entry;
    U8 WasProbing = 0;
    U8 MacChanged = 0;

    DEBUG(TEXT("[ArpCacheUpdate] Entry for IP %x, Context=%x"), Ntohl(IPv4_Be), (U32)Context);
    if (Context == NULL) {
        DEBUG(TEXT("[ArpCacheUpdate] Context is NULL, returning"));
        return;
    }

    // Validate MAC address before storing
    if (!IsValidMacAddress(MacAddress)) {
        DEBUG(TEXT("[ArpCacheUpdate] Invalid MAC address, ignoring update"));
        return;
    }

    Entry = ArpLookup(Context, IPv4_Be);
    if (!Entry) {
        DEBUG(TEXT("[ArpCacheUpdate] No existing entry, allocating new slot"));
        Entry = ArpAllocateSlot(Context, IPv4_Be);
    } else {
        DEBUG(TEXT("[ArpCacheUpdate] Found existing entry, IsProbing=%u IsValid=%u"), Entry->IsProbing, Entry->IsValid);
        // Check if MAC address changed for existing valid entries
        if (Entry->IsValid) {
            MacChanged = (MemoryCompare(Entry->MacAddress, MacAddress, 6) != 0);
        }
    }

    SAFE_USE(Entry) {
        WasProbing = Entry->IsProbing;
        DEBUG(TEXT("[ArpCacheUpdate] Entry=%x, WasProbing=%u MacChanged=%u before update"), (U32)Entry, WasProbing, MacChanged);
        MacCopy(Entry->MacAddress, MacAddress);
        Entry->IsValid = 1;
        Entry->IsProbing = 0;
        Entry->TimeToLive = ARP_ENTRY_TTL_TICKS;

        // Send notification if this was a pending resolution OR if MAC changed
        DEBUG(TEXT("[ArpCacheUpdate] WasProbing=%u MacChanged=%u, NotificationContext=%x"), WasProbing, MacChanged, (U32)Context->NotificationContext);
        if ((WasProbing || MacChanged) && Context->NotificationContext) {
            ARP_RESOLVED_DATA ResolvedData;
            ResolvedData.IPv4_Be = IPv4_Be;
            MacCopy(ResolvedData.MacAddress, MacAddress);

            DEBUG(TEXT("[ArpCacheUpdate] Sending ARP resolved notification for IP %x"), Ntohl(IPv4_Be));
            Notification_Send(Context->NotificationContext, NOTIF_EVENT_ARP_RESOLVED, &ResolvedData, sizeof(ResolvedData));
        } else {
            DEBUG(TEXT("[ArpCacheUpdate] No notification sent: WasProbing=%u MacChanged=%u, NotificationContext=%x"),
                  WasProbing, MacChanged, (U32)Context->NotificationContext);
        }

        // Signal success to adaptive delay if this was probing
        if (WasProbing) {
            AdaptiveDelay_OnSuccess(&Entry->DelayState);
        }
    }
}

/************************************************************************/
// Transmission

/**
 * @brief Sends a raw Ethernet frame.
 *
 * @param Data   Pointer to frame data.
 * @param Length Frame length in bytes.
 * @return 1 on success, otherwise 0.
 */

static int ArpSendFrame(LPARP_CONTEXT Context, const U8* Data, U32 Length) {
    NETWORKSEND Send;
    LPDEVICE Device;
    int Result = 0;

    if (Context == NULL || Context->Device == NULL) return 0;

    // Validate Data pointer and Length to prevent memory corruption
    if (Data == NULL || Length == 0) {
        DEBUG(TEXT("[ArpSendFrame] Invalid Data pointer or Length: Data=%x Length=%u"), (U32)Data, Length);
        return 0;
    }

    Device = Context->Device;

    LockMutex(&(Device->Mutex), INFINITY);

    Send.Device = (LPPCI_DEVICE)Device;
    Send.Data = Data;
    Send.Length = Length;
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        SAFE_USE_VALID_ID(((LPPCI_DEVICE)Device)->Driver, KOID_DRIVER) {
            Result = (((LPPCI_DEVICE)Device)->Driver->Command(DF_NT_SEND, (UINT)(LPVOID)&Send) == DF_ERROR_SUCCESS) ? 1 : 0;
        }
    }

    UnlockMutex(&(Device->Mutex));
    return Result;
}

/************************************************************************/

/**
 * @brief Sends an ARP request for the specified target.
 *
 * @param TargetIPv4_Be IPv4 address to resolve in big-endian.
 * @return 1 on success, otherwise 0.
 */

static int ArpSendRequest(LPARP_CONTEXT Context, U32 TargetIPv4_Be) {
    U8 Buffer[sizeof(ETHERNET_HEADER) + sizeof(ARP_PACKET)];
    LPETHERNET_HEADER Ethernet = (LPETHERNET_HEADER)Buffer;
    LPARP_PACKET Packet = (LPARP_PACKET)(Buffer + sizeof(ETHERNET_HEADER));
    int result;

    if (Context == NULL) return 0;

    // Validate buffer size to ensure structures haven't grown unexpectedly
    if (sizeof(Buffer) < sizeof(ETHERNET_HEADER) + sizeof(ARP_PACKET)) {
        DEBUG(TEXT("[ArpSendRequest] Buffer too small: %u < %u"),
              (U32)sizeof(Buffer), (U32)(sizeof(ETHERNET_HEADER) + sizeof(ARP_PACKET)));
        return 0;
    }

    U32 TargetIPHost = Ntohl(TargetIPv4_Be);
    DEBUG(TEXT("[ArpSendRequest] Sending ARP request for %u.%u.%u.%u"),
          (TargetIPHost >> 24) & 0xFF, (TargetIPHost >> 16) & 0xFF,
          (TargetIPHost >> 8) & 0xFF, TargetIPHost & 0xFF);

    /* Ethernet header */
    U8 BroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    MacCopy(Ethernet->Destination, BroadcastMac);
    MacCopy(Ethernet->Source, Context->LocalMacAddress);
    Ethernet->EthType = Htons(ETHTYPE_ARP);

    /* ARP payload */
    Packet->HardwareType = Htons(ARP_HTYPE_ETH);
    Packet->ProtocolType = Htons(ARP_PTYPE_IPV4);
    Packet->HardwareLength = ARP_HLEN_ETH;
    Packet->ProtocolLength = ARP_PLEN_IPV4;
    Packet->Operation = Htons(ARP_OP_REQUEST);

    MacCopy(Packet->SenderHardwareAddress, Context->LocalMacAddress);
    Packet->SenderProtocolAddress = Context->LocalIPv4_Be;

    MemorySet(Packet->TargetHardwareAddress, 0, 6);
    Packet->TargetProtocolAddress = TargetIPv4_Be;

    result = ArpSendFrame(Context, Buffer, (U32)sizeof(Buffer));
    DEBUG(TEXT("[ArpSendRequest] ArpSendFrame returned %d"), result);
    return result;
}

/************************************************************************/

/**
 * @brief Sends an ARP reply to the requester.
 *
 * @param DestinationMac      Target MAC address.
 * @param DestinationIPv4_Be  Target IPv4 address in big-endian.
 * @return 1 on success, otherwise 0.
 */

static int ArpSendReply(LPARP_CONTEXT Context, const U8 DestinationMac[6], U32 DestinationIPv4_Be) {
    U8 Buffer[sizeof(ETHERNET_HEADER) + sizeof(ARP_PACKET)];
    LPETHERNET_HEADER Ethernet = (LPETHERNET_HEADER)Buffer;
    LPARP_PACKET Packet = (LPARP_PACKET)(Buffer + sizeof(ETHERNET_HEADER));

    if (Context == NULL) return 0;

    // Validate buffer size to ensure structures haven't grown unexpectedly
    if (sizeof(Buffer) < sizeof(ETHERNET_HEADER) + sizeof(ARP_PACKET)) {
        DEBUG(TEXT("[ArpSendReply] Buffer too small: %u < %u"),
              (U32)sizeof(Buffer), (U32)(sizeof(ETHERNET_HEADER) + sizeof(ARP_PACKET)));
        return 0;
    }

    // Validate destination MAC address parameter
    if (DestinationMac == NULL) {
        DEBUG(TEXT("[ArpSendReply] NULL destination MAC address"));
        return 0;
    }

    // Validate MAC address before using it
    if (!IsValidMacAddress(DestinationMac)) {
        DEBUG(TEXT("[ArpSendReply] Invalid destination MAC address"));
        return 0;
    }

    MacCopy(Ethernet->Destination, DestinationMac);
    MacCopy(Ethernet->Source, Context->LocalMacAddress);
    Ethernet->EthType = Htons(ETHTYPE_ARP);

    Packet->HardwareType = Htons(ARP_HTYPE_ETH);
    Packet->ProtocolType = Htons(ARP_PTYPE_IPV4);
    Packet->HardwareLength = ARP_HLEN_ETH;
    Packet->ProtocolLength = ARP_PLEN_IPV4;
    Packet->Operation = Htons(ARP_OP_REPLY);

    /* Sender = us, Target = original requester */
    MacCopy(Packet->SenderHardwareAddress, Context->LocalMacAddress);
    Packet->SenderProtocolAddress = Context->LocalIPv4_Be;

    MacCopy(Packet->TargetHardwareAddress, DestinationMac);
    Packet->TargetProtocolAddress = DestinationIPv4_Be;

    return ArpSendFrame(Context, Buffer, (U32)sizeof(Buffer));
}

/************************************************************************/
// Receive path

/**
 * @brief Processes an incoming ARP packet.
 *
 * Updates the cache and replies if the packet targets the local address.
 *
 * @param Packet Pointer to the ARP packet.
 */

static void ArpHandlePacket(LPARP_CONTEXT Context, const ARP_PACKET* Packet) {
    U16 HardwareType, ProtocolType, Operation;

    if (Context == NULL) return;

    // Validate packet pointer to prevent NULL dereference
    if (Packet == NULL) {
        DEBUG(TEXT("[ArpHandlePacket] NULL packet pointer"));
        return;
    }

    HardwareType = Ntohs(Packet->HardwareType);
    ProtocolType = Ntohs(Packet->ProtocolType);
    Operation = Ntohs(Packet->Operation);

    if (HardwareType != ARP_HTYPE_ETH) return;
    if (ProtocolType != ARP_PTYPE_IPV4) return;
    if (Packet->HardwareLength != 6 || Packet->ProtocolLength != 4) return;
    if (Operation != ARP_OP_REQUEST && Operation != ARP_OP_REPLY) {
        DEBUG(TEXT("[ArpHandlePacket] Unsupported operation type: %u"), Operation);
        return;
    }

    // Validate sender MAC address before processing
    if (!IsValidMacAddress(Packet->SenderHardwareAddress)) {
        DEBUG(TEXT("[ArpHandlePacket] Invalid sender MAC address, ignoring packet"));
        return;
    }

    /* Update cache from sender */
    DEBUG(TEXT("[ArpHandlePacket] Calling ArpCacheUpdate for IP %x"), Ntohl(Packet->SenderProtocolAddress));
    ArpCacheUpdate(Context, Packet->SenderProtocolAddress, Packet->SenderHardwareAddress);

    /* If request targets our IP, send a reply */
    if (Operation == ARP_OP_REQUEST && Packet->TargetProtocolAddress == Context->LocalIPv4_Be) {
        ArpSendReply(Context, Packet->SenderHardwareAddress, Packet->SenderProtocolAddress);
    }
    /* For replies: cache already updated */
}

/************************************************************************/

/**
 * @brief Handles incoming Ethernet frames for ARP.
 *
 * Validates that the frame contains an ARP packet before processing it.
 *
 * @param Frame  Pointer to Ethernet frame data.
 * @param Length Frame length in bytes.
 */

void ARP_OnEthernetFrame(LPDEVICE Device, const U8* Frame, U32 Length) {
    LPARP_CONTEXT Context;

    DEBUG(TEXT("[ARP_OnEthernetFrame] Entry called Device=%x Frame=%x Length=%u"), (U32)Device, (U32)Frame, Length);

    if (Device == NULL || Frame == NULL) {
        DEBUG(TEXT("[ARP_OnEthernetFrame] NULL parameter: Device=%x Frame=%x"), (U32)Device, (U32)Frame);
        return;
    }
    if (Length < sizeof(ETHERNET_HEADER)) {
        DEBUG(TEXT("[ARP_OnEthernetFrame] Frame too short: %u < %u"), Length, (U32)sizeof(ETHERNET_HEADER));
        return;
    }

    Context = ARP_GetContext(Device);
    if (Context == NULL) {
        DEBUG(TEXT("[ARP_OnEthernetFrame] No ARP context for device %x"), (U32)Device);
        return;
    }

    const LPETHERNET_HEADER Ethernet = (const LPETHERNET_HEADER)Frame;
    U16 EthType = Ntohs(Ethernet->EthType);

    DEBUG(TEXT("[ARP_OnEthernetFrame] Received frame, EthType=%x, Length=%u"), EthType, Length);
    DEBUG(TEXT("[ARP_OnEthernetFrame] Dest MAC: %02X:%02X:%02X:%02X:%02X:%02X"),
          Ethernet->Destination[0], Ethernet->Destination[1], Ethernet->Destination[2],
          Ethernet->Destination[3], Ethernet->Destination[4], Ethernet->Destination[5]);
    DEBUG(TEXT("[ARP_OnEthernetFrame] Src MAC: %02X:%02X:%02X:%02X:%02X:%02X"),
          Ethernet->Source[0], Ethernet->Source[1], Ethernet->Source[2],
          Ethernet->Source[3], Ethernet->Source[4], Ethernet->Source[5]);

    if (EthType != ETHTYPE_ARP) {
        DEBUG(TEXT("[ARP_OnEthernetFrame] Not ARP packet, ignoring (EthType=%x)"), EthType);
        return;
    }

    DEBUG(TEXT("[ARP_OnEthernetFrame] Processing ARP packet"));

    if (Length < (U32)(sizeof(ETHERNET_HEADER) + sizeof(ARP_PACKET))) {
        DEBUG(TEXT("[ARP_OnEthernetFrame] ARP packet too short: %u < %u"), Length, (U32)(sizeof(ETHERNET_HEADER) + sizeof(ARP_PACKET)));
        return;
    }

    const LPARP_PACKET Packet = (const LPARP_PACKET)(Frame + sizeof(ETHERNET_HEADER));
    ArpHandlePacket(Context, Packet);
}

/************************************************************************/
// Public API

void ARP_Initialize(LPDEVICE Device, U32 LocalIPv4_Be, const NETWORKINFO* DeviceInfo) {
    LPARP_CONTEXT Context;
    U32 Index;
    BOOL Success = FALSE;
    BOOL MacRetrieved = FALSE;

    if (Device == NULL) return;

    Context = (LPARP_CONTEXT)KernelHeapAlloc(sizeof(ARP_CONTEXT));
    if (Context == NULL) return;

    Context->Device = Device;
    Context->LocalIPv4_Be = LocalIPv4_Be;
    Context->NotificationContext = Notification_CreateContext();
    if (Context->NotificationContext == NULL) {
        DEBUG(TEXT("[ARP_Initialize] Failed to create notification context"));
        goto Out;
    }

    for (Index = 0; Index < ARP_CACHE_SIZE; Index++) {
        Context->Cache[Index].IsValid = 0;
        Context->Cache[Index].IsProbing = 0;
        Context->Cache[Index].TimeToLive = 0;
        Context->Cache[Index].IPv4_Be = 0;
        AdaptiveDelay_Initialize(&Context->Cache[Index].DelayState);
    }

    LockMutex(&(Device->Mutex), INFINITY);

    if (DeviceInfo != NULL) {
        Context->LocalMacAddress[0] = DeviceInfo->MAC[0];
        Context->LocalMacAddress[1] = DeviceInfo->MAC[1];
        Context->LocalMacAddress[2] = DeviceInfo->MAC[2];
        Context->LocalMacAddress[3] = DeviceInfo->MAC[3];
        Context->LocalMacAddress[4] = DeviceInfo->MAC[4];
        Context->LocalMacAddress[5] = DeviceInfo->MAC[5];
        MacRetrieved = TRUE;
    } else {
        NETWORKGETINFO GetInfo;
        NETWORKINFO Info;
        MemorySet(&GetInfo, 0, sizeof(GetInfo));
        MemorySet(&Info, 0, sizeof(Info));
        GetInfo.Device = (LPPCI_DEVICE)Device;
        GetInfo.Info = &Info;

        SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
            SAFE_USE_VALID_ID(((LPPCI_DEVICE)Device)->Driver, KOID_DRIVER) {
                if (((LPPCI_DEVICE)Device)->Driver->Command(DF_NT_GETINFO, (UINT)(LPVOID)&GetInfo) == DF_ERROR_SUCCESS) {
                    DEBUG(TEXT("[ARP_Initialize] Network MAC = %x:%x:%x:%x:%x:%x"),
                          (U32)Info.MAC[0],
                          (U32)Info.MAC[1],
                          (U32)Info.MAC[2],
                          (U32)Info.MAC[3],
                          (U32)Info.MAC[4],
                          (U32)Info.MAC[5]);

                    Context->LocalMacAddress[0] = Info.MAC[0];
                    Context->LocalMacAddress[1] = Info.MAC[1];
                    Context->LocalMacAddress[2] = Info.MAC[2];
                    Context->LocalMacAddress[3] = Info.MAC[3];
                    Context->LocalMacAddress[4] = Info.MAC[4];
                    Context->LocalMacAddress[5] = Info.MAC[5];
                    MacRetrieved = TRUE;

                    DEBUG(TEXT("[ARP_Initialize] ARP layer initialized, callbacks handled by NetworkManager"));
                } else {
                    DEBUG(TEXT("[ARP_Initialize] DF_NT_GETINFO failed"));
                }
            } else {
                DEBUG(TEXT("[ARP_Initialize] Device driver not valid"));
            }
        } else {
            DEBUG(TEXT("[ARP_Initialize] Device not valid"));
        }
    }

    if (!MacRetrieved) {
        UnlockMutex(&(Device->Mutex));
        goto Out;
    }

    SetDeviceContext(Device, KOID_ARP, Context);
    Success = TRUE;

    UnlockMutex(&(Device->Mutex));

Out:
    if (!Success) {
        if (Context->NotificationContext) {
            Notification_DestroyContext(Context->NotificationContext);
            Context->NotificationContext = NULL;
        }
        KernelHeapFree(Context);
    }
}

/************************************************************************/

void ARP_SetLocalAddress(LPDEVICE Device, U32 LocalIPv4_Be) {
    LPARP_CONTEXT Context;

    if (Device == NULL) return;

    Context = ARP_GetContext(Device);
    if (Context == NULL) return;

    Context->LocalIPv4_Be = LocalIPv4_Be;

    U32 IpHost = Ntohl(LocalIPv4_Be);
    DEBUG(TEXT("[ARP_SetLocalAddress] Local IPv4 updated to %u.%u.%u.%u"),
          (IpHost >> 24) & 0xFF,
          (IpHost >> 16) & 0xFF,
          (IpHost >> 8) & 0xFF,
          IpHost & 0xFF);
}

/************************************************************************/

void ARP_Destroy(LPDEVICE Device) {
    LPARP_CONTEXT Context;

    if (Device == NULL) return;

    Context = ARP_GetContext(Device);
    if (Context == NULL) return;

    LockMutex(&(Device->Mutex), INFINITY);
    RemoveDeviceContext(Device, KOID_ARP);
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

void ARP_Tick(LPDEVICE Device) {
    LPARP_CONTEXT Context;
    U32 Index;

    if (Device == NULL) return;

    Context = ARP_GetContext(Device);
    if (Context == NULL) return;

    for (Index = 0; Index < ARP_CACHE_SIZE; Index++) {
        LPARP_CACHE_ENTRY Entry = &Context->Cache[Index];

        // Handle valid entries with TTL
        if (Entry->IsValid && Entry->TimeToLive) {
            Entry->TimeToLive--;
            if (Entry->TimeToLive == 0) {
                Entry->IsValid = 0;
                Entry->IsProbing = 0;
                Entry->IPv4_Be = 0;
                AdaptiveDelay_Reset(&Entry->DelayState);
            }
        }

        // Handle probing entries that need retry
        if (Entry->IsProbing && !Entry->IsValid && Entry->TimeToLive) {
            Entry->TimeToLive--;
            if (Entry->TimeToLive == 0) {
                // Time to send another ARP request
                if (AdaptiveDelay_ShouldContinue(&Entry->DelayState)) {
                    DEBUG(TEXT("[ARP_Tick] Sending retry ARP request for IP %x"), Ntohl(Entry->IPv4_Be));
                    ArpSendRequest(Context, Entry->IPv4_Be);
                    U32 NextDelay = AdaptiveDelay_GetNextDelay(&Entry->DelayState);
                    Entry->TimeToLive = NextDelay;
                } else {
                    DEBUG(TEXT("[ARP_Tick] Max retries reached for IP %x, giving up"), Ntohl(Entry->IPv4_Be));
                    Entry->IsProbing = 0;
                    AdaptiveDelay_Reset(&Entry->DelayState);
                }
            }
        }
    }
}

/************************************************************************/

int ARP_Resolve(LPDEVICE Device, U32 TargetIPv4_Be, U8 OutMacAddress[6]) {
    LPARP_CONTEXT Context;
    LPARP_CACHE_ENTRY Entry;

    if (Device == NULL || OutMacAddress == NULL) return 0;

    Context = ARP_GetContext(Device);
    if (Context == NULL) return 0;

    U32 TargetIPHost = Ntohl(TargetIPv4_Be);

    // Special-case: broadcast 255.255.255.255
    if (TargetIPHost == 0xFFFFFFFF) {
        // Return broadcast MAC immediately
        U8 BroadcastMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        MemoryCopy(OutMacAddress, BroadcastMac, 6);
        return 1;
    }

    // Ignore 0.0.0.0
    if (TargetIPHost == 0x00000000) {
        return 0;
    }
    DEBUG(TEXT("[ARP_Resolve] Resolving %u.%u.%u.%u"),
          (TargetIPHost >> 24) & 0xFF, (TargetIPHost >> 16) & 0xFF,
          (TargetIPHost >> 8) & 0xFF, TargetIPHost & 0xFF);

    Entry = ArpLookup(Context, TargetIPv4_Be);
    if (Entry && Entry->IsValid) {
        DEBUG(TEXT("[ARP_Resolve] Found in cache: %x:%x:%x:%x:%x:%x"),
              Entry->MacAddress[0], Entry->MacAddress[1], Entry->MacAddress[2],
              Entry->MacAddress[3], Entry->MacAddress[4], Entry->MacAddress[5]);
        MacCopy(OutMacAddress, Entry->MacAddress);
        return 1;
    }

    if (!Entry) {
        Entry = ArpAllocateSlot(Context, TargetIPv4_Be);
        DEBUG(TEXT("[ARP_Resolve] Allocated new cache entry"));
        if (Entry) {
            Entry->IPv4_Be = TargetIPv4_Be;
            AdaptiveDelay_Initialize(&Entry->DelayState);
        }
    }

    if (Entry && !Entry->IsProbing) {
        // First attempt - send immediately
        DEBUG(TEXT("[ARP_Resolve] Sending initial ARP request"));
        ArpSendRequest(Context, TargetIPv4_Be);
        Entry->IsProbing = 1;
        Entry->TimeToLive = ARP_PROBE_INTERVAL_TICKS;
        DEBUG(TEXT("[ARP_Resolve] Set Entry=%x IsProbing=1"), (U32)Entry);
        AdaptiveDelay_GetNextDelay(&Entry->DelayState); // Initialize delay state
    } else if (Entry && Entry->IsProbing) {
        // Check if we should retry based on adaptive delay
        if (AdaptiveDelay_ShouldContinue(&Entry->DelayState)) {
            U32 NextDelay = AdaptiveDelay_GetNextDelay(&Entry->DelayState);
            if (NextDelay > 0) {
                DEBUG(TEXT("[ARP_Resolve] Retry available, will wait %u ticks for next attempt"), NextDelay);
                Entry->TimeToLive = NextDelay;
            } else {
                DEBUG(TEXT("[ARP_Resolve] Max attempts reached, giving up"));
                Entry->IsProbing = 0;
                AdaptiveDelay_Reset(&Entry->DelayState);
                return 0;
            }
        } else {
            DEBUG(TEXT("[ARP_Resolve] No more retries allowed"));
            Entry->IsProbing = 0;
            AdaptiveDelay_Reset(&Entry->DelayState);
            return 0;
        }
    }
    return 0;
}

/************************************************************************/

void ARP_DumpCache(LPDEVICE Device) {
    LPARP_CONTEXT Context;
    U32 Index;

    if (Device == NULL) return;

    Context = ARP_GetContext(Device);
    if (Context == NULL) return;

    for (Index = 0; Index < ARP_CACHE_SIZE; Index++) {
        LPARP_CACHE_ENTRY Entry = &Context->Cache[Index];
        if (!Entry->IsValid) continue;

        U32 HostOrder = Ntohl(Entry->IPv4_Be);
        DEBUG(TEXT("[ARP] %u.%u.%u.%u -> %x:%x:%x:%x:%x:%x ttl=%u"), (U32)((HostOrder >> 24) & 0xFF),
            (U32)((HostOrder >> 16) & 0xFF), (U32)((HostOrder >> 8) & 0xFF), (U32)((HostOrder >> 0) & 0xFF),
            (U32)Entry->MacAddress[0], (U32)Entry->MacAddress[1], (U32)Entry->MacAddress[2], (U32)Entry->MacAddress[3],
            (U32)Entry->MacAddress[4], (U32)Entry->MacAddress[5], (U32)Entry->TimeToLive);
    }
}

/************************************************************************/

/**
 * @brief Registers a callback for ARP notifications.
 * @param Device Device context.
 * @param EventID Event ID to register for.
 * @param Callback Callback function.
 * @param UserData User data passed to callback.
 * @return 1 on success, 0 on failure.
 */
U32 ARP_RegisterNotification(LPDEVICE Device, U32 EventID, NOTIFICATION_CALLBACK Callback, LPVOID UserData) {
    LPARP_CONTEXT Context;

    if (!Device || !Callback) return 0;

    Context = ARP_GetContext(Device);
    if (!Context || !Context->NotificationContext) return 0;

    return Notification_Register(Context->NotificationContext, EventID, Callback, UserData);
}

/************************************************************************/

/**
 * @brief Unregisters a callback for ARP notifications.
 * @param Device Device context.
 * @param EventID Event ID to unregister from.
 * @param Callback Callback function.
 * @param UserData User data passed to callback.
 * @return 1 on success, 0 on failure.
 */
U32 ARP_UnregisterNotification(LPDEVICE Device, U32 EventID, NOTIFICATION_CALLBACK Callback, LPVOID UserData) {
    LPARP_CONTEXT Context;

    if (!Device || !Callback) return 0;

    Context = ARP_GetContext(Device);
    if (!Context || !Context->NotificationContext) return 0;

    return Notification_Unregister(Context->NotificationContext, EventID, Callback, UserData);
}
