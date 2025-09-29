
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


    FAT32

\************************************************************************/

#include "../include/FAT.h"
#include "../include/FileSystem.h"
#include "../include/Kernel.h"
#include "../include/Log.h"

/***************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

U32 FAT32Commands(U32, U32);

DRIVER FAT32Driver = {
    .ID = ID_DRIVER,
    .References = 1,
    .OwnerProcess = &KernelProcess,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_FILESYSTEM,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Microsoft Corporation",
    .Product = "Fat 32 File System",
    .Command = FAT32Commands};

/***************************************************************************/
// The file system object allocated when mounting

typedef struct tag_FAT32FILESYSTEM {
    FILESYSTEM Header;
    LPPHYSICALDISK Disk;
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

/**
 * @brief Allocate and initialize a FAT32 file system object.
 * @param Disk Physical disk hosting the partition.
 * @return Pointer to a new FAT32 file system or NULL on failure.
 */
static LPFAT32FILESYSTEM NewFATFileSystem(LPPHYSICALDISK Disk) {
    LPFAT32FILESYSTEM This;

    This = (LPFAT32FILESYSTEM)KernelHeapAlloc(sizeof(FAT32FILESYSTEM));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(FAT32FILESYSTEM));

    This->Header.ID = ID_FILESYSTEM;
    This->Header.References = 1;
    This->Header.Next = NULL;
    This->Header.Prev = NULL;
    This->Header.Driver = &FAT32Driver;
    This->Disk = Disk;
    This->FATStart = 0;
    This->FATStart2 = 0;
    This->DataStart = 0;
    This->BytesPerCluster = 0;
    This->IOBuffer = NULL;

    InitMutex(&(This->Header.Mutex));

    return This;
}

/***************************************************************************/

/**
 * @brief Allocate and initialize a FAT32 file handle.
 * @param FileSystem Owning file system.
 * @param FileLoc Initial file location information.
 * @return Pointer to a new FATFILE or NULL on failure.
 */
static LPFATFILE NewFATFile(LPFAT32FILESYSTEM FileSystem, LPFATFILELOC FileLoc) {
    LPFATFILE This;

    This = (LPFATFILE)KernelHeapAlloc(sizeof(FATFILE));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(FATFILE));

    This->Header.ID = ID_FILE;
    This->Header.References = 1;
    This->Header.Next = NULL;
    This->Header.Prev = NULL;
    This->Header.FileSystem = (LPFILESYSTEM)FileSystem;
    This->Location.PreviousCluster = FileLoc->PreviousCluster;
    This->Location.FolderCluster = FileLoc->FolderCluster;
    This->Location.FileCluster = FileLoc->FileCluster;
    This->Location.DataCluster = FileLoc->DataCluster;
    This->Location.Offset = FileLoc->Offset;

    InitMutex(&(This->Header.Mutex));
    InitSecurity(&(This->Header.Security));

    return This;
}

/***************************************************************************/

/**
 * @brief Mount a FAT32 partition and register the file system.
 * @param Disk Physical disk containing the partition.
 * @param Partition Partition descriptor.
 * @param Base Base sector offset.
 * @param PartIndex Partition index for naming.
 * @return TRUE on success, FALSE on failure.
 */
BOOL MountPartition_FAT32(LPPHYSICALDISK Disk, LPBOOTPARTITION Partition, U32 Base, U32 PartIndex) {
    U8 Buffer[SECTOR_SIZE];
    IOCONTROL Control;
    LPFAT32MBR Master;
    LPFAT32FILESYSTEM FileSystem;
    U32 Result;


    Control.ID = ID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = Base + Partition->LBA;
    Control.SectorHigh = 0;
    Control.NumSectors = 1;
    Control.Buffer = (LPVOID)Buffer;
    Control.BufferSize = SECTOR_SIZE;

    Result = Disk->Driver->Command(DF_DISK_READ, (U32)&Control);

    if (Result != DF_ERROR_SUCCESS) return FALSE;

    //-------------------------------------
    // Assign a pointer to the sector

    Master = (LPFAT32MBR)Buffer;

    //-------------------------------------
    // Check if this is really a FAT32 partition

    if (Master->FATName[0] != 'F') return FALSE;
    if (Master->FATName[1] != 'A') return FALSE;
    if (Master->FATName[2] != 'T') return FALSE;
    if (Master->FATName[3] != '3') return FALSE;
    if (Master->FATName[4] != '2') return FALSE;

    //-------------------------------------
    // Check for presence of BIOS mark

    if (Master->BIOSMark != 0xAA55) return FALSE;

    //-------------------------------------
    // Create the file system object

    FileSystem = NewFATFileSystem(Disk);
    if (FileSystem == NULL) return FALSE;

    GetDefaultFileSystemName(FileSystem->Header.Name, Disk, PartIndex);

    //-------------------------------------
    // Copy the Master Sector

    MemoryCopy(&(FileSystem->Master), Buffer, SECTOR_SIZE);

    FileSystem->PartitionStart = Base + Partition->LBA;
    FileSystem->PartitionSize = Partition->Size;
    FileSystem->BytesPerCluster = FileSystem->Master.SectorsPerCluster * SECTOR_SIZE;

    FileSystem->IOBuffer = (U8*)KernelHeapAlloc(FileSystem->Master.SectorsPerCluster * SECTOR_SIZE);

    //-------------------------------------
    // Compute the start of the FAT

    FileSystem->FATStart = FileSystem->PartitionStart + FileSystem->Master.ReservedSectors;

    if (FileSystem->Master.NumFATs > 1) {
        FileSystem->FATStart2 = FileSystem->FATStart + FileSystem->Master.NumSectorsPerFAT;
    }

    //-------------------------------------
    // Compute the start of the data

    FileSystem->DataStart = FileSystem->FATStart + (FileSystem->Master.NumFATs * FileSystem->Master.NumSectorsPerFAT);

    //-------------------------------------
    // Register the file system

    ListAddItem(Kernel.FileSystem, FileSystem);


    return TRUE;
}

/***************************************************************************/

/**
 * @brief Compute FAT32 short name checksum.
 * @param Name 11-character short name.
 * @return Calculated checksum value.
 */
static U32 GetNameChecksum(LPSTR Name) {
    U32 Checksum = 0;
    U32 Index = 0;

    for (Index = 0; Index < 11; Index++) {
        Checksum = (((Checksum & 0x01) << 7) | ((Checksum & 0xFE) >> 1)) + Name[Index];
    }

    Checksum &= 0xFF;

    return Checksum;
}

/***************************************************************************/

/**
 * @brief Read a cluster from disk into memory.
 * @param FileSystem Target file system.
 * @param Cluster Cluster number to read.
 * @param Buffer Destination buffer.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL ReadCluster(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster, LPVOID Buffer) {
    IOCONTROL Control;
    SECTOR Sector;
    U32 Result;

    Sector =
        FileSystem->DataStart + ((Cluster - FileSystem->Master.RootCluster) * FileSystem->Master.SectorsPerCluster);

    if (Sector < FileSystem->PartitionStart || Sector >= FileSystem->PartitionStart + FileSystem->PartitionSize) {
        return FALSE;
    }

    Control.ID = ID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.SectorLow = Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = FileSystem->Master.SectorsPerCluster;
    Control.Buffer = Buffer;
    Control.BufferSize = FileSystem->Master.SectorsPerCluster * SECTOR_SIZE;

    Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (U32)&Control);

    if (Result != DF_ERROR_SUCCESS) return FALSE;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Write a memory buffer to a specific cluster.
 * @param FileSystem Target file system.
 * @param Cluster Cluster number to write.
 * @param Buffer Source buffer.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL WriteCluster(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster, LPVOID Buffer) {
    IOCONTROL Control;
    SECTOR Sector;
    U32 Result;

    Sector =
        FileSystem->DataStart + ((Cluster - FileSystem->Master.RootCluster) * FileSystem->Master.SectorsPerCluster);

    if (Sector < FileSystem->PartitionStart || Sector >= FileSystem->PartitionStart + FileSystem->PartitionSize) {
        return FALSE;
    }

    Control.ID = ID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.SectorLow = Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = FileSystem->Master.SectorsPerCluster;
    Control.Buffer = Buffer;
    Control.BufferSize = FileSystem->Master.SectorsPerCluster * SECTOR_SIZE;

    Result = FileSystem->Disk->Driver->Command(DF_DISK_WRITE, (U32)&Control);

    if (Result != DF_ERROR_SUCCESS) return FALSE;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve the next cluster in a FAT chain.
 * @param FileSystem Target file system.
 * @param Cluster Current cluster in chain.
 * @return Next cluster number or 0 on failure.
 */
static CLUSTER GetNextClusterInChain(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster) {
    U32 Buffer[SECTOR_SIZE / sizeof(U32)];
    IOCONTROL Control;
    CLUSTER NextCluster;
    U32 NumEntriesPerSector;
    U32 Sector;
    U32 Offset;
    U32 Result;

    NextCluster = FAT32_CLUSTER_LAST;

    NumEntriesPerSector = SECTOR_SIZE / sizeof(U32);
    Sector = Cluster / NumEntriesPerSector;
    Offset = Cluster % NumEntriesPerSector;

    Control.ID = ID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.SectorLow = FileSystem->FATStart + Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = 1;
    Control.Buffer = Buffer;
    Control.BufferSize = SECTOR_SIZE;

    Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (U32)&Control);

    if (Result == DF_ERROR_SUCCESS) {
        NextCluster = Buffer[Offset];
    }

    return NextCluster;
}

/***************************************************************************/

/**
 * @brief Search the FAT for a free cluster.
 * @param FileSystem Target file system.
 * @return Cluster number or 0 if none available.
 */
static CLUSTER FindFreeCluster(LPFAT32FILESYSTEM FileSystem) {
    U32 Buffer[SECTOR_SIZE / sizeof(U32)];
    IOCONTROL Control;
    CLUSTER NewCluster;
    U32 NumEntriesPerSector;
    U32 CurrentSector;
    U32 Index;
    U32 Result;


    //-------------------------------------
    // Setup variables

    Control.ID = ID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.Buffer = Buffer;
    Control.BufferSize = SECTOR_SIZE;
    NumEntriesPerSector = SECTOR_SIZE / sizeof(U32);
    CurrentSector = 0;


    while (1) {
        Control.SectorLow = FileSystem->FATStart + CurrentSector;
        Control.SectorHigh = 0;
        Control.NumSectors = 1;

        Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (U32)&Control);
        if (Result != DF_ERROR_SUCCESS) {
            goto Out;
        }

        for (Index = 0; Index < NumEntriesPerSector; Index++) {
            if (Buffer[Index] == FAT32_CLUSTER_AVAIL) {
                //-------------------------------------
                // Mark the cluster as used

                Buffer[Index] = FAT32_CLUSTER_LAST;

                //-------------------------------------
                // Write the FAT sector

                Control.SectorLow = FileSystem->FATStart + CurrentSector;
                Control.SectorHigh = 0;
                Control.NumSectors = 1;
                Result = FileSystem->Disk->Driver->Command(DF_DISK_WRITE, (U32)&Control);
                if (Result != DF_ERROR_SUCCESS) {
                    goto Out;
                }

                NewCluster = (CurrentSector * NumEntriesPerSector) + Index;

                if (FileSystem->Master.NumFATs > 1) {
                    //-------------------------------------
                    // Read the FAT sector

                    Control.SectorLow = FileSystem->FATStart2 + CurrentSector;
                    Control.SectorHigh = 0;
                    Control.NumSectors = 1;
                    Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (U32)&Control);
                    if (Result != DF_ERROR_SUCCESS) {
                        goto Out;
                    }

                    //-------------------------------------
                    // Mark the cluster as used

                    Buffer[Index] = FAT32_CLUSTER_LAST;

                    //-------------------------------------
                    // Write the FAT sector

                    Control.SectorLow = FileSystem->FATStart2 + CurrentSector;
                    Control.SectorHigh = 0;
                    Control.NumSectors = 1;
                    Result = FileSystem->Disk->Driver->Command(DF_DISK_WRITE, (U32)&Control);
                    if (Result != DF_ERROR_SUCCESS) {
                        goto Out;
                    }
                }

                return NewCluster;
            }
        }

        CurrentSector++;
        if (CurrentSector >= FileSystem->Master.NumSectorsPerFAT) {
            break;
        }
    }

Out:

    return 0;
}

/***************************************************************************/

/*
static BOOL FindFreeFATEntry(LPFAT32FILESYSTEM FileSystem, U32* Sector,
                             U32* Offset) {
    U32 Buffer[SECTOR_SIZE / sizeof(U32)];
    IOCONTROL Control;
    U32 NumEntriesPerSector;
    U32 CurrentSector;
    U32 CurrentOffset;
    U32 Result;

    *Sector = 0;
    *Offset = 0;

    NumEntriesPerSector = SECTOR_SIZE / sizeof(U32);
    CurrentSector = 0;

    while (1) {
        Control.ID = ID_IOCONTROL;
        Control.Disk = FileSystem->Disk;
        Control.SectorLow = FileSystem->FATStart + CurrentSector;
        Control.SectorHigh = 0;
        Control.NumSectors = 1;
        Control.Buffer = Buffer;
        Control.BufferSize = SECTOR_SIZE;

        Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (U32)&Control);

        if (Result != DF_ERROR_SUCCESS) {
            return FALSE;
        }

        for (CurrentOffset = 0; CurrentOffset < NumEntriesPerSector;
             CurrentOffset++) {
            if (Buffer[CurrentOffset] == FAT32_CLUSTER_AVAIL) {
                *Sector = CurrentSector;
                *Offset = CurrentOffset * sizeof(U32);
                return TRUE;
            }
        }

        CurrentSector++;
        if (CurrentSector >= FileSystem->Master.NumSectorsPerFAT) break;
    }

    return FALSE;
}
*/

/***************************************************************************/

/**
 * @brief Populate a directory entry in a buffer.
 * @param Buffer Directory sector buffer.
 * @param Name 8.3 formatted name.
 * @param Cluster Starting cluster of file.
 * @param Attributes Attribute flags.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL SetDirEntry(LPVOID Buffer, LPSTR Name, CLUSTER Cluster, U32 Attributes) {
    LPFATDIRENTRY_EXT DirEntry = NULL;
    LPFATDIRENTRY_LFN LFNEntry = NULL;
    STR ShortName[16];
    U32 Checksum = 0;
    U32 Ordinal = 0;
    U32 Index = 0;
    U32 Length = 0;
    U32 NumEntries = 0;

    Length = StringLength(Name);
    if (Length > 255) return FALSE;

    //-------------------------------------
    // Compute the number of entries required

    NumEntries = ((Length + 1) / 13) + 1;
    NumEntries++;

    //-------------------------------------
    // Create the short name

    for (Index = 0; Index < 6; Index++) {
        if (Name[Index] == '\0') break;
        ShortName[Index] = Name[Index];
    }

    ShortName[Index++] = '~';
    ShortName[Index++] = '1';

    for (; Index < 11; Index++) {
        ShortName[Index] = STR_SPACE;
    }

    //-------------------------------------
    // Compute checksum

    Checksum = GetNameChecksum(ShortName);

    //-------------------------------------
    // Fill the directory entry

    DirEntry = (LPFATDIRENTRY_EXT)(Buffer + ((NumEntries - 1) * sizeof(FATDIRENTRY_EXT)));

    DirEntry->Name[0] = ShortName[0];
    DirEntry->Name[1] = ShortName[1];
    DirEntry->Name[2] = ShortName[2];
    DirEntry->Name[3] = ShortName[3];
    DirEntry->Name[4] = ShortName[4];
    DirEntry->Name[5] = ShortName[5];
    DirEntry->Name[6] = ShortName[6];
    DirEntry->Name[7] = ShortName[7];

    DirEntry->Ext[0] = ShortName[8];
    DirEntry->Ext[1] = ShortName[9];
    DirEntry->Ext[2] = ShortName[10];

    DirEntry->Attributes = Attributes;
    DirEntry->NT = 0;
    DirEntry->CreationMS = 0;
    DirEntry->CreationHM = 0;
    DirEntry->CreationYM = 0;
    DirEntry->LastAccessDate = 0;
    DirEntry->ClusterHigh = (Cluster & 0xFFFF0000) >> 16;
    DirEntry->Time = 0;
    DirEntry->Date = 0;
    DirEntry->ClusterLow = (Cluster & 0xFFFF);
    DirEntry->Size = 0;

    //-------------------------------------
    // Store the long name

    LFNEntry = (LPFATDIRENTRY_LFN)DirEntry;

    Index = 0;
    Ordinal = 1;

    while (1) {
        LFNEntry--;

        LFNEntry->Ordinal = Ordinal++;
        LFNEntry->Checksum = Checksum;
        LFNEntry->Attributes = FAT_ATTR_LFN;

        if (Name[Index])
            LFNEntry->Char01 = (USTR)Name[Index++];
        else {
            LFNEntry->Char01 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char02 = (USTR)Name[Index++];
        else {
            LFNEntry->Char02 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char03 = (USTR)Name[Index++];
        else {
            LFNEntry->Char03 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char04 = (USTR)Name[Index++];
        else {
            LFNEntry->Char04 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char05 = (USTR)Name[Index++];
        else {
            LFNEntry->Char05 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char06 = (USTR)Name[Index++];
        else {
            LFNEntry->Char06 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char07 = (USTR)Name[Index++];
        else {
            LFNEntry->Char07 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char08 = (USTR)Name[Index++];
        else {
            LFNEntry->Char08 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char09 = (USTR)Name[Index++];
        else {
            LFNEntry->Char09 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char10 = (USTR)Name[Index++];
        else {
            LFNEntry->Char10 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char11 = (USTR)Name[Index++];
        else {
            LFNEntry->Char11 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char12 = (USTR)Name[Index++];
        else {
            LFNEntry->Char12 = (USTR)'\0';
            break;
        }
        if (Name[Index])
            LFNEntry->Char13 = (USTR)Name[Index++];
        else {
            LFNEntry->Char13 = (USTR)'\0';
            break;
        }
    }

    LFNEntry->Ordinal |= BIT_6;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Create a directory entry for a file or folder.
 * @param FileSystem Target file system.
 * @param FolderCluster Cluster of parent folder.
 * @param Name 8.3 formatted name.
 * @param Attributes Attribute flags.
 * @return TRUE on success, FALSE on failure.
 */
static BOOL CreateDirEntry(LPFAT32FILESYSTEM FileSystem, CLUSTER FolderCluster, LPSTR Name, U32 Attributes) {
    LPFATDIRENTRY_EXT DirEntry;
    LPFATDIRENTRY_EXT BaseEntry;
    CLUSTER CurrentCluster;
    CLUSTER NewCluster;
    U32 CurrentOffset;
    U32 Length;
    U32 RequiredEntries;
    U32 FreeEntries;


    Length = StringLength(Name);
    if (Length > 255) {
        return FALSE;
    }

    //-------------------------------------
    // Compute the number of entries required

    RequiredEntries = ((Length + 1) / 13) + 1;
    RequiredEntries++;

    //-------------------------------------
    // First we try to find some space in the
    // existing directory entries

    CurrentCluster = FolderCluster;

    while (1) {
        if (!ReadCluster(FileSystem, CurrentCluster, FileSystem->IOBuffer)) {
            return FALSE;
        }

        BaseEntry = NULL;
        FreeEntries = 0;
        CurrentOffset = 0;

        while (1) {
            DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + CurrentOffset);

            if (DirEntry->Name[0] == 0 && DirEntry->Name[1] == 0) {
                if (BaseEntry == NULL) {
                    BaseEntry = DirEntry;
                }
                FreeEntries++;
            } else {
                if (FreeEntries > 0) {
                }
                BaseEntry = NULL;
                FreeEntries = 0;
            }

            if (FreeEntries == RequiredEntries) {
                NewCluster = FindFreeCluster(FileSystem);

                if (NewCluster == 0) {
                    return FALSE;
                }

                SetDirEntry(BaseEntry, Name, NewCluster, Attributes);

                if (!WriteCluster(FileSystem, CurrentCluster, FileSystem->IOBuffer)) {
                    return FALSE;
                }

                return TRUE;
            }

            CurrentOffset += sizeof(FATDIRENTRY_EXT);

            if (CurrentOffset >= FileSystem->BytesPerCluster) {
                BaseEntry = NULL;
                FreeEntries = 0;
                CurrentOffset = 0;
                CurrentCluster = GetNextClusterInChain(FileSystem, CurrentCluster);
                break;
            }
        }

        if (CurrentCluster == 0 || CurrentCluster >= FAT32_CLUSTER_RESERVED) {
            break;
        }
    }

    // FileLoc.FileCluster = ChainNewCluster(FileSystem, LastValidCluster);

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Append a new cluster to an existing chain.
 * @param FileSystem Target file system.
 * @param Cluster Current last cluster in chain.
 * @return Number of new cluster or 0 on failure.
 */
static CLUSTER ChainNewCluster(LPFAT32FILESYSTEM FileSystem, CLUSTER Cluster) {
    U32 Buffer[SECTOR_SIZE / sizeof(U32)];
    IOCONTROL Control;
    U32 Result;
    U32 NumEntriesPerSector;
    U32 CurrentSector;
    U32 CurrentOffset;
    U32 CurrentFAT;
    U32 FATStart;
    CLUSTER NewCluster;

    NumEntriesPerSector = SECTOR_SIZE / sizeof(U32);
    CurrentSector = 0;
    NewCluster = 0;
    Control.ID = ID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.Buffer = Buffer;
    Control.BufferSize = SECTOR_SIZE;

    while (1) {
        Control.SectorLow = FileSystem->FATStart + CurrentSector;
        Control.SectorHigh = 0;
        Control.NumSectors = 1;

        Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (U32)&Control);

        if (Result != DF_ERROR_SUCCESS) {
            return NewCluster;
        }

        for (CurrentOffset = 0; CurrentOffset < NumEntriesPerSector; CurrentOffset++) {
            if (Buffer[CurrentOffset] == 0) goto Next;
        }

        CurrentSector++;

        if (CurrentSector >= FileSystem->Master.NumSectorsPerFAT) break;
    }

    return NewCluster;

Next:

    NewCluster = (CurrentSector * NumEntriesPerSector) + CurrentOffset;
    CurrentSector = Cluster / NumEntriesPerSector;
    CurrentOffset = Cluster % NumEntriesPerSector;

    FATStart = FileSystem->FATStart;

    for (CurrentFAT = 0; CurrentFAT < FileSystem->Master.NumFATs; CurrentFAT++) {
        Control.SectorLow = FATStart + CurrentSector;
        Control.SectorHigh = 0;
        Control.NumSectors = 1;

        Result = FileSystem->Disk->Driver->Command(DF_DISK_READ, (U32)&Control);

        if (Result != DF_ERROR_SUCCESS) {
            return NewCluster;
        }

        Buffer[CurrentOffset] = NewCluster;

        Result = FileSystem->Disk->Driver->Command(DF_DISK_WRITE, (U32)&Control);

        if (Result != DF_ERROR_SUCCESS) {
            return NewCluster;
        }

        FATStart += FileSystem->Master.NumSectorsPerFAT;
    }

    return NewCluster;
}

/***************************************************************************/

/**
 * @brief Convert a FAT directory entry name to a null-terminated string.
 * @param DirEntry Directory entry containing 8.3 name.
 * @param Name Output buffer for decoded name.
 */
static void DecodeFileName(LPFATDIRENTRY_EXT DirEntry, LPSTR Name) {
    LPFATDIRENTRY_LFN LFNEntry = NULL;
    LPSTR LongName = Name;
    U32 Index;
    U32 Checksum;

    //-------------------------------------
    // 8.3 names

    for (Index = 0; Index < 8; Index++) {
        if (DirEntry->Name[Index] == STR_SPACE) break;
        *Name++ = DirEntry->Name[Index];
    }

    if (DirEntry->Ext[0] != STR_SPACE) {
        *Name++ = STR_DOT;
        for (Index = 0; Index < 3; Index++) {
            if (DirEntry->Ext[Index] == STR_SPACE) break;
            *Name++ = DirEntry->Ext[Index];
        }
    }

    *Name++ = STR_NULL;

    //-------------------------------------
    // Long names

    // Compute checksum

    Checksum = GetNameChecksum(DirEntry->Name);

    LFNEntry = (LPFATDIRENTRY_LFN)DirEntry;

    while (1) {
        LFNEntry--;
        if (LFNEntry->Checksum != Checksum) break;

        *LongName++ = LFNEntry->Char01;
        *LongName++ = LFNEntry->Char02;
        *LongName++ = LFNEntry->Char03;
        *LongName++ = LFNEntry->Char04;
        *LongName++ = LFNEntry->Char05;
        *LongName++ = LFNEntry->Char06;
        *LongName++ = LFNEntry->Char07;
        *LongName++ = LFNEntry->Char08;
        *LongName++ = LFNEntry->Char09;
        *LongName++ = LFNEntry->Char10;
        *LongName++ = LFNEntry->Char11;
        *LongName++ = LFNEntry->Char12;
        *LongName++ = LFNEntry->Char13;
        *LongName = STR_NULL;

        if (LFNEntry->Ordinal & BIT_6) break;
    }
}

/***************************************************************************/

/**
 * @brief Locate a file within the FAT32 file system.
 * @param FileSystem Target file system.
 * @param Path Path of the file to locate.
 * @param FileLoc Output location information.
 * @return TRUE on success, FALSE if not found.
 */
static BOOL LocateFile(LPFAT32FILESYSTEM FileSystem, LPCSTR Path, LPFATFILELOC FileLoc) {
    STR Component[MAX_FILE_NAME];
    STR Name[MAX_FILE_NAME];
    LPFATDIRENTRY_EXT DirEntry;
    U32 PathIndex = 0;
    U32 CompIndex = 0;

    FileLoc->PreviousCluster = 0;
    FileLoc->FolderCluster = FileSystem->Master.RootCluster;
    FileLoc->FileCluster = FileLoc->FolderCluster;
    FileLoc->Offset = 0;
    FileLoc->DataCluster = 0;

    //-------------------------------------
    // Read the root cluster

    if (!ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer)) {
        return FALSE;
    }

    while (1) {
        //-------------------------------------
        // Parse the next component to look for

    NextComponent:

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
        if (Component[0] == STR_NULL) {
            if (Path[PathIndex] == STR_NULL) {
                FileLoc->DataCluster = FileLoc->FolderCluster;
                return TRUE;
            }
            continue;
        }

        //-------------------------------------
        // Loop through all directory entries

        while (1) {
            DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + FileLoc->Offset);

            if ((DirEntry->ClusterLow || DirEntry->ClusterHigh) && (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 &&
                (DirEntry->Name[0] != 0xE5)) {
                DecodeFileName(DirEntry, Name);

                if (StringCompare(Component, TEXT("*")) == 0 || StringCompare(Component, Name) == 0) {
                    if (Path[PathIndex] == STR_NULL) {
                        FileLoc->DataCluster = (((U32)DirEntry->ClusterLow) | (((U32)DirEntry->ClusterHigh) << 16));

                        return TRUE;
                    } else {
                        if (DirEntry->Attributes & FAT_ATTR_FOLDER) {
                            U32 NextDir;

                            NextDir = (U32)DirEntry->ClusterLow;
                            NextDir |= ((U32)DirEntry->ClusterHigh) << 16;

                            FileLoc->FolderCluster = NextDir;
                            FileLoc->FileCluster = FileLoc->FolderCluster;
                            FileLoc->Offset = 0;

                            if (ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer) == FALSE)
                                return FALSE;

                            goto NextComponent;
                        } else {
                            return FALSE;
                        }
                    }
                }
            }

            //-------------------------------------
            // Advance to the next entry

            FileLoc->Offset += sizeof(FATDIRENTRY_EXT);

            if (FileLoc->Offset >= FileSystem->BytesPerCluster) {
                FileLoc->Offset = 0;
                FileLoc->FileCluster = GetNextClusterInChain(FileSystem, FileLoc->FileCluster);

                if (FileLoc->FileCluster == 0 || FileLoc->FileCluster >= FAT32_CLUSTER_RESERVED) return FALSE;

                if (ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer) == FALSE) return FALSE;
            }
        }
    }
}

/***************************************************************************/

/**
 * @brief Populate file information from a directory entry.
 * @param DirEntry Directory entry source.
 * @param File Target FAT file object.
 */
static void TranslateFileInfo(LPFATDIRENTRY_EXT DirEntry, LPFATFILE File) {
    //-------------------------------------
    // Translate the attributes

    File->Header.Attributes = 0;

    if (DirEntry->Attributes & FAT_ATTR_FOLDER) {
        File->Header.Attributes |= FS_ATTR_FOLDER;
    }

    if (DirEntry->Attributes & FAT_ATTR_READONLY) {
        File->Header.Attributes |= FS_ATTR_READONLY;
    }

    if (DirEntry->Attributes & FAT_ATTR_HIDDEN) {
        File->Header.Attributes |= FS_ATTR_HIDDEN;
    }

    if (DirEntry->Attributes & FAT_ATTR_SYSTEM) {
        File->Header.Attributes |= FS_ATTR_SYSTEM;
    }

    File->Header.Attributes |= FS_ATTR_EXECUTABLE;

    //-------------------------------------
    // Translate the size

    File->Header.SizeLow = DirEntry->Size;
    File->Header.SizeHigh = 0;

    //-------------------------------------
    // Translate the time

    File->Header.Creation.Year = ((DirEntry->CreationYM & 0xFE00) >> 9) + 1980;
    File->Header.Creation.Month = (DirEntry->CreationYM & 0x01E0) >> 5;
    File->Header.Creation.Day = (DirEntry->CreationYM & 0x001F) >> 0;
    File->Header.Creation.Hour = (DirEntry->CreationHM & 0xF800) >> 11;
    File->Header.Creation.Minute = (DirEntry->CreationHM & 0x07E0) >> 5;
    File->Header.Creation.Second = ((DirEntry->CreationHM & 0x001F) >> 0) * 2;
    File->Header.Creation.Milli = 0;
}

/***************************************************************************/

/**
 * @brief Initialize the FAT32 driver.
 * @return DF_ERROR_SUCCESS.
 */
static U32 Initialize(void) { return DF_ERROR_SUCCESS; }

/***************************************************************************/

/**
 * @brief Create a file or folder on the file system.
 * @param File File information containing path and attributes.
 * @param IsFolder TRUE to create a folder, FALSE to create a file.
 * @return DF_ERROR_* code.
 */
static U32 CreateFile(LPFILEINFO File, BOOL IsFolder) {
    LPFAT32FILESYSTEM FileSystem = NULL;
    FATFILELOC FileLoc;
    STR Component[MAX_FILE_NAME];
    STR Name[MAX_FILE_NAME];
    LPFATDIRENTRY_EXT DirEntry;
    U32 PathIndex = 0;
    U32 CompIndex = 0;


    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) {
        return DF_ERROR_BADPARAM;
    }

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT32FILESYSTEM)File->FileSystem;
    if (FileSystem == NULL) {
        return DF_ERROR_BADPARAM;
    }

    //-------------------------------------
    // Initialize file location

    FileLoc.PreviousCluster = 0;
    FileLoc.FolderCluster = FileSystem->Master.RootCluster;
    FileLoc.FileCluster = FileLoc.FolderCluster;
    FileLoc.Offset = 0;
    FileLoc.DataCluster = 0;

    //-------------------------------------
    // Read the root cluster

    if (!ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer)) {
        return DF_ERROR_IO;
    }

    while (1) {
        //-------------------------------------
        // Parse the next component to look for

    NextComponent:

        CompIndex = 0;

        while (1) {
            if (File->Name[PathIndex] == STR_SLASH) {
                Component[CompIndex] = STR_NULL;
                PathIndex++;
                break;
            } else if (File->Name[PathIndex] == STR_NULL) {
                Component[CompIndex] = STR_NULL;
                break;
            } else {
                Component[CompIndex++] = File->Name[PathIndex++];
            }
        }

        // Check if we're at the last component (file/folder to create)
        BOOL IsLastComponent = (File->Name[PathIndex] == STR_NULL);

        //-------------------------------------
        // Loop through all directory entries

        while (1) {
            DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + FileLoc.Offset);

            if ((DirEntry->ClusterLow || DirEntry->ClusterHigh) && (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 &&
                (DirEntry->Name[0] != 0xE5)) {
                DecodeFileName(DirEntry, Name);

                if (StringCompare(Component, TEXT("*")) == 0 || StringCompare(Component, Name) == 0) {
                    if (IsLastComponent) {
                        // Found existing item with same name
                        if (IsFolder && (DirEntry->Attributes & FAT_ATTR_FOLDER)) {
                            return DF_ERROR_SUCCESS; // Folder already exists
                        } else if (!IsFolder && !(DirEntry->Attributes & FAT_ATTR_FOLDER)) {
                            return DF_ERROR_SUCCESS; // File already exists
                        }
                        return DF_ERROR_GENERIC; // Type mismatch
                    } else {
                        // Navigating to next directory component
                        if (DirEntry->Attributes & FAT_ATTR_FOLDER) {
                            U32 NextDir;

                            NextDir = (U32)DirEntry->ClusterLow;
                            NextDir |= ((U32)DirEntry->ClusterHigh) << 16;

                            FileLoc.FolderCluster = NextDir;
                            FileLoc.FileCluster = FileLoc.FolderCluster;
                            FileLoc.Offset = 0;

                            if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE)
                                return DF_ERROR_IO;

                            goto NextComponent;
                        } else {
                            return DF_ERROR_GENERIC; // Path component is not a directory
                        }
                    }
                }
            }

            //-------------------------------------
            // Advance to the next entry

            FileLoc.Offset += sizeof(FATDIRENTRY_EXT);

            if (FileLoc.Offset >= FileSystem->BytesPerCluster) {
                FileLoc.Offset = 0;
                FileLoc.FileCluster = GetNextClusterInChain(FileSystem, FileLoc.FileCluster);

                if (FileLoc.FileCluster == 0 || (FileLoc.FileCluster & 0x0FFFFFFF) >= 0x0FFFFFF8) {
                    //-------------------------------------
                    // We are at the end of this directory
                    // and we did not find the current component
                    // so we create it

                    if (IsLastComponent) {
                        // Create the final file/folder
                        BOOL result = CreateDirEntry(FileSystem, FileLoc.FolderCluster, Component, IsFolder ? FAT_ATTR_FOLDER : FAT_ATTR_ARCHIVE);
                        return result ? DF_ERROR_SUCCESS : DF_ERROR_GENERIC;
                    } else {
                        // Create intermediate directory and continue navigation
                        if (CreateDirEntry(FileSystem, FileLoc.FolderCluster, Component, FAT_ATTR_FOLDER) != TRUE) {
                            return DF_ERROR_GENERIC;
                        }

                        // Find the newly created directory and navigate into it
                        FileLoc.Offset = 0;
                        FileLoc.FileCluster = FileLoc.FolderCluster;

                        if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) {
                            return DF_ERROR_IO;
                        }

                        // Search for the newly created directory
                        while (1) {
                            DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + FileLoc.Offset);

                            if ((DirEntry->ClusterLow || DirEntry->ClusterHigh) && (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 &&
                                (DirEntry->Name[0] != 0xE5)) {
                                DecodeFileName(DirEntry, Name);
                
                                if (StringCompare(Component, Name) == 0 && (DirEntry->Attributes & FAT_ATTR_FOLDER)) {
                                    U32 NextDir = (U32)DirEntry->ClusterLow | (((U32)DirEntry->ClusterHigh) << 16);
                                    FileLoc.FolderCluster = NextDir;
                                    FileLoc.FileCluster = NextDir;
                                    FileLoc.Offset = 0;

                                    if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) {
                                        return DF_ERROR_IO;
                                    }

                                    goto NextComponent;
                                }
                            }

                            FileLoc.Offset += sizeof(FATDIRENTRY_EXT);
                            if (FileLoc.Offset >= FileSystem->BytesPerCluster) {
                                return DF_ERROR_GENERIC; // Should have found the directory we just created
                            }
                        }
                    }
                }

                if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) return DF_ERROR_IO;
            }
        }
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Delete a folder from the file system.
 * @param File File information describing folder.
 * @return DF_ERROR_* code.
 */
static U32 DeleteFolder(LPFILEINFO File) {
    UNUSED(File);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Rename a folder within the file system.
 * @param File File information with old and new names.
 * @return DF_ERROR_* code.
 */
static U32 RenameFolder(LPFILEINFO File) {
    UNUSED(File);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Open a file for reading or writing.
 * @param Find File information containing path.
 * @return Handle to FAT file or NULL on failure.
 */
static LPFATFILE OpenFile(LPFILEINFO Find) {
    LPFAT32FILESYSTEM FileSystem = NULL;
    LPFATFILE File = NULL;
    LPFATDIRENTRY_EXT DirEntry = NULL;
    FATFILELOC FileLoc;

    //-------------------------------------
    // Check validity of parameters

    if (Find == NULL) return NULL;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT32FILESYSTEM)Find->FileSystem;

    if (LocateFile(FileSystem, Find->Name, &FileLoc) == TRUE) {
        if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) return FALSE;

        DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + FileLoc.Offset);

        File = NewFATFile(FileSystem, &FileLoc);
        if (File == NULL) return NULL;

        DecodeFileName(DirEntry, File->Header.Name);
        TranslateFileInfo(DirEntry, File);

        //-------------------------------------
        // Handle FILE_OPEN_TRUNCATE flag

        if (Find->Flags & FILE_OPEN_TRUNCATE) {

            // Reset file size to zero in memory
            File->Header.SizeLow = 0;
            File->Header.SizeHigh = 0;

            // Update directory entry
            DirEntry->Size = 0;

            // Write the updated directory entry back to disk
            if (WriteCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) {
                // Don't free directly - let ReleaseKernelObject handle it
                return NULL;
            }

        }
    } else if (Find->Flags & FILE_OPEN_CREATE_ALWAYS) {

        //-------------------------------------
        // Create the file

        FILEINFO TempFileInfo;
        TempFileInfo.FileSystem = (LPFILESYSTEM)FileSystem;
        StringCopy(TempFileInfo.Name, Find->Name);

        if (CreateFile(&TempFileInfo, FALSE) != DF_ERROR_SUCCESS) {
            return NULL;
        }

        //-------------------------------------
        // Now locate the newly created file

        if (LocateFile(FileSystem, Find->Name, &FileLoc) == FALSE) {
            return NULL;
        }

        if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer) == FALSE) {
            return NULL;
        }

        DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + FileLoc.Offset);

        File = NewFATFile(FileSystem, &FileLoc);
        if (File == NULL) {
            return NULL;
        }

        DecodeFileName(DirEntry, File->Header.Name);
        TranslateFileInfo(DirEntry, File);

    }

    return File;
}

/***************************************************************************/

/**
 * @brief Advance to next directory entry during enumeration.
 * @param File Current FAT file handle representing directory.
 * @return DF_ERROR_SUCCESS or error code.
 */
static U32 OpenNext(LPFATFILE File) {
    LPFAT32FILESYSTEM FileSystem = NULL;
    LPFATDIRENTRY_EXT DirEntry = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) return DF_ERROR_BADPARAM;
    if (File->Header.ID != ID_FILE) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT32FILESYSTEM)File->Header.FileSystem;

    //-------------------------------------
    // Read the cluster containing the file

    if (ReadCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer) == FALSE) return DF_ERROR_IO;

    while (1) {
        File->Location.Offset += sizeof(FATDIRENTRY_EXT);

        if (File->Location.Offset >= FileSystem->BytesPerCluster) {
            File->Location.Offset = 0;

            File->Location.FileCluster = GetNextClusterInChain(FileSystem, File->Location.FileCluster);

            if (File->Location.FileCluster == 0 || File->Location.FileCluster >= FAT32_CLUSTER_RESERVED)
                return DF_ERROR_GENERIC;

            if (ReadCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer) == FALSE) return DF_ERROR_IO;
        }

        DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + File->Location.Offset);

        if ((DirEntry->ClusterLow || DirEntry->ClusterHigh) && (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 &&
            (DirEntry->Name[0] != 0xE5)) {
            File->Location.DataCluster = (((U32)DirEntry->ClusterLow) | (((U32)DirEntry->ClusterHigh) << 16));

            DecodeFileName(DirEntry, File->Header.Name);
            TranslateFileInfo(DirEntry, File);
            break;
        }
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Close an open FAT32 file handle.
 * @param File File handle to close.
 * @return DF_ERROR_SUCCESS.
 */
static U32 CloseFile(LPFATFILE File) {
    LPFAT32FILESYSTEM FileSystem;
    LPFATDIRENTRY_EXT DirEntry;

    if (File == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT32FILESYSTEM)File->Header.FileSystem;

    //-------------------------------------
    // Update file information in directory entry

    if (ReadCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer) == FALSE) {
        return DF_ERROR_IO;
    }

    DirEntry = (LPFATDIRENTRY_EXT)(FileSystem->IOBuffer + File->Location.Offset);

    if (File->Header.SizeLow > DirEntry->Size) {
        DirEntry->Size = File->Header.SizeLow;

        if (WriteCluster(FileSystem, File->Location.FileCluster, FileSystem->IOBuffer) == FALSE) {
            return DF_ERROR_IO;
        }
    }

    ReleaseKernelObject(File);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Read data from a file.
 * @param File File handle with read parameters.
 * @return DF_ERROR_SUCCESS or error code.
 */
static U32 ReadFile(LPFATFILE File) {
    LPFAT32FILESYSTEM FileSystem;
    CLUSTER RelativeCluster;
    CLUSTER Cluster;
    U32 OffsetInCluster;
    U32 BytesRemaining;
    U32 ByteCount;
    U32 Index;

    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) return DF_ERROR_BADPARAM;
    if (File->Header.ID != ID_FILE) return DF_ERROR_BADPARAM;
    if (File->Header.Buffer == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT32FILESYSTEM)File->Header.FileSystem;

    //-------------------------------------
    // Compute the starting cluster and the offset

    RelativeCluster = File->Header.Position / FileSystem->BytesPerCluster;
    OffsetInCluster = File->Header.Position % FileSystem->BytesPerCluster;
    BytesRemaining = File->Header.ByteCount;
    File->Header.BytesTransferred = 0;

    Cluster = File->Location.DataCluster;

    for (Index = 0; Index < RelativeCluster; Index++) {
        Cluster = GetNextClusterInChain(FileSystem, Cluster);
        if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED) {
            return DF_ERROR_IO;
        }
    }

    while (1) {
        //-------------------------------------
        // Read the current data cluster

        if (ReadCluster(FileSystem, Cluster, FileSystem->IOBuffer) == FALSE) {
            return DF_ERROR_IO;
        }

        ByteCount = FileSystem->BytesPerCluster - OffsetInCluster;
        if (ByteCount > BytesRemaining) ByteCount = BytesRemaining;

        //-------------------------------------
        // Copy the data to the user buffer

        MemoryCopy(
            ((U8*)File->Header.Buffer) + File->Header.BytesTransferred, FileSystem->IOBuffer + OffsetInCluster, ByteCount);

        //-------------------------------------
        // Update counters

        OffsetInCluster = 0;
        BytesRemaining -= ByteCount;
        File->Header.BytesTransferred += ByteCount;
        File->Header.Position += ByteCount;

        //-------------------------------------
        // Check if we read all data

        if (BytesRemaining == 0) break;

        //-------------------------------------
        // Get the next cluster in the chain

        Cluster = GetNextClusterInChain(FileSystem, Cluster);

        if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED) {
            break;
        }
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Write data to a file.
 * @param File File handle with write parameters.
 * @return DF_ERROR_SUCCESS or error code.
 */
static U32 WriteFile(LPFATFILE File) {
    LPFAT32FILESYSTEM FileSystem;
    CLUSTER RelativeCluster;
    CLUSTER Cluster;
    CLUSTER LastValidCluster;
    U32 OffsetInCluster;
    U32 BytesRemaining;
    U32 ByteCount;
    U32 Index;

    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) return DF_ERROR_BADPARAM;
    if (File->Header.ID != ID_FILE) return DF_ERROR_BADPARAM;
    if (File->Header.Buffer == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------
    // Get the associated file system

    FileSystem = (LPFAT32FILESYSTEM)File->Header.FileSystem;

    //-------------------------------------
    // Compute the starting cluster and the offset

    RelativeCluster = File->Header.Position / FileSystem->BytesPerCluster;
    OffsetInCluster = File->Header.Position % FileSystem->BytesPerCluster;
    ByteCount = FileSystem->BytesPerCluster - OffsetInCluster;
    BytesRemaining = File->Header.ByteCount;
    File->Header.BytesTransferred = 0;

    Cluster = File->Location.DataCluster;
    LastValidCluster = Cluster;

    for (Index = 0; Index < RelativeCluster; Index++) {
        Cluster = GetNextClusterInChain(FileSystem, Cluster);

        if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED) {
            Cluster = ChainNewCluster(FileSystem, LastValidCluster);

            if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED) {
                return DF_ERROR_FS_NOSPACE;
            }
        }

        LastValidCluster = Cluster;
    }

    while (1) {
        //-------------------------------------
        // Read the current data cluster

        if (ReadCluster(FileSystem, Cluster, FileSystem->IOBuffer) == FALSE) {
            return DF_ERROR_IO;
        }

        //-------------------------------------
        // Copy the user buffer

        MemoryCopy(
            FileSystem->IOBuffer + OffsetInCluster, ((U8*)File->Header.Buffer) + File->Header.BytesTransferred, ByteCount);

        //-------------------------------------
        // Write the current data cluster

        if (WriteCluster(FileSystem, Cluster, FileSystem->IOBuffer) == FALSE) {
            return DF_ERROR_IO;
        }

        ByteCount = FileSystem->BytesPerCluster - OffsetInCluster;
        if (ByteCount > BytesRemaining) ByteCount = BytesRemaining;

        //-------------------------------------
        // Update counters

        OffsetInCluster = 0;
        BytesRemaining -= ByteCount;
        File->Header.BytesTransferred += ByteCount;
        File->Header.Position += ByteCount;

        //-------------------------------------
        // Check if we wrote all data

        if (BytesRemaining == 0) break;

        //-------------------------------------
        // Get the next cluster in the chain

        Cluster = GetNextClusterInChain(FileSystem, Cluster);

        if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED) {
            Cluster = ChainNewCluster(FileSystem, LastValidCluster);

            if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED) {
                return DF_ERROR_FS_NOSPACE;
            }
        }

        LastValidCluster = Cluster;
    }

    if (File->Header.Position > File->Header.SizeLow) {
        File->Header.SizeLow = File->Header.Position;
    }

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Create a new FAT32 partition on disk.
 * @param Create Partition creation parameters.
 * @return DF_ERROR_* code.
 */
static U32 CreatePartition(LPPARTITION_CREATION Create) {
    LPFAT32MBR Master = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (Create == NULL) return DF_ERROR_BADPARAM;
    if (Create->Disk == NULL) return DF_ERROR_BADPARAM;

    //-------------------------------------

    Master = (LPFAT32MBR)KernelHeapAlloc(sizeof(FAT32MBR));

    if (Master == NULL) return DF_ERROR_NOMEMORY;

    //-------------------------------------
    // Fill the master boot record

    Master->OEMName[0] = 'M';
    Master->OEMName[1] = 'S';
    Master->OEMName[2] = 'W';
    Master->OEMName[3] = 'I';
    Master->OEMName[4] = 'N';
    Master->OEMName[5] = '4';
    Master->OEMName[6] = '.';
    Master->OEMName[7] = '1';
    Master->BytesPerSector = 512;
    Master->SectorsPerCluster = 8;
    Master->ReservedSectors = 3;
    Master->NumFATs = 2;
    Master->NumRootEntries_NA = 0;
    Master->NumSectors_NA = 0;
    Master->MediaDescriptor = 0xF8;
    Master->SectorsPerFAT_NA = 0;
    Master->SectorsPerTrack = 63;
    Master->NumHeads = 255;
    Master->NumHiddenSectors = 127;
    Master->NumSectors = Create->PartitionNumSectors;
    Master->NumSectorsPerFAT = 4;
    Master->Flags = 0;
    Master->Version = 0;
    Master->RootCluster = 2;
    Master->InfoSector = 1;
    Master->BackupBootSector = 6;
    Master->LogicalDriveNumber = 0x80;
    Master->Reserved2 = 0;
    Master->ExtendedSignature = 0x29;
    Master->SerialNumber = 0;
    Master->FATName[0] = 'F';
    Master->FATName[1] = 'A';
    Master->FATName[2] = 'T';
    Master->FATName[3] = '3';
    Master->FATName[4] = '2';
    Master->FATName[5] = ' ';
    Master->FATName[6] = ' ';
    Master->FATName[7] = ' ';
    Master->BIOSMark = 0xAA55;

    //-------------------------------------

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Dispatch function for FAT32 driver commands.
 * @param Function Requested driver function.
 * @param Parameter Optional parameter pointer.
 * @return DF_ERROR_* result code.
 */
U32 FAT32Commands(U32 Function, U32 Parameter) {
    switch (Function) {
        case DF_LOAD:
            return Initialize();
        case DF_GETVERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_FS_GETVOLUMEINFO:
            return DF_ERROR_NOTIMPL;
        case DF_FS_SETVOLUMEINFO:
            return DF_ERROR_NOTIMPL;
        case DF_FS_CREATEFOLDER:
            return (U32)CreateFile((LPFILEINFO)Parameter, TRUE);
        case DF_FS_DELETEFOLDER:
            return (U32)DeleteFolder((LPFILEINFO)Parameter);
        case DF_FS_RENAMEFOLDER:
            return (U32)RenameFolder((LPFILEINFO)Parameter);
        case DF_FS_OPENFILE:
            return (U32)OpenFile((LPFILEINFO)Parameter);
        case DF_FS_OPENNEXT:
            return (U32)OpenNext((LPFATFILE)Parameter);
        case DF_FS_CLOSEFILE:
            return (U32)CloseFile((LPFATFILE)Parameter);
        case DF_FS_DELETEFILE:
            return DF_ERROR_NOTIMPL;
        case DF_FS_RENAMEFILE:
            return DF_ERROR_NOTIMPL;
        case DF_FS_READ:
            return (U32)ReadFile((LPFATFILE)Parameter);
        case DF_FS_WRITE:
            return (U32)WriteFile((LPFATFILE)Parameter);
        case DF_FS_CREATEPARTITION:
            return (U32)CreatePartition((LPPARTITION_CREATION)Parameter);
    }

    return DF_ERROR_NOTIMPL;
}
