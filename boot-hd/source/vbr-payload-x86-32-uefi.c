
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


    x86-32 specific portion of the VBR payload

\************************************************************************/

#include "../include/vbr-payload-shared.h"
#include "boot-reservation.h"
#include "arch/x86-32/x86-32-Memory.h"
#include "arch/x86-32/x86-32.h"

/************************************************************************/

#define PROTECTED_ZONE_START 0xC0000
#define PROTECTED_ZONE_END 0xFFFFF

/************************************************************************/

static U32 GdtPhysicalAddress = LOW_MEMORY_PAGE_4;
static LPPAGE_DIRECTORY PageDirectory = (LPPAGE_DIRECTORY)LOW_MEMORY_PAGE_1;
static LPPAGE_TABLE PageTableLow = (LPPAGE_TABLE)LOW_MEMORY_PAGE_2;
static LPPAGE_TABLE PageTableKrn = (LPPAGE_TABLE)LOW_MEMORY_PAGE_3;
static SEGMENT_DESCRIPTOR GdtEntries[3];
static GDT_REGISTER Gdtr;

/************************************************************************/

static void SetSegmentDescriptorX86_32(
    LPSEGMENT_DESCRIPTOR Descriptor,
    U32 Base,
    U32 Limit,
    BOOL Executable,
    BOOL Writable,
    U32 Privilege,
    BOOL Operand32,
    BOOL Granularity) {
    Descriptor->Limit_00_15 = (U16)(Limit & 0xFFFFu);
    Descriptor->Base_00_15 = (U16)(Base & 0xFFFFu);
    Descriptor->Base_16_23 = (U8)((Base >> 16) & 0xFFu);
    Descriptor->Accessed = 0;
    Descriptor->CanWrite = (Writable ? 1U : 0U);
    Descriptor->ConformExpand = 0;
    Descriptor->Type = (Executable ? 1U : 0U);
    Descriptor->Segment = 1;
    Descriptor->Privilege = (Privilege & 3U);
    Descriptor->Present = 1;
    Descriptor->Limit_16_19 = (U32)((Limit >> 16) & 0x0Fu);
    Descriptor->Available = 0;
    Descriptor->Unused = 0;
    Descriptor->OperandSize = (Operand32 ? 1U : 0U);
    Descriptor->Granularity = (Granularity ? 1U : 0U);
    Descriptor->Base_24_31 = (U8)((Base >> 24) & 0xFFu);
}

/************************************************************************/

static void ClearPdPt(void) {
    MemorySet(PageDirectory, 0, PAGE_TABLE_SIZE);
    MemorySet(PageTableLow, 0, PAGE_TABLE_SIZE);
    MemorySet(PageTableKrn, 0, PAGE_TABLE_SIZE);
}

/************************************************************************/

static void SetPageDirectoryEntry(LPPAGE_DIRECTORY Entry, U32 PageTablePhysical) {
    Entry->Present = 1;
    Entry->ReadWrite = 1;
    Entry->Privilege = 0;
    Entry->WriteThrough = 0;
    Entry->CacheDisabled = 0;
    Entry->Accessed = 0;
    Entry->Reserved = 0;
    Entry->PageSize = 0;
    Entry->Global = 0;
    Entry->User = 0;
    Entry->Fixed = 1;
    Entry->Address = (PageTablePhysical >> 12);
}

/************************************************************************/

static void SetPageTableEntry(LPPAGE_TABLE Entry, U32 Physical) {
    BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);

    Entry->Present = !Protected;
    Entry->ReadWrite = 1;
    Entry->Privilege = 0;
    Entry->WriteThrough = 0;
    Entry->CacheDisabled = 0;
    Entry->Accessed = 0;
    Entry->Dirty = 0;
    Entry->Reserved = 0;
    Entry->Global = 0;
    Entry->User = 0;
    Entry->Fixed = 1;
    Entry->Address = (Physical >> 12);
}

/************************************************************************/

static void BuildPaging(U32 KernelPhysBase, U32 KernelVirtBase, U32 MapSize) {
    ClearPdPt();

    for (U32 Index = 0; Index < 1024; ++Index) {
        SetPageTableEntry(PageTableLow + Index, Index * PAGE_SIZE);
    }

    SetPageDirectoryEntry(PageDirectory + 0, (U32)PageTableLow);

    const U32 KernelDirectoryIndex = (KernelVirtBase >> 22) & 0x3FFU;
    SetPageDirectoryEntry(PageDirectory + KernelDirectoryIndex, (U32)PageTableKrn);

    const U32 NumPages = (MapSize + PAGE_SIZE - 1U) >> MUL_4KB;
    for (U32 Index = 0; Index < NumPages && Index < 1024U; ++Index) {
        SetPageTableEntry(PageTableKrn + Index, KernelPhysBase + (Index << MUL_4KB));
    }

    SetPageDirectoryEntry(PageDirectory + 1023, (U32)PageDirectory);
}

/************************************************************************/

static void BuildGdtFlat(void) {
    MemorySet(GdtEntries, 0, sizeof(GdtEntries));

    SetSegmentDescriptorX86_32(&GdtEntries[1], 0x00000000u, 0x000FFFFFu, TRUE, TRUE, 0, TRUE, TRUE);
    SetSegmentDescriptorX86_32(&GdtEntries[2], 0x00000000u, 0x000FFFFFu, FALSE, TRUE, 0, TRUE, TRUE);

    MemoryCopy((void*)GdtPhysicalAddress, GdtEntries, sizeof(GdtEntries));

    Gdtr.Limit = (U16)(sizeof(GdtEntries) - 1U);
    Gdtr.Base = GdtPhysicalAddress;
}

/************************************************************************/

void NORETURN EnterProtectedPagingAndJump(U32 FileSize, U32 MultibootInfoPtr, U64 UefiImageBase, U64 UefiImageSize) {
    const U32 KernelPhysBase = KERNEL_LINEAR_LOAD_ADDRESS;
    const U32 MapSize = PAGE_ALIGN(FileSize + BOOT_KERNEL_MAP_PADDING_BYTES);

    UNUSED(UefiImageBase);
    UNUSED(UefiImageSize);

    const U32 KernelVirtBase = (U32)CONFIG_VMA_KERNEL;
    BuildPaging(KernelPhysBase, KernelVirtBase, MapSize);
    BuildGdtFlat();

    const U32 KernelEntryLo = KernelVirtBase;
    const U32 KernelEntryHi = 0U;
    const U32 PagingStructure = (U32)(UINT)PageDirectory;

    for (volatile int Delay = 0; Delay < 100000; ++Delay) {
        __asm__ __volatile__("nop");
    }

    StubJumpToImage((U32)(&Gdtr), PagingStructure, KernelEntryLo, KernelEntryHi, MultibootInfoPtr, MULTIBOOT_BOOTLOADER_MAGIC);

    __builtin_unreachable();
}
