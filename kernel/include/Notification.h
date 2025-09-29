
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


    Generic Notification System

\************************************************************************/

#ifndef NOTIFICATION_H_INCLUDED
#define NOTIFICATION_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "List.h"

/************************************************************************/
// Event IDs

#define NOTIF_EVENT_ARP_RESOLVED    0x00000001
#define NOTIF_EVENT_ARP_FAILED      0x00000002
#define NOTIF_EVENT_TCP_CONNECTED   0x00000003
#define NOTIF_EVENT_TCP_FAILED      0x00000004
#define NOTIF_EVENT_TCP_DATA        0x00000005
#define NOTIF_EVENT_IPV4_PACKET_SENT 0x00000006

/************************************************************************/
// Structures

typedef struct tag_NOTIFICATION_DATA {
    U32 EventID;
    U32 DataSize;
    LPVOID Data;
} NOTIFICATION_DATA, *LPNOTIFICATION_DATA;

typedef void (*NOTIFICATION_CALLBACK)(LPNOTIFICATION_DATA NotificationData, LPVOID UserData);

typedef struct tag_NOTIFICATION_ENTRY {
    LISTNODE_FIELDS
    U32 EventID;
    NOTIFICATION_CALLBACK Callback;
    LPVOID UserData;
} NOTIFICATION_ENTRY, *LPNOTIFICATION_ENTRY;

typedef struct tag_NOTIFICATION_CONTEXT {
    LPLIST NotificationList;
} NOTIFICATION_CONTEXT, *LPNOTIFICATION_CONTEXT;

/************************************************************************/
// ARP Notification Data

typedef struct tag_ARP_RESOLVED_DATA {
    U32 IPv4_Be;
    U8 MacAddress[6];
} ARP_RESOLVED_DATA, *LPARP_RESOLVED_DATA;

typedef struct tag_ARP_FAILED_DATA {
    U32 IPv4_Be;
} ARP_FAILED_DATA, *LPARP_FAILED_DATA;

typedef struct tag_IPV4_PACKET_SENT_DATA {
    U32 DestinationIP;
    U8 Protocol;
    U32 PayloadLength;
} IPV4_PACKET_SENT_DATA, *LPIPV4_PACKET_SENT_DATA;

/************************************************************************/
// Functions

LPNOTIFICATION_CONTEXT Notification_CreateContext(void);
void Notification_DestroyContext(LPNOTIFICATION_CONTEXT Context);
U32 Notification_Register(LPNOTIFICATION_CONTEXT Context, U32 EventID, NOTIFICATION_CALLBACK Callback, LPVOID UserData);
U32 Notification_Unregister(LPNOTIFICATION_CONTEXT Context, U32 EventID, NOTIFICATION_CALLBACK Callback, LPVOID UserData);
void Notification_Send(LPNOTIFICATION_CONTEXT Context, U32 EventID, LPVOID Data, U32 DataSize);

#endif  // NOTIFICATION_H_INCLUDED
