
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

/***************************************************************************/

#define ID_NONE 0x00000000
#define ID_PROCESS 0x434F5250           // "PROC"
#define ID_TASK 0x4B534154              // "TASK"
#define ID_MUTEX 0x4D555458             // "MUTX"
#define ID_SECURITY 0x55434553          // "SECU"
#define ID_MESSAGE 0x4753534D           // "MSSG"
#define ID_HEAP 0x50414548              // "HEAP"
#define ID_DRIVER 0x52565244            // "DRVR"
#define ID_PCIDEVICE 0x44494350         // "PCID"
#define ID_DISK 0x4B534944              // "DISK"
#define ID_IOCONTROL 0x54434F49         // "IOCT"
#define ID_FILESYSTEM 0x53595346        // "FSYS"
#define ID_FILE 0x454C4946              // "FILE"
#define ID_GRAPHICSCONTEXT 0x43584647   // "GFXC"
#define ID_DESKTOP 0x544B5344           // "DSKT"
#define ID_WINDOW 0x444E4957            // "WIND"
#define ID_BRUSH 0x48535242             // "BRSH"
#define ID_PEN 0x5F4E4550               // "PEN_"
#define ID_FONT 0x544E4F46              // "FONT"
#define ID_BITMAP 0x504D5442            // "BTMP"
#define ID_USERACCOUNT 0x52455355       // "USER"
#define ID_USERSESSION 0x53534553       // "SESS"
#define ID_NETWORKDEVICE 0x4454454E     // "NETD"
#define ID_SOCKET 0x4B434F53            // "SOCK"
#define ID_ARP 0x5F505241               // "ARP_"
#define ID_IPV4 0x34565049              // "IPV4"
#define ID_TCP 0x5F504354               // "TCP_"

/***************************************************************************/

#endif  // ID_H_INCLUDED
