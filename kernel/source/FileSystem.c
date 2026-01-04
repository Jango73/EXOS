
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

#include "FileSystem.h"

#include "Console.h"
#include "File.h"
#include "Kernel.h"
#include "Log.h"
#include "CoreString.h"
#include "SystemFS.h"
#include "User.h"
#include "Text.h"
#include "utils/TOML.h"

extern BOOL MountPartition_FAT16(LPPHYSICALDISK, LPBOOTPARTITION, U32, U32);
extern BOOL MountPartition_FAT32(LPPHYSICALDISK, LPBOOTPARTITION, U32, U32);
extern BOOL MountPartition_NTFS(LPPHYSICALDISK, LPBOOTPARTITION, U32, U32);
extern BOOL MountPartition_EXFS(LPPHYSICALDISK, LPBOOTPARTITION, U32, U32);
extern BOOL MountPartition_EXT2(LPPHYSICALDISK, LPBOOTPARTITION, U32, U32);

/***************************************************************************/

#define FILESYSTEM_VER_MAJOR 1
#define FILESYSTEM_VER_MINOR 0

static UINT FileSystemDriverCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION FileSystemDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_INIT,
    .VersionMajor = FILESYSTEM_VER_MAJOR,
    .VersionMinor = FILESYSTEM_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "FileSystems",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = FileSystemDriverCommands};

/***************************************************************************/

/**
 * @brief Retrieves the file system driver descriptor.
 * @return Pointer to the file system driver.
 */
LPDRIVER FileSystemGetDriver(void) {
    return &FileSystemDriver;
}

/***************************************************************************/

/**
 * @brief Loads and parses the kernel configuration file.
 *
 * Attempts to read "exos.toml" (case insensitive) and stores the resulting
 * TOML data in the kernel configuration state.
 */
static void ReadKernelConfiguration(void) {
    DEBUG(TEXT("[ReadKernelConfiguration] Enter"));

    UINT Size = 0;
    LPVOID Buffer = FileReadAll(TEXT("exos.toml"), &Size);

    if (Buffer == NULL) {
        Buffer = FileReadAll(TEXT("EXOS.TOML"), &Size);

        SAFE_USE(Buffer) {
            DEBUG(TEXT("[ReadKernelConfiguration] Config read from EXOS.TOML"));
        }
    } else {
        DEBUG(TEXT("[ReadKernelConfiguration] Config read from exos.toml"));
    }

    SAFE_USE(Buffer) {
        SetConfiguration(TomlParse((LPCSTR)Buffer));
        KernelHeapFree(Buffer);
    }

    DEBUG(TEXT("[ReadKernelConfiguration] Exit"));
}

/***************************************************************************/

/**
 * @brief Gets the number of mounted file systems.
 *
 * @return Number of file systems currently mounted in the system
 */
U32 GetNumFileSystems(void) {
    LPLIST FileSystemList = GetFileSystemList();
    return FileSystemList != NULL ? FileSystemList->NumItems : 0;
}

/***************************************************************************/

/**
 * @brief Generates a default file system name for a disk partition.
 *
 * Creates a volume name using the disk type and zero-based partition index.
 * The naming convention helps identify partitions systematically.
 *
 * @param Name Buffer to store the generated name (must be large enough)
 * @param Disk Pointer to physical disk structure
 * @param PartIndex Zero-based partition index on the disk
 * @return TRUE if name was generated successfully, FALSE otherwise
 */
BOOL GetDefaultFileSystemName(LPSTR Name, LPPHYSICALDISK Disk, U32 PartIndex) {
    STR Temp[12];
    LPLISTNODE Node;
    LPPHYSICALDISK CurrentDisk;
    U32 DiskIndex = 0;

    // Find the index of this disk among disks of the same type
    LPLIST DiskList = GetDiskList();
    for (Node = DiskList != NULL ? DiskList->First : NULL; Node; Node = Node->Next) {
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

/**
 * @brief Stores the logical name of the active partition.
 *
 * Updates the kernel-wide file system information so that higher level
 * components can retrieve the currently active partition name.
 *
 * @param FileSystem Mounted file system flagged as active in the MBR.
 */
void FileSystemSetActivePartition(LPFILESYSTEM FileSystem) {
    SAFE_USE(FileSystem) {
        FILESYSTEM_GLOBAL_INFO* GlobalInfo = GetFileSystemGlobalInfo();
        StringCopy(GlobalInfo->ActivePartitionName, FileSystem->Name);
        DEBUG(TEXT("[FileSystemSetActivePartition] Active partition name set to %s"), FileSystem->Name);
    }
}

/***************************************************************************/

/**
 * @brief Mounts extended partitions from a disk.
 *
 * Reads and processes extended partition table entries to discover
 * and mount logical drives within extended partitions.
 *
 * @param Disk Pointer to physical disk structure
 * @param Partition Pointer to boot partition information
 * @param Base Base sector address for partition calculations
 * @return TRUE if extended partitions were processed successfully, FALSE otherwise
 */
BOOL MountPartition_Extended(LPPHYSICALDISK Disk, LPBOOTPARTITION Partition, U32 Base) {
    U8 Buffer[SECTOR_SIZE];
    IOCONTROL Control;
    U32 Result;

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = Partition->LBA;
    Control.SectorHigh = 0;
    Control.NumSectors = 1;
    Control.Buffer = (LPVOID)Buffer;
    Control.BufferSize = SECTOR_SIZE;

    Result = Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);

    if (Result != DF_RETURN_SUCCESS) return FALSE;

    Base += Partition->LBA;

    Partition = (LPBOOTPARTITION)(Buffer + MBR_PARTITION_START);

    return MountDiskPartitions(Disk, Partition, Base);
}

/***************************************************************************/

/**
 * @brief Mounts all partitions found on a physical disk.
 *
 * Reads the Master Boot Record (MBR) and processes each partition entry,
 * attempting to mount supported file system types (FAT16, FAT32, NTFS, EXFS).
 * Handles both primary and extended partitions recursively.
 *
 * @param Disk Pointer to physical disk structure
 * @param Partition Pointer to boot partition array, or NULL to read from disk
 * @param Base Base sector address for partition offset calculations
 * @return TRUE if partitions were processed successfully, FALSE otherwise
 */
BOOL MountDiskPartitions(LPPHYSICALDISK Disk, LPBOOTPARTITION Partition, U32 Base) {
    U8 Buffer[SECTOR_SIZE];
    IOCONTROL Control;
    U32 Result;
    U32 Index;

    DEBUG(TEXT("[MountDiskPartitions] Disk = %x, Partition = %x, Base = %x"), Disk, Partition, Base);

    if (Partition == NULL) {
        Control.TypeID = KOID_IOCONTROL;
        Control.Disk = Disk;
        Control.SectorLow = 0;
        Control.SectorHigh = 0;
        Control.NumSectors = 1;
        Control.Buffer = (LPVOID)Buffer;
        Control.BufferSize = SECTOR_SIZE;

        Result = Disk->Driver->Command(DF_DISK_READ, (UINT)&Control);
        if (Result != DF_RETURN_SUCCESS) {
            WARNING(TEXT("[MountDiskPartitions] MBR read failed result=%x"), Result);
            return FALSE;
        }

        Partition = (LPBOOTPARTITION)(Buffer + MBR_PARTITION_START);
    }

    //-------------------------------------
    // Read the list of partitions

    for (Index = 0; Index < MBR_PARTITION_COUNT; Index++) {
        if (Partition[Index].LBA != 0) {
            BOOL PartitionMounted = FALSE;
            BOOL PartitionIsActive = ((Partition[Index].Disk & 0x80) != 0);
            LPLIST FileSystemList = GetFileSystemList();
            LPFILESYSTEM PreviousLast =
                (LPFILESYSTEM)(FileSystemList != NULL ? FileSystemList->Last : NULL);

            switch (Partition[Index].Type) {
                case FSID_NONE:
                    break;

                case FSID_EXTENDED:
                case FSID_LINUX_EXTENDED: {
                    MountPartition_Extended(Disk, Partition + Index, Base);
                } break;

                case FSID_DOS_FAT16S:
                case FSID_DOS_FAT16L: {
                    DEBUG(TEXT("[MountDiskPartitions] Mounting FAT16 partition"));
                    PartitionMounted =
                        MountPartition_FAT16(Disk, Partition + Index, Base, Index);
                } break;

                case FSID_DOS_FAT32:
                case FSID_DOS_FAT32_LBA1: {
                    DEBUG(TEXT("[MountDiskPartitions] Mounting FAT32 partition"));
                    PartitionMounted =
                        MountPartition_FAT32(Disk, Partition + Index, Base, Index);
                } break;

                case FSID_EXOS: {
                    DEBUG(TEXT("[MountDiskPartitions] Mounting EXFS partition"));
                    PartitionMounted =
                        MountPartition_EXFS(Disk, Partition + Index, Base, Index);
                } break;

                case FSID_LINUX_EXT2:
#if FSID_LINUX_EXT3 != FSID_LINUX_EXT2
                case FSID_LINUX_EXT3:
#endif
#if FSID_LINUX_EXT4 != FSID_LINUX_EXT2
                case FSID_LINUX_EXT4:
#endif
#if FSID_LINUXNATIVE != FSID_LINUX_EXT2
                case FSID_LINUXNATIVE:
#endif
                {
                    DEBUG(TEXT("[MountDiskPartitions] Mounting EXT2 partition"));
                    PartitionMounted =
                        MountPartition_EXT2(Disk, Partition + Index, Base, Index);
                } break;

                default: {
                    WARNING(TEXT("[MountDiskPartitions] Partition type %X not implemented\n"),
                        (U32)Partition[Index].Type);
                } break;
            }

            if (PartitionMounted) {
                LPLIST FileSystemList = GetFileSystemList();
                LPFILESYSTEM MountedFileSystem =
                    (LPFILESYSTEM)(FileSystemList != NULL ? FileSystemList->Last : NULL);

                if (MountedFileSystem != NULL && MountedFileSystem != PreviousLast) {
                    if (GetSystemFSData()->Root != NULL) {
                        if (!SystemFSMountFileSystem(MountedFileSystem)) {
                            WARNING(TEXT("[MountDiskPartitions] SystemFS mount failed for %s"),
                                MountedFileSystem->Name);
                        }
                    } else {
                        WARNING(TEXT("[MountDiskPartitions] SystemFS not ready for %s"),
                            MountedFileSystem->Name);
                    }
                    if (PartitionIsActive) {
                        FileSystemSetActivePartition(MountedFileSystem);
                    }
                }
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

    FILESYSTEM_GLOBAL_INFO* GlobalInfo = GetFileSystemGlobalInfo();
    StringClear(GlobalInfo->ActivePartitionName);

    LPLIST DiskList = GetDiskList();
    for (Node = DiskList != NULL ? DiskList->First : NULL; Node; Node = Node->Next) {
        MountDiskPartitions((LPPHYSICALDISK)Node, NULL, 0);
    }

    MountSystemFS();
    ReadKernelConfiguration();
    MountUserNodes();
}

/***************************************************************************/

/**
 * @brief Driver command handler for filesystem initialization.
 *
 * DF_LOAD mounts disk partitions and system FS once; DF_UNLOAD only clears
 * readiness.
 */
static UINT FileSystemDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((FileSystemDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            InitializeFileSystems();
            FileSystemDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((FileSystemDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            FileSystemDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(FILESYSTEM_VER_MAJOR, FILESYSTEM_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
