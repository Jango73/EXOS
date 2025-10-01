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


    DHCP Context - Per-device DHCP context

\************************************************************************/

#ifndef DHCPCONTEXT_H_INCLUDED
#define DHCPCONTEXT_H_INCLUDED

#include "Base.h"
#include "Device.h"
#include "DHCP.h"
#include "Endianness.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/
// DHCP Client States

#define DHCP_STATE_INIT 0
#define DHCP_STATE_SELECTING 1
#define DHCP_STATE_REQUESTING 2
#define DHCP_STATE_BOUND 3
#define DHCP_STATE_RENEWING 4
#define DHCP_STATE_REBINDING 5
#define DHCP_STATE_FAILED 6

/************************************************************************/
// Configuration

#define DHCP_RETRY_TIMEOUT_MILLIS (30 * 1000)  // 30 seconds for retry
#define DHCP_MAX_RETRIES 5

/************************************************************************/

typedef struct tag_DHCP_CONTEXT {
    LPDEVICE Device;

    U8 LocalMacAddress[6];
    U32 TransactionID;
    U32 State;

    // Timing
    U32 StartTicks;
    U32 RetryCount;

    // Offered/Assigned configuration
    U32 OfferedIP_Be;
    U32 SubnetMask_Be;
    U32 Gateway_Be;
    U32 DNSServer_Be;
    U32 ServerID_Be;
    U32 LeaseTime;      // Seconds
    U32 RenewalTime;    // T1 (seconds)
    U32 RebindTime;     // T2 (seconds)

    // Lease management
    U32 LeaseStartMillis;
} DHCP_CONTEXT, *LPDHCP_CONTEXT;

/************************************************************************/

LPDHCP_CONTEXT DHCP_GetContext(LPDEVICE Device);
void DHCP_Initialize(LPDEVICE Device);
void DHCP_Destroy(LPDEVICE Device);
void DHCP_Start(LPDEVICE Device);
void DHCP_Tick(LPDEVICE Device);
void DHCP_OnUDPPacket(U32 SourceIP, U16 SourcePort, U16 DestinationPort, const U8* Payload, U32 PayloadLength);

/************************************************************************/

#pragma pack(pop)

#endif  // DHCPCONTEXT_H_INCLUDED
