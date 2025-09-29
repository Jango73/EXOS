
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


    Endianness

\************************************************************************/

#ifndef ENDIANNESS_H_INCLUDED
#define ENDIANNESS_H_INCLUDED

#include "../include/Base.h"

/************************************************************************\

    Network byte order is defined as big endian.

    This convention dates back to the early design of ARP/IP/TCP in the 1970s–80s
    (Jon Postel, Vint Cerf, Bob Kahn, etc.), when many host systems (e.g. Motorola 68000,
    PDP-11, mainframes) used big-endian architectures. Standardizing multi-byte fields
    in big endian ensured consistent interpretation of packets across heterogeneous
    hardware.

    Even though most modern CPUs (x86, little-endian ARM) use little endian internally,
    all multi-byte values in network protocols are still transmitted in big endian.
    Conversion between host and network byte order must be done at the protocol edges.

\************************************************************************/

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

static inline U16 Htons(U16 Value) { return Value; }

static inline U16 Ntohs(U16 Value) { return Value; }

static inline U32 Htonl(U32 Value) { return Value; }

static inline U32 Ntohl(U32 Value) { return Value; }

#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

static inline U16 Htons(U16 Value) { return (U16)((Value << 8) | (Value >> 8)); }

static inline U16 Ntohs(U16 Value) { return Htons(Value); }

static inline U32 Htonl(U32 Value) {
    return ((Value & 0x000000FFU) << 24) | ((Value & 0x0000FF00U) << 8) | ((Value & 0x00FF0000U) >> 8) |
           ((Value & 0xFF000000U) >> 24);
}

static inline U32 Ntohl(U32 Value) { return Htonl(Value); }

#else
    #error "Endianness not defined"
#endif

/************************************************************************/

#endif  // ENDIANNESS_H_INCLUDED
