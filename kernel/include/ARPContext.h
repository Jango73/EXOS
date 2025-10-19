
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


    ARP Context - Per-device ARP context

\************************************************************************/

#ifndef ARPCONTEXT_H_INCLUDED
#define ARPCONTEXT_H_INCLUDED

#include "Base.h"
#include "Device.h"
#include "Network.h"
#include "Endianness.h"
#include "utils/Notification.h"
#include "utils/AdaptiveDelay.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#define ARP_CACHE_SIZE 32U
#define ARP_ENTRY_TTL_TICKS 600U    /* ~10 minutes if ARP_Tick is called each 1s */
#define ARP_PROBE_INTERVAL_TICKS 3U /* pacing for repeated requests */

/************************************************************************/

typedef struct tag_ARP_CACHE_ENTRY {
    U32 IPv4_Be; /* IPv4 address (big-endian) */
    U8 MacAddress[6];
    U32 TimeToLive; /* in ticks */
    U8 IsValid;
    U8 IsProbing; /* request already sent recently */
    ADAPTIVE_DELAY_STATE DelayState; /* Adaptive delay for this entry */
} ARP_CACHE_ENTRY, *LPARP_CACHE_ENTRY;

typedef struct tag_ARP_CONTEXT {
    LPDEVICE Device;

    U8 LocalMacAddress[6];
    U32 LocalIPv4_Be;

    ARP_CACHE_ENTRY Cache[ARP_CACHE_SIZE];

    LPNOTIFICATION_CONTEXT NotificationContext;
} ARP_CONTEXT, *LPARP_CONTEXT;

/************************************************************************/

LPARP_CONTEXT ARP_GetContext(LPDEVICE Device);
void ARP_Initialize(LPDEVICE Device, U32 LocalIPv4_Be, const NETWORKINFO* DeviceInfo);
void ARP_Destroy(LPDEVICE Device);
void ARP_Tick(LPDEVICE Device);
int ARP_Resolve(LPDEVICE Device, U32 TargetIPv4_Be, U8 OutMacAddress[6]);
void ARP_DumpCache(LPDEVICE Device);
void ARP_OnEthernetFrame(LPDEVICE Device, const U8* Frame, U32 Length);
void ARP_SetLocalAddress(LPDEVICE Device, U32 LocalIPv4_Be);
U32 ARP_RegisterNotification(LPDEVICE Device, U32 EventID, NOTIFICATION_CALLBACK Callback, LPVOID UserData);
U32 ARP_UnregisterNotification(LPDEVICE Device, U32 EventID, NOTIFICATION_CALLBACK Callback, LPVOID UserData);

/************************************************************************/

#pragma pack(pop)

#endif  // ARPCONTEXT_H_INCLUDED