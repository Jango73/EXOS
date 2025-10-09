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


    x86-64 specific portion of the VBR payload

\************************************************************************/

#include <stdint.h>

#include "../../kernel/include/arch/i386/i386.h"
#include "../../kernel/include/arch/x86-64/x86-64-Memory.h"
#include "../include/VbrPayloadShared.h"

/************************************************************************/

#define PML4_ADDRESS LOW_MEMORY_PAGE_1
#define PDPT_LOW_ADDRESS LOW_MEMORY_PAGE_2
#define PDPT_KERNEL_ADDRESS LOW_MEMORY_PAGE_3
#define PAGE_DIRECTORY_LOW_ADDRESS LOW_MEMORY_PAGE_4
#define PAGE_DIRECTORY_KERNEL_ADDRESS LOW_MEMORY_PAGE_5
#define PAGE_TABLE_LOW_ADDRESS LOW_MEMORY_PAGE_6

static U32 GdtPhysicalAddress = 0U;
static const U64 KERNEL_LONG_MODE_BASE = 0xFFFFFFFF80000000ull;
static const UINT MAX_KERNEL_PAGE_TABLES = 64u;

static LPPML4 PageMapLevel4 = (LPPML4)PML4_ADDRESS;
static LPPDPT PageDirectoryPointerLow = (LPPDPT)PDPT_LOW_ADDRESS;
static LPPDPT PageDirectoryPointerKernel = (LPPDPT)PDPT_KERNEL_ADDRESS;
static LPPAGE_DIRECTORY PageDirectoryLow = (LPPAGE_DIRECTORY)PAGE_DIRECTORY_LOW_ADDRESS;
static LPPAGE_DIRECTORY PageDirectoryKernel = (LPPAGE_DIRECTORY)PAGE_DIRECTORY_KERNEL_ADDRESS;
static LPPAGE_TABLE PageTableLow = (LPPAGE_TABLE)PAGE_TABLE_LOW_ADDRESS;
static SEGMENT_DESCRIPTOR GdtEntries[3];
static GDT_REGISTER Gdtr;

/************************************************************************/

static void ClearLongModeStructures(void) {
    MemorySet(PageMapLevel4, 0, PAGE_TABLE_SIZE);
    MemorySet(PageDirectoryPointerLow, 0, PAGE_TABLE_SIZE);
    MemorySet(PageDirectoryPointerKernel, 0, PAGE_TABLE_SIZE);
    MemorySet(PageDirectoryLow, 0, PAGE_TABLE_SIZE);
    MemorySet(PageDirectoryKernel, 0, PAGE_TABLE_SIZE);
    MemorySet(PageTableLow, 0, PAGE_TABLE_SIZE);
}

/************************************************************************/

static void SetLongModeEntry(LPX86_64_PAGING_ENTRY Entry, U64 Physical, U32 Global) {
    Entry->Present = 1;
    Entry->ReadWrite = 1;
    Entry->Privilege = 0;
    Entry->WriteThrough = 0;
    Entry->CacheDisabled = 0;
    Entry->Accessed = 0;
    Entry->Dirty = 0;
    Entry->PageSize = 0;
    Entry->Global = (Global ? 1U : 0U);
    Entry->Available_9_11 = 0;
    Entry->Address = (Physical >> 12);
    Entry->Available_52_58 = 0;
    Entry->Reserved_59_62 = 0;
    Entry->NoExecute = 0;
}

/************************************************************************/

static void BuildPaging(U32 KernelPhysBase, U64 KernelVirtBase, U32 MapSize) {
    ClearLongModeStructures();

    SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageMapLevel4 + 0), (U64)(uintptr_t)PageDirectoryPointerLow, 0);
    SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageDirectoryPointerLow + 0), (U64)(uintptr_t)PageDirectoryLow, 0);
    SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageDirectoryLow + 0), (U64)(uintptr_t)PageTableLow, 0);

    for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; ++Index) {
        const U64 Physical = (U64)Index * PAGE_SIZE;
        SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageTableLow + Index), Physical, 1);
    }

    SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageMapLevel4 + PML4_RECURSIVE_SLOT), (U64)(uintptr_t)PageMapLevel4, 0);

    const UINT KernelPml4Index = (UINT)((KernelVirtBase >> 39) & 0x1FFu);
    const UINT KernelPdptIndex = (UINT)((KernelVirtBase >> 30) & 0x1FFu);
    UINT KernelPdIndex = (UINT)((KernelVirtBase >> 21) & 0x1FFu);
    UINT KernelPtIndex = (UINT)((KernelVirtBase >> 12) & 0x1FFu);

    SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageMapLevel4 + KernelPml4Index), (U64)(uintptr_t)PageDirectoryPointerKernel, 0);
    SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageDirectoryPointerKernel + KernelPdptIndex), (U64)(uintptr_t)PageDirectoryKernel, 0);

    const U32 TotalPages = (MapSize + PAGE_SIZE - 1U) / PAGE_SIZE;
    const U32 TablesRequired = (TotalPages + PAGE_TABLE_NUM_ENTRIES - 1U) / PAGE_TABLE_NUM_ENTRIES;

    StringPrintFormat(TempString, TEXT("[VBR] Long mode mapping %u pages (%u tables)\r\n"), TotalPages, TablesRequired);
    BootDebugPrint(TempString);

    if (TablesRequired > MAX_KERNEL_PAGE_TABLES) {
        StringPrintFormat(
            TempString,
            TEXT("[VBR] ERROR: Required kernel tables %u exceed limit %u. Halting.\r\n"),
            TablesRequired,
            MAX_KERNEL_PAGE_TABLES);
        BootErrorPrint(TempString);
        Hang();
    }

    const U32 BaseTablePhysical = KernelPhysBase + MapSize;
    U32 RemainingPages = TotalPages;
    U32 TableIndex = 0;
    U32 PhysicalCursor = KernelPhysBase;

    while (RemainingPages > 0U) {
        if (KernelPdIndex >= PAGE_DIRECTORY_ENTRY_COUNT) {
            BootErrorPrint(TEXT("[VBR] ERROR: Kernel page directory overflow. Halting.\r\n"));
            Hang();
        }

        const U32 TablePhysical = BaseTablePhysical + (TableIndex * PAGE_TABLE_SIZE);
        LPPAGE_TABLE CurrentTable = (LPPAGE_TABLE)(uintptr_t)TablePhysical;
        MemorySet(CurrentTable, 0, PAGE_TABLE_SIZE);

        SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageDirectoryKernel + KernelPdIndex), (U64)TablePhysical, 0);

        for (UINT EntryIndex = KernelPtIndex; EntryIndex < PAGE_TABLE_NUM_ENTRIES && RemainingPages > 0U; ++EntryIndex) {
            SetLongModeEntry((LPX86_64_PAGING_ENTRY)(CurrentTable + EntryIndex), (U64)PhysicalCursor, 1);
            PhysicalCursor += PAGE_SIZE;
            --RemainingPages;
        }

        ++KernelPdIndex;
        KernelPtIndex = 0;
        ++TableIndex;
    }

    GdtPhysicalAddress = BaseTablePhysical + (TableIndex * PAGE_TABLE_SIZE);
}

/************************************************************************/

static void BuildGdtFlat(void) {
    BootDebugPrint(TEXT("[VBR] BuildGdtFlat\r\n"));

    MemorySet(GdtEntries, 0, sizeof(GdtEntries));

    VbrSetSegmentDescriptor(&GdtEntries[1], 0x00000000u, 0x00000000u, 1, 1, 0, 0, 1, 1);
    VbrSetSegmentDescriptor(&GdtEntries[2], 0x00000000u, 0x000FFFFFu, 0, 1, 0, 1, 1, 0);

    if (GdtPhysicalAddress == 0U) {
        BootErrorPrint(TEXT("[VBR] ERROR: Missing GDT physical address. Halting.\r\n"));
        Hang();
    }

    MemoryCopy((void*)GdtPhysicalAddress, GdtEntries, sizeof(GdtEntries));

    Gdtr.Limit = (U16)(sizeof(GdtEntries) - 1U);
    Gdtr.Base = GdtPhysicalAddress;
}

/************************************************************************/

void __attribute__((noreturn)) EnterProtectedPagingAndJump(U32 FileSize) {
    const U32 KernelPhysBase = SegOfsToLinear(LOADADDRESS_SEG, LOADADDRESS_OFS);
    const U32 MapSize = (U32)PAGE_ALIGN(FileSize + N_512KB);

    EnableA20();

    const U64 KernelVirtBase = KERNEL_LONG_MODE_BASE;
    BuildPaging(KernelPhysBase, KernelVirtBase, MapSize);
    BuildGdtFlat();

    const U32 KernelEntryLo = (U32)(KernelVirtBase & 0xFFFFFFFFu);
    const U32 KernelEntryHi = (U32)((KernelVirtBase >> 32) & 0xFFFFFFFFu);
    const U32 PagingStructure = (U32)(uintptr_t)PageMapLevel4;

    U32 MultibootInfoPtr = BuildMultibootInfo(KernelPhysBase, FileSize);

    for (volatile int Delay = 0; Delay < 100000; ++Delay) {
        __asm__ __volatile__("nop");
    }

    StubJumpToImage((U32)(&Gdtr), PagingStructure, KernelEntryLo, KernelEntryHi, MultibootInfoPtr, MULTIBOOT_BOOTLOADER_MAGIC);

    __builtin_unreachable();
}

