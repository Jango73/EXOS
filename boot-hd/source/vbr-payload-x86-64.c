
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

#include "../include/vbr-payload-x86-64.h"
#include "../include/vbr-payload-shared.h"
#include "boot-reservation.h"

/************************************************************************/

#ifndef CONFIG_VMA_KERNEL
#error "CONFIG_VMA_KERNEL is not defined"
#endif

#ifdef __EXOS_32__
static const U64 KERNEL_LONG_MODE_BASE = {
    (U32)(CONFIG_VMA_KERNEL & 0xFFFFFFFFu),
    (U32)((CONFIG_VMA_KERNEL >> 32) & 0xFFFFFFFFu)
};
#else
static const U64 KERNEL_LONG_MODE_BASE = (U64)CONFIG_VMA_KERNEL;
#endif
static const UINT MAX_KERNEL_PAGE_TABLES = 64u;

enum {
    LONG_MODE_ENTRY_GLOBAL = 0x00000001u,
    LONG_MODE_ENTRY_LARGE_PAGE = 0x00000002u,
    LONG_MODE_ENTRY_NO_EXECUTE = 0x00000004u
};

static const U32 GdtPhysicalAddress = LOW_MEMORY_PAGE_1;
static LPPML4 PageMapLevel4 = (LPPML4)LOW_MEMORY_PAGE_2;
static LPPDPT PageDirectoryPointerLow = (LPPDPT)LOW_MEMORY_PAGE_3;
static LPPDPT PageDirectoryPointerKernel = (LPPDPT)LOW_MEMORY_PAGE_4;
static LPPAGE_DIRECTORY PageDirectoryLow = (LPPAGE_DIRECTORY)LOW_MEMORY_PAGE_5;
static LPPAGE_DIRECTORY PageDirectoryKernel = (LPPAGE_DIRECTORY)LOW_MEMORY_PAGE_6;
static LPPAGE_TABLE PageTableLow = (LPPAGE_TABLE)LOW_MEMORY_PAGE_7;
static LPPAGE_TABLE PageTableLowHigh = (LPPAGE_TABLE)LOW_MEMORY_PAGE_8;
static SEGMENT_DESCRIPTOR GdtEntries[VBR_GDT_ENTRY_LONG_MODE_DATA + 1u];
static GDT_REGISTER Gdtr;

#if defined(BOOT_UEFI)
static void UefiSerialWriteByte(U8 Value) {
    const U16 Port = 0x3F8u;
    const U16 LineStatusPort = (U16)(Port + 0x05u);
    const U8 LineStatusThre = 0x20u;
    U8 Status = 0u;

    do {
        __asm__ __volatile__("inb %1, %0" : "=a"(Status) : "Nd"(LineStatusPort));
    } while ((Status & LineStatusThre) == 0u);

    __asm__ __volatile__("outb %0, %1" ::"a"(Value), "Nd"(Port));
}

static void UefiSerialWriteString(LPCSTR Text) {
    if (Text == NULL) {
        return;
    }

    while (*Text != 0u) {
        UefiSerialWriteByte(*Text);
        Text++;
    }
}

/************************************************************************/

/**
 * @brief Write a 32-bit value as hexadecimal to the legacy serial port.
 *
 * @param Value Value to output.
 */
static void UefiSerialWriteHex32(U32 Value) {
    STR HexValue[9];

    U32ToHexString(Value, HexValue);
    UefiSerialWriteString((LPCSTR)HexValue);
}

/************************************************************************/

/**
 * @brief Write a 64-bit value as hexadecimal to the legacy serial port.
 *
 * @param Value Value to output.
 */
static void UefiSerialWriteHex64(U64 Value) {
    UefiSerialWriteHex32(U64_High32(Value));
    UefiSerialWriteHex32(U64_Low32(Value));
}

/************************************************************************/

/**
 * @brief Write a labeled 32-bit hexadecimal value to the legacy serial port.
 *
 * @param Label Prefix string.
 * @param Value Value to output.
 */
static void UefiSerialWriteLabelHex32(LPCSTR Label, U32 Value) {
    UefiSerialWriteString(Label);
    UefiSerialWriteString((LPCSTR)"0x");
    UefiSerialWriteHex32(Value);
    UefiSerialWriteString((LPCSTR)"\r\n");
}

/************************************************************************/

/**
 * @brief Write a labeled 64-bit hexadecimal value to the legacy serial port.
 *
 * @param Label Prefix string.
 * @param Value Value to output.
 */
static void UefiSerialWriteLabelHex64(LPCSTR Label, U64 Value) {
    UefiSerialWriteString(Label);
    UefiSerialWriteString((LPCSTR)"0x");
    UefiSerialWriteHex64(Value);
    UefiSerialWriteString((LPCSTR)"\r\n");
}
#endif

typedef char VerifySegmentDescriptorSize[(sizeof(SEGMENT_DESCRIPTOR) == 8u) ? 1 : -1];
typedef char VerifyProtectedCodeSelector[
    (VBR_PROTECTED_MODE_CODE_SELECTOR == (U16)(VBR_GDT_ENTRY_PROTECTED_CODE * (U16)sizeof(SEGMENT_DESCRIPTOR))) ? 1 : -1];
typedef char VerifyProtectedDataSelector[
    (VBR_PROTECTED_MODE_DATA_SELECTOR == (U16)(VBR_GDT_ENTRY_PROTECTED_DATA * (U16)sizeof(SEGMENT_DESCRIPTOR))) ? 1 : -1];
typedef char VerifyLongModeCodeSelector[
    (VBR_LONG_MODE_CODE_SELECTOR == (U16)(VBR_GDT_ENTRY_LONG_MODE_CODE * (U16)sizeof(SEGMENT_DESCRIPTOR))) ? 1 : -1];
typedef char VerifyLongModeDataSelector[
    (VBR_LONG_MODE_DATA_SELECTOR == (U16)(VBR_GDT_ENTRY_LONG_MODE_DATA * (U16)sizeof(SEGMENT_DESCRIPTOR))) ? 1 : -1];

const U16 VbrProtectedModeCodeSelector = VBR_PROTECTED_MODE_CODE_SELECTOR;
const U16 VbrProtectedModeDataSelector = VBR_PROTECTED_MODE_DATA_SELECTOR;
const U16 VbrLongModeCodeSelector = VBR_LONG_MODE_CODE_SELECTOR;
const U16 VbrLongModeDataSelector = VBR_LONG_MODE_DATA_SELECTOR;

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
    MemorySet(PageTableLowHigh, 0, PAGE_TABLE_SIZE);
}

/************************************************************************/

static void SetLongModeEntry(LPX86_64_PAGING_ENTRY Entry, U64 Physical, U32 Flags) {
    U32 Low = 0x00000003u;
    U32 High = 0u;

    if ((Flags & LONG_MODE_ENTRY_GLOBAL) != 0u) {
        Low |= 0x00000100u;
    }

    if ((Flags & LONG_MODE_ENTRY_LARGE_PAGE) != 0u) {
        Low |= 0x00000080u;
    }

    const U32 PhysicalLow = U64_Low32(Physical);
    const U32 PhysicalHigh = U64_High32(Physical);

    Low |= (PhysicalLow & 0xFFFFF000u);
    High |= (PhysicalHigh & 0x000FFFFFu);

    if ((Flags & LONG_MODE_ENTRY_NO_EXECUTE) != 0u) {
        High |= 0x80000000u;
    }

    Entry->Low = Low;
    Entry->High = High;
}

/************************************************************************/

/**
 * @brief Convert a 64-bit physical address into a register-sized integer.
 *
 * @param Value Physical address to convert.
 * @return Register-sized value.
 */
static UINT VbrU64ToUINT(U64 Value) {
    return (UINT)U64_Low32(Value);
}

/************************************************************************/

/**
 * @brief Check whether a paging entry is present.
 *
 * @param Entry Paging entry.
 * @return TRUE when present, otherwise FALSE.
 */
static BOOL VbrIsLongModeEntryPresent(const X86_64_PAGING_ENTRY* Entry) {
    return (Entry->Low & 0x00000001u) != 0u;
}

/************************************************************************/

/**
 * @brief Extract the physical address from a paging entry.
 *
 * @param Entry Paging entry.
 * @return Physical address stored in the entry.
 */
static U64 VbrEntryToPhysical(const X86_64_PAGING_ENTRY* Entry) {
    const U32 PhysicalLow = Entry->Low & 0xFFFFF000u;
    const U32 PhysicalHigh = Entry->High & 0x000FFFFFu;

    return U64_Make(PhysicalHigh, PhysicalLow);
}

/************************************************************************/

/**
 * @brief Identity-map a physical range so the UEFI image remains executable after CR3 switch.
 *
 * @param Base Physical base address.
 * @param Size Size in bytes.
 * @param NextTablePhysical Physical address allocator for paging tables.
 */
#if defined(BOOT_UEFI)
static void MapIdentityRange(U64 Base, U64 Size, U64* NextTablePhysical) {
    if (Size == 0) {
        return;
    }

    U64 Start = Base & ~(U64)(PAGE_SIZE - 1u);
    U64 End = Base + Size;
    End = (End + (U64)(PAGE_SIZE - 1u)) & ~(U64)(PAGE_SIZE - 1u);

    for (U64 Address = Start; Address < End; Address += PAGE_SIZE) {
        const UINT Pml4Index = (UINT)((Address >> 39) & 0x1FFu);
        const UINT PdptIndex = (UINT)((Address >> 30) & 0x1FFu);
        const UINT PdIndex = (UINT)((Address >> 21) & 0x1FFu);
        const UINT PtIndex = (UINT)((Address >> 12) & 0x1FFu);

        if (Pml4Index != 0u) {
            UefiSerialWriteString((LPCSTR)"[MapIdentityRange] ERROR: Pml4 index not supported\r\n");
            Hang();
        }

        LPX86_64_PAGING_ENTRY PdptEntry = (LPX86_64_PAGING_ENTRY)(PageDirectoryPointerLow + PdptIndex);
        LPPAGE_DIRECTORY PageDirectory = NULL;
        if (!VbrIsLongModeEntryPresent(PdptEntry)) {
            const U64 TablePhysical = *NextTablePhysical;
            *NextTablePhysical += PAGE_TABLE_SIZE;
            PageDirectory = (LPPAGE_DIRECTORY)VbrU64ToUINT(TablePhysical);
            MemorySet(PageDirectory, 0, PAGE_TABLE_SIZE);
            SetLongModeEntry(PdptEntry, U64_FromUINT(TablePhysical), 0u);
        } else {
            const U64 Physical = VbrEntryToPhysical(PdptEntry);
            PageDirectory = (LPPAGE_DIRECTORY)VbrU64ToUINT(Physical);
        }

        LPX86_64_PAGING_ENTRY PdEntry = (LPX86_64_PAGING_ENTRY)(PageDirectory + PdIndex);
        LPPAGE_TABLE PageTable = NULL;
        if (!VbrIsLongModeEntryPresent(PdEntry)) {
            const U64 TablePhysical = *NextTablePhysical;
            *NextTablePhysical += PAGE_TABLE_SIZE;
            PageTable = (LPPAGE_TABLE)VbrU64ToUINT(TablePhysical);
            MemorySet(PageTable, 0, PAGE_TABLE_SIZE);
            SetLongModeEntry(PdEntry, U64_FromUINT(TablePhysical), 0u);
        } else {
            const U64 Physical = VbrEntryToPhysical(PdEntry);
            PageTable = (LPPAGE_TABLE)VbrU64ToUINT(Physical);
        }

        SetLongModeEntry(
            (LPX86_64_PAGING_ENTRY)(PageTable + PtIndex),
            Address,
            LONG_MODE_ENTRY_GLOBAL);
    }
}
#endif

/************************************************************************/

static void SetSegmentDescriptorX8664(
    LPSEGMENT_DESCRIPTOR Descriptor,
    U32 Base,
    U32 Limit,
    U32 Privilege,
    BOOL Executable,
    BOOL LongMode,
    BOOL DefaultSize,
    BOOL Granularity) {
    MemorySet(Descriptor, 0, sizeof(SEGMENT_DESCRIPTOR));

    Descriptor->Limit_00_15 = (U16)(Limit & 0xFFFF);
    Descriptor->Base_00_15 = (U16)(Base & 0xFFFF);
    Descriptor->Base_16_23 = (U8)((Base >> 16) & 0xFF);
    Descriptor->Accessed = 0;
    Descriptor->CanWrite = 1;
    Descriptor->ConformExpand = 0;
    Descriptor->Code = Executable;
    Descriptor->S = 1;
    Descriptor->DPL = (U8)(Privilege & 3);
    Descriptor->Present = 1;
    Descriptor->Limit_16_19 = (U8)((Limit >> 16) & 0x0F);
    Descriptor->AVL = 0;
    Descriptor->LongMode = (U8)(LongMode ? 1 : 0);
    Descriptor->DefaultSize = (U8)(DefaultSize ? 1 : 0);
    Descriptor->Granularity = (U8)(Granularity ? 1 : 0);
    Descriptor->Base_24_31 = (U8)((Base >> 24) & 0xFF);
}

/************************************************************************/

static void BuildPaging(U32 KernelPhysBase, U64 KernelVirtBase, U32 MapSize, U64 UefiImageBase, U64 UefiImageSize) {
    ClearLongModeStructures();

    SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageMapLevel4 + 0), VbrPointerToPhysical(PageDirectoryPointerLow), 0u);
    SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageDirectoryPointerLow + 0), VbrPointerToPhysical(PageDirectoryLow), 0u);
    SetLongModeEntry(
        (LPX86_64_PAGING_ENTRY)(PageDirectoryLow + 0),
        VbrPointerToPhysical(PageTableLow),
        0u);
    SetLongModeEntry(
        (LPX86_64_PAGING_ENTRY)(PageDirectoryLow + 1),
        VbrPointerToPhysical(PageTableLowHigh),
        0u);

    for (UINT EntryIndex = 0u; EntryIndex < PAGE_TABLE_NUM_ENTRIES; ++EntryIndex) {
        const U32 Physical = EntryIndex * PAGE_SIZE;
        SetLongModeEntry(
            (LPX86_64_PAGING_ENTRY)(PageTableLow + EntryIndex),
            U64_FromU32(Physical),
            LONG_MODE_ENTRY_GLOBAL);
    }

    for (UINT EntryIndex = 0u; EntryIndex < PAGE_TABLE_NUM_ENTRIES; ++EntryIndex) {
        const U32 Physical = 0x00200000u + (EntryIndex * PAGE_SIZE);
        SetLongModeEntry(
            (LPX86_64_PAGING_ENTRY)(PageTableLowHigh + EntryIndex),
            U64_FromU32(Physical),
            LONG_MODE_ENTRY_GLOBAL);
    }

    SetLongModeEntry((LPX86_64_PAGING_ENTRY)(PageMapLevel4 + PML4_RECURSIVE_SLOT), VbrPointerToPhysical(PageMapLevel4), 0u);

    const UINT KernelPml4Index = VbrExtractU64Bits(KernelVirtBase, 39u, 9u);
    const UINT KernelPdptIndex = VbrExtractU64Bits(KernelVirtBase, 30u, 9u);
    UINT KernelPdIndex = VbrExtractU64Bits(KernelVirtBase, 21u, 9u);
    UINT KernelPtIndex = VbrExtractU64Bits(KernelVirtBase, 12u, 9u);

    SetLongModeEntry(
        (LPX86_64_PAGING_ENTRY)(PageMapLevel4 + KernelPml4Index),
        VbrPointerToPhysical(PageDirectoryPointerKernel),
        0u);
    SetLongModeEntry(
        (LPX86_64_PAGING_ENTRY)(PageDirectoryPointerKernel + KernelPdptIndex),
        VbrPointerToPhysical(PageDirectoryKernel),
        0u);

    const U32 TotalPages = (MapSize + PAGE_SIZE - 1U) / PAGE_SIZE;
    const U32 TablesRequired = (TotalPages + PAGE_TABLE_NUM_ENTRIES - 1U) / PAGE_TABLE_NUM_ENTRIES;

    if (TablesRequired > MAX_KERNEL_PAGE_TABLES) {
        BootErrorPrint(
            TEXT("[VBR x86-64] ERROR: Required kernel tables %u exceed limit %u. Halting.\r\n"),
            TablesRequired,
            MAX_KERNEL_PAGE_TABLES);
        Hang();
    }

    const U32 BaseTablePhysical = KernelPhysBase + MapSize;
    U32 RemainingPages = TotalPages;
    U32 TableIndex = 0;
    U32 PhysicalCursor = KernelPhysBase;

    while (RemainingPages > 0U) {
        if (KernelPdIndex >= PAGE_DIRECTORY_ENTRY_COUNT) {
            BootErrorPrint(TEXT("[VBR x86-64] ERROR: Kernel page directory overflow. Halting.\r\n"));
            Hang();
        }

        const U32 TablePhysical = BaseTablePhysical + (TableIndex * PAGE_TABLE_SIZE);
        LPPAGE_TABLE CurrentTable = (LPPAGE_TABLE)(UINT)TablePhysical;
        MemorySet(CurrentTable, 0, PAGE_TABLE_SIZE);

        SetLongModeEntry(
            (LPX86_64_PAGING_ENTRY)(PageDirectoryKernel + KernelPdIndex),
            U64_FromU32(TablePhysical),
            0u);

        for (UINT EntryIndex = KernelPtIndex; EntryIndex < PAGE_TABLE_NUM_ENTRIES && RemainingPages > 0U; ++EntryIndex) {
            SetLongModeEntry(
                (LPX86_64_PAGING_ENTRY)(CurrentTable + EntryIndex),
                U64_FromU32(PhysicalCursor),
                LONG_MODE_ENTRY_GLOBAL);
            PhysicalCursor += PAGE_SIZE;
            --RemainingPages;
        }

        ++KernelPdIndex;
        KernelPtIndex = 0;
        ++TableIndex;
    }

    U64 NextTablePhysical = (U64)BaseTablePhysical + (U64)(TablesRequired * PAGE_TABLE_SIZE);

    MapIdentityRange(
        U64_FromU32(KernelPhysBase),
        U64_FromU32(MapSize + BOOT_KERNEL_IDENTITY_WORKSPACE_BYTES),
        &NextTablePhysical);

#if defined(BOOT_UEFI)
    if (UefiImageSize != 0) {
        MapIdentityRange(UefiImageBase, UefiImageSize, &NextTablePhysical);
    }
#else
    UNUSED(UefiImageBase);
    UNUSED(UefiImageSize);
#endif
}

/************************************************************************/

static void BuildGdtFlat(void) {
    MemorySet(GdtEntries, 0, sizeof(GdtEntries));

    SetSegmentDescriptorX8664(
        &GdtEntries[VBR_GDT_ENTRY_PROTECTED_CODE],
        0x00000000,
        0x000FFFFF,
        0,
        TRUE,
        FALSE,
        TRUE,
        TRUE);
    SetSegmentDescriptorX8664(
        &GdtEntries[VBR_GDT_ENTRY_PROTECTED_DATA],
        0x00000000,
        0x000FFFFF,
        0,
        FALSE,
        FALSE,
        TRUE,
        TRUE);
    SetSegmentDescriptorX8664(
        &GdtEntries[VBR_GDT_ENTRY_LONG_MODE_CODE],
        0x00000000,
        0x00000000,
        0,
        TRUE,
        TRUE,
        FALSE,
        TRUE);
    SetSegmentDescriptorX8664(
        &GdtEntries[VBR_GDT_ENTRY_LONG_MODE_DATA],
        0x00000000,
        0x00000000,
        0,
        FALSE,
        FALSE,
        FALSE,
        TRUE);

    MemoryCopy((void*)GdtPhysicalAddress, GdtEntries, sizeof(GdtEntries));

    Gdtr.Limit = (U16)(sizeof(GdtEntries) - 1U);
    Gdtr.Base = GdtPhysicalAddress;
}

/************************************************************************/

void NORETURN EnterProtectedPagingAndJump(U32 FileSize, U32 MultibootInfoPtr, U64 UefiImageBase, U64 UefiImageSize) {
    const U32 KernelPhysBase = KERNEL_LINEAR_LOAD_ADDRESS;
    U32 MapSize = VbrAlignToPage(FileSize + BOOT_KERNEL_MAP_PADDING_BYTES);

    if (MapSize < BOOT_X86_64_TEMP_LINEAR_REQUIRED_SPAN) {
        MapSize = BOOT_X86_64_TEMP_LINEAR_REQUIRED_SPAN;
    }

#if !defined(BOOT_UEFI)
    EnableA20();
    UNUSED(UefiImageBase);
    UNUSED(UefiImageSize);
#endif

#if defined(BOOT_UEFI)
    UefiSerialWriteString((LPCSTR)"[EnterProtectedPagingAndJump] Start\r\n");
    UefiSerialWriteLabelHex64(
        (LPCSTR)"[EnterProtectedPagingAndJump] UefiImageBase=",
        UefiImageBase);
    UefiSerialWriteLabelHex64(
        (LPCSTR)"[EnterProtectedPagingAndJump] UefiImageSize=",
        UefiImageSize);
#endif

    const U64 KernelVirtBase = KERNEL_LONG_MODE_BASE;
    BuildPaging(KernelPhysBase, KernelVirtBase, MapSize, UefiImageBase, UefiImageSize);
    BuildGdtFlat();

#if defined(BOOT_UEFI)
    UefiSerialWriteString((LPCSTR)"[EnterProtectedPagingAndJump] Paging and GDT ready\r\n");
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] KernelPhysicalBase=", KernelPhysBase);
    UefiSerialWriteLabelHex64((LPCSTR)"[EnterProtectedPagingAndJump] KernelVirtualBase=", KernelVirtBase);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] MapSize=", MapSize);
#endif

    const U32 KernelEntryLo = U64_Low32(KernelVirtBase);
    const U32 KernelEntryHi = U64_High32(KernelVirtBase);
    const U32 PagingStructure = (U32)(UINT)PageMapLevel4;

    for (volatile int Delay = 0; Delay < 100000; ++Delay) {
        __asm__ __volatile__("nop");
    }

    BootDebugPrint(TEXT("[VBR x86-64] About to jump\r\n"));

#if defined(BOOT_UEFI)
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] KernelEntryLow=", KernelEntryLo);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] KernelEntryHigh=", KernelEntryHi);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] PagingStructure=", PagingStructure);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] MultibootInfoPointer=", MultibootInfoPtr);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] GdtRegister=", (U32)(UINT)&Gdtr);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] GdtRegisterBase=", Gdtr.Base);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] GdtRegisterLimit=", Gdtr.Limit);
    UefiSerialWriteString((LPCSTR)"[EnterProtectedPagingAndJump] Jumping to kernel\r\n");
#endif
    StubJumpToImage((U32)(&Gdtr), PagingStructure, KernelEntryLo, KernelEntryHi, MultibootInfoPtr, MULTIBOOT_BOOTLOADER_MAGIC);

    __builtin_unreachable();
}
