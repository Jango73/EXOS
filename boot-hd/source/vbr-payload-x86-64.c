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

#include "../../kernel/include/arch/i386/i386.h"
#include "../include/LongModeStructures.h"
#include "../include/VbrPayloadShared.h"

/************************************************************************/

#define PML4_ADDRESS LOW_MEMORY_PAGE_1
#define PDPT_LOW_ADDRESS LOW_MEMORY_PAGE_2
#define PDPT_KERNEL_ADDRESS LOW_MEMORY_PAGE_3
#define PAGE_DIRECTORY_LOW_ADDRESS LOW_MEMORY_PAGE_4
#define PAGE_DIRECTORY_KERNEL_ADDRESS LOW_MEMORY_PAGE_5
#define PAGE_TABLE_LOW_ADDRESS LOW_MEMORY_PAGE_6

static U32 GdtPhysicalAddress = 0U;
#ifdef __EXOS_32__
static const U64 KERNEL_LONG_MODE_BASE = { 0x80000000u, 0xFFFFFFFFu };
#else
static const U64 KERNEL_LONG_MODE_BASE = 0xFFFFFFFF80000000ull;
#endif
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

#ifdef __EXOS_32__
static U64 VbrShiftRightU64(U64 Value, UINT Count) {
    U64 Result = { 0u, 0u };

    if (Count >= 64u) {
        return Result;
    }

    if (Count >= 32u) {
        const UINT Shift = Count - 32u;
        Result.LO = Value.HI >> Shift;
        Result.HI = 0u;
        return Result;
    }

    if (Count != 0u) {
        const UINT Right = Count;
        const UINT Left = 32u - Right;
        Result.LO = (Value.LO >> Right) | (Value.HI << Left);
        Result.HI = Value.HI >> Right;
        return Result;
    }

    return Value;
}
#else
static U64 VbrShiftRightU64(U64 Value, UINT Count) {
    return Value >> Count;
}
#endif

/************************************************************************/

static UINT VbrExtractU64Bits(U64 Value, UINT Shift, UINT Width) {
    U64 Shifted = VbrShiftRightU64(Value, Shift);

#ifdef __EXOS_32__
    U32 Mask;
    if (Width >= 32u) {
        Mask = 0xFFFFFFFFu;
    } else {
        Mask = (1u << Width) - 1u;
    }
    return Shifted.LO & Mask;
#else
    U64 Mask;
    if (Width >= 64u) {
        Mask = ~0ull;
    } else {
        Mask = (1ull << Width) - 1ull;
    }
    return (UINT)(Shifted & Mask);
#endif
}

/************************************************************************/

static U64 VbrPointerToPhysical(const void* Pointer) {
#ifdef __EXOS_32__
    return U64_FromU32((U32)(UINT)Pointer);
#else
    return U64_FromUINT((UINT)Pointer);
#endif
}

/************************************************************************/

static U32 VbrAlignToPage(U32 Value) {
    return (Value + (PAGE_SIZE - 1u)) & ~(PAGE_SIZE - 1u);
}

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
    U32 Low = 0x00000003u;
    U32 High = 0u;

    if (Global != 0u) {
        Low |= 0x00000100u;
    }

    const U32 PhysicalLow = U64_Low32(Physical);
    const U32 PhysicalHigh = U64_High32(Physical);

    Low |= (PhysicalLow & 0xFFFFF000u);
    High |= (PhysicalHigh & 0x000FFFFFu);

    Entry->Low = Low;
    Entry->High = High;
}

/************************************************************************/

static void BuildPaging(U32 KernelPhysBase, U64 KernelVirtBase, U32 MapSize) {
    ClearLongModeStructures();

    SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageMapLevel4 + 0), VbrPointerToPhysical(PageDirectoryPointerLow), 0);
    SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageDirectoryPointerLow + 0), VbrPointerToPhysical(PageDirectoryLow), 0);
    SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageDirectoryLow + 0), VbrPointerToPhysical(PageTableLow), 0);

    for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; ++Index) {
        const U32 PhysicalValue = Index * PAGE_SIZE;
        SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageTableLow + Index), U64_FromU32(PhysicalValue), 1);
    }

    SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageMapLevel4 + PML4_RECURSIVE_SLOT), VbrPointerToPhysical(PageMapLevel4), 0);

    const UINT KernelPml4Index = VbrExtractU64Bits(KernelVirtBase, 39u, 9u);
    const UINT KernelPdptIndex = VbrExtractU64Bits(KernelVirtBase, 30u, 9u);
    UINT KernelPdIndex = VbrExtractU64Bits(KernelVirtBase, 21u, 9u);
    UINT KernelPtIndex = VbrExtractU64Bits(KernelVirtBase, 12u, 9u);

    SetLongModeEntry(
        (LPX86_64_PAGING_ENTRY)(PageMapLevel4 + KernelPml4Index),
        VbrPointerToPhysical(PageDirectoryPointerKernel),
        0);
    SetLongModeEntry(
        (LPX86_64_PAGING_ENTRY)(PageDirectoryPointerKernel + KernelPdptIndex),
        VbrPointerToPhysical(PageDirectoryKernel),
        0);

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
        LPPAGE_TABLE CurrentTable = (LPPAGE_TABLE)(UINT)TablePhysical;
        MemorySet(CurrentTable, 0, PAGE_TABLE_SIZE);

        SetLongModeEntry(
            (LPX86_64_PAGING_ENTRY)(PageDirectoryKernel + KernelPdIndex),
            U64_FromU32(TablePhysical),
            0);

        for (UINT EntryIndex = KernelPtIndex; EntryIndex < PAGE_TABLE_NUM_ENTRIES && RemainingPages > 0U; ++EntryIndex) {
            SetLongModeEntry(
                (LPX86_64_PAGING_ENTRY)(CurrentTable + EntryIndex),
                U64_FromU32(PhysicalCursor),
                1);
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
    const U32 MapSize = VbrAlignToPage(FileSize + N_512KB);

    EnableA20();

    const U64 KernelVirtBase = KERNEL_LONG_MODE_BASE;
    BuildPaging(KernelPhysBase, KernelVirtBase, MapSize);
    BuildGdtFlat();

    const U32 KernelEntryLo = U64_Low32(KernelVirtBase);
    const U32 KernelEntryHi = U64_High32(KernelVirtBase);
    const U32 PagingStructure = (U32)(UINT)PageMapLevel4;

    U32 MultibootInfoPtr = BuildMultibootInfo(KernelPhysBase, FileSize);

    for (volatile int Delay = 0; Delay < 100000; ++Delay) {
        __asm__ __volatile__("nop");
    }

    StubJumpToImage((U32)(&Gdtr), PagingStructure, KernelEntryLo, KernelEntryHi, MultibootInfoPtr, MULTIBOOT_BOOTLOADER_MAGIC);

    __builtin_unreachable();
}

