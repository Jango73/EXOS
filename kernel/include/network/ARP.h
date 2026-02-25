
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

#ifndef ARP_H_INCLUDED
#define ARP_H_INCLUDED

#include "Base.h"
#include "Driver.h"
#include "network/Network.h"
#include "drivers/bus/PCI.h"
#include "CoreString.h"
#include "Endianness.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/
// EtherTypes
#define ETHTYPE_IPV4 0x0800
#define ETHTYPE_ARP 0x0806

// ARP constants
#define ARP_HTYPE_ETH 0x0001
#define ARP_PTYPE_IPV4 0x0800
#define ARP_HLEN_ETH 6
#define ARP_PLEN_IPV4 4
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2

/************************************************************************/

typedef struct tag_ETHERNET_HEADER {
    U8 Destination[6];
    U8 Source[6];
    U16 EthType;  // Big-endian on the wire
} ETHERNET_HEADER, *LPETHERNET_HEADER;

typedef struct tag_ARP_PACKET {
    U16 HardwareType;   // 1 = Ethernet (be)
    U16 ProtocolType;   /// 0x0800 = IPv4 (be)
    U8 HardwareLength;  // 6
    U8 ProtocolLength;  // 4
    U16 Operation;      // 1 = request, 2 = reply (be)

    U8 SenderHardwareAddress[6];  // MAC
    U32 SenderProtocolAddress;    // IPv4 (be)

    U8 TargetHardwareAddress[6];  // MAC
    U32 TargetProtocolAddress;    // IPv4 (be)
} ARP_PACKET, *LPARP_PACKET;

// Per-device ARP API
#include "ARPContext.h"

/************************************************************************/

#pragma pack(pop)

#endif  // ARP_H_INCLUDED
