
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

#ifndef IPV4_H_INCLUDED
#define IPV4_H_INCLUDED

#include "Base.h"
#include "Driver.h"
#include "Network.h"
#include "PCI.h"
#include "Endianness.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/
// IPv4 Protocol Numbers

#define IPV4_PROTOCOL_ICMP 1
#define IPV4_PROTOCOL_TCP 6
#define IPV4_PROTOCOL_UDP 17

// IPv4 Flags

#define IPV4_FLAG_DONT_FRAGMENT 0x4000
#define IPV4_FLAG_MORE_FRAGMENTS 0x2000
#define IPV4_FRAGMENT_OFFSET_MASK 0x1FFF

/************************************************************************/
// Words are in network byte order

typedef struct tag_IPV4_HEADER {
    U8 VersionIHL;          // Version (4 bits) + IHL (4 bits)
    U8 TypeOfService;       // Type of Service / DSCP
    U16 TotalLength;        // Total packet length
    U16 Identification;     // Fragment identification
    U16 FlagsFragmentOffset; // Flags (3 bits) + Fragment offset (13 bits)
    U8 TimeToLive;          // TTL
    U8 Protocol;            // Next protocol
    U16 HeaderChecksum;     // Header checksum
    U32 SourceAddress;      // Source IPv4 address
    U32 DestinationAddress; // Destination IPv4 address
} IPV4_HEADER, *LPIPV4_HEADER;

/************************************************************************/
// Callback type for protocol handlers

typedef void (*IPv4_ProtocolHandler)(const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP);

/************************************************************************/
// Per-device IPv4 API
#include "IPv4Context.h"

// Utility functions
U16 IPv4_CalculateChecksum(IPV4_HEADER* Header);
int IPv4_ValidateChecksum(IPV4_HEADER* Header);

/************************************************************************/

#pragma pack(pop)

#endif // IPV4_H_INCLUDED
