
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


    ID

\************************************************************************/

#ifndef ID_H_INCLUDED
#define ID_H_INCLUDED

/************************************************************************/
// Kernel object type identifiers

#define KOID_NONE 0x00000000
#define KOID_PROCESS 0x434F5250           // "PROC"
#define KOID_TASK 0x4B534154              // "TASK"
#define KOID_MUTEX 0x4D555458             // "MUTX"
#define KOID_SECURITY 0x55434553          // "SECU"
#define KOID_MESSAGE 0x4753534D           // "MSSG"
#define KOID_HEAP 0x50414548              // "HEAP"
#define KOID_DRIVER 0x52565244            // "DRVR"
#define KOID_PCIDEVICE 0x44494350         // "PCID"
#define KOID_DISK 0x4B534944              // "DISK"
#define KOID_IOCONTROL 0x54434F49         // "IOCT"
#define KOID_FILESYSTEM 0x53595346        // "FSYS"
#define KOID_FILE 0x454C4946              // "FILE"
#define KOID_GRAPHICSCONTEXT 0x43584647   // "GFXC"
#define KOID_DESKTOP 0x544B5344           // "DSKT"
#define KOID_WINDOW 0x444E4957            // "WIND"
#define KOID_BRUSH 0x48535242             // "BRSH"
#define KOID_PEN 0x5F4E4550               // "PEN_"
#define KOID_FONT 0x544E4F46              // "FONT"
#define KOID_BITMAP 0x504D5442            // "BTMP"
#define KOID_USERACCOUNT 0x52455355       // "USER"
#define KOID_USERSESSION 0x53534553       // "SESS"
#define KOID_NETWORKDEVICE 0x4454454E     // "NETD"
#define KOID_SOCKET 0x4B434F53            // "SOCK"
#define KOID_ARP 0x5F505241               // "ARP_"
#define KOID_IPV4 0x34565049              // "IPV4"
#define KOID_UDP 0x5F504455               // "UDP_"
#define KOID_DHCP 0x50434844              // "DHCP"
#define KOID_TCP 0x5F504354               // "TCP_"
#define KOID_KERNELEVENT 0x544E5645       // "EVNT"
#define KOID_MEMORY_REGION_DESCRIPTOR 0x44524D56 // "VMRD"

/************************************************************************/

#endif  // ID_H_INCLUDED
