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


    i386 specific portion of the VBR payload

\************************************************************************/

#include "../../kernel/include/arch/i386/i386-Memory.h"
#include "../../kernel/include/arch/i386/i386.h"
#include "../include/vbr-payload-shared.h"

/************************************************************************/

#define PAGE_DIRECTORY_ADDRESS LOW_MEMORY_PAGE_1
#define PAGE_TABLE_LOW_ADDRESS LOW_MEMORY_PAGE_2
#define PAGE_TABLE_KERNEL_ADDRESS LOW_MEMORY_PAGE_3

#define PROTECTED_ZONE_START 0xC0000
#define PROTECTED_ZONE_END 0xFFFFF

/************************************************************************/

static U32 GdtPhysicalAddress = LOW_MEMORY_PAGE_4;
static LPPAGE_DIRECTORY PageDirectory = (LPPAGE_DIRECTORY)PAGE_DIRECTORY_ADDRESS;
static LPPAGE_TABLE PageTableLow = (LPPAGE_TABLE)PAGE_TABLE_LOW_ADDRESS;
static LPPAGE_TABLE PageTableKrn = (LPPAGE_TABLE)PAGE_TABLE_KERNEL_ADDRESS;
static SEGMENT_DESCRIPTOR GdtEntries[3];
static GDT_REGISTER Gdtr;

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
    BootDebugPrint(TEXT("[VBR] BuildGdtFlat\r\n"));

    MemorySet(GdtEntries, 0, sizeof(GdtEntries));

    VbrSetSegmentDescriptor(&GdtEntries[1], 0x00000000u, 0x000FFFFFu, 1, 1, 0, 1, 1, 0);
    VbrSetSegmentDescriptor(&GdtEntries[2], 0x00000000u, 0x000FFFFFu, 0, 1, 0, 1, 1, 0);

    MemoryCopy((void*)GdtPhysicalAddress, GdtEntries, sizeof(GdtEntries));

    Gdtr.Limit = (U16)(sizeof(GdtEntries) - 1U);
    Gdtr.Base = GdtPhysicalAddress;
}

/************************************************************************/

void __attribute__((noreturn)) EnterProtectedPagingAndJump(U32 FileSize) {
    const U32 KernelPhysBase = KERNEL_LINEAR_LOAD_ADDRESS;
    const U32 MapSize = PAGE_ALIGN(FileSize + N_512KB);

    EnableA20();

    const U32 KernelVirtBase = 0xC0000000U;
    BuildPaging(KernelPhysBase, KernelVirtBase, MapSize);
    BuildGdtFlat();

    const U32 KernelEntryLo = KernelVirtBase;
    const U32 KernelEntryHi = 0U;
    const U32 PagingStructure = (U32)(UINT)PageDirectory;

    U32 MultibootInfoPtr = BuildMultibootInfo(KernelPhysBase, FileSize);

    for (volatile int Delay = 0; Delay < 100000; ++Delay) {
        __asm__ __volatile__("nop");
    }

    StubJumpToImage((U32)(&Gdtr), PagingStructure, KernelEntryLo, KernelEntryHi, MultibootInfoPtr, MULTIBOOT_BOOTLOADER_MAGIC);

    __builtin_unreachable();
}
