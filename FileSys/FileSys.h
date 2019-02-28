
// FileSys.H

/*************************************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius, Inc.

  This file is a part of the EXOS Kernel.
  It is the property of Exelsius, Inc.
  All rights reserved.

\*************************************************************************************************/

#ifndef FILESYS_H_INCLUDED
#define FILESYS_H_INCLUDED

/*************************************************************************************************/

#include "..\Kernel\Base.h"

/*************************************************************************************************/

#define MAX_FILENAME  200
#define MAX_PATHNAME 1024

/*************************************************************************************************/

typedef struct tag_BlockDeviceLocation
{
  U32 Cylinder;
  U32 Head;
  U32 Track;
  U32 Sector;
  U32 NumSectors;
} BlockDeviceLocation, *pBlockDeviceLocation;

/*************************************************************************************************/

typedef struct tag_BlockDevice
{
  U32 ID;
  U32 DeviceType;
  U32 Cylinders;
  U32 Heads;
  U32 Sectors;
  U32 TracksPerCylinder;
  U32 SectorsPerTrack;
  U32 BytesPerSector;
  U64 Capacity;
} BlockDevice, *pBlockDevice;

/*************************************************************************************************/

// Max year               : 4,194,303
// Max month              : 15
// Max day                : 63
// Max seconds            : (seconds in a day : 86400)

typedef struct tag_FileTime
{
  U32 Year    : 22;
  U32 Month   : 4;
  U32 Day     : 6;
  U32 Seconds : 18;
  U32 Unused  : 14;
} FileTime, *pFileTime;

/*************************************************************************************************\

  Description of FileRecord members

    Size : 64-bit value indicating the size fo the file

      SizeLO : Lower 32 bits of file size
      SizeHI : Higher 32 bits of file size

    AccessFlags : Bit field

      Bit 0 : If set, can read from the file
      Bit 1 : If set, can write to the file

    SecurityFlags : Bit field

      Bit 0 : If set, only the OS can view and access the file
      Bit 1 : If set, the clusters of the file will be filled with zeros on deletion

    Time_Creation : Time at which the file was created
    Time_Accessed : Time at which the file was last accessed
    Time_Modified : Time at which the file was last modified

    GroupID : Group owner of this file
    UserID  : User owner of this file

      If GroupID and UserID are zero, no check is made for access

\*************************************************************************************************/

// The file record is 256 bytes long

typedef struct tag_FileRec
{
  U32      Size;                       // Size - 4 bytes
  U32      SizeReserved;               // Reserved for 64-bit OS
  U16      AccessFlags;                // Access permission - 2 bytes
  FileTime Time_Creation;              // Time stamp - 8 * 3 = 24 bytes
  FileTime Time_Accessed;
  FileTime Time_Modified;
  U32      GroupID;                    // Group owner of this file - 4 bytes
  U32      UserID;                     // User owner of this file - 4 bytes
  U8       SecurityFlags;              // Security description - 1 byte
  U8       NameLength;                 // Length of name - 1 byte
  U8       Name [200];                 // Name - 200 bytes
  U32      Res1;                       // Reserved
  U32      Res2;
  U32      Res3;
} FileRec, *pFileRec;

/*************************************************************************************************\

  Cluster pointers :

  All cluster pointers are 32-bit values.
  The first cluster actually begins at physical byte 2048 of the disc because the first 1024 bytes
  contain the boot sector and the next 1024 bytes contain the SuperBlock.

  Cluster size :

  The following table shows the maximum addressable bytes using different cluster sizes :

  Cluster size  Max addressable byte

  1024           4,398,046,510,080
  2048           8,796,093,020,160
  4096          17,592,186,040,320
  8192          35,184,372,080,640

  The clusters 0 and 1 are always 1024 bytes in size.
  The first contains the boot sector.
  The second contains the SuperBlock.
  So to calculate the number of clusters on a disc, use the following formula :

  (Disc size in bytes - 2048) / Cluster size

  If the value is not an integer, the fractional part represents unusable space

  Examples

  |--------------------------|---------------|------------------------|
  | Disc size                | Cluster size  | Total clusters on disc |
  |--------------------------|---------------|------------------------|
  |     536,870,912 (500 MB) |  1,024 (1 KB) |                524,286 |
  |     536,870,912 (500 MB) |  2,048 (2 KB) |                262,143 |
  |     536,870,912 (500 MB) |  4,096 (4 KB) |                131,071 |
  |     536,870,912 (500 MB) |  8,192 (8 KB) |                 65,535 |
  |--------------------------|---------------|------------------------|
  |   4,294,967,296 (  4 GB) |  1,024 (1 KB) |              4,194,302 |
  |   4,294,967,296 (  4 GB) |  2,048 (2 KB) |              2,097,151 |
  |   4,294,967,296 (  4 GB) |  4,096 (4 KB) |              1,048,575 |
  |   4,294,967,296 (  4 GB) |  8,192 (8 KB) |                524,287 |
  |--------------------------|---------------|------------------------|

  -------------------------------------

  Cluster bitmap :

  The cluster bitmap is a collection of bits indicating the status of each cluster on the disc.
  If a cluster is free, the corresponding bit is set to 0.
  If the cluster is used, the corresponding bit is set to 1.

  The size of the cluster bitmap is calculated as follows :

    (Total disc size / Cluster size) / 8

  Examples

  |--------------------------|---------------|----------------------|--------------------|
  | Disc size                | Cluster size  |  Cluster bitmap size | Number of clusters |
  |--------------------------|---------------|----------------------|--------------------|
  |     536,870,912 (500 MB) |  1,024 (1 KB) |               65,536 |                 64 |
  |     536,870,912 (500 MB) |  2,048 (2 KB) |               32,768 |                 16 |
  |     536,870,912 (500 MB) |  4,096 (4 KB) |               16,384 |                  4 |
  |     536,870,912 (500 MB) |  8,192 (8 KB) |                8,192 |                  1 |
  |--------------------------|---------------|----------------------|--------------------|
  |   4,294,967,296 (  4 GB) |  1,024 (1 KB) |              524,288 |                512 |
  |   4,294,967,296 (  4 GB) |  2,048 (2 KB) |              262,144 |                128 |
  |   4,294,967,296 (  4 GB) |  4,096 (4 KB) |              131,072 |                 32 |
  |   4,294,967,296 (  4 GB) |  8,192 (8 KB) |               65,536 |                  8 |
  |--------------------------|---------------|----------------------|--------------------|
  |  17,179,869,184 ( 16 GB) |  1,024 (1 KB) |            4,194,304 |              8,192 |
  |  17,179,869,184 ( 16 GB) |  2,048 (2 KB) |            1,048,576 |                512 |
  |  17,179,869,184 ( 16 GB) |  4,096 (2 KB) |              524,288 |                128 |
  |  17,179,869,184 ( 16 GB) |  8,192 (2 KB) |              262,144 |                 32 |
  |--------------------------|---------------|----------------------|--------------------|

\*************************************************************************************************/

typedef struct tag_SuperBlock
{
  U32 EXOSMagic;                       // Magic number = "EXOS"
  U32 Version;                         // Version of the file system
  U32 ClusterSize;                     // Size of clusters on this disc
  U32 ClusterBitmap;                   // Cluster offset to the cluster bitmap
  U32 NumClusters;                     // Total number of clusters
  U32 NumFreeClusters;                 // Total number of free clusters
  U32 FileTable;                       // Cluster offset to the node table
  U32 FileBitmap;                      // Cluster offset to the node table bitmap
  U32 NumFiles;                        // Total number of nodes
  U32 NumFreeFiles;                    // Total number of free nodes
  U32 File_OS;                         // Contains an index in the NodeTable for the OS
  U32 File_Root;                       // Contains an index in the NodeTable for the root
  U32 CreatorMagic;                    // Magic number of OS that created this file system
  U32 MaxMountCount;                   // Max number of times file system is mounted before check
  U32 MountCount;                      // Number of times file system has been mounted
} SuperBlock, *pSuperBlock;

/*************************************************************************************************/

#define XFS_MIN_CLUSTER_SIZE    0x00000400
#define XFS_SYSTEM_CLUSTER_SIZE 0x00000400

#define XFS_SETVALIDCLUSTERSIZE(a) \
  { a = (a / XFS_MIN_CLUSTER_SIZE) * XFS_MIN_CLUSTER_SIZE; if (a == 0) a = XFS_MIN_CLUSTER_SIZE; }

/*************************************************************************************************/

typedef struct tag_DeviceControlBlock
{
  BlockDevice         Device;
  SuperBlock          Super;
  BlockDeviceLocation Location_BootCluster;
  BlockDeviceLocation Location_SuperBlock;
  BlockDeviceLocation Location_ClusterBitmap;
  BlockDeviceLocation Location_FileTable;
  BlockDeviceLocation Location_FileBitmap;
} DeviceControlBlock, *pDeviceControlBlock;

/*************************************************************************************************/

#endif
