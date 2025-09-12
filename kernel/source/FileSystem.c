
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


    File System

\************************************************************************/

#include "../include/FileSystem.h"

#include "../include/Console.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/String.h"
#include "../include/Text.h"

extern BOOL MountPartition_FAT16(LPPHYSICALDISK, LPBOOTPARTITION, U32, U32);
extern BOOL MountPartition_FAT32(LPPHYSICALDISK, LPBOOTPARTITION, U32, U32);
extern BOOL MountPartition_NTFS(LPPHYSICALDISK, LPBOOTPARTITION, U32, U32);
extern BOOL MountPartition_EXFS(LPPHYSICALDISK, LPBOOTPARTITION, U32, U32);

/***************************************************************************/

U32 GetNumFileSystems(void) { return Kernel.FileSystem->NumItems; }

/***************************************************************************/

/*
    Build a default volume name using zero-based disk and partition indexes
    per disk type.
*/
BOOL GetDefaultFileSystemName(LPSTR Name, LPPHYSICALDISK Disk, U32 PartIndex) {
    STR Temp[12];
    LPLISTNODE Node;
    LPPHYSICALDISK CurrentDisk;
    U32 DiskIndex = 0;

    // Find the index of this disk among disks of the same type
    for (Node = Kernel.Disk->First; Node; Node = Node->Next) {
        CurrentDisk = (LPPHYSICALDISK)Node;
        if (CurrentDisk == Disk) break;
        if (CurrentDisk->Driver->Type == Disk->Driver->Type) DiskIndex++;
    }

    switch (Disk->Driver->Type) {
        case DRIVER_TYPE_RAMDISK:
            StringCopy(Name, Text_Rd);
            break;
        case DRIVER_TYPE_FLOPPYDISK:
            StringCopy(Name, Text_Fd);
            break;
        default:
            StringCopy(Name, Text_Hd);
            break;
    }

    // Append the zero-based disk index
    U32ToString(DiskIndex, Temp);
    StringConcat(Name, Temp);
    StringConcat(Name, TEXT("p"));

    // Append the zero-based partition index
    U32ToString(PartIndex, Temp);
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
                    KernelLogText(LOG_VERBOSE, TEXT("[MountDiskPartitions] Mounting EXFS partition"));
                    MountPartition_EXFS(Disk, Partition + Index, Base, Index);
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
/**
 * @brief Mounts available disk partitions and the system file system.
 */

void InitializeFileSystems(void) {
    LPLISTNODE Node;

    for (Node = Kernel.Disk->First; Node; Node = Node->Next) {
        MountDiskPartitions((LPPHYSICALDISK)Node, NULL, 0);
    }

    MountSystemFS();
}

/***************************************************************************/
