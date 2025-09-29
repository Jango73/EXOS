
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


    IPv4 Context - Per-device IPv4 context

\************************************************************************/

#ifndef IPV4CONTEXT_H_INCLUDED
#define IPV4CONTEXT_H_INCLUDED

#include "Base.h"
#include "Device.h"
#include "IPv4.h"
#include "Endianness.h"
#include "Notification.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#define IPV4_MAX_PROTOCOLS 256
#define IPV4_MAX_PENDING_PACKETS 16

// IPv4_Send return codes
#define IPV4_SEND_FAILED 0
#define IPV4_SEND_PENDING 1
#define IPV4_SEND_IMMEDIATE 2

/************************************************************************/

typedef struct tag_IPV4_PENDING_PACKET {
    U32 DestinationIP;
    U32 NextHopIP;
    U8 Protocol;
    U8 Payload[1500];  // Maximum Ethernet payload
    U32 PayloadLength;
    U32 IsValid;
} IPV4_PENDING_PACKET, *LPIPV4_PENDING_PACKET;

typedef struct tag_IPV4_CONTEXT {
    LPDEVICE Device;
    U32 LocalIPv4_Be;
    U32 NetmaskBe;
    U32 DefaultGatewayBe;
    IPv4_ProtocolHandler ProtocolHandlers[IPV4_MAX_PROTOCOLS];
    IPV4_PENDING_PACKET PendingPackets[IPV4_MAX_PENDING_PACKETS];
    U32 ARPCallbackRegistered;
    LPNOTIFICATION_CONTEXT NotificationContext;
} IPV4_CONTEXT, *LPIPV4_CONTEXT;

/************************************************************************/

LPIPV4_CONTEXT IPv4_GetContext(LPDEVICE Device);
void IPv4_Initialize(LPDEVICE Device, U32 LocalIPv4_Be);
void IPv4_Destroy(LPDEVICE Device);
void IPv4_SetLocalAddress(LPDEVICE Device, U32 LocalIPv4_Be);
void IPv4_SetNetworkConfig(LPDEVICE Device, U32 LocalIPv4_Be, U32 NetmaskBe, U32 DefaultGatewayBe);
void IPv4_RegisterProtocolHandler(LPDEVICE Device, U8 Protocol, IPv4_ProtocolHandler Handler);
int IPv4_Send(LPDEVICE Device, U32 DestinationIP, U8 Protocol, const U8* Payload, U32 PayloadLength);
void IPv4_OnEthernetFrame(LPDEVICE Device, const U8* Frame, U32 Length);
void IPv4_ARPResolvedCallback(LPNOTIFICATION_DATA NotificationData, LPVOID UserData);
int IPv4_AddPendingPacket(LPIPV4_CONTEXT Context, U32 DestinationIP, U32 NextHopIP, U8 Protocol, const U8* Payload, U32 PayloadLength);
void IPv4_ProcessPendingPackets(LPIPV4_CONTEXT Context, U32 ResolvedIP);
U32 IPv4_RegisterNotification(LPDEVICE Device, U32 EventID, NOTIFICATION_CALLBACK Callback, LPVOID UserData);

/************************************************************************/

#pragma pack(pop)

#endif // IPV4CONTEXT_H_INCLUDED
