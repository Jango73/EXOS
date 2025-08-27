
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/FileSystem.h"

#include "../include/Console.h"
#include "../include/Kernel.h"
#include "../include/String.h"
#include "../include/Log.h"

extern BOOL MountPartition_FAT16(LPPHYSICALDISK, LPBOOTPARTITION, U32, U32);
extern BOOL MountPartition_FAT32(LPPHYSICALDISK, LPBOOTPARTITION, U32, U32);
extern BOOL MountPartition_NTFS(LPPHYSICALDISK, LPBOOTPARTITION, U32, U32);
extern BOOL MountPartition_XFS(LPPHYSICALDISK, LPBOOTPARTITION, U32, U32);

/***************************************************************************/

U32 GetNumFileSystems(void) { return Kernel.FileSystem->NumItems; }

/***************************************************************************/

BOOL GetDefaultFileSystemName(LPSTR Name, LPPHYSICALDISK Disk, U32 PartIndex) {
    STR Temp[12];
    LPLISTNODE Node;
    U32 DiskIndex = 0;

    for (Node = Kernel.Disk->First; Node; Node = Node->Next) {
        if ((LPPHYSICALDISK)Node == Disk) break;
        DiskIndex++;
    }

    switch (Disk->Driver->Type) {
        case DRIVER_TYPE_RAMDISK:
            StringCopy(Name, TEXT("rd"));
            break;
        default:
            StringCopy(Name, TEXT("hd"));
            break;
    }

    U32ToString(DiskIndex, Temp);
    StringConcat(Name, Temp);
    StringConcat(Name, TEXT("p"));
    U32ToString(PartIndex + 1, Temp);
    StringConcat(Name, Temp);

    return TRUE;
}

/***************************************************************************/

BOOL MountPartition_Extended(LPPHYSICALDISK Disk, LPBOOTPARTITION Partition, U32 Base) {
    U8 Buffer[SECTOR_SIZE];
    IOCONTROL Control;
    U32 Result;

    Control.ID = ID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = Partition->LBA;
    Control.SectorHigh = 0;
    Control.NumSectors = 1;
    Control.Buffer = (LPVOID)Buffer;
    Control.BufferSize = SECTOR_SIZE;

    Result = Disk->Driver->Command(DF_DISK_READ, (U32)&Control);

    if (Result != DF_ERROR_SUCCESS) return FALSE;

    Base += Partition->LBA;

    Partition = (LPBOOTPARTITION)(Buffer + MBR_PARTITION_START);

    return MountDiskPartitions(Disk, Partition, Base);
}

/***************************************************************************/

BOOL MountDiskPartitions(LPPHYSICALDISK Disk, LPBOOTPARTITION Partition, U32 Base) {
    U8 Buffer[SECTOR_SIZE];
    IOCONTROL Control;
    U32 Result;
    U32 Index;

    if (Partition == NULL) {
        Control.ID = ID_IOCONTROL;
        Control.Disk = Disk;
        Control.SectorLow = 0;
        Control.SectorHigh = 0;
        Control.NumSectors = 1;
        Control.Buffer = (LPVOID)Buffer;
        Control.BufferSize = SECTOR_SIZE;

        Result = Disk->Driver->Command(DF_DISK_READ, (U32)&Control);
        if (Result != DF_ERROR_SUCCESS) return FALSE;

        Partition = (LPBOOTPARTITION)(Buffer + MBR_PARTITION_START);
    }

    //-------------------------------------
    // Read the list of partitions

    for (Index = 0; Index < MBR_PARTITION_COUNT; Index++) {
        if (Partition[Index].LBA != 0) {
            switch (Partition[Index].Type) {
                case FSID_NONE:
                    break;

                case FSID_EXTENDED: {
                    MountPartition_Extended(Disk, Partition + Index, Base);
                } break;

                case FSID_DOS_FAT16S:
                case FSID_DOS_FAT16L: {
                    KernelLogText(LOG_VERBOSE, TEXT("[MountDiskPartitions] Mounting FAT16 partition"));
                    MountPartition_FAT16(Disk, Partition + Index, Base, Index);
                } break;

                case FSID_DOS_FAT32:
                case FSID_DOS_FAT32_LBA1: {
                    KernelLogText(LOG_VERBOSE, TEXT("[MountDiskPartitions] Mounting FAT32 partition"));
                    MountPartition_FAT32(Disk, Partition + Index, Base, Index);
                } break;

                case FSID_EXOS: {
                    KernelLogText(LOG_VERBOSE, TEXT("[MountDiskPartitions] Mounting XFS partition"));
                    MountPartition_XFS(Disk, Partition + Index, Base, Index);
                } break;

                default: {
                    KernelLogText(
                        LOG_VERBOSE, TEXT("[MountDiskPartitions] Partition type %X not implemented\n"),
                        (U32)Partition[Index].Type);
                } break;
            }
        }
    }

    return TRUE;
}

/***************************************************************************/

static void PathCompDestructor(LPVOID This) { KernelMemFree(This); }

/***************************************************************************/

LPLIST DecompPath(LPCSTR Path) {
    STR Component[MAX_FILE_NAME];
    U32 PathIndex = 0;
    U32 CompIndex = 0;
    LPLIST List = NewList(PathCompDestructor, KernelMemAlloc, KernelMemFree);
    LPPATHNODE Node = NULL;

    while (1) {
        CompIndex = 0;

        while (1) {
            if (Path[PathIndex] == STR_SLASH) {
                Component[CompIndex] = STR_NULL;
                PathIndex++;
                break;
            } else if (Path[PathIndex] == STR_NULL) {
                Component[CompIndex] = STR_NULL;
                break;
            } else {
                Component[CompIndex++] = Path[PathIndex++];
            }
        }

        Node = KernelMemAlloc(sizeof(PATHNODE));
        if (Node == NULL) goto Out;
        StringCopy(Node->Name, Component);
        ListAddItem(List, Node);

        if (Path[PathIndex] == STR_NULL) break;
    }

Out:

    return List;
}

/***************************************************************************/
