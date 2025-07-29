
// HD.H

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#ifndef HD_H_INCLUDED
#define HD_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Driver.h"
#include "ID.h"

/***************************************************************************/

#pragma pack (1)

/***************************************************************************/

// Functions supplied by a disk driver

#define DF_DISK_RESET     (DF_FIRSTFUNC + 0)
#define DF_DISK_READ      (DF_FIRSTFUNC + 1)
#define DF_DISK_WRITE     (DF_FIRSTFUNC + 2)
#define DF_DISK_GETINFO   (DF_FIRSTFUNC + 3)
#define DF_DISK_SETACCESS (DF_FIRSTFUNC + 4)

/***************************************************************************/

typedef U32 SECTOR;
typedef U32 CLUSTER;

/***************************************************************************/

#define SECTOR_SIZE 512

/***************************************************************************/

#define HD_IRQ 0x0E

#define HD_PORT_0 0x01F0
#define HD_PORT_1 0x0170

#define HD_DATA         0x00
#define HD_ERROR        0x01      // Read only
#define HD_NUMSECTORS   0x02
#define HD_SECTOR       0x03
#define HD_CYLINDERLOW  0x04
#define HD_CYLINDERHIGH 0x05
#define HD_HEAD         0x06
#define HD_STATUS       0x07      // Read only
#define HD_FEATURE      HD_ERROR  // Write only
#define HD_COMMAND      HD_STATUS // Write only

// Bit pattern of HD_HEAD : 101DHHHH (D = Drive, H = Head)

#define HD_ALTCOMMAND   0x03F6 // Used for resets
#define HD_ALTSTATUS    0x03F6 // Same as HD_STATUS but doesn't clear irq

// Bit values for Status

#define HD_STATUS_ERROR  0x01
#define HD_STATUS_INDEX  0x02
#define HD_STATUS_ECC    0x04
#define HD_STATUS_DRQ    0x08
#define HD_STATUS_SEEK   0x10
#define HD_STATUS_WERROR 0x20
#define HD_STATUS_READY  0x40
#define HD_STATUS_BUSY   0x80

// Values for command

#define HD_COMMAND_RESTORE     0x10
#define HD_COMMAND_READ        0x20
#define HD_COMMAND_WRITE       0x30
#define HD_COMMAND_VERIFY      0x40
#define HD_COMMAND_FORMAT      0x50
#define HD_COMMAND_INIT        0x60
#define HD_COMMAND_SEEK        0x70
#define HD_COMMAND_DIAGNOSE    0x90
#define HD_COMMAND_SPECIFY     0x91    // Set drive geometry translation
#define HD_COMMAND_SETIDLE1    0xE3
#define HD_COMMAND_SETIDLE2    0x97

#define HD_COMMAND_DOORLOCK    0xDE    // Lock door on removable drives
#define HD_COMMAND_DOORUNLOCK  0xDF    // Unlock door on removable drives
#define HD_COMMAND_ACKMC       0xDB    // Acknowledge media change

#define HD_COMMAND_MULTREAD    0xC4    // Read sectors using multiple mode
#define HD_COMMAND_MULTWRITE   0xC5    // Write sectors using multiple mode
#define HD_COMMAND_SETMULT     0xC6    // Enable/disable multiple mode
#define HD_COMMAND_IDENTIFY    0xEC    // Ask drive to identify itself
#define HD_COMMAND_SETFEATURES 0xEF    // Set special drive features
#define HD_COMMAND_READDMA     0xC8    // Read sectors using DMA
#define HD_COMMAND_WRITEDMA    0xCA    // Write sectors using DMA

// Additional drive command codes used by ATAPI devices

#define HD_COMMAND_PIDENTIFY   0xA1    // identify ATAPI device
#define HD_COMMAND_SRST        0x08    // ATAPI soft reset command
#define HD_COMMAND_PACKETCMD   0xA0    // Send a packet command

// Bit values for Error

#define HD_ERROR_MARK         0x01    // Bad address mark
#define HD_ERROR_TRACK0       0x02    // Couldn't find track 0
#define HD_ERROR_ABORT        0x04    // Command aborted
#define HD_ERROR_ID           0x10    // ID field not found
#define HD_ERROR_MEDIACHANGED 0x20    // Media changed
#define HD_ERROR_ECC          0x40    // Uncorrectable ECC error
#define HD_ERROR_BBD          0x80    // Pre-EIDE meaning:  block marked bad
#define HD_ERROR_ICRC         0x80    // New meaning : CRC error during transfer

/***************************************************************************/

typedef struct tag_ATADRIVEID
{
  U16  Config;
  U16  PhysicalCylinders;
  U16  Reserved2;
  U16  PhysicalHeads;
  U16  RawBytesPerTrack;
  U16  RawBytesPerSector;
  U16  PhysicalSectors;
  U16  Vendor0;
  U16  Vendor1;
  U16  Vendor2;
} ATADRIVEID, *LPATADRIVEID;

/***************************************************************************/

typedef struct tag_DISKGEOMETRY
{
  U32 Cylinders;
  U32 Heads;
  U32 SectorsPerTrack;
  U32 BytesPerSector;
} DISKGEOMETRY, *LPDISKGEOMETRY;

/***************************************************************************/

typedef struct tag_PHYSICALDISK
{
  LISTNODE_FIELDS
  LPDRIVER Driver;
} PHYSICALDISK, *LPPHYSICALDISK;

/***************************************************************************/

typedef struct tag_IOCONTROL
{
  LISTNODE_FIELDS
  LPPHYSICALDISK Disk;
  U32            SectorLow;
  U32            SectorHigh;
  U32            NumSectors;
  LPVOID         Buffer;
  U32            BufferSize;
} IOCONTROL, *LPIOCONTROL;

/***************************************************************************/

typedef struct tag_DISKINFO
{
  LISTNODE_FIELDS
  LPPHYSICALDISK Disk;
  U32            Type;
  U32            Removable;
  U32            NumSectors;
  U32            Access;
} DISKINFO, *LPDISKINFO;

/***************************************************************************/

typedef struct tag_DISKACCESS
{
  LISTNODE_FIELDS
  LPPHYSICALDISK Disk;
  U32            Access;
} DISKACCESS, *LPDISKACCESS;

/***************************************************************************/

#define DISK_ACCESS_DISABLE  0x0001
#define DISK_ACCESS_READONLY 0x0002

/***************************************************************************/

#endif
