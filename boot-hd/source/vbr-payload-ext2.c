
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

#include "arch/i386/i386.h"
#include "String.h"
#include "../include/vbr-realmode-utils.h"

/************************************************************************/

#define EXT2_SUPER_MAGIC 0xEF53
#define EXT2_DIRECT_BLOCK_COUNT 12U
#define EXT2_SINGLE_INDIRECT_BLOCK_INDEX 12U
#define EXT2_DOUBLE_INDIRECT_BLOCK_INDEX 13U
#define EXT2_TRIPLE_INDIRECT_BLOCK_INDEX 14U

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

static U8* const Ext2Scratch = (U8*)(USABLE_RAM_START);

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

static U32 Ext2FindInDirectory(const EXT2_CONTEXT* Ctx, const EXT2_INODE* Dir, const char* Name) {
    U32 TargetLen = Ext2StringLength(Name);

    for (U32 i = 0; i < EXT2_DIRECT_BLOCK_COUNT; ++i) {
        U32 Block = Dir->Block[i];
        if (Block == 0) continue;

        Ext2ReadBlock(Ctx, Block, MakeSegOfs(Ext2Scratch));

        U32 Offset = 0;
        while (Offset + sizeof(EXT2_DIR_ENTRY) <= Ctx->BlockSize) {
            EXT2_DIR_ENTRY* Entry = (EXT2_DIR_ENTRY*)(Ext2Scratch + Offset);
            if (Entry->RecLen == 0) break;
            if (Entry->RecLen < sizeof(EXT2_DIR_ENTRY)) break;

            if (Entry->Inode != 0) {
                U32 NameAvail = Entry->RecLen - (U32)sizeof(EXT2_DIR_ENTRY);
                if (Entry->NameLen <= NameAvail) {
                    U8* EntryName = Ext2Scratch + Offset + sizeof(EXT2_DIR_ENTRY);
                    if (Ext2NamesEqual(EntryName, Entry->NameLen, Name, TargetLen)) {
                        return Entry->Inode;
                    }
                }
            }

            Offset += Entry->RecLen;
            if (Offset >= Ctx->BlockSize) break;
        }
    }

    return 0;
}

/************************************************************************/

static void Ext2LoadBlockToDestination(
    const EXT2_CONTEXT* Ctx,
    U32 BlockNumber,
    KERNEL_BUFFER_REQUEST BufferRequest,
    void* BufferContext,
    U32* Remaining) {
    if (BlockNumber == 0 || *Remaining == 0) return;

    U32 DestFar = BufferRequest(BufferContext, Ctx->BlockSize);
    Ext2ReadBlock(Ctx, BlockNumber, DestFar);

    if (*Remaining <= Ctx->BlockSize) {
        *Remaining = 0;
    } else {
        *Remaining -= Ctx->BlockSize;
    }
}

/************************************************************************/

static void Ext2LoadIndirect(
    const EXT2_CONTEXT* Ctx,
    U32 BlockNumber,
    U32 Level,
    KERNEL_BUFFER_REQUEST BufferRequest,
    void* BufferContext,
    U32* Remaining) {
    if (BlockNumber == 0 || *Remaining == 0) return;

    Ext2ReadBlock(Ctx, BlockNumber, MakeSegOfs(Ext2Scratch));
    U32* Entries = (U32*)Ext2Scratch;

    for (U32 Index = 0; Index < Ctx->EntriesPerBlock && *Remaining > 0; ++Index) {
        U32 Child = Entries[Index];
        if (Child == 0) continue;

        if (Level == 1) {
            Ext2LoadBlockToDestination(Ctx, Child, BufferRequest, BufferContext, Remaining);
        } else {
            Ext2LoadIndirect(Ctx, Child, Level - 1, BufferRequest, BufferContext, Remaining);
        }
    }
}

/************************************************************************/

BOOL LoadKernelExt2(
    U32 BootDrive,
    U32 PartitionLba,
    const char* KernelName,
    KERNEL_BUFFER_REQUEST BufferRequest,
    void* BufferContext,
    U32* FileSizeOut) {
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

    if (BufferRequest == NULL) {
        BootErrorPrint(TEXT("[VBR] Missing kernel buffer callback. Halting.\r\n"));
        Hang();
    }

    U32 Remaining = FileSize;

    for (U32 i = 0; i < EXT2_DIRECT_BLOCK_COUNT && Remaining > 0; ++i) {
        Ext2LoadBlockToDestination(&Ctx, KernelInode.Block[i], BufferRequest, BufferContext, &Remaining);
    }

    if (Remaining > 0 && KernelInode.Block[EXT2_SINGLE_INDIRECT_BLOCK_INDEX] != 0) {
        Ext2LoadIndirect(
            &Ctx,
            KernelInode.Block[EXT2_SINGLE_INDIRECT_BLOCK_INDEX],
            1,
            BufferRequest,
            BufferContext,
            &Remaining);
    }

    if (Remaining > 0 && KernelInode.Block[EXT2_DOUBLE_INDIRECT_BLOCK_INDEX] != 0) {
        Ext2LoadIndirect(
            &Ctx,
            KernelInode.Block[EXT2_DOUBLE_INDIRECT_BLOCK_INDEX],
            2,
            BufferRequest,
            BufferContext,
            &Remaining);
    }

    if (Remaining > 0 && KernelInode.Block[EXT2_TRIPLE_INDIRECT_BLOCK_INDEX] != 0) {
        Ext2LoadIndirect(
            &Ctx,
            KernelInode.Block[EXT2_TRIPLE_INDIRECT_BLOCK_INDEX],
            3,
            BufferRequest,
            BufferContext,
            &Remaining);
    }

    if (Remaining > 0) {
        BootErrorPrint(TEXT("[VBR] EXT2 kernel load incomplete. Halting.\r\n"));
        Hang();
    }

    if (FileSizeOut != NULL) {
        *FileSizeOut = FileSize;
    }

    return TRUE;
}
