
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


    UDP Context - Per-device UDP context

\************************************************************************/

#ifndef UDPCONTEXT_H_INCLUDED
#define UDPCONTEXT_H_INCLUDED

#include "Base.h"
#include "Device.h"
#include "UDP.h"
#include "Endianness.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#define UDP_MAX_PORTS 16

/************************************************************************/

typedef struct tag_UDP_PORT_BINDING {
    U16 Port;
    UDP_PortHandler Handler;
    U32 IsValid;
} UDP_PORT_BINDING, *LPUDP_PORT_BINDING;

typedef struct tag_UDP_CONTEXT {
    LPDEVICE Device;
    UDP_PORT_BINDING PortBindings[UDP_MAX_PORTS];
} UDP_CONTEXT, *LPUDP_CONTEXT;

/************************************************************************/

LPUDP_CONTEXT UDP_GetContext(LPDEVICE Device);
void UDP_Initialize(LPDEVICE Device);
void UDP_Destroy(LPDEVICE Device);
void UDP_RegisterPortHandler(LPDEVICE Device, U16 Port, UDP_PortHandler Handler);
void UDP_UnregisterPortHandler(LPDEVICE Device, U16 Port);
int UDP_Send(LPDEVICE Device, U32 DestinationIP, U16 SourcePort, U16 DestinationPort, const U8* Payload, U32 PayloadLength);
void UDP_OnIPv4Packet(const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP);

/************************************************************************/

#pragma pack(pop)

#endif // UDPCONTEXT_H_INCLUDED
