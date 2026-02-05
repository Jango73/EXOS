
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

#ifndef BOOT_STAGE_MARKERS
#define BOOT_STAGE_MARKERS 0
#endif

/************************************************************************/

#ifndef CONFIG_VMA_KERNEL
#error "CONFIG_VMA_KERNEL is not defined"
#endif

static const UINT MAX_KERNEL_PAGE_TABLES = 64u;
static const U32 BOOT_MARKER_BASE_X = 2u;
static const U32 BOOT_MARKER_Y_TRANSITION = 2u;
static const U32 BOOT_MARKER_GROUP_SIZE = 10u;
static const U32 BOOT_MARKER_LINE_STRIDE = 10u;
static const U32 BOOT_MARKER_SIZE = 8u;
static const U32 BOOT_MARKER_SPACING = 2u;

enum {
    BOOT_STAGE_TRANSITION_ENTRY = 16u,          // Jump of 2 after UEFI stage 14
    BOOT_STAGE_TRANSITION_FRAMEBUFFER = 17u,
    BOOT_STAGE_TRANSITION_PAGING = 18u,
    BOOT_STAGE_TRANSITION_GDT = 19u,
    BOOT_STAGE_TRANSITION_BEFORE_STUB = 20u,
    BOOT_STAGE_STUB_ENTRY = 22u,                // Jump of 2 before stub stages
    BOOT_STAGE_STUB_AFTER_CR3 = 23u,
    BOOT_STAGE_LONG_MODE_ENTRY = 25u            // Jump of 2 before long mode stage
};

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
extern U32 UefiStubKernelPhysicalBase;
U32 UefiStubFramebufferLow = 0u;
U32 UefiStubFramebufferHigh = 0u;
U32 UefiStubFramebufferPitch = 0u;
U32 UefiStubFramebufferBytesPerPixel = 0u;

/************************************************************************/

static U32 VbrScaleColorToMask(U32 Value, U32 MaskSize) {
    if (MaskSize == 0u) {
        return 0u;
    }

    if (MaskSize >= 8u) {
        return Value & 0xFFu;
    }

    U32 MaxValue = (1u << MaskSize) - 1u;
    return (Value * MaxValue) / 255u;
}

/************************************************************************/

static U32 VbrComposeFramebufferPixel(const multiboot_info_t* Info, U32 Red, U32 Green, U32 Blue) {
    if (Info == NULL || Info->framebuffer_type != MULTIBOOT_FRAMEBUFFER_RGB) {
        return 0u;
    }

    U32 Pixel = 0u;
    Pixel |= VbrScaleColorToMask(Red, Info->color_info[1]) << Info->color_info[0];
    Pixel |= VbrScaleColorToMask(Green, Info->color_info[3]) << Info->color_info[2];
    Pixel |= VbrScaleColorToMask(Blue, Info->color_info[5]) << Info->color_info[4];
    return Pixel;
}

/************************************************************************/

#if BOOT_STAGE_MARKERS == 1
static void PayloadFramebufferMarkStage(const multiboot_info_t* Info, U32 StageIndex, U32 Red, U32 Green, U32 Blue) {
    if (Info == NULL || (Info->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) == 0u) {
        return;
    }

    if (Info->framebuffer_bpp != 32u || Info->framebuffer_pitch == 0u || Info->framebuffer_addr_low == 0u) {
        return;
    }

    U64 Address = U64_Make(Info->framebuffer_addr_high, Info->framebuffer_addr_low);
    U8* Framebuffer = (U8*)(UINT)U64_Low32(Address);
    if (Framebuffer == NULL) {
        return;
    }

    U32 Pixel = VbrComposeFramebufferPixel(Info, Red, Green, Blue);
    U32 GroupIndex = StageIndex / BOOT_MARKER_GROUP_SIZE;
    U32 GroupOffset = StageIndex % BOOT_MARKER_GROUP_SIZE;
    U32 StartX = BOOT_MARKER_BASE_X + GroupOffset * (BOOT_MARKER_SIZE + BOOT_MARKER_SPACING);
    U32 StartY = BOOT_MARKER_Y_TRANSITION + GroupIndex * BOOT_MARKER_LINE_STRIDE;

    if (StartX >= Info->framebuffer_width || StartY >= Info->framebuffer_height) {
        return;
    }

    U32 DrawWidth = BOOT_MARKER_SIZE;
    U32 DrawHeight = BOOT_MARKER_SIZE;
    if (StartX + DrawWidth > Info->framebuffer_width) {
        DrawWidth = Info->framebuffer_width - StartX;
    }
    if (StartY + DrawHeight > Info->framebuffer_height) {
        DrawHeight = Info->framebuffer_height - StartY;
    }

    for (U32 Y = 0u; Y < DrawHeight; Y++) {
        U32* Row = (U32*)(Framebuffer + ((StartY + Y) * Info->framebuffer_pitch) + (StartX * 4u));
        for (U32 X = 0u; X < DrawWidth; X++) {
            Row[X] = Pixel;
        }
    }
}
#else
static void PayloadFramebufferMarkStage(const multiboot_info_t* Info, U32 StageIndex, U32 Red, U32 Green, U32 Blue) {
    UNUSED(Info);
    UNUSED(StageIndex);
    UNUSED(Red);
    UNUSED(Green);
    UNUSED(Blue);
}
#endif

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

/************************************************************************/

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

static U64 VbrGetKernelLongModeBase(void) {
    return U64_Make((U32)((CONFIG_VMA_KERNEL >> 32) & 0xFFFFFFFFu), (U32)(CONFIG_VMA_KERNEL & 0xFFFFFFFFu));
}

/************************************************************************/

static U64 VbrShiftRightU64(U64 Value, UINT Count) {
    for (UINT Index = 0u; Index < Count; ++Index) {
        Value = U64_ShiftRight1(Value);
    }
    return Value;
}

/************************************************************************/

static UINT VbrExtractU64Bits(U64 Value, UINT Shift, UINT Width) {
    U64 Shifted = VbrShiftRightU64(Value, Shift);

    if (Width >= 32u) {
        return U64_Low32(Shifted);
    }

    return U64_Low32(Shifted) & ((1u << Width) - 1u);
}

/************************************************************************/

static U64 VbrPointerToPhysical(const void* Pointer) {
    return U64_FromUINT((UINT)Pointer);
}

/************************************************************************/

static U32 VbrAlignToPage(U32 Value) {
    return (Value + (PAGE_SIZE - 1u)) & ~(PAGE_SIZE - 1u);
}

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

        LPX86_64_PAGING_ENTRY Pml4Entry = (LPX86_64_PAGING_ENTRY)(PageMapLevel4 + Pml4Index);
        LPPDPT PageDirectoryPointer = NULL;
        if (!VbrIsLongModeEntryPresent(Pml4Entry)) {
            const U64 TablePhysical = *NextTablePhysical;
            *NextTablePhysical += PAGE_TABLE_SIZE;
            PageDirectoryPointer = (LPPDPT)VbrU64ToUINT(TablePhysical);
            MemorySet(PageDirectoryPointer, 0, PAGE_TABLE_SIZE);
            SetLongModeEntry(Pml4Entry, U64_FromUINT(TablePhysical), 0u);
        } else {
            const U64 Physical = VbrEntryToPhysical(Pml4Entry);
            PageDirectoryPointer = (LPPDPT)VbrU64ToUINT(Physical);
        }

        LPX86_64_PAGING_ENTRY PdptEntry = (LPX86_64_PAGING_ENTRY)(PageDirectoryPointer + PdptIndex);
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

static void BuildPaging(
    U32 KernelPhysBase,
    U64 KernelVirtBase,
    U32 MapSize,
    U64 UefiImageBase,
    U64 UefiImageSize,
    U64 FramebufferBase,
    U64 FramebufferSize) {
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

    // Keep the loader-reserved kernel span identity-mapped because early
    // kernel code still accesses some bootstrap data through physical pointers.
    MapIdentityRange(
        U64_FromU32(KernelPhysBase),
        U64_FromU32(MapSize + BOOT_KERNEL_IDENTITY_WORKSPACE_BYTES),
        &NextTablePhysical);

    if (UefiImageSize != 0) {
        MapIdentityRange(UefiImageBase, UefiImageSize, &NextTablePhysical);
        if (FramebufferSize != 0u) {
            const U32 Pml4Index = (U32)U64_Low32(VbrShiftRightU64(FramebufferBase, 39u)) & 0x1FFu;
            if (Pml4Index == 0u) {
                MapIdentityRange(FramebufferBase, FramebufferSize, &NextTablePhysical);
            }
        }
    }
}

/************************************************************************/

#if UEFI_STUB_C_FALLBACK == 1
U64 _fltused = 0u;
U64 __fltused = 0u;

void NORETURN StubJumpToImage(
    U32 GdtRegister,
    U32 PagingStructure,
    U32 KernelEntryLow,
    U32 KernelEntryHigh,
    U32 MultibootInfoPtr,
    U32 MultibootMagic) {
    UNUSED(GdtRegister);
    UNUSED(PagingStructure);
    UNUSED(KernelEntryLow);
    UNUSED(KernelEntryHigh);
    UNUSED(MultibootMagic);

    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

/************************************************************************/
#endif

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
    U32 KernelPhysBase = UefiStubKernelPhysicalBase;
    if (KernelPhysBase == 0u) {
        KernelPhysBase = KERNEL_LINEAR_LOAD_ADDRESS;
    }
    U32 MapSize = VbrAlignToPage(FileSize + BOOT_KERNEL_MAP_PADDING_BYTES);
    U64 FramebufferBase = U64_FromU32(0u);
    U64 FramebufferSize = U64_FromU32(0u);

    if (MapSize < BOOT_X86_64_TEMP_LINEAR_REQUIRED_SPAN) {
        MapSize = BOOT_X86_64_TEMP_LINEAR_REQUIRED_SPAN;
    }

    UefiSerialWriteString((LPCSTR)"[EnterProtectedPagingAndJump] Start\r\n");
    UefiSerialWriteLabelHex64(
        (LPCSTR)"[EnterProtectedPagingAndJump] UefiImageBase=",
        UefiImageBase);
    UefiSerialWriteLabelHex64(
        (LPCSTR)"[EnterProtectedPagingAndJump] UefiImageSize=",
        UefiImageSize);

    multiboot_info_t* Info = (multiboot_info_t*)(UINT)MultibootInfoPtr;
    PayloadFramebufferMarkStage(Info, BOOT_STAGE_TRANSITION_ENTRY, 255u, 0u, 0u);
    if (Info != NULL && (Info->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) != 0u) {
        FramebufferBase = U64_Make(Info->framebuffer_addr_high, Info->framebuffer_addr_low);
        if (Info->framebuffer_pitch != 0u && Info->framebuffer_height != 0u) {
            FramebufferSize = U64_FromU32(Info->framebuffer_pitch * Info->framebuffer_height);
        }
        UefiStubFramebufferLow = Info->framebuffer_addr_low;
        UefiStubFramebufferHigh = Info->framebuffer_addr_high;
        UefiStubFramebufferPitch = Info->framebuffer_pitch;
        UefiStubFramebufferBytesPerPixel = (U32)(Info->framebuffer_bpp / 8u);
    }
    PayloadFramebufferMarkStage(Info, BOOT_STAGE_TRANSITION_FRAMEBUFFER, 255u, 128u, 0u);

    const U64 KernelVirtBase = VbrGetKernelLongModeBase();
    BuildPaging(
        KernelPhysBase,
        KernelVirtBase,
        MapSize,
        UefiImageBase,
        UefiImageSize,
        FramebufferBase,
        FramebufferSize);
    PayloadFramebufferMarkStage(Info, BOOT_STAGE_TRANSITION_PAGING, 255u, 255u, 0u);
    BuildGdtFlat();
    PayloadFramebufferMarkStage(Info, BOOT_STAGE_TRANSITION_GDT, 0u, 255u, 0u);

    UefiSerialWriteString((LPCSTR)"[EnterProtectedPagingAndJump] Paging and GDT ready\r\n");
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] KernelPhysicalBase=", KernelPhysBase);
    UefiSerialWriteLabelHex64((LPCSTR)"[EnterProtectedPagingAndJump] KernelVirtualBase=", KernelVirtBase);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] MapSize=", MapSize);

    const U32 KernelEntryLo = U64_Low32(KernelVirtBase);
    const U32 KernelEntryHi = U64_High32(KernelVirtBase);
    const U32 PagingStructure = (U32)(UINT)PageMapLevel4;

    for (volatile int Delay = 0; Delay < 100000; ++Delay) {
        __asm__ __volatile__("nop");
    }

    BootDebugPrint(TEXT("[VBR x86-64] About to jump\r\n"));

    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] KernelEntryLow=", KernelEntryLo);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] KernelEntryHigh=", KernelEntryHi);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] PagingStructure=", PagingStructure);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] MultibootInfoPointer=", MultibootInfoPtr);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] GdtRegister=", (U32)(UINT)&Gdtr);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] GdtRegisterBase=", Gdtr.Base);
    UefiSerialWriteLabelHex32((LPCSTR)"[EnterProtectedPagingAndJump] GdtRegisterLimit=", Gdtr.Limit);
    UefiSerialWriteString((LPCSTR)"[EnterProtectedPagingAndJump] Jumping to kernel\r\n");
    PayloadFramebufferMarkStage(Info, BOOT_STAGE_TRANSITION_BEFORE_STUB, 0u, 255u, 255u);
#if UEFI_STUB_REPLACE == 1
    for (;;) {
        __asm__ __volatile__("hlt");
    }
#else
    StubJumpToImage((U32)(&Gdtr), PagingStructure, KernelEntryLo, KernelEntryHi, MultibootInfoPtr, MULTIBOOT_BOOTLOADER_MAGIC);
#endif

    __builtin_unreachable();
}
