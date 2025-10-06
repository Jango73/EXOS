
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


    EXT2

\************************************************************************/

#include "drivers/EXT2.h"

#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "String.h"

/************************************************************************/

#define VER_MAJOR 0
#define VER_MINOR 1

#define EXT2_DEFAULT_BLOCK_SIZE 1024
#define EXT2_MODE_TYPE_MASK 0xF000
#define EXT2_MODE_DIRECTORY 0x4000
#define EXT2_MODE_REGULAR 0x8000
#define EXT2_DIRECT_BLOCKS 12
#define EXT2_DIR_ENTRY_HEADER_SIZE (sizeof(U32) + sizeof(U16) + sizeof(U8) + sizeof(U8))
#define EXT2_MODE_USER_WRITE 0x0080
#define EXT2_MODE_GROUP_WRITE 0x0010
#define EXT2_MODE_OTHER_WRITE 0x0002
#define EXT2_MODE_USER_EXECUTE 0x0040
#define EXT2_MODE_GROUP_EXECUTE 0x0008
#define EXT2_MODE_OTHER_EXECUTE 0x0001

/************************************************************************/

typedef struct tag_EXT2FILESYSTEM {
    FILESYSTEM Header;
    LPPHYSICALDISK Disk;
    EXT2SUPER Super;
    LPEXT2BLOCKGROUP Groups;
    U32 GroupCount;
    SECTOR PartitionStart;
    U32 PartitionSize;
    U32 BlockSize;
    U32 SectorsPerBlock;
    U32 InodeSize;
    U32 InodesPerBlock;
    MUTEX FilesMutex;
    U8* IOBuffer;
} EXT2FILESYSTEM, *LPEXT2FILESYSTEM;

/************************************************************************/

typedef struct tag_EXT2FILE {
    FILE Header;
    EXT2INODE Inode;
    U32 InodeIndex;
    BOOL IsDirectory;
    BOOL Enumerate;
    U32 DirectoryBlockIndex;
    U32 DirectoryBlockOffset;
    U8* DirectoryBlock;
    BOOL DirectoryBlockValid;
    STR Pattern[MAX_FILE_NAME];
} EXT2FILE, *LPEXT2FILE;

/************************************************************************/

static LPEXT2FILESYSTEM NewEXT2FileSystem(LPPHYSICALDISK Disk);
static LPEXT2FILE NewEXT2File(LPEXT2FILESYSTEM FileSystem);
static BOOL HasWildcard(LPCSTR Path);
static void ExtractBaseName(LPCSTR Path, LPSTR Name);
static void ReleaseDirectoryResources(LPEXT2FILE File);
static BOOL MatchPattern(LPCSTR Name, LPCSTR Pattern);
static BOOL ReadSectors(LPEXT2FILESYSTEM FileSystem, U32 Sector, U32 Count, LPVOID Buffer);
static BOOL ReadBlock(LPEXT2FILESYSTEM FileSystem, U32 Block, LPVOID Buffer);
static BOOL WriteSectors(LPEXT2FILESYSTEM FileSystem, U32 Sector, U32 Count, LPCVOID Buffer);
static BOOL WriteBlock(LPEXT2FILESYSTEM FileSystem, U32 Block, LPCVOID Buffer);
static BOOL LoadGroupDescriptors(LPEXT2FILESYSTEM FileSystem);
static BOOL ReadInode(LPEXT2FILESYSTEM FileSystem, U32 InodeIndex, LPEXT2INODE Inode);
static BOOL GetInodeBlockNumber(LPEXT2FILESYSTEM FileSystem, LPEXT2INODE Inode, U32 BlockIndex, U32* BlockNumber);
static BOOL FindInodeInDirectory(
    LPEXT2FILESYSTEM FileSystem, LPEXT2INODE Directory, LPCSTR Name, U32* InodeIndex);
static BOOL ResolvePath(
    LPEXT2FILESYSTEM FileSystem, LPCSTR Path, LPEXT2INODE Inode, U32* InodeIndex);
static BOOL LoadDirectoryInode(
    LPEXT2FILESYSTEM FileSystem, LPCSTR Path, LPEXT2INODE Inode, U32* InodeIndex);
static void FillFileHeaderFromInode(LPEXT2FILE File, LPCSTR Name, LPEXT2INODE Inode);
static BOOL SetupDirectoryHandle(
    LPEXT2FILE File,
    LPEXT2FILESYSTEM FileSystem,
    LPEXT2INODE Directory,
    U32 InodeIndex,
    BOOL Enumerate,
    LPCSTR Pattern);
static BOOL LoadNextDirectoryEntry(LPEXT2FILE File);

static U32 Initialize(void);
static LPEXT2FILE OpenFile(LPFILEINFO Info);
static U32 OpenNext(LPEXT2FILE File);
static U32 CloseFile(LPEXT2FILE File);
static U32 ReadFile(LPEXT2FILE File);
static U32 WriteFile(LPEXT2FILE File);

BOOL MountPartition_EXT2(LPPHYSICALDISK Disk, LPBOOTPARTITION Partition, U32 Base, U32 PartIndex);
U32 EXT2Commands(U32 Function, U32 Parameter);

/************************************************************************/

static BOOL HasWildcard(LPCSTR Path) {
    if (STRING_EMPTY(Path)) return FALSE;

    if (StringFindChar(Path, '*') != NULL) return TRUE;
    if (StringFindChar(Path, '?') != NULL) return TRUE;

    return FALSE;
}

/************************************************************************/

static void ExtractBaseName(LPCSTR Path, LPSTR Name) {
    STR Buffer[MAX_PATH_NAME];
    LPSTR Slash;
    U32 Length;

    if (Name == NULL) return;

    Name[0] = STR_NULL;

    if (STRING_EMPTY(Path)) {
        StringCopy(Name, TEXT("/"));
        return;
    }

    StringCopy(Buffer, Path);

    Length = StringLength(Buffer);
    while (Length > 0 && Buffer[Length - 1] == PATH_SEP) {
        Buffer[Length - 1] = STR_NULL;
        Length--;
    }

    if (Length == 0) {
        StringCopy(Name, TEXT("/"));
        return;
    }

    Slash = StringFindCharR(Buffer, PATH_SEP);
    if (Slash != NULL) {
        StringCopy(Name, Slash + 1);
    } else {
        StringCopy(Name, Buffer);
    }
}

/************************************************************************/

static void ReleaseDirectoryResources(LPEXT2FILE File) {
    if (File == NULL) return;

    if (File->DirectoryBlock != NULL) {
        KernelHeapFree(File->DirectoryBlock);
        File->DirectoryBlock = NULL;
    }

    File->DirectoryBlockValid = FALSE;
}

/************************************************************************/

static BOOL MatchPattern(LPCSTR Name, LPCSTR Pattern) {
    if (Pattern == NULL) return FALSE;
    if (Name == NULL) return FALSE;

    if (Pattern[0] == STR_NULL) return Name[0] == STR_NULL;

    if (Pattern[0] == '*') {
        while (*Pattern == '*') Pattern++;
        if (*Pattern == STR_NULL) return TRUE;

        while (*Name != STR_NULL) {
            if (MatchPattern(Name, Pattern)) return TRUE;
            Name++;
        }

        return MatchPattern(Name, Pattern);
    }

    if (Pattern[0] == '?') {
        if (*Name == STR_NULL) return FALSE;
        return MatchPattern(Name + 1, Pattern + 1);
    }

    if (*Name != *Pattern) return FALSE;

    if (*Name == STR_NULL) return TRUE;

    return MatchPattern(Name + 1, Pattern + 1);
}

/************************************************************************/

static BOOL LoadDirectoryInode(
    LPEXT2FILESYSTEM FileSystem, LPCSTR Path, LPEXT2INODE Inode, U32* InodeIndex) {
    STR Normalized[MAX_PATH_NAME];
    U32 Length;

    if (FileSystem == NULL || Inode == NULL) return FALSE;

    if (STRING_EMPTY(Path)) {
        if (ReadInode(FileSystem, EXT2_ROOT_INODE, Inode) == FALSE) return FALSE;
        if (InodeIndex != NULL) *InodeIndex = EXT2_ROOT_INODE;
        return (Inode->Mode & EXT2_MODE_TYPE_MASK) == EXT2_MODE_DIRECTORY;
    }

    StringCopy(Normalized, Path);

    Length = StringLength(Normalized);
    while (Length > 0 && Normalized[Length - 1] == PATH_SEP) {
        Normalized[Length - 1] = STR_NULL;
        Length--;
    }

    if (Length == 0) {
        if (ReadInode(FileSystem, EXT2_ROOT_INODE, Inode) == FALSE) return FALSE;
        if (InodeIndex != NULL) *InodeIndex = EXT2_ROOT_INODE;
    } else {
        if (ResolvePath(FileSystem, Normalized, Inode, InodeIndex) == FALSE) return FALSE;
    }

    if ((Inode->Mode & EXT2_MODE_TYPE_MASK) != EXT2_MODE_DIRECTORY) return FALSE;

    return TRUE;
}

/************************************************************************/

static void FillFileHeaderFromInode(LPEXT2FILE File, LPCSTR Name, LPEXT2INODE Inode) {
    if (File == NULL || Inode == NULL) return;

    if (Name != NULL && Name[0] != STR_NULL) {
        StringCopy(File->Header.Name, Name);
    } else {
        File->Header.Name[0] = STR_NULL;
    }

    File->Header.Attributes = 0;

    if ((Inode->Mode & EXT2_MODE_TYPE_MASK) == EXT2_MODE_DIRECTORY) {
        File->Header.Attributes |= FS_ATTR_FOLDER;
    }

    if ((Inode->Mode & (EXT2_MODE_USER_WRITE | EXT2_MODE_GROUP_WRITE | EXT2_MODE_OTHER_WRITE)) == 0) {
        File->Header.Attributes |= FS_ATTR_READONLY;
    }

    if ((Inode->Mode & EXT2_MODE_TYPE_MASK) == EXT2_MODE_REGULAR) {
        if (Inode->Mode & (EXT2_MODE_USER_EXECUTE | EXT2_MODE_GROUP_EXECUTE | EXT2_MODE_OTHER_EXECUTE)) {
            File->Header.Attributes |= FS_ATTR_EXECUTABLE;
        }
    }

    File->Header.SizeLow = Inode->Size;
    File->Header.SizeHigh = 0;

    MemorySet(&(File->Header.Creation), 0, sizeof(DATETIME));
    MemorySet(&(File->Header.Accessed), 0, sizeof(DATETIME));
    MemorySet(&(File->Header.Modified), 0, sizeof(DATETIME));
}

/************************************************************************/

static BOOL SetupDirectoryHandle(
    LPEXT2FILE File,
    LPEXT2FILESYSTEM FileSystem,
    LPEXT2INODE Directory,
    U32 InodeIndex,
    BOOL Enumerate,
    LPCSTR Pattern) {
    if (File == NULL || FileSystem == NULL || Directory == NULL) return FALSE;

    File->IsDirectory = TRUE;
    File->Enumerate = Enumerate;
    MemoryCopy(&(File->Inode), Directory, sizeof(EXT2INODE));
    File->InodeIndex = InodeIndex;
    File->DirectoryBlockIndex = 0;
    File->DirectoryBlockOffset = 0;
    File->DirectoryBlockValid = FALSE;
    File->DirectoryBlock = NULL;
    File->Pattern[0] = STR_NULL;

    if (Pattern != NULL && Pattern[0] != STR_NULL) {
        StringCopy(File->Pattern, Pattern);
    } else {
        StringCopy(File->Pattern, TEXT("*"));
    }

    if (Enumerate) {
        if (FileSystem->BlockSize == 0) return FALSE;

        File->DirectoryBlock = (U8*)KernelHeapAlloc(FileSystem->BlockSize);
        if (File->DirectoryBlock == NULL) return FALSE;

        if (LoadNextDirectoryEntry(File) == FALSE) {
            ReleaseDirectoryResources(File);
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

static BOOL LoadNextDirectoryEntry(LPEXT2FILE File) {
    LPEXT2FILESYSTEM FileSystem;
    U32 BlockCount;

    if (File == NULL) return FALSE;

    FileSystem = (LPEXT2FILESYSTEM)File->Header.FileSystem;
    if (FileSystem == NULL) return FALSE;

    if (FileSystem->BlockSize == 0) return FALSE;

    BlockCount = (File->Inode.Size + FileSystem->BlockSize - 1) / FileSystem->BlockSize;
    if (BlockCount == 0) {
        BlockCount = 1;
    }

    while (File->DirectoryBlockIndex < BlockCount) {
        if (File->DirectoryBlockValid == FALSE) {
            U32 BlockNumber;

            if (GetInodeBlockNumber(FileSystem, &(File->Inode), File->DirectoryBlockIndex, &BlockNumber) == FALSE) {
                return FALSE;
            }

            if (BlockNumber == 0) {
                File->DirectoryBlockIndex++;
                File->DirectoryBlockOffset = 0;
                File->DirectoryBlockValid = FALSE;
                continue;
            }

            if (ReadBlock(FileSystem, BlockNumber, File->DirectoryBlock) == FALSE) {
                return FALSE;
            }

            File->DirectoryBlockValid = TRUE;
            File->DirectoryBlockOffset = 0;
        }

        while (File->DirectoryBlockOffset + EXT2_DIR_ENTRY_HEADER_SIZE <= FileSystem->BlockSize) {
            U32 Offset;
            LPEXT2DIRECTORYENTRY Entry;
            U16 EntryLength;
            U8 NameLength;
            STR EntryName[MAX_FILE_NAME];
            EXT2INODE EntryInode;

            Offset = File->DirectoryBlockOffset;
            Entry = (LPEXT2DIRECTORYENTRY)(File->DirectoryBlock + Offset);
            EntryLength = Entry->RecordLength;
            NameLength = Entry->NameLength;

            if (EntryLength < EXT2_DIR_ENTRY_HEADER_SIZE) {
                File->DirectoryBlockOffset = FileSystem->BlockSize;
                break;
            }

            if (Offset + EntryLength > FileSystem->BlockSize) {
                File->DirectoryBlockOffset = FileSystem->BlockSize;
                break;
            }

            File->DirectoryBlockOffset += EntryLength;

            if (Entry->Inode == 0 || NameLength == 0) continue;

            if (NameLength >= MAX_FILE_NAME) {
                NameLength = MAX_FILE_NAME - 1;
            }

            MemorySet(EntryName, 0, sizeof(EntryName));
            MemoryCopy(EntryName, Entry->Name, NameLength);

            if (MatchPattern(EntryName, File->Pattern) == FALSE) continue;

            if (ReadInode(FileSystem, Entry->Inode, &EntryInode) == FALSE) continue;

            FillFileHeaderFromInode(File, EntryName, &EntryInode);

            return TRUE;
        }

        File->DirectoryBlockIndex++;
        File->DirectoryBlockOffset = 0;
        File->DirectoryBlockValid = FALSE;
    }

    return FALSE;
}

/************************************************************************/

DRIVER EXT2Driver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .OwnerProcess = &KernelProcess,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_FILESYSTEM,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Jango73",
    .Product = "Minimal EXT2",
    .Command = EXT2Commands};

/************************************************************************/

/**
 * @brief Reads raw sectors relative to the partition start.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Sector Sector index relative to the partition start.
 * @param Count Number of sectors to read.
 * @param Buffer Destination buffer for the read data.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL ReadSectors(LPEXT2FILESYSTEM FileSystem, U32 Sector, U32 Count, LPVOID Buffer) {
    IOCONTROL Control;

    if (FileSystem == NULL || FileSystem->Disk == NULL) return FALSE;
    if (Buffer == NULL || Count == 0) return FALSE;

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.SectorLow = FileSystem->PartitionStart + Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = Count;
    Control.Buffer = Buffer;
    Control.BufferSize = Count * SECTOR_SIZE;

    return FileSystem->Disk->Driver->Command(DF_DISK_READ, (U32)&Control) == DF_ERROR_SUCCESS;
}

/************************************************************************/

/**
 * @brief Reads a complete EXT2 block into the provided buffer.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Block Block index to read.
 * @param Buffer Destination buffer sized to hold one block.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL ReadBlock(LPEXT2FILESYSTEM FileSystem, U32 Block, LPVOID Buffer) {
    if (FileSystem == NULL) return FALSE;
    if (Buffer == NULL) return FALSE;
    if (FileSystem->SectorsPerBlock == 0) return FALSE;

    return ReadSectors(FileSystem, Block * FileSystem->SectorsPerBlock, FileSystem->SectorsPerBlock, Buffer);
}

/************************************************************************/

static BOOL WriteSectors(LPEXT2FILESYSTEM FileSystem, U32 Sector, U32 Count, LPCVOID Buffer) {
    IOCONTROL Control;

    if (FileSystem == NULL || FileSystem->Disk == NULL) return FALSE;
    if (Buffer == NULL || Count == 0) return FALSE;

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = FileSystem->Disk;
    Control.SectorLow = FileSystem->PartitionStart + Sector;
    Control.SectorHigh = 0;
    Control.NumSectors = Count;
    Control.Buffer = (LPVOID)Buffer;
    Control.BufferSize = Count * SECTOR_SIZE;

    return FileSystem->Disk->Driver->Command(DF_DISK_WRITE, (U32)&Control) == DF_ERROR_SUCCESS;
}

/************************************************************************/

static BOOL WriteBlock(LPEXT2FILESYSTEM FileSystem, U32 Block, LPCVOID Buffer) {
    if (FileSystem == NULL) return FALSE;
    if (Buffer == NULL) return FALSE;
    if (FileSystem->SectorsPerBlock == 0) return FALSE;

    return WriteSectors(FileSystem, Block * FileSystem->SectorsPerBlock, FileSystem->SectorsPerBlock, Buffer);
}

/************************************************************************/

/**
 * @brief Loads block group descriptors from disk into memory.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @return TRUE when descriptors are successfully loaded, FALSE otherwise.
 */
static BOOL LoadGroupDescriptors(LPEXT2FILESYSTEM FileSystem) {
    U32 GroupCount;
    U32 TableSize;
    U32 BlocksToRead;
    U32 Index;
    U32 StartBlock;
    U8* Buffer;

    if (FileSystem == NULL) return FALSE;
    if (FileSystem->Super.BlocksPerGroup == 0) return FALSE;

    if (FileSystem->Groups != NULL) {
        KernelHeapFree(FileSystem->Groups);
        FileSystem->Groups = NULL;
        FileSystem->GroupCount = 0;
    }

    GroupCount = (FileSystem->Super.BlocksCount + FileSystem->Super.BlocksPerGroup - 1) /
        FileSystem->Super.BlocksPerGroup;

    if (GroupCount == 0) return FALSE;

    TableSize = GroupCount * sizeof(EXT2BLOCKGROUP);
    FileSystem->Groups = (LPEXT2BLOCKGROUP)KernelHeapAlloc(TableSize);
    if (FileSystem->Groups == NULL) return FALSE;

    MemorySet(FileSystem->Groups, 0, TableSize);

    BlocksToRead = (TableSize + FileSystem->BlockSize - 1) / FileSystem->BlockSize;
    if (BlocksToRead == 0) BlocksToRead = 1;

    Buffer = (U8*)KernelHeapAlloc(BlocksToRead * FileSystem->BlockSize);
    if (Buffer == NULL) {
        KernelHeapFree(FileSystem->Groups);
        FileSystem->Groups = NULL;
        return FALSE;
    }

    MemorySet(Buffer, 0, BlocksToRead * FileSystem->BlockSize);

    StartBlock = FileSystem->Super.FirstDataBlock + 1;

    for (Index = 0; Index < BlocksToRead; Index++) {
        if (ReadBlock(FileSystem, StartBlock + Index, Buffer + (Index * FileSystem->BlockSize)) == FALSE) {
            KernelHeapFree(Buffer);
            KernelHeapFree(FileSystem->Groups);
            FileSystem->Groups = NULL;
            return FALSE;
        }
    }

    MemoryCopy(FileSystem->Groups, Buffer, TableSize);

    KernelHeapFree(Buffer);

    FileSystem->GroupCount = GroupCount;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Reads an inode from disk.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param InodeIndex Index of the inode to read.
 * @param Inode Destination buffer for the inode data.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL ReadInode(LPEXT2FILESYSTEM FileSystem, U32 InodeIndex, LPEXT2INODE Inode) {
    LPEXT2BLOCKGROUP Group;
    U32 GroupIndex;
    U32 IndexInGroup;
    U32 BlockOffset;
    U32 OffsetInBlock;
    U8* BlockBuffer;
    U32 CopySize;

    if (FileSystem == NULL || Inode == NULL) return FALSE;
    if (InodeIndex == 0) return FALSE;
    if (FileSystem->InodesPerBlock == 0) return FALSE;
    if (FileSystem->GroupCount == 0 || FileSystem->Groups == NULL) return FALSE;

    GroupIndex = (InodeIndex - 1) / FileSystem->Super.InodesPerGroup;
    if (GroupIndex >= FileSystem->GroupCount) return FALSE;

    Group = &(FileSystem->Groups[GroupIndex]);
    if (Group->InodeTable == 0) return FALSE;

    IndexInGroup = (InodeIndex - 1) % FileSystem->Super.InodesPerGroup;
    BlockOffset = IndexInGroup / FileSystem->InodesPerBlock;
    OffsetInBlock = (IndexInGroup % FileSystem->InodesPerBlock) * FileSystem->InodeSize;

    BlockBuffer = (U8*)KernelHeapAlloc(FileSystem->BlockSize);
    if (BlockBuffer == NULL) return FALSE;

    if (ReadBlock(FileSystem, Group->InodeTable + BlockOffset, BlockBuffer) == FALSE) {
        KernelHeapFree(BlockBuffer);
        return FALSE;
    }

    MemorySet(Inode, 0, sizeof(EXT2INODE));

    CopySize = FileSystem->InodeSize;
    if (CopySize > sizeof(EXT2INODE)) {
        CopySize = sizeof(EXT2INODE);
    }

    MemoryCopy(Inode, BlockBuffer + OffsetInBlock, CopySize);

    KernelHeapFree(BlockBuffer);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Retrieves the physical block number for a given inode block index.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Inode Pointer to the inode describing the file.
 * @param BlockIndex Zero-based data block index within the file.
 * @param BlockNumber Receives the resolved block number (0 if sparse).
 * @return TRUE if the block number could be determined, FALSE otherwise.
 */
static BOOL GetInodeBlockNumber(LPEXT2FILESYSTEM FileSystem, LPEXT2INODE Inode, U32 BlockIndex, U32* BlockNumber) {
    U32 EntriesPerBlock;

    if (FileSystem == NULL || Inode == NULL || BlockNumber == NULL) return FALSE;

    if (BlockIndex < EXT2_DIRECT_BLOCKS) {
        *BlockNumber = Inode->Block[BlockIndex];
        return TRUE;
    }

    if (FileSystem->BlockSize == 0) return FALSE;

    EntriesPerBlock = FileSystem->BlockSize / sizeof(U32);
    if (EntriesPerBlock == 0) return FALSE;

    BlockIndex -= EXT2_DIRECT_BLOCKS;

    if (BlockIndex < EntriesPerBlock) {
        U32 SingleBlock = Inode->Block[EXT2_DIRECT_BLOCKS];
        U32* Indirect;

        if (SingleBlock == 0) {
            *BlockNumber = 0;
            return TRUE;
        }

        Indirect = (U32*)KernelHeapAlloc(FileSystem->BlockSize);
        if (Indirect == NULL) return FALSE;

        if (ReadBlock(FileSystem, SingleBlock, Indirect) == FALSE) {
            KernelHeapFree(Indirect);
            return FALSE;
        }

        *BlockNumber = Indirect[BlockIndex];
        KernelHeapFree(Indirect);
        return TRUE;
    }

    BlockIndex -= EntriesPerBlock;

    if (Inode->Block[EXT2_DIRECT_BLOCKS + 1] == 0) {
        *BlockNumber = 0;
        return TRUE;
    }

    {
        U32* DoubleBuffer;
        U32 DoubleEntries = EntriesPerBlock;
        U32 DoubleIndex;
        U32 SingleIndex;
        U32 SingleBlock;
        U32* SingleBuffer;

        DoubleBuffer = (U32*)KernelHeapAlloc(FileSystem->BlockSize);
        if (DoubleBuffer == NULL) return FALSE;

        if (ReadBlock(FileSystem, Inode->Block[EXT2_DIRECT_BLOCKS + 1], DoubleBuffer) == FALSE) {
            KernelHeapFree(DoubleBuffer);
            return FALSE;
        }

        DoubleIndex = BlockIndex / EntriesPerBlock;
        SingleIndex = BlockIndex % EntriesPerBlock;

        if (DoubleIndex >= DoubleEntries) {
            KernelHeapFree(DoubleBuffer);
            return FALSE;
        }

        SingleBlock = DoubleBuffer[DoubleIndex];

        if (SingleBlock == 0) {
            *BlockNumber = 0;
            KernelHeapFree(DoubleBuffer);
            return TRUE;
        }

        SingleBuffer = (U32*)KernelHeapAlloc(FileSystem->BlockSize);
        if (SingleBuffer == NULL) {
            KernelHeapFree(DoubleBuffer);
            return FALSE;
        }

        if (ReadBlock(FileSystem, SingleBlock, SingleBuffer) == FALSE) {
            KernelHeapFree(SingleBuffer);
            KernelHeapFree(DoubleBuffer);
            return FALSE;
        }

        *BlockNumber = SingleBuffer[SingleIndex];
        KernelHeapFree(SingleBuffer);
        KernelHeapFree(DoubleBuffer);
        return TRUE;
    }
}

/************************************************************************/

/**
 * @brief Finds a child inode within a directory by name.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Directory Pointer to the directory inode description.
 * @param Name Name of the entry to locate.
 * @param InodeIndex Receives the inode index when found.
 * @return TRUE if the entry exists, FALSE otherwise.
 */
static BOOL FindInodeInDirectory(
    LPEXT2FILESYSTEM FileSystem, LPEXT2INODE Directory, LPCSTR Name, U32* InodeIndex) {
    U32 BlockCount;
    U32 BlockIndex;
    U32 NameLength;
    U8* BlockBuffer;
    BOOL Found;

    if (FileSystem == NULL || Directory == NULL || InodeIndex == NULL) return FALSE;
    if (STRING_EMPTY(Name)) return FALSE;

    if ((Directory->Mode & EXT2_MODE_TYPE_MASK) != EXT2_MODE_DIRECTORY) return FALSE;

    NameLength = StringLength(Name);
    BlockCount = 0;
    Found = FALSE;

    if (FileSystem->BlockSize == 0) return FALSE;

    if (Directory->Size != 0) {
        BlockCount = (Directory->Size + FileSystem->BlockSize - 1) / FileSystem->BlockSize;
    }

    BlockBuffer = (U8*)KernelHeapAlloc(FileSystem->BlockSize);
    if (BlockBuffer == NULL) return FALSE;

    for (BlockIndex = 0; BlockIndex < BlockCount && Found == FALSE; BlockIndex++) {
        U32 BlockNumber;

        if (GetInodeBlockNumber(FileSystem, Directory, BlockIndex, &BlockNumber) == FALSE) break;
        if (BlockNumber == 0) continue;

        if (ReadBlock(FileSystem, BlockNumber, BlockBuffer) == FALSE) break;

        U32 Offset = 0;
        while (Offset + sizeof(EXT2DIRECTORYENTRY) <= FileSystem->BlockSize) {
            LPEXT2DIRECTORYENTRY Entry = (LPEXT2DIRECTORYENTRY)(BlockBuffer + Offset);
            U32 EntryLength;

            EntryLength = Entry->RecordLength;
            if (EntryLength == 0) break;
            if (Offset + EntryLength > FileSystem->BlockSize) break;

            if (Entry->Inode != 0 && Entry->NameLength == NameLength && Entry->NameLength < MAX_FILE_NAME) {
                STR EntryName[MAX_FILE_NAME];

                MemorySet(EntryName, 0, sizeof(EntryName));
                MemoryCopy(EntryName, Entry->Name, Entry->NameLength);

                if (StringCompare(EntryName, Name) == 0) {
                    *InodeIndex = Entry->Inode;
                    Found = TRUE;
                    break;
                }
            }

            Offset += EntryLength;
        }
    }

    KernelHeapFree(BlockBuffer);

    return Found;
}

/************************************************************************/

/**
 * @brief Resolves a path to its inode by traversing directories.
 * @param FileSystem Pointer to the EXT2 file system instance.
 * @param Path UTF-8 path using '/' as separator.
 * @param Inode Receives the inode information on success.
 * @param InodeIndex Receives the inode index on success (may be NULL).
 * @return TRUE when the path is resolved, FALSE otherwise.
 */
static BOOL ResolvePath(
    LPEXT2FILESYSTEM FileSystem, LPCSTR Path, LPEXT2INODE Inode, U32* InodeIndex) {
    EXT2INODE CurrentInode;
    U32 CurrentIndex;
    U32 Offset;
    U32 Length;

    if (FileSystem == NULL || STRING_EMPTY(Path) || Inode == NULL || InodeIndex == NULL) return FALSE;

    if (ReadInode(FileSystem, EXT2_ROOT_INODE, &CurrentInode) == FALSE) return FALSE;
    CurrentIndex = EXT2_ROOT_INODE;

    Length = StringLength(Path);
    Offset = 0;

    while (Offset < Length) {
        STR Component[MAX_FILE_NAME];
        U32 ComponentLength;

        while (Offset < Length && Path[Offset] == PATH_SEP) {
            Offset++;
        }

        if (Offset >= Length) break;

        ComponentLength = 0;
        while ((Offset + ComponentLength) < Length && Path[Offset + ComponentLength] != PATH_SEP) {
            ComponentLength++;
        }

        if (ComponentLength == 0 || ComponentLength >= MAX_FILE_NAME) return FALSE;

        MemorySet(Component, 0, sizeof(Component));
        MemoryCopy(Component, Path + Offset, ComponentLength);

        if (FindInodeInDirectory(FileSystem, &CurrentInode, Component, &CurrentIndex) == FALSE) {
            return FALSE;
        }

        if (ReadInode(FileSystem, CurrentIndex, &CurrentInode) == FALSE) return FALSE;

        Offset += ComponentLength;
    }

    MemoryCopy(Inode, &CurrentInode, sizeof(EXT2INODE));
    if (InodeIndex != NULL) {
        *InodeIndex = CurrentIndex;
    }

    return TRUE;
}

/**
 * @brief Allocates and initializes a new EXT2 filesystem structure.
 * @param Disk Pointer to the physical disk hosting the filesystem.
 * @return Newly allocated filesystem descriptor, or NULL on failure.
 */
static LPEXT2FILESYSTEM NewEXT2FileSystem(LPPHYSICALDISK Disk) {
    LPEXT2FILESYSTEM FileSystem;

    FileSystem = (LPEXT2FILESYSTEM)KernelHeapAlloc(sizeof(EXT2FILESYSTEM));
    if (FileSystem == NULL) return NULL;

    MemorySet(FileSystem, 0, sizeof(EXT2FILESYSTEM));

    FileSystem->Header.TypeID = KOID_FILESYSTEM;
    FileSystem->Header.References = 1;
    FileSystem->Header.Next = NULL;
    FileSystem->Header.Prev = NULL;
    FileSystem->Header.Driver = &EXT2Driver;
    FileSystem->Disk = Disk;
    FileSystem->Groups = NULL;
    FileSystem->GroupCount = 0;
    FileSystem->PartitionStart = 0;
    FileSystem->PartitionSize = 0;
    FileSystem->BlockSize = EXT2_DEFAULT_BLOCK_SIZE;
    FileSystem->SectorsPerBlock = 0;
    FileSystem->InodeSize = 0;
    FileSystem->InodesPerBlock = 0;
    FileSystem->IOBuffer = NULL;

    InitMutex(&(FileSystem->Header.Mutex));
    InitMutex(&(FileSystem->FilesMutex));

    return FileSystem;
}

/************************************************************************/

/**
 * @brief Allocates a new EXT2 file handle.
 * @param FileSystem Owning EXT2 filesystem instance.
 * @return Newly allocated file handle, or NULL on failure.
 */
static LPEXT2FILE NewEXT2File(LPEXT2FILESYSTEM FileSystem) {
    LPEXT2FILE File;

    File = (LPEXT2FILE)KernelHeapAlloc(sizeof(EXT2FILE));
    if (File == NULL) return NULL;

    MemorySet(File, 0, sizeof(EXT2FILE));

    File->Header.TypeID = KOID_FILE;
    File->Header.References = 1;
    File->Header.Next = NULL;
    File->Header.Prev = NULL;
    File->Header.FileSystem = (LPFILESYSTEM)FileSystem;

    InitMutex(&(File->Header.Mutex));
    InitSecurity(&(File->Header.Security));

    File->InodeIndex = 0;
    File->DirectoryBlockIndex = 0;
    File->DirectoryBlockOffset = 0;
    File->DirectoryBlock = NULL;
    File->DirectoryBlockValid = FALSE;

    return File;
}

/**
 * @brief Initializes the EXT2 driver when it is loaded by the kernel.
 * @return DF_ERROR_SUCCESS on success.
 */
static U32 Initialize(void) { return DF_ERROR_SUCCESS; }

/************************************************************************/

/**
 * @brief Opens a file from the EXT2 filesystem.
 * @param Info File open parameters provided by the kernel.
 * @return Pointer to the opened file object, or NULL on failure.
 */
static LPEXT2FILE OpenFile(LPFILEINFO Info) {
    LPEXT2FILESYSTEM FileSystem;
    LPEXT2FILE File;
    BOOL Wildcard;

    if (Info == NULL || STRING_EMPTY(Info->Name)) return NULL;

    FileSystem = (LPEXT2FILESYSTEM)Info->FileSystem;
    if (FileSystem == NULL) return NULL;

    LockMutex(&(FileSystem->FilesMutex), INFINITY);

    if (Info->Flags & (FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_TRUNCATE)) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return NULL;
    }

    Wildcard = HasWildcard(Info->Name);

    if (Wildcard) {
        STR DirectoryPath[MAX_PATH_NAME];
        STR Pattern[MAX_FILE_NAME];
        LPSTR Slash;
        EXT2INODE DirectoryInode;
        U32 DirectoryIndex;

        StringCopy(DirectoryPath, Info->Name);
        Slash = StringFindCharR(DirectoryPath, PATH_SEP);

        if (Slash != NULL) {
            StringCopy(Pattern, Slash + 1);
            *Slash = STR_NULL;
        } else {
            DirectoryPath[0] = STR_NULL;
            StringCopy(Pattern, Info->Name);
        }

        if (LoadDirectoryInode(FileSystem, DirectoryPath, &DirectoryInode, &DirectoryIndex) == FALSE) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return NULL;
        }

        File = NewEXT2File(FileSystem);
        if (File == NULL) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return NULL;
        }

        if (SetupDirectoryHandle(File, FileSystem, &DirectoryInode, DirectoryIndex, TRUE, Pattern) == FALSE) {
            ReleaseDirectoryResources(File);
            KernelHeapFree(File);
            UnlockMutex(&(FileSystem->FilesMutex));
            return NULL;
        }

        File->Header.OpenFlags = Info->Flags;

        UnlockMutex(&(FileSystem->FilesMutex));

        return File;
    }

    {
        EXT2INODE Inode;
        U32 InodeIndex;

        if (ResolvePath(FileSystem, Info->Name, &Inode, &InodeIndex) == FALSE) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return NULL;
        }

        File = NewEXT2File(FileSystem);
        if (File == NULL) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return NULL;
        }

        MemoryCopy(&(File->Inode), &Inode, sizeof(EXT2INODE));
        File->InodeIndex = InodeIndex;

        if ((Inode.Mode & EXT2_MODE_TYPE_MASK) == EXT2_MODE_DIRECTORY) {
            if (SetupDirectoryHandle(File, FileSystem, &Inode, InodeIndex, FALSE, NULL) == FALSE) {
                ReleaseDirectoryResources(File);
                KernelHeapFree(File);
                UnlockMutex(&(FileSystem->FilesMutex));
                return NULL;
            }

            {
                STR BaseName[MAX_FILE_NAME];
                ExtractBaseName(Info->Name, BaseName);
                FillFileHeaderFromInode(File, BaseName, &Inode);
            }

            File->Header.OpenFlags = Info->Flags;

            UnlockMutex(&(FileSystem->FilesMutex));

            return File;
        }

        if ((Inode.Mode & EXT2_MODE_TYPE_MASK) != EXT2_MODE_REGULAR) {
            KernelHeapFree(File);
            UnlockMutex(&(FileSystem->FilesMutex));
            return NULL;
        }

        {
            STR BaseName[MAX_FILE_NAME];
            ExtractBaseName(Info->Name, BaseName);
            FillFileHeaderFromInode(File, BaseName, &Inode);
        }

        File->IsDirectory = FALSE;
        File->Enumerate = FALSE;
        File->Header.OpenFlags = Info->Flags;
        File->Header.SizeLow = Inode.Size;
        File->Header.SizeHigh = 0;
        File->Header.Position = (Info->Flags & FILE_OPEN_APPEND) ? Inode.Size : 0;
        File->Header.BytesTransferred = 0;

        UnlockMutex(&(FileSystem->FilesMutex));

        return File;
    }
}

/************************************************************************/

static U32 OpenNext(LPEXT2FILE File) {
    if (File == NULL) return DF_ERROR_BADPARAM;
    if (File->Header.TypeID != KOID_FILE) return DF_ERROR_BADPARAM;

    if (File->IsDirectory == FALSE) return DF_ERROR_GENERIC;
    if (File->Enumerate == FALSE) return DF_ERROR_GENERIC;

    if (LoadNextDirectoryEntry(File) == FALSE) return DF_ERROR_GENERIC;

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

/**
 * @brief Closes an EXT2 file handle and releases its memory.
 * @param File File handle to close.
 * @return DF_ERROR_SUCCESS on success, DF_ERROR_BADPARAM if the handle is invalid.
 */
static U32 CloseFile(LPEXT2FILE File) {
    if (File == NULL) return DF_ERROR_BADPARAM;
    if (File->Header.TypeID != KOID_FILE) return DF_ERROR_BADPARAM;

    if (File->IsDirectory) {
        ReleaseDirectoryResources(File);
    }

    KernelHeapFree(File);

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

/**
 * @brief Reads data from an EXT2 file into the provided buffer.
 * @param File File handle describing the read request.
 * @return DF_ERROR_SUCCESS on success or an error code on failure.
 */
static U32 ReadFile(LPEXT2FILE File) {
    LPEXT2FILESYSTEM FileSystem;
    U32 Remaining;

    if (File == NULL) return DF_ERROR_BADPARAM;
    if (File->Header.TypeID != KOID_FILE) return DF_ERROR_BADPARAM;
    if (File->Header.Buffer == NULL) return DF_ERROR_BADPARAM;

    if ((File->Header.OpenFlags & FILE_OPEN_READ) == 0) {
        return DF_ERROR_NOPERM;
    }

    if (File->IsDirectory) {
        return DF_ERROR_GENERIC;
    }

    FileSystem = (LPEXT2FILESYSTEM)File->Header.FileSystem;
    if (FileSystem == NULL) return DF_ERROR_BADPARAM;
    if (FileSystem->BlockSize == 0 || FileSystem->IOBuffer == NULL) return DF_ERROR_IO;

    LockMutex(&(FileSystem->FilesMutex), INFINITY);

    File->Header.BytesTransferred = 0;

    if (File->Header.Position >= File->Inode.Size) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return DF_ERROR_SUCCESS;
    }

    if (File->Header.ByteCount == 0) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return DF_ERROR_SUCCESS;
    }

    Remaining = File->Inode.Size - File->Header.Position;
    if (Remaining > File->Header.ByteCount) {
        Remaining = File->Header.ByteCount;
    }

    while (Remaining > 0) {
        U32 BlockIndex;
        U32 OffsetInBlock;
        U32 BlockNumber;
        U32 Chunk;

        BlockIndex = File->Header.Position / FileSystem->BlockSize;
        OffsetInBlock = File->Header.Position % FileSystem->BlockSize;

        if (GetInodeBlockNumber(FileSystem, &(File->Inode), BlockIndex, &BlockNumber) == FALSE) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_ERROR_IO;
        }

        if (BlockNumber == 0) {
            MemorySet(FileSystem->IOBuffer, 0, FileSystem->BlockSize);
        } else if (ReadBlock(FileSystem, BlockNumber, FileSystem->IOBuffer) == FALSE) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_ERROR_IO;
        }

        Chunk = FileSystem->BlockSize - OffsetInBlock;
        if (Chunk > Remaining) {
            Chunk = Remaining;
        }

        MemoryCopy(((U8*)File->Header.Buffer) + File->Header.BytesTransferred,
            FileSystem->IOBuffer + OffsetInBlock,
            Chunk);

        File->Header.Position += Chunk;
        File->Header.BytesTransferred += Chunk;
        Remaining -= Chunk;
    }

    UnlockMutex(&(FileSystem->FilesMutex));

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

/**
 * @brief Writes buffered data to an EXT2 file block by block.
 * @param File File handle describing the write request.
 * @return DF_ERROR_SUCCESS on success or an error code on failure.
 */
static U32 WriteFile(LPEXT2FILE File) {
    LPEXT2FILESYSTEM FileSystem;
    U32 Remaining;
    U32 EndPosition;

    if (File == NULL) return DF_ERROR_BADPARAM;
    if (File->Header.TypeID != KOID_FILE) return DF_ERROR_BADPARAM;
    if (File->Header.Buffer == NULL) return DF_ERROR_BADPARAM;

    if ((File->Header.OpenFlags & FILE_OPEN_WRITE) == 0) {
        return DF_ERROR_NOPERM;
    }

    if (File->IsDirectory) {
        return DF_ERROR_GENERIC;
    }

    FileSystem = (LPEXT2FILESYSTEM)File->Header.FileSystem;
    if (FileSystem == NULL) return DF_ERROR_BADPARAM;
    if (FileSystem->BlockSize == 0 || FileSystem->IOBuffer == NULL) return DF_ERROR_IO;

    LockMutex(&(FileSystem->FilesMutex), INFINITY);

    if (File->Header.OpenFlags & FILE_OPEN_APPEND) {
        File->Header.Position = File->Inode.Size;
    }

    File->Header.BytesTransferred = 0;

    if (File->Header.ByteCount == 0) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return DF_ERROR_SUCCESS;
    }

    EndPosition = File->Header.Position + File->Header.ByteCount;

    if (EndPosition > File->Inode.Size) {
        UnlockMutex(&(FileSystem->FilesMutex));
        return DF_ERROR_NOTIMPL;
    }

    Remaining = File->Header.ByteCount;

    while (Remaining > 0) {
        U32 BlockIndex;
        U32 OffsetInBlock;
        U32 BlockNumber;
        U32 Chunk;
        U8* Source;

        BlockIndex = File->Header.Position / FileSystem->BlockSize;
        OffsetInBlock = File->Header.Position % FileSystem->BlockSize;

        if (GetInodeBlockNumber(FileSystem, &(File->Inode), BlockIndex, &BlockNumber) == FALSE) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_ERROR_IO;
        }

        if (BlockNumber == 0) {
            UnlockMutex(&(FileSystem->FilesMutex));
            return DF_ERROR_NOTIMPL;
        }

        Chunk = FileSystem->BlockSize - OffsetInBlock;
        if (Chunk > Remaining) {
            Chunk = Remaining;
        }

        Source = ((U8*)File->Header.Buffer) + File->Header.BytesTransferred;

        if (Chunk != FileSystem->BlockSize || OffsetInBlock != 0) {
            if (ReadBlock(FileSystem, BlockNumber, FileSystem->IOBuffer) == FALSE) {
                UnlockMutex(&(FileSystem->FilesMutex));
                return DF_ERROR_IO;
            }

            MemoryCopy(FileSystem->IOBuffer + OffsetInBlock, Source, Chunk);

            if (WriteBlock(FileSystem, BlockNumber, FileSystem->IOBuffer) == FALSE) {
                UnlockMutex(&(FileSystem->FilesMutex));
                return DF_ERROR_IO;
            }
        } else {
            if (WriteBlock(FileSystem, BlockNumber, Source) == FALSE) {
                UnlockMutex(&(FileSystem->FilesMutex));
                return DF_ERROR_IO;
            }
        }

        File->Header.Position += Chunk;
        File->Header.BytesTransferred += Chunk;
        Remaining -= Chunk;
    }

    File->Header.SizeLow = File->Inode.Size;

    UnlockMutex(&(FileSystem->FilesMutex));

    return DF_ERROR_SUCCESS;
}

/************************************************************************/

/**
 * @brief Mounts an EXT2 partition and registers it with the kernel.
 * @param Disk Physical disk hosting the partition.
 * @param Partition Partition descriptor provided by the kernel.
 * @param Base Base LBA of the containing disk extent.
 * @param PartIndex Index of the partition on the disk.
 * @return TRUE on success, FALSE if the partition could not be mounted.
 */
BOOL MountPartition_EXT2(LPPHYSICALDISK Disk, LPBOOTPARTITION Partition, U32 Base, U32 PartIndex) {
    U8 Buffer[SECTOR_SIZE * 2];
    IOCONTROL Control;
    LPEXT2SUPER Super;
    LPEXT2FILESYSTEM FileSystem;
    U32 Result;
    SECTOR PartitionStart;

    if (Disk == NULL || Partition == NULL) return FALSE;

    PartitionStart = Base + Partition->LBA;

    Control.TypeID = KOID_IOCONTROL;
    Control.Disk = Disk;
    Control.SectorLow = PartitionStart + 2;
    Control.SectorHigh = 0;
    Control.NumSectors = 2;
    Control.Buffer = (LPVOID)Buffer;
    Control.BufferSize = sizeof(Buffer);

    Result = Disk->Driver->Command(DF_DISK_READ, (U32)&Control);

    if (Result != DF_ERROR_SUCCESS) return FALSE;

    Super = (LPEXT2SUPER)Buffer;

    if (Super->Magic != EXT2_SUPER_MAGIC) {
        DEBUG(TEXT("[MountPartition_EXT2] Invalid superblock magic: %04X"), Super->Magic);
        return FALSE;
    }

    FileSystem = NewEXT2FileSystem(Disk);
    if (FileSystem == NULL) return FALSE;

    MemoryCopy(&(FileSystem->Super), Super, sizeof(EXT2SUPER));

    FileSystem->PartitionStart = PartitionStart;
    FileSystem->PartitionSize = Partition->Size;
    FileSystem->BlockSize = EXT2_DEFAULT_BLOCK_SIZE;

    if (Super->LogBlockSize <= 4) {
        FileSystem->BlockSize = EXT2_DEFAULT_BLOCK_SIZE << Super->LogBlockSize;
    }

    FileSystem->SectorsPerBlock = FileSystem->BlockSize / SECTOR_SIZE;
    if (FileSystem->SectorsPerBlock == 0) {
        KernelHeapFree(FileSystem);
        return FALSE;
    }

    FileSystem->InodeSize = Super->InodeSize;
    if (FileSystem->InodeSize == 0) {
        FileSystem->InodeSize = sizeof(EXT2INODE);
    }

    FileSystem->InodesPerBlock = FileSystem->BlockSize / FileSystem->InodeSize;
    if (FileSystem->InodesPerBlock == 0) {
        KernelHeapFree(FileSystem);
        return FALSE;
    }

    if (LoadGroupDescriptors(FileSystem) == FALSE) {
        KernelHeapFree(FileSystem);
        return FALSE;
    }

    FileSystem->IOBuffer = (U8*)KernelHeapAlloc(FileSystem->BlockSize);
    if (FileSystem->IOBuffer == NULL) {
        KernelHeapFree(FileSystem->Groups);
        KernelHeapFree(FileSystem);
        return FALSE;
    }

    GetDefaultFileSystemName(FileSystem->Header.Name, Disk, PartIndex);

    ListAddItem(Kernel.FileSystem, FileSystem);

    DEBUG(TEXT("[MountPartition_EXT2] Mounted EXT2 volume %s (block size %u)"),
        FileSystem->Header.Name, FileSystem->BlockSize);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Dispatches EXT2 driver commands requested by the kernel.
 * @param Function Identifier of the requested operation.
 * @param Parameter Optional parameter pointer or value.
 * @return Driver-specific result or error code.
 */
U32 EXT2Commands(U32 Function, U32 Parameter) {
    switch (Function) {
        case DF_LOAD:
            return Initialize();
        case DF_GETVERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_FS_OPENFILE:
            return (U32)OpenFile((LPFILEINFO)Parameter);
        case DF_FS_OPENNEXT:
            return OpenNext((LPEXT2FILE)Parameter);
        case DF_FS_CLOSEFILE:
            return CloseFile((LPEXT2FILE)Parameter);
        case DF_FS_READ:
            return ReadFile((LPEXT2FILE)Parameter);
        case DF_FS_WRITE:
            return WriteFile((LPEXT2FILE)Parameter);
        default:
            break;
    }

    return DF_ERROR_NOTIMPL;
}
