
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


    Driver

\************************************************************************/

#ifndef DRIVER_H_INCLUDED
#define DRIVER_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "ID.h"
#include "List.h"
#include "DriverEnum.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

#define DRIVER_TYPE_NONE 0x00000000
#define DRIVER_TYPE_INIT 0x00000001
#define DRIVER_TYPE_CLOCK 0x00000002
#define DRIVER_TYPE_CONSOLE 0x00000003
#define DRIVER_TYPE_INTERRUPT 0x00000004
#define DRIVER_TYPE_MEMORY 0x00000005
#define DRIVER_TYPE_FLOPPYDISK 0x00000006
#define DRIVER_TYPE_HARDDISK 0x00000007
#define DRIVER_TYPE_RAMDISK 0x00000008
#define DRIVER_TYPE_FILESYSTEM 0x00000009
#define DRIVER_TYPE_KEYBOARD 0x0000000A
#define DRIVER_TYPE_GRAPHICS 0x0000000B
#define DRIVER_TYPE_MONITOR 0x0000000C
#define DRIVER_TYPE_MOUSE 0x0000000D
#define DRIVER_TYPE_CDROM 0x0000000E
#define DRIVER_TYPE_MODEM 0x0000000F
#define DRIVER_TYPE_NETWORK 0x00000010
#define DRIVER_TYPE_WAVE 0x00000011
#define DRIVER_TYPE_MIDI 0x00000012
#define DRIVER_TYPE_SYNTH 0x00000013
#define DRIVER_TYPE_PRINTER 0x00000014
#define DRIVER_TYPE_SCANNER 0x00000015
#define DRIVER_TYPE_GRAPHTABLE 0x00000016
#define DRIVER_TYPE_DVD 0x00000017
#define DRIVER_TYPE_OTHER 0xFFFFFFFF

/***************************************************************************/

#define DRIVER_FLAG_READY 0x00000001
#define DRIVER_FLAG_CRITICAL 0x00000002

/***************************************************************************/

typedef UINT (*DRVFUNC)(UINT Function, UINT Parameter);

/***************************************************************************/

#define DRIVER_FIELDS                   \
    U32 Type;                           \
    U32 VersionMajor;                   \
    U32 VersionMinor;                   \
    STR Designer[MAX_NAME];             \
    STR Manufacturer[MAX_NAME];         \
    STR Product[MAX_NAME];              \
    U32 Flags;                          \
    DRVFUNC Command;                    \
    UINT EnumDomainCount;               \
    UINT EnumDomains[DRIVER_ENUM_MAX_DOMAINS];

typedef struct tag_DRIVER {
    LISTNODE_FIELDS
    DRIVER_FIELDS
} DRIVER, *LPDRIVER;

/***************************************************************************/
// Structure to retrieve driver capabilities

#define DRIVER_CAPS1_CREATEFOLDERS 0x00000001
#define DRIVER_CAPS1_CREATEFILES 0x00000002
#define DRIVER_CAPS1_DISPLAYGRAPHICS 0x00000004
#define DRIVER_CAPS1_CAPTUREGRAPHICS 0x00000008
#define DRIVER_CAPS1_PLAYSOUND 0x00000010
#define DRIVER_CAPS1_RECORDSOUND 0x00000020

typedef struct tag_DRIVERCAPS {
    U32 Size;
    U32 Caps1;
} DRIVERCAPS, *LPDRIVERCAPS;

/***************************************************************************/
// EXOS Driver Services

#define DRVCALL_RequestIRQ 0x00000000
#define DRVCALL_ReleaseIRQ 0x00000001
#define DRVCALL_RequestRegion 0x00000002
#define DRVCALL_ReleaseRegion 0x00000003
#define DRVCALL_RequestDMA 0x00000004
#define DRVCALL_ReleaseDMA 0x00000005

/***************************************************************************/

#pragma pack(pop)

#endif
