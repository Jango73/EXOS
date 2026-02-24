#ifndef FAT32_PRIVATE_H_INCLUDED
#define FAT32_PRIVATE_H_INCLUDED

#include "drivers/filesystems/FAT.h"
#include "FileSystem.h"
#include "Kernel.h"
#include "Log.h"
#include "CoreString.h"

/***************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

/***************************************************************************/

// The file system object allocated when mounting

typedef struct tag_FAT32FILESYSTEM {
    FILESYSTEM Header;
    LPSTORAGE_UNIT Disk;
    FAT32MBR Master;
    SECTOR PartitionStart;
    U32 PartitionSize;
    SECTOR FATStart;
    SECTOR FATStart2;
    SECTOR DataStart;
    U32 BytesPerCluster;
    U8* IOBuffer;
} FAT32FILESYSTEM, *LPFAT32FILESYSTEM;

/***************************************************************************/

typedef struct tag_FATFILE {
    FILE Header;
    FATFILELOC Location;
} FATFILE, *LPFATFILE;

/***************************************************************************/

extern DRIVER FAT32Driver;

UINT FAT32Commands(UINT Function, UINT Parameter);

U32 GetNameChecksum(LPSTR Name);
LPFATFILE NewFATFile(LPFAT32FILESYSTEM FileSystem, LPFATFILELOC FileLoc);
BOOL ReadCluster(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster, LPVOID Buffer);
BOOL WriteCluster(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster, LPVOID Buffer);
CLUSTER GetNextClusterInChain(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster);
BOOL CreateDirEntry(LPFAT32FILESYSTEM FileSystem, CLUSTER FolderCluster, LPSTR Name, U32 Attributes);
CLUSTER ChainNewCluster(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster);

#endif
