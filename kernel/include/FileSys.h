
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef FILESYS_H_INCLUDED
#define FILESYS_H_INCLUDED

/***************************************************************************/

#include "Driver.h"
#include "FSID.h"
#include "HD.h"
#include "Process.h"

/***************************************************************************/

#pragma pack(1)

/***************************************************************************/

// Functions supplied by a file system driver

#define DF_FS_GETVOLUMEINFO (DF_FIRSTFUNC + 0)
#define DF_FS_SETVOLUMEINFO (DF_FIRSTFUNC + 1)
#define DF_FS_FLUSH (DF_FIRSTFUNC + 2)
#define DF_FS_CREATEFOLDER (DF_FIRSTFUNC + 3)
#define DF_FS_DELETEFOLDER (DF_FIRSTFUNC + 4)
#define DF_FS_RENAMEFOLDER (DF_FIRSTFUNC + 5)
#define DF_FS_OPENFILE (DF_FIRSTFUNC + 6)
#define DF_FS_OPENNEXT (DF_FIRSTFUNC + 7)
#define DF_FS_CLOSEFILE (DF_FIRSTFUNC + 8)
#define DF_FS_DELETEFILE (DF_FIRSTFUNC + 9)
#define DF_FS_RENAMEFILE (DF_FIRSTFUNC + 10)
#define DF_FS_READ (DF_FIRSTFUNC + 11)
#define DF_FS_WRITE (DF_FIRSTFUNC + 12)
#define DF_FS_GETPOSITION (DF_FIRSTFUNC + 13)
#define DF_FS_SETPOSITION (DF_FIRSTFUNC + 14)
#define DF_FS_GETATTRIBUTES (DF_FIRSTFUNC + 15)
#define DF_FS_SETATTRIBUTES (DF_FIRSTFUNC + 16)
#define DF_FS_CREATEPARTITION (DF_FIRSTFUNC + 17)

/***************************************************************************/

#define DF_ERROR_FS_BADSECTOR (DF_ERROR_FIRST + 0)
#define DF_ERROR_FS_NOSPACE (DF_ERROR_FIRST + 1)
#define DF_ERROR_FS_CANT_READ_SECTOR (DF_ERROR_FIRST + 2)
#define DF_ERROR_FS_CANT_WRITE_SECTOR (DF_ERROR_FIRST + 3)

/***************************************************************************/

// Generic file attributes

#define FS_ATTR_FOLDER 0x0001
#define FS_ATTR_READONLY 0x0002
#define FS_ATTR_HIDDEN 0x0004
#define FS_ATTR_SYSTEM 0x0008

/***************************************************************************/

#define MBR_PARTITION_START 0x01BE
#define MBR_PARTITION_SIZE 0x0010
#define MBR_PARTITION_COUNT 0x0004

/***************************************************************************/

// The BIOS "Cylinder-Head-Sector" format

typedef struct tag_PCHS {
    U8 Head;
    U8 Cylinder;
    U8 Sector;  // Bits 6 & 7 are high bits of cylinder
} PCHS, *LPPCHS;

/***************************************************************************/

// The logical "Cylinder-Head-Sector" format

typedef struct tag_LCHS {
    U32 Cylinder;
    U32 Head;
    U32 Sector;
} LCHS, *LPLCHS;

/***************************************************************************/

typedef struct tag_BOOTPARTITION {
    U8 Disk;        // 0x80 for active partition
    PCHS StartCHS;  // CHS of disk start
    U8 Type;        // Type of partition
    PCHS EndCHS;    // CHS of disk end
    SECTOR LBA;     // Logical Block Addressing start
    U32 Size;       // Size in sectors
} BOOTPARTITION, *LPBOOTPARTITION;

/***************************************************************************/

typedef struct tag_FILESYSTEM {
    LISTNODE_FIELDS
    MUTEX Mutex;
    LPDRIVER Driver;
    STR Name[MAX_FS_LOGICAL_NAME];
} FILESYSTEM, *LPFILESYSTEM;

/***************************************************************************/
// The structure used by the folder commands
// and the file open command

typedef struct tag_FILEINFO {
    U32 Size;
    LPFILESYSTEM FileSystem;
    U32 Attributes;
    U32 Flags;
    STR Name[MAX_PATH_NAME];
} FILEINFO, *LPFILEINFO;

#define FIF_CREATE_ALWAYS 0x00000001
#define FIF_DELETE_TREE 0x00000002

/***************************************************************************/

typedef struct tag_FILE {
    LISTNODE_FIELDS
    MUTEX Mutex;
    LPFILESYSTEM FileSystem;
    SECURITY Security;
    LPTASK OwnerTask;
    U32 OpenFlags;
    U32 Attributes;
    U32 SizeLow;
    U32 SizeHigh;
    SYSTEMTIME Creation;
    SYSTEMTIME Accessed;
    SYSTEMTIME Modified;
    U32 Position;
    U32 BytesToRead;
    U32 BytesRead;
    LPVOID Buffer;
    STR Name[MAX_FILE_NAME];
} FILE, *LPFILE;

/***************************************************************************/

// Structure used by the partition commands

#define FLAG_PART_CREATE_QUICK_FORMAT 0x0001

typedef struct tag_PARTITION_CREATION {
    U32 Size;
    LPPHYSICALDISK Disk;
    U32 PartitionStartSector;
    U32 PartitionNumSectors;
    U32 SectorsPerCluster;
    U32 Flags;
    STR VolumeName[MAX_PATH_NAME];
} PARTITION_CREATION, *LPPARTITION_CREATION;

/***************************************************************************/

typedef struct tag_PATHNODE {
    LISTNODE_FIELDS
    STR Name[MAX_FILE_NAME];
} PATHNODE, *LPPATHNODE;

/***************************************************************************/

BOOL MountDiskPartitions(LPPHYSICALDISK, LPBOOTPARTITION, U32);
U32 GetNumFileSystems(void);
BOOL GetDefaultFileSystemName(LPSTR);
BOOL MountSystemFS(void);
LPLIST DecompPath(LPCSTR);

/***************************************************************************/

#endif
