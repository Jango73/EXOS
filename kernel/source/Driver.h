
// Driver.h

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#ifndef DRIVER_H_INCLUDED
#define DRIVER_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "ID.h"
#include "List.h"

/***************************************************************************/

#define DRIVER_TYPE_NONE 0x00000000
#define DRIVER_TYPE_FLOPPYDISK 0x00000001
#define DRIVER_TYPE_HARDDISK 0x00000002
#define DRIVER_TYPE_RAMDISK 0x00000003
#define DRIVER_TYPE_FILESYSTEM 0x00000004
#define DRIVER_TYPE_KEYBOARD 0x00000005
#define DRIVER_TYPE_GRAPHICS 0x00000006
#define DRIVER_TYPE_MONITOR 0x00000007
#define DRIVER_TYPE_MOUSE 0x00000008
#define DRIVER_TYPE_CDROM 0x00000009
#define DRIVER_TYPE_MODEM 0x0000000A
#define DRIVER_TYPE_NETWORK 0x0000000B
#define DRIVER_TYPE_WAVE 0x0000000C
#define DRIVER_TYPE_MIDI 0x0000000D
#define DRIVER_TYPE_SYNTH 0x0000000E
#define DRIVER_TYPE_PRINTER 0x0000000F
#define DRIVER_TYPE_SCANNER 0x00000010
#define DRIVER_TYPE_GRAPHTABLE 0x00000011
#define DRIVER_TYPE_DVD 0x00000012
#define DRIVER_TYPE_OTHER 0xFFFFFFFF

/***************************************************************************/

#define DF_LOAD 0x0000
#define DF_UNLOAD 0x0001
#define DF_GETVERSION 0x0002
#define DF_GETCAPS 0x0003
#define DF_GETLASTFUNC 0x0004
#define DF_FIRSTFUNC 0x1000

/***************************************************************************/
// Error codes common to all drivers

#define DF_ERROR_SUCCESS 0x00000000
#define DF_ERROR_NOTIMPL 0x00000001
#define DF_ERROR_BADPARAM 0x00000002
#define DF_ERROR_NOMEMORY 0x00000003
#define DF_ERROR_UNEXPECT 0x00000004
#define DF_ERROR_IO 0x00000005
#define DF_ERROR_NOPERM 0x00000006
#define DF_ERROR_FIRST 0x00001000
#define DF_ERROR_GENERIC 0xFFFFFFFF

/***************************************************************************/

typedef U32 (*DRVFUNC)(U32, U32);

/***************************************************************************/

typedef struct tag_DRIVER {
    LISTNODE_FIELDS
    U32 Type;
    U32 VersionMajor;
    U32 VersionMinor;
    STR Designer[MAX_NAME];
    STR Manufacturer[MAX_NAME];
    STR Product[MAX_NAME];
    DRVFUNC Command;
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
// Fixed standard drivers

extern DRIVER StdKeyboardDriver;
extern DRIVER SerialMouseDriver;
extern DRIVER StdHardDiskDriver;
extern DRIVER RAMDiskDriver;
extern DRIVER VESADriver;
extern DRIVER XFSDriver;

/***************************************************************************/

#endif
