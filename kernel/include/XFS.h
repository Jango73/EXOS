
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef XFS_H_INCLUDED
#define XFS_H_INCLUDED

/***************************************************************************/

#include "FSID.h"
#include "FileSystem.h"

/***************************************************************************/

#pragma pack(1)

/***************************************************************************/
// XFS Master Boot Record

typedef struct tag_XFSMBR {
    U8 Jump[4];          // Jump to code and 2 NOPs
    U8 OEMName[8];       // "EXOS    "
    U8 MediaDescriptor;  // 0xF8 for Hard Disks
    U8 LogicalDriveNumber;
    U16 Cylinders;
    U16 Heads;
    U16 SectorsPerTrack;
    U16 BytesPerSector;
    U16 SectorsPerCluster;
    U8 Code[486];
    U16 BIOSMark;  // 0xAA55
} XFSMBR, *LPXFSMBR;

/***************************************************************************/
// XFS Super Block

typedef struct tag_XFSSUPER {
    U8 Magic[4];  // "EXOS"
    U32 Version;
    U32 BytesPerCluster;
    U32 NumClusters;
    U32 NumFreeClusters;
    U32 BitmapCluster;    // First cluster of bitmap
    U32 BadCluster;       // Page for bad clusters
    U32 RootCluster;      // Page for root directory
    U32 SecurityCluster;  // Security info
    U32 KernelFileIndex;
    U32 NumFolders;
    U32 NumFiles;
    U32 MaxMountCount;
    U32 CurrentMountCount;
    U32 VolumeNameFormat;
    U8 Reserved[4];
    U8 Password[32];
    U8 Creator[32];
    U8 VolumeName[128];
} XFSSUPER, *LPXFSSUPER;

/***************************************************************************/
// File time, 64 bytes

typedef struct tag_XFSTIME {
    U32 Year : 22;
    U32 Month : 4;
    U32 Day : 6;
    U32 Hour : 6;
    U32 Minute : 6;
    U32 Second : 6;
    U32 Milli : 10;
    U32 Reserved : 4;
} XFSTIME, *LPXFSTIME;

/***************************************************************************/
// XFS File Record, 256 bytes

typedef struct tag_XFSFILEREC {
    U32 SizeLo;
    U32 SizeHi;
    XFSTIME CreationTime;
    XFSTIME LastAccessTime;
    XFSTIME LastModificationTime;
    U32 ClusterTable;  // 0xFFFFFFFF = End of list
    U32 Attributes;
    U32 Security;
    U32 Group;
    U32 User;
    U32 NameFormat;
    U8 Reserved[72];  // Zeroes
    U8 Name[128];
} XFSFILEREC, *LPXFSFILEREC;

#define XFS_ATTR_FOLDER BIT_0
#define XFS_ATTR_READONLY BIT_1
#define XFS_ATTR_SYSTEM BIT_2
#define XFS_ATTR_ARCHIVE BIT_3
#define XFS_ATTR_HIDDEN BIT_4

/***************************************************************************/

#define XFS_CLUSTER_RESERVED ((U32)0xFFFFFFF0)
#define XFS_CLUSTER_END ((U32)0xFFFFFFFF)

/***************************************************************************/
// XFS File location

typedef struct tag_XFSFILELOC {
    U32 PageCluster;
    U32 PageOffset;
    U32 FileCluster;  // Actual cluster of this file
    U32 FileOffset;   // Offset in actual cluster of this file
    U32 DataCluster;  // Data cluster of this file
} XFSFILELOC, *LPXFSFILELOC;

/***************************************************************************/

#endif
