
/************************************************************************\

    EXOS Bootloader
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


    VBR Payload ext2 load

\************************************************************************/

#include "arch/i386/I386.h"
#include "String.h"
#include "../include/SegOfs.h"

/************************************************************************/

#define EXT2_SUPER_MAGIC 0xEF53

/************************************************************************/

typedef struct __attribute__((packed)) tag_EXT2_SUPERBLOCK {
    U32 InodesCount;
    U32 BlocksCount;
    U32 ReservedBlocksCount;
    U32 FreeBlocksCount;
    U32 FreeInodesCount;
    U32 FirstDataBlock;
    U32 LogBlockSize;
    U32 LogFragmentSize;
    U32 BlocksPerGroup;
    U32 FragmentsPerGroup;
    U32 InodesPerGroup;
    U32 Mtime;
    U32 Wtime;
    U16 MountCount;
    U16 MaxMountCount;
    U16 Magic;
    U16 State;
    U16 Errors;
    U16 MinorRevLevel;
    U32 Lastcheck;
    U32 Checkinterval;
    U32 CreatorOs;
    U32 RevLevel;
    U16 DefResuid;
    U16 DefResgid;
    U32 FirstInode;
    U16 InodeSize;
    U16 BlockGroupNumber;
} EXT2_SUPERBLOCK;

typedef struct __attribute__((packed)) tag_EXT2_GROUP_DESC {
    U32 BlockBitmap;
    U32 InodeBitmap;
    U32 InodeTable;
    U16 FreeBlocksCount;
    U16 FreeInodesCount;
    U16 UsedDirsCount;
    U16 Pad;
    U32 Reserved[3];
} EXT2_GROUP_DESC;

typedef struct __attribute__((packed)) tag_EXT2_INODE {
    U16 Mode;
    U16 Uid;
    U32 SizeLow;
    U32 Atime;
    U32 Ctime;
    U32 Mtime;
    U32 Dtime;
    U16 Gid;
    U16 LinksCount;
    U32 Blocks;
    U32 Flags;
    U32 Osd1;
    U32 Block[15];
    U32 Generation;
    U32 FileAcl;
    U32 DirAcl;
    U32 Faddr;
    U32 Osd2[3];
} EXT2_INODE;

typedef struct __attribute__((packed)) tag_EXT2_DIR_ENTRY {
    U32 Inode;
    U16 RecLen;
    U8 NameLen;
    U8 FileType;
} EXT2_DIR_ENTRY;

/************************************************************************/

typedef struct tag_EXT2_CONTEXT {
    U32 BootDrive;
    U32 PartitionLba;
    U32 BlockSize;
    U32 SectorsPerBlock;
    U32 InodeSize;
    U32 InodesPerGroup;
    U32 BlocksPerGroup;
    U32 FirstDataBlock;
    U32 BgdtBlock;
    U32 EntriesPerBlock;
} EXT2_CONTEXT;

/************************************************************************/

enum {
    EXT2_DIRECT_BLOCK_COUNT = 12,
    EXT2_SINGLE_INDIRECT_INDEX = EXT2_DIRECT_BLOCK_COUNT,
    EXT2_DOUBLE_INDIRECT_INDEX = EXT2_SINGLE_INDIRECT_INDEX + 1,
    EXT2_TRIPLE_INDIRECT_INDEX = EXT2_DOUBLE_INDIRECT_INDEX + 1,
    EXT2_MAX_BLOCK_SIZE = 4096U,
    EXT2_MAX_POINTERS_PER_BLOCK = EXT2_MAX_BLOCK_SIZE / sizeof(U32)
};

/************************************************************************/

static U8* const Ext2Scratch = (U8*)(USABLE_RAM_START);
static U32 Ext2PointerScratch[3][EXT2_MAX_POINTERS_PER_BLOCK];

/************************************************************************/

static U32 Ext2StringLength(const char* Str) {
    U32 Len = 0;
    while (Str[Len] != '\0') {
        ++Len;
    }
    return Len;
}

/************************************************************************/

static BOOL Ext2NamesEqual(const U8* Name, U8 NameLen, const char* Target, U32 TargetLen) {
    if (NameLen != TargetLen) return FALSE;
    for (U32 Index = 0; Index < TargetLen; ++Index) {
        if ((char)Name[Index] != Target[Index]) return FALSE;
    }
    return TRUE;
}

/************************************************************************/

static void Ext2ReadBlock(const EXT2_CONTEXT* Ctx, U32 BlockNumber, U32 DestFar) {
    U32 Lba = Ctx->PartitionLba + BlockNumber * Ctx->SectorsPerBlock;
    if (BiosReadSectors(Ctx->BootDrive, Lba, Ctx->SectorsPerBlock, DestFar)) {
        BootErrorPrint(TEXT("[VBR] EXT2 block read failed. Halting.\r\n"));
        Hang();
    }
}

/************************************************************************/

static void Ext2LoadGroupDescriptor(const EXT2_CONTEXT* Ctx, U32 Group, EXT2_GROUP_DESC* Out) {
    U32 DescriptorsPerBlock = Ctx->BlockSize / sizeof(EXT2_GROUP_DESC);
    U32 Block = Ctx->BgdtBlock + (Group / DescriptorsPerBlock);
    U32 Index = Group % DescriptorsPerBlock;

    Ext2ReadBlock(Ctx, Block, MakeSegOfs(Ext2Scratch));
    MemoryCopy(Out, Ext2Scratch + Index * sizeof(EXT2_GROUP_DESC), sizeof(EXT2_GROUP_DESC));
}

/************************************************************************/

static void Ext2ReadInode(const EXT2_CONTEXT* Ctx, U32 InodeNumber, EXT2_INODE* Out) {
    if (InodeNumber == 0) {
        BootErrorPrint(TEXT("[VBR] EXT2 invalid inode number. Halting.\r\n"));
        Hang();
    }

    U32 Index = InodeNumber - 1U;
    U32 Group = Index / Ctx->InodesPerGroup;
    U32 IndexInGroup = Index % Ctx->InodesPerGroup;

    EXT2_GROUP_DESC GroupDesc;
    Ext2LoadGroupDescriptor(Ctx, Group, &GroupDesc);

    U32 InodeTableBlock = GroupDesc.InodeTable;
    U32 ByteOffset = IndexInGroup * Ctx->InodeSize;
    U32 BlockOffset = ByteOffset / Ctx->BlockSize;
    U32 OffsetWithinBlock = ByteOffset % Ctx->BlockSize;

    Ext2ReadBlock(Ctx, InodeTableBlock + BlockOffset, MakeSegOfs(Ext2Scratch));

    MemorySet(Out, 0, sizeof(EXT2_INODE));
    U32 CopySize = Ctx->InodeSize;
    if (CopySize > sizeof(EXT2_INODE)) {
        CopySize = sizeof(EXT2_INODE);
    }
    MemoryCopy(Out, Ext2Scratch + OffsetWithinBlock, CopySize);
}

/************************************************************************/

typedef struct tag_EXT2_DIR_SEARCH {
    const char* Name;
    U32 TargetLen;
    U32 ResultInode;
} EXT2_DIR_SEARCH;

/************************************************************************/

typedef struct tag_EXT2_LOAD_STATE {
    U16 DestSeg;
    U16 DestOfs;
    U32 Remaining;
} EXT2_LOAD_STATE;

/************************************************************************/

static void Ext2ScanDirectoryBlock(const EXT2_CONTEXT* Ctx, U32 BlockNumber, EXT2_DIR_SEARCH* Search) {
    if (BlockNumber == 0 || Search->ResultInode != 0) {
        return;
    }

    Ext2ReadBlock(Ctx, BlockNumber, MakeSegOfs(Ext2Scratch));

    U32 Offset = 0;
    while (Offset + sizeof(EXT2_DIR_ENTRY) <= Ctx->BlockSize) {
        EXT2_DIR_ENTRY* Entry = (EXT2_DIR_ENTRY*)(Ext2Scratch + Offset);
        if (Entry->RecLen == 0 || Entry->RecLen < sizeof(EXT2_DIR_ENTRY)) {
            break;
        }

        if (Entry->Inode != 0U) {
            U32 NameAvail = Entry->RecLen - (U32)sizeof(EXT2_DIR_ENTRY);
            if (Entry->NameLen <= NameAvail) {
                U8* EntryName = Ext2Scratch + Offset + sizeof(EXT2_DIR_ENTRY);
                if (Ext2NamesEqual(EntryName, Entry->NameLen, Search->Name, Search->TargetLen)) {
                    Search->ResultInode = Entry->Inode;
                    return;
                }
            }
        }

        Offset += Entry->RecLen;
        if (Offset >= Ctx->BlockSize) {
            break;
        }
    }
}

/************************************************************************/

typedef BOOL (*EXT2_BLOCK_VISITOR)(const EXT2_CONTEXT* Ctx, U32 BlockNumber, void* UserData);

/************************************************************************/

static BOOL Ext2VisitIndirectBlocks(
    const EXT2_CONTEXT* Ctx, U32 BlockNumber, U32 Level, EXT2_BLOCK_VISITOR Visitor, void* UserData) {
    if (BlockNumber == 0U) {
        return TRUE;
    }

    Ext2ReadBlock(Ctx, BlockNumber, MakeSegOfs(Ext2Scratch));
    U32 EntryCount = Ctx->EntriesPerBlock;
    if (EntryCount > EXT2_MAX_POINTERS_PER_BLOCK) {
        EntryCount = EXT2_MAX_POINTERS_PER_BLOCK;
    }

    U32 LevelIndex = (Level == 0U) ? 0U : (Level - 1U);
    if (LevelIndex >= 3U) {
        LevelIndex = 2U;
    }

    MemoryCopy(Ext2PointerScratch[LevelIndex], Ext2Scratch, EntryCount * sizeof(U32));
    U32* Entries = Ext2PointerScratch[LevelIndex];

    for (U32 Index = 0; Index < EntryCount; ++Index) {
        U32 Child = Entries[Index];
        if (Child == 0U) {
            continue;
        }

        BOOL Continue = FALSE;
        if (Level == 1U) {
            Continue = Visitor(Ctx, Child, UserData);
        } else {
            Continue = Ext2VisitIndirectBlocks(Ctx, Child, Level - 1U, Visitor, UserData);
        }

        if (!Continue) {
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

static BOOL Ext2VisitInodeBlocks(
    const EXT2_CONTEXT* Ctx, const EXT2_INODE* Inode, EXT2_BLOCK_VISITOR Visitor, void* UserData) {
    for (U32 Index = 0; Index < EXT2_DIRECT_BLOCK_COUNT; ++Index) {
        U32 BlockNumber = Inode->Block[Index];
        if (BlockNumber == 0U) {
            continue;
        }

        if (!Visitor(Ctx, BlockNumber, UserData)) {
            return FALSE;
        }
    }

    if (!Ext2VisitIndirectBlocks(Ctx, Inode->Block[EXT2_SINGLE_INDIRECT_INDEX], 1U, Visitor, UserData)) {
        return FALSE;
    }

    if (!Ext2VisitIndirectBlocks(Ctx, Inode->Block[EXT2_DOUBLE_INDIRECT_INDEX], 2U, Visitor, UserData)) {
        return FALSE;
    }

    if (!Ext2VisitIndirectBlocks(Ctx, Inode->Block[EXT2_TRIPLE_INDIRECT_INDEX], 3U, Visitor, UserData)) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

static BOOL Ext2DirectoryVisitor(const EXT2_CONTEXT* Ctx, U32 BlockNumber, void* UserData) {
    EXT2_DIR_SEARCH* Search = (EXT2_DIR_SEARCH*)UserData;
    Ext2ScanDirectoryBlock(Ctx, BlockNumber, Search);
    return (Search->ResultInode == 0U);
}

/************************************************************************/

static U32 Ext2FindInDirectory(const EXT2_CONTEXT* Ctx, const EXT2_INODE* Dir, const char* Name) {
    EXT2_DIR_SEARCH Search;
    Search.Name = Name;
    Search.TargetLen = Ext2StringLength(Name);
    Search.ResultInode = 0U;

    Ext2VisitInodeBlocks(Ctx, Dir, Ext2DirectoryVisitor, &Search);

    return Search.ResultInode;
}

/************************************************************************/

static void Ext2AdvanceDestination(const EXT2_CONTEXT* Ctx, U16* DestSeg, U16* DestOfs) {
    U32 AdvanceBytes = Ctx->BlockSize;
    U16 Low = (U16)(AdvanceBytes & 0xF);
    U16 NewOfs = (U16)(*DestOfs + Low);
    U16 Carry = (NewOfs < *DestOfs) ? 1U : 0U;
    *DestSeg += (U16)(AdvanceBytes >> 4) + Carry;
    *DestOfs = NewOfs;
}

/************************************************************************/

static void Ext2LoadBlockToDestination(
    const EXT2_CONTEXT* Ctx, U32 BlockNumber, U16* DestSeg, U16* DestOfs, U32* Remaining) {
    if (BlockNumber == 0 || *Remaining == 0) return;

    Ext2ReadBlock(Ctx, BlockNumber, PackSegOfs(*DestSeg, *DestOfs));
    Ext2AdvanceDestination(Ctx, DestSeg, DestOfs);

    if (*Remaining <= Ctx->BlockSize) {
        *Remaining = 0;
    } else {
        *Remaining -= Ctx->BlockSize;
    }
}

/************************************************************************/

static BOOL Ext2FileLoadVisitor(const EXT2_CONTEXT* Ctx, U32 BlockNumber, void* UserData) {
    EXT2_LOAD_STATE* State = (EXT2_LOAD_STATE*)UserData;
    if (State->Remaining == 0U) {
        return FALSE;
    }

    Ext2LoadBlockToDestination(Ctx, BlockNumber, &State->DestSeg, &State->DestOfs, &State->Remaining);
    return (State->Remaining > 0U);
}

/************************************************************************/

BOOL LoadKernelExt2(U32 BootDrive, U32 PartitionLba, const char* KernelName, U32* FileSizeOut) {
    BootDebugPrint(TEXT("[VBR] Probing EXT2 filesystem\r\n"));

    if (BiosReadSectors(BootDrive, PartitionLba + 2U, 2U, MakeSegOfs(Ext2Scratch))) {
        BootErrorPrint(TEXT("[VBR] EXT2 superblock read failed. Halting.\r\n"));
        Hang();
    }

    EXT2_SUPERBLOCK* Super = (EXT2_SUPERBLOCK*)Ext2Scratch;
    if (Super->Magic != EXT2_SUPER_MAGIC) {
        return FALSE;
    }

    EXT2_CONTEXT Ctx;
    Ctx.BootDrive = BootDrive;
    Ctx.PartitionLba = PartitionLba;
    Ctx.BlockSize = 1024U << Super->LogBlockSize;
    if (Ctx.BlockSize == 0U || Ctx.BlockSize > EXT2_MAX_BLOCK_SIZE) {
        BootErrorPrint(TEXT("[VBR] EXT2 block size unsupported. Halting.\r\n"));
        Hang();
    }

    Ctx.SectorsPerBlock = Ctx.BlockSize / SECTORSIZE;
    if (Ctx.SectorsPerBlock == 0) {
        BootErrorPrint(TEXT("[VBR] EXT2 block size invalid. Halting.\r\n"));
        Hang();
    }
    Ctx.InodeSize = (Super->InodeSize != 0U) ? Super->InodeSize : 128U;
    Ctx.InodesPerGroup = Super->InodesPerGroup;
    Ctx.BlocksPerGroup = Super->BlocksPerGroup;
    Ctx.FirstDataBlock = Super->FirstDataBlock;
    Ctx.BgdtBlock = Super->FirstDataBlock + 1U;
    Ctx.EntriesPerBlock = Ctx.BlockSize / sizeof(U32);
    if (Ctx.EntriesPerBlock == 0U || Ctx.EntriesPerBlock > EXT2_MAX_POINTERS_PER_BLOCK) {
        BootErrorPrint(TEXT("[VBR] EXT2 pointer block unsupported. Halting.\r\n"));
        Hang();
    }

    EXT2_INODE RootInode;
    Ext2ReadInode(&Ctx, 2U, &RootInode);

    U32 KernelInodeNumber = Ext2FindInDirectory(&Ctx, &RootInode, KernelName);
    if (KernelInodeNumber == 0) {
        STR Message[128];
        StringPrintFormat(Message, TEXT("[VBR] Kernel %s not found on EXT2 volume.\r\n"), KernelName);
        BootErrorPrint(Message);
        Hang();
    }

    EXT2_INODE KernelInode;
    Ext2ReadInode(&Ctx, KernelInodeNumber, &KernelInode);

    U32 FileSize = KernelInode.SizeLow;
    StringPrintFormat(TempString, TEXT("[VBR] EXT2 kernel size %08X bytes\r\n"), FileSize);
    BootDebugPrint(TempString);

    EXT2_LOAD_STATE LoadState;
    LoadState.DestSeg = LOADADDRESS_SEG;
    LoadState.DestOfs = LOADADDRESS_OFS;
    LoadState.Remaining = FileSize;

    Ext2VisitInodeBlocks(&Ctx, &KernelInode, Ext2FileLoadVisitor, &LoadState);

    if (LoadState.Remaining > 0) {
        BootErrorPrint(TEXT("[VBR] EXT2 kernel load incomplete. Halting.\r\n"));
        Hang();
    }

    if (FileSizeOut != NULL) {
        *FileSizeOut = FileSize;
    }

    return TRUE;
}
