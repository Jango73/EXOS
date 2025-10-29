
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


    Intel x86-64 Architecture Support

\************************************************************************/

#include "arch/x86-64/x86-64.h"
#include "arch/x86-64/x86-64-Log.h"

#include "Console.h"
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "Stack.h"
#include "CoreString.h"
#include "System.h"
#include "Text.h"
#include "Interrupt.h"
#include "SYSCall.h"

/************************************************************************\

                              ┌──────────────────────────────────────────┐
                              │        48-bit Virtual Address            │
                              │  [ 47 ................. 0 ]              │
                              └──────────────────────────────────────────┘
                                               │
                                               ▼
    ────────────────────────────────────────────────────────────────────────────
     Step 1: PML4 (Page-Map Level-4 Table)
    ────────────────────────────────────────────────────────────────────────────
     Virtual bits [47:39] = index into the PML4 table (512 entries)
     Each PML4E → points to one Page-Directory-Pointer Table (PDPT)

            +------------------+
            | PML4 Entry (PML4E) ───► PDPT base address
            +------------------+
                     │
                     ▼
    ────────────────────────────────────────────────────────────────────────────
     Step 2: PDPT (Page-Directory-Pointer Table)
    ────────────────────────────────────────────────────────────────────────────
     Virtual bits [38:30] = index into PDPT (512 entries)
     Each PDPTE normally points to a Page Directory.
     But if bit 7 (PS) = 1 → 1 GiB *large page*.

             ┌──────────────────────────────┐
             │ PDPTE                       │
             │ ─ bit 7 (PS) = 1 → 1 GiB page│────► Physical 1 GiB page
             │ ─ bit 7 (PS) = 0 → Page Dir. │────► PD base address
             └──────────────────────────────┘
                     │
                     ▼
    ────────────────────────────────────────────────────────────────────────────
     Step 3: PD (Page Directory)
    ────────────────────────────────────────────────────────────────────────────
     Virtual bits [29:21] = index into PD (512 entries)
     Each PDE normally points to a Page Table.
     But if bit 7 (PS) = 1 → 2 MiB *large page*.

             ┌──────────────────────────────┐
             │ PDE                         │
             │ ─ bit 7 (PS) = 1 → 2 MiB page│────► Physical 2 MiB page
             │ ─ bit 7 (PS) = 0 → Page Tbl. │────► PT base address
             └──────────────────────────────┘
                     │
                     ▼
    ────────────────────────────────────────────────────────────────────────────
     Step 4: PT (Page Table)
    ────────────────────────────────────────────────────────────────────────────
     Virtual bits [20:12] = index into PT (512 entries)
     Each PTE points to a 4 KiB physical page.

             ┌──────────────────────────────┐
             │ PTE → Physical 4 KiB page    │
             └──────────────────────────────┘
                     │
                     ▼
    ────────────────────────────────────────────────────────────────────────────
     Step 5: Physical Address
    ────────────────────────────────────────────────────────────────────────────
     Offset bits [11:0] select the byte within the final page.

             Physical Address = { FrameBase[51:12], VA[11:0] }

    ────────────────────────────────────────────────────────────────────────────
     Summary of page sizes per level (4-level paging)
    ────────────────────────────────────────────────────────────────────────────

     | Level | Table name | Page size (if PS=1) | Entries | Coverage per entry |
     |--------|-------------|--------------------|----------|--------------------|
     | PML4   | PML4 table  | —                  | 512      | 512 GiB            |
     | PDPT   | PDP table   | 1 GiB (PS=1)       | 512      | 1 GiB              |
     | PD     | Page Dir.   | 2 MiB (PS=1)       | 512      | 2 MiB              |
     | PT     | Page Table  | 4 KiB              | 512      | 4 KiB              |

    ────────────────────────────────────────────────────────────────────────────
     Example:
       0x00007F12_3456_789A
       ├─[47:39]→ PML4 index
       ├─[38:30]→ PDPT index
       ├─[29:21]→ PD index
       ├─[20:12]→ PT index
       └─[11:0] → Offset inside 4 KiB page
    ────────────────────────────────────────────────────────────────────────────

\************************************************************************/

typedef enum _PAGE_TABLE_POPULATE_MODE {
    PAGE_TABLE_POPULATE_IDENTITY,
    PAGE_TABLE_POPULATE_SINGLE_ENTRY,
    PAGE_TABLE_POPULATE_EMPTY
} PAGE_TABLE_POPULATE_MODE;

#define USERLAND_SEEDED_TABLES 1u

typedef struct _PAGE_TABLE_SETUP {
    UINT DirectoryIndex;
    U32 ReadWrite;
    U32 Privilege;
    U32 Global;
    PAGE_TABLE_POPULATE_MODE Mode;
    PHYSICAL Physical;
    union {
        struct {
            PHYSICAL PhysicalBase;
            BOOL ProtectBios;
        } Identity;
        struct {
            UINT TableIndex;
            PHYSICAL Physical;
            U32 ReadWrite;
            U32 Privilege;
            U32 Global;
        } Single;
    } Data;
} PAGE_TABLE_SETUP;

typedef struct _REGION_SETUP {
    LPCSTR Label;
    UINT PdptIndex;
    U32 ReadWrite;
    U32 Privilege;
    U32 Global;
    PHYSICAL PdptPhysical;
    PHYSICAL DirectoryPhysical;
    PAGE_TABLE_SETUP Tables[64];
    UINT TableCount;
} REGION_SETUP;

/************************************************************************/

typedef struct _LOW_REGION_SHARED_TABLES {
    PHYSICAL BiosTablePhysical;
    PHYSICAL IdentityTablePhysical;
} LOW_REGION_SHARED_TABLES;

static LOW_REGION_SHARED_TABLES LowRegionSharedTables = {
    .BiosTablePhysical = NULL,
    .IdentityTablePhysical = NULL,
};

/************************************************************************/

static BOOL EnsureSharedLowTable(
    PHYSICAL* TablePhysical,
    PHYSICAL PhysicalBase,
    BOOL ProtectBios,
    LPCSTR Label) {

    if (TablePhysical == NULL || Label == NULL) {
        ERROR(TEXT("[SetupLowRegion] Invalid shared table parameters"));
        return FALSE;
    }

    if (*TablePhysical != NULL) {
        DEBUG(TEXT("[SetupLowRegion] Reusing shared %s table at %p"), Label, *TablePhysical);
        return TRUE;
    }

    PHYSICAL Physical = AllocPhysicalPage();

    if (Physical == NULL) {
        ERROR(TEXT("[SetupLowRegion] Out of physical pages for shared %s table"), Label);
        return FALSE;
    }

    LINEAR Linear = MapTemporaryPhysicalPage3(Physical);

    if (Linear == NULL) {
        ERROR(TEXT("[SetupLowRegion] MapTemporaryPhysicalPage3 failed for shared %s table"), Label);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    LPPAGE_TABLE Table = (LPPAGE_TABLE)Linear;
    MemorySet(Table, 0, PAGE_SIZE);

#if !defined(PROTECT_BIOS)
    UNUSED(ProtectBios);
#endif

    for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL EntryPhysical = PhysicalBase + ((PHYSICAL)Index << PAGE_SIZE_MUL);

#ifdef PROTECT_BIOS
        if (ProtectBios) {
            BOOL Protected =
                (EntryPhysical == 0) || (EntryPhysical > PROTECTED_ZONE_START && EntryPhysical <= PROTECTED_ZONE_END);

            if (Protected) {
                ClearPageTableEntry(Table, Index);
                continue;
            }
        }
#endif

        WritePageTableEntryValue(
            Table,
            Index,
            MakePageTableEntryValue(
                EntryPhysical,
                /*ReadWrite*/ 1,
                PAGE_PRIVILEGE_KERNEL,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                /*Global*/ 0,
                /*Fixed*/ 1));
    }

    *TablePhysical = Physical;

    DEBUG(TEXT("[SetupLowRegion] Shared %s table prepared at %p (base %p)"), Label, Physical, PhysicalBase);

    return TRUE;
}

/************************************************************************/

KERNELDATA_X86_64 SECTION(".data") Kernel_i386 = {
    .IDT = NULL,
    .GDT = NULL,
    .TSS = NULL,
};

extern void Interrupt_SystemCall(void);

/**
 * @brief Read a 64-bit value from the specified MSR.
 * @param Msr Model-specific register index to read.
 * @return Combined 64-bit value of the MSR contents.
 */
static U64 ReadMSR64Local(U32 Msr) {
    U32 Low;
    U32 High;

    __asm__ volatile ("rdmsr" : "=a"(Low), "=d"(High) : "c"(Msr));

    return (((U64)High) << 32) | (U64)Low;
}

/************************************************************************/

/**
 * @brief Set the handler address for a 64-bit IDT gate descriptor.
 * @param Descriptor IDT entry to update.
 * @param Handler Linear address of the interrupt handler.
 */
void SetGateDescriptorOffset(LPGATE_DESCRIPTOR Descriptor, LINEAR Handler) {
    U64 Offset = (U64)Handler;

    Descriptor->Offset_00_15 = (U16)(Offset & 0x0000FFFF);
    Descriptor->Offset_16_31 = (U16)((Offset >> 16) & 0x0000FFFF);
    Descriptor->Offset_32_63 = (U32)((Offset >> 32) & 0xFFFFFFFF);
    Descriptor->Reserved_2 = 0;
}

/************************************************************************/

/**
 * @brief Initialize a 64-bit IDT gate descriptor.
 * @param Descriptor IDT entry to configure.
 * @param Handler Linear address of the interrupt handler.
 * @param Type Gate type to install.
 * @param Privilege Descriptor privilege level.
 */
void InitializeGateDescriptor(
    LPGATE_DESCRIPTOR Descriptor,
    LINEAR Handler,
    U16 Type,
    U16 Privilege,
    U8 InterruptStackTable) {
    Descriptor->Selector = SELECTOR_KERNEL_CODE;
    Descriptor->InterruptStackTable = InterruptStackTable & 0x7u;
    Descriptor->Reserved_0 = 0;
    Descriptor->Type = Type;
    Descriptor->Privilege = Privilege;
    Descriptor->Present = 1;
    Descriptor->Reserved_1 = 0;

    SetGateDescriptorOffset(Descriptor, Handler);
}

/***************************************************************************/

extern GATE_DESCRIPTOR IDT[];
extern VOIDFUNC InterruptTable[];

/***************************************************************************/

static U8 SelectInterruptStackTable(U32 InterruptIndex) {
    switch (InterruptIndex) {
    case 8u:   // Double fault
    case 10u:  // Invalid TSS
    case 11u:  // Segment not present
    case 12u:  // Stack fault
    case 13u:  // General protection fault
    case 14u:  // Page fault
        return 1u;
    default:
        return 0u;
    }
}

/***************************************************************************/

void InitializeInterrupts(void) {
    Kernel_i386.IDT = IDT;

    for (U32 Index = 0; Index < NUM_INTERRUPTS; Index++) {
        U8 InterruptStack = SelectInterruptStackTable(Index);

        InitializeGateDescriptor(
            IDT + Index,
            (LINEAR)(InterruptTable[Index]),
            GATE_TYPE_386_INT,
            PRIVILEGE_KERNEL,
            InterruptStack);
    }

    InitializeSystemCall();

    LoadInterruptDescriptorTable((LINEAR)IDT, IDT_SIZE - 1u);

    ClearDR7();

    InitializeSystemCallTable();
}

/************************************************************************/

/**
 * @brief Populate the limit fields of a system segment descriptor.
 * @param Descriptor Descriptor to update.
 * @param Limit Segment limit value encoded on 20 bits.
 */
static void SetSystemSegmentDescriptorLimit(LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR Descriptor, U32 Limit) {
    Descriptor->Limit_00_15 = (U16)(Limit & 0xFFFF);
    Descriptor->Limit_16_19 = (U8)((Limit >> 16) & 0x0F);
}

/************************************************************************/

/**
 * @brief Populate the base fields of a system segment descriptor.
 * @param Descriptor Descriptor to update.
 * @param Base 64-bit base address of the segment.
 */
static void SetSystemSegmentDescriptorBase(LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR Descriptor, U64 Base) {
    Descriptor->Base_00_15 = (U16)(Base & 0xFFFF);
    Descriptor->Base_16_23 = (U8)((Base >> 16) & 0xFF);
    Descriptor->Base_24_31 = (U8)((Base >> 24) & 0xFF);
    Descriptor->Base_32_63 = (U32)((Base >> 32) & 0xFFFFFFFF);
}

/************************************************************************/

/**
 * @brief Clear a REGION_SETUP structure to its default state.
 * @param Region Structure to reset.
 */
static void ResetRegionSetup(REGION_SETUP* Region) {
    MemorySet(Region, 0, sizeof(REGION_SETUP));
}

/************************************************************************/

/**
 * @brief Release the physical resources owned by a REGION_SETUP.
 * @param Region Structure that tracks the allocated tables.
 */
static void ReleaseRegionSetup(REGION_SETUP* Region) {
    if (Region->PdptPhysical != NULL) {
        FreePhysicalPage(Region->PdptPhysical);
        Region->PdptPhysical = NULL;
    }

    if (Region->DirectoryPhysical != NULL) {
        FreePhysicalPage(Region->DirectoryPhysical);
        Region->DirectoryPhysical = NULL;
    }

    for (UINT Index = 0; Index < Region->TableCount; Index++) {
        if (Region->Tables[Index].Physical != NULL) {
            FreePhysicalPage(Region->Tables[Index].Physical);
            Region->Tables[Index].Physical = NULL;
        }
    }

    Region->TableCount = 0;
}

/************************************************************************/

/**
 * @brief Allocate a page table and populate it according to the setup entry.
 * @param Region Parent region that will own the table.
 * @param Table Table description containing allocation parameters.
 * @param Directory Page-directory view used to link the table.
 * @return TRUE on success, FALSE when allocation or mapping fails.
 */
static BOOL AllocateTableAndPopulate(
    REGION_SETUP* Region,
    PAGE_TABLE_SETUP* Table,
    LPPAGE_DIRECTORY Directory) {

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] begin"), Region->Label, Table->DirectoryIndex);

    Table->Physical = AllocPhysicalPage();

    if (Table->Physical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] %s region out of physical pages"), Region->Label);
        return FALSE;
    }

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] physical %p mode %u"),
        Region->Label,
        Table->DirectoryIndex,
        Table->Physical,
        (UINT)Table->Mode);

    LINEAR TableLinear = MapTemporaryPhysicalPage3(Table->Physical);

    if (TableLinear == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage3 failed for %s table"), Region->Label);
        FreePhysicalPage(Table->Physical);
        Table->Physical = NULL;
        return FALSE;
    }

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] mapped at %p"),
        Region->Label,
        Table->DirectoryIndex,
        TableLinear);

    LPPAGE_TABLE TableVA = (LPPAGE_TABLE)TableLinear;
    MemorySet(TableVA, 0, PAGE_SIZE);

    switch (Table->Mode) {
    case PAGE_TABLE_POPULATE_IDENTITY:
        for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
            PHYSICAL Physical = Table->Data.Identity.PhysicalBase + ((PHYSICAL)Index << PAGE_SIZE_MUL);

#ifdef PROTECT_BIOS
            if (Table->Data.Identity.ProtectBios) {
                BOOL Protected =
                    (Physical == 0) || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);

                if (Protected) {
                    ClearPageTableEntry(TableVA, Index);
                    continue;
                }
            }
#endif

            WritePageTableEntryValue(
                TableVA,
                Index,
                MakePageTableEntryValue(
                    Physical,
                    Table->ReadWrite,
                    Table->Privilege,
                    /*WriteThrough*/ 0,
                    /*CacheDisabled*/ 0,
                    Table->Global,
                    /*Fixed*/ 1));
        }
        break;

    case PAGE_TABLE_POPULATE_SINGLE_ENTRY:
        WritePageTableEntryValue(
            TableVA,
            Table->Data.Single.TableIndex,
            MakePageTableEntryValue(
                Table->Data.Single.Physical,
                Table->Data.Single.ReadWrite,
                Table->Data.Single.Privilege,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                Table->Data.Single.Global,
                /*Fixed*/ 1));
        break;

    case PAGE_TABLE_POPULATE_EMPTY:
    default:
        break;
    }

    WritePageDirectoryEntryValue(
        Directory,
        Table->DirectoryIndex,
        MakePageDirectoryEntryValue(
            Table->Physical,
            Table->ReadWrite,
            Table->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Table->Global,
            /*Fixed*/ 1));

    U64 DirectoryEntryValue = ReadPageDirectoryEntryValue(Directory, Table->DirectoryIndex);
    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] entry value=%p"),
        Region->Label,
        Table->DirectoryIndex,
        (LINEAR)DirectoryEntryValue);

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] table ready at %p"),
        Region->Label,
        Table->DirectoryIndex,
        Table->Physical);

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] complete"), Region->Label, Table->DirectoryIndex);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Build identity-mapped tables for the low virtual address space.
 * @param Region Region descriptor to populate.
 * @param UserSeedTables Number of empty user tables to pre-allocate.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL SetupLowRegion(REGION_SETUP* Region, UINT UserSeedTables) {
    ResetRegionSetup(Region);

    Region->Label = TEXT("Low");
    Region->PdptIndex = GetPdptEntry(0);
    Region->ReadWrite = 1;
    Region->Privilege = (UserSeedTables != 0u) ? PAGE_PRIVILEGE_USER : PAGE_PRIVILEGE_KERNEL;
    Region->Global = 0;

    DEBUG(TEXT("[SetupLowRegion] Config PdptIndex=%u Privilege=%u UserSeedTables=%u"),
        Region->PdptIndex,
        Region->Privilege,
        UserSeedTables);

    if (EnsureSharedLowTable(&LowRegionSharedTables.BiosTablePhysical, 0, TRUE, TEXT("BIOS")) == FALSE) {
        return FALSE;
    }

    if (EnsureSharedLowTable(
            &LowRegionSharedTables.IdentityTablePhysical,
            ((PHYSICAL)PAGE_TABLE_NUM_ENTRIES << PAGE_SIZE_MUL),
            FALSE,
            TEXT("low identity")) == FALSE) {
        return FALSE;
    }

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();

    DEBUG(TEXT("[SetupLowRegion] PDPT %p, directory %p"), Region->PdptPhysical, Region->DirectoryPhysical);

    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Low region out of physical pages"));
        if (Region->PdptPhysical != NULL) {
            FreePhysicalPage(Region->PdptPhysical);
            Region->PdptPhysical = NULL;
        }
        if (Region->DirectoryPhysical != NULL) {
            FreePhysicalPage(Region->DirectoryPhysical);
            Region->DirectoryPhysical = NULL;
        }
        return FALSE;
    }

    LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(Region->PdptPhysical);

    if (Pdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed for low PDPT"));
        return FALSE;
    }

    DEBUG(TEXT("[SetupLowRegion] PDPT mapped at %p"), Pdpt);

    MemorySet(Pdpt, 0, PAGE_SIZE);

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(Region->DirectoryPhysical);

    if (Directory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed for low directory"));
        return FALSE;
    }

    DEBUG(TEXT("[SetupLowRegion] Directory mapped at %p"), Directory);

    MemorySet(Directory, 0, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        Pdpt,
        Region->PdptIndex,
        MakePageDirectoryEntryValue(
            Region->DirectoryPhysical,
            Region->ReadWrite,
            Region->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Region->Global,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupLowRegion] PDPT[%u] -> %p"), Region->PdptIndex, Region->DirectoryPhysical);

    UINT LowDirectoryIndex = GetDirectoryEntry(0);

    WritePageDirectoryEntryValue(
        Directory,
        LowDirectoryIndex,
        MakePageDirectoryEntryValue(
            LowRegionSharedTables.BiosTablePhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupLowRegion] Directory[%u] -> shared BIOS table %p"),
        LowDirectoryIndex,
        LowRegionSharedTables.BiosTablePhysical);

    WritePageDirectoryEntryValue(
        Directory,
        LowDirectoryIndex + 1u,
        MakePageDirectoryEntryValue(
            LowRegionSharedTables.IdentityTablePhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupLowRegion] Directory[%u] -> shared identity table %p"),
        LowDirectoryIndex + 1u,
        LowRegionSharedTables.IdentityTablePhysical);

    if (UserSeedTables != 0u) {
        UINT TableCapacity = (UINT)(sizeof(Region->Tables) / sizeof(Region->Tables[0]));
        DEBUG(TEXT("[SetupLowRegion] User seed request=%u current=%u capacity=%u region=%p tables=%p"),
            UserSeedTables,
            Region->TableCount,
            TableCapacity,
            Region,
            Region->Tables);

        UINT BaseDirectory = GetDirectoryEntry((U64)VMA_USER);

        for (UINT Index = 0; Index < UserSeedTables; Index++) {
            if (Region->TableCount >= TableCapacity) {
                ERROR(TEXT("[SetupLowRegion] User seed table overflow index=%u count=%u capacity=%u"),
                    Index,
                    Region->TableCount,
                    TableCapacity);
                return FALSE;
            }

            PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];
            DEBUG(TEXT("[SetupLowRegion] Seeding idx=%u count=%u table=%p base=%u"),
                Index,
                Region->TableCount,
                Table,
                BaseDirectory);

            Table->DirectoryIndex = BaseDirectory + Index;
            Table->ReadWrite = 1;
            Table->Privilege = PAGE_PRIVILEGE_USER;
            Table->Global = 0;
            Table->Mode = PAGE_TABLE_POPULATE_EMPTY;
            DEBUG(TEXT("[SetupLowRegion] Preparing user seed table slot=%u"), Table->DirectoryIndex);
            if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) return FALSE;
            DEBUG(TEXT("[SetupLowRegion] Seed slot=%u populated physical=%p"),
                Table->DirectoryIndex,
                Table->Physical);
            Region->TableCount++;
            DEBUG(TEXT("[SetupLowRegion] Table count advanced to %u"), Region->TableCount);
        }
    }

    DEBUG(TEXT("[SetupLowRegion] Completed table count %u (shared bios=%p identity=%p)"),
        Region->TableCount,
        LowRegionSharedTables.BiosTablePhysical,
        LowRegionSharedTables.IdentityTablePhysical);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Compute the number of bytes of kernel memory that must be mapped.
 * @return Size in bytes covered by kernel tables.
 */
static UINT ComputeKernelCoverageBytes(void) {
    PHYSICAL PhysBaseKernel = KernelStartup.KernelPhysicalBase;
    PHYSICAL CoverageEnd = PhysBaseKernel + (PHYSICAL)KernelStartup.KernelSize;

    if (KernelStartup.StackTop > CoverageEnd) {
        CoverageEnd = KernelStartup.StackTop;
    }

    if (CoverageEnd <= PhysBaseKernel) {
        return PAGE_TABLE_CAPACITY;
    }

    PHYSICAL Coverage = CoverageEnd - PhysBaseKernel;
    UINT CoverageBytes = (UINT)PAGE_ALIGN((UINT)Coverage);

    if (CoverageBytes < PAGE_TABLE_CAPACITY) {
        CoverageBytes = PAGE_TABLE_CAPACITY;
    }

    return CoverageBytes;
}

/************************************************************************/

/**
 * @brief Create identity mappings for the kernel virtual address space.
 * @param Region Region descriptor to populate.
 * @param TableCountRequired Number of tables that must be allocated.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL SetupKernelRegion(REGION_SETUP* Region, UINT TableCountRequired) {
    ResetRegionSetup(Region);

    Region->Label = TEXT("Kernel");
    Region->PdptIndex = GetPdptEntry((U64)VMA_KERNEL);
    Region->ReadWrite = 1;
    Region->Privilege = PAGE_PRIVILEGE_KERNEL;
    Region->Global = 0;

    if (TableCountRequired > ARRAY_COUNT(Region->Tables)) {
        ERROR(TEXT("[AllocPageDirectory] Kernel region requires too many tables"));
        return FALSE;
    }

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();

    DEBUG(TEXT("[SetupKernelRegion] PDPT %p, directory %p"), Region->PdptPhysical, Region->DirectoryPhysical);

    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Kernel region out of physical pages"));
        return FALSE;
    }

    LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(Region->PdptPhysical);

    if (Pdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed for kernel PDPT"));
        return FALSE;
    }

    MemorySet(Pdpt, 0, PAGE_SIZE);

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(Region->DirectoryPhysical);

    if (Directory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed for kernel directory"));
        return FALSE;
    }

    MemorySet(Directory, 0, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        Pdpt,
        Region->PdptIndex,
        MakePageDirectoryEntryValue(
            Region->DirectoryPhysical,
            Region->ReadWrite,
            Region->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Region->Global,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupKernelRegion] PDPT[%u] -> %p"), Region->PdptIndex, Region->DirectoryPhysical);

    UINT DirectoryIndex = GetDirectoryEntry((U64)VMA_KERNEL);
    PHYSICAL PhysBaseKernel = KernelStartup.KernelPhysicalBase;

    for (UINT TableIndex = 0; TableIndex < TableCountRequired; TableIndex++) {
        PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];
        Table->DirectoryIndex = DirectoryIndex + TableIndex;
        Table->ReadWrite = 1;
        Table->Privilege = PAGE_PRIVILEGE_KERNEL;
        Table->Global = 0;
        Table->Mode = PAGE_TABLE_POPULATE_IDENTITY;
        Table->Data.Identity.PhysicalBase = PhysBaseKernel + ((PHYSICAL)TableIndex << PAGE_TABLE_CAPACITY_MUL);
        Table->Data.Identity.ProtectBios = FALSE;

        if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) {
            return FALSE;
        }
        Region->TableCount++;
    }

    DEBUG(TEXT("[SetupKernelRegion] Completed table count %u"), Region->TableCount);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Map the user-mode task runner trampoline into the new address space.
 * @param Region Region descriptor to populate.
 * @param TaskRunnerPhysical Physical address of the task runner code.
 * @param TaskRunnerTableIndex Page table index that contains the trampoline.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL SetupTaskRunnerRegion(
    REGION_SETUP* Region,
    PHYSICAL TaskRunnerPhysical,
    UINT TaskRunnerTableIndex) {
    ResetRegionSetup(Region);

    Region->Label = TEXT("TaskRunner");
    Region->PdptIndex = GetPdptEntry((U64)VMA_TASK_RUNNER);
    Region->ReadWrite = 1;
    Region->Privilege = PAGE_PRIVILEGE_USER;
    Region->Global = 0;

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();

    DEBUG(TEXT("[SetupTaskRunnerRegion] PDPT %p, directory %p"), Region->PdptPhysical, Region->DirectoryPhysical);

    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] TaskRunner region out of physical pages"));
        return FALSE;
    }

    LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(Region->PdptPhysical);

    if (Pdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed for TaskRunner PDPT"));
        return FALSE;
    }

    MemorySet(Pdpt, 0, PAGE_SIZE);

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(Region->DirectoryPhysical);

    if (Directory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed for TaskRunner directory"));
        return FALSE;
    }

    MemorySet(Directory, 0, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        Pdpt,
        Region->PdptIndex,
        MakePageDirectoryEntryValue(
            Region->DirectoryPhysical,
            Region->ReadWrite,
            Region->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Region->Global,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupTaskRunnerRegion] PDPT[%u] -> %p"), Region->PdptIndex, Region->DirectoryPhysical);

    PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];
    Table->DirectoryIndex = GetDirectoryEntry((U64)VMA_TASK_RUNNER);
    Table->ReadWrite = 1;
    Table->Privilege = PAGE_PRIVILEGE_USER;
    Table->Global = 0;
    Table->Mode = PAGE_TABLE_POPULATE_SINGLE_ENTRY;
    Table->Data.Single.TableIndex = TaskRunnerTableIndex;
    Table->Data.Single.Physical = TaskRunnerPhysical;
    Table->Data.Single.ReadWrite = 0;
    Table->Data.Single.Privilege = PAGE_PRIVILEGE_USER;
    Table->Data.Single.Global = 0;

    if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) {
        return FALSE;
    }

    Region->TableCount++;
    DEBUG(TEXT("[SetupTaskRunnerRegion] Completed table count %u"), Region->TableCount);
    return TRUE;
}

/************************************************************************/

/*
static U64 ReadTableEntrySnapshot(PHYSICAL TablePhysical, UINT Index) {
    if (TablePhysical == NULL) {
        return 0;
    }

    LINEAR Linear = MapTemporaryPhysicalPage3(TablePhysical);

    if (Linear == NULL) {
        return 0;
    }

    return ReadPageTableEntryValue((LPPAGE_TABLE)Linear, Index);
}
*/

/************************************************************************/

/**
 * @brief Allocate a new page directory.
 * @return Physical address of the page directory or NULL on failure.
 */
PHYSICAL AllocPageDirectory(void) {
    REGION_SETUP LowRegion;
    REGION_SETUP KernelRegion;
    REGION_SETUP TaskRunnerRegion;
    PHYSICAL Pml4Physical = NULL;
    BOOL Success = FALSE;

    DEBUG(TEXT("[AllocPageDirectory] Enter"));

    if (EnsureCurrentStackSpace(N_32KB) == FALSE) {
        ERROR(TEXT("[AllocPageDirectory] Unable to ensure stack availability"));
        return NULL;
    }

    ResetRegionSetup(&LowRegion);
    ResetRegionSetup(&KernelRegion);
    ResetRegionSetup(&TaskRunnerRegion);

    UINT LowPml4Index = GetPml4Entry(0);
    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);
    UINT TaskRunnerPml4Index = GetPml4Entry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerTableIndex = GetTableEntry((U64)VMA_TASK_RUNNER);

    UINT KernelCoverageBytes = ComputeKernelCoverageBytes();
    UINT KernelTableCount = KernelCoverageBytes >> PAGE_TABLE_CAPACITY_MUL;
    if (KernelTableCount == 0u) KernelTableCount = 1u;

    if (SetupLowRegion(&LowRegion, 0u) == FALSE) goto Out;
    DEBUG(TEXT("[AllocPageDirectory] Low region tables=%u"), LowRegion.TableCount);

    if (SetupKernelRegion(&KernelRegion, KernelTableCount) == FALSE) goto Out;
    DEBUG(TEXT("[AllocPageDirectory] Kernel region tables=%u"), KernelRegion.TableCount);

    LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = KernelToPhysical(TaskRunnerLinear);

    DEBUG(TEXT("[AllocPageDirectory] TaskRunnerPhysical = %p + (%p - %p) = %p"),
        KernelStartup.KernelPhysicalBase,
        TaskRunnerLinear,
        VMA_KERNEL,
        TaskRunnerPhysical);

    if (SetupTaskRunnerRegion(&TaskRunnerRegion, TaskRunnerPhysical, TaskRunnerTableIndex) == FALSE) goto Out;
    DEBUG(TEXT("[AllocPageDirectory] TaskRunner tables=%u"), TaskRunnerRegion.TableCount);

    Pml4Physical = AllocPhysicalPage();

    if (Pml4Physical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Out of physical pages"));
        goto Out;
    }

    LINEAR Pml4Linear = MapTemporaryPhysicalPage1(Pml4Physical);

    if (Pml4Linear == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed on PML4"));
        goto Out;
    }

    LPPAGE_DIRECTORY Pml4 = (LPPAGE_DIRECTORY)Pml4Linear;
    MemorySet(Pml4, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] PML4 mapped at %p"), Pml4);

    WritePageDirectoryEntryValue(
        Pml4,
        LowPml4Index,
        MakePageDirectoryEntryValue(
            LowRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            LowRegion.Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Pml4,
        KernelPml4Index,
        MakePageDirectoryEntryValue(
            KernelRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Pml4,
        TaskRunnerPml4Index,
        MakePageDirectoryEntryValue(
            TaskRunnerRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Pml4,
        PML4_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            Pml4Physical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    U64 LowEntry = ReadPageDirectoryEntryValue(Pml4, LowPml4Index);
    U64 KernelEntry = ReadPageDirectoryEntryValue(Pml4, KernelPml4Index);
    U64 TaskEntry = ReadPageDirectoryEntryValue(Pml4, TaskRunnerPml4Index);
    U64 RecursiveEntry = ReadPageDirectoryEntryValue(Pml4, PML4_RECURSIVE_SLOT);

    DEBUG(TEXT("[AllocPageDirectory] PML4 entries set (low=%p, kernel=%p, task=%p, recursive=%p)"),
        (LINEAR)LowEntry,
        (LINEAR)KernelEntry,
        (LINEAR)TaskEntry,
        (LINEAR)RecursiveEntry);

    FlushTLB();

    Success = TRUE;

Out:
    if (!Success) {
        if (Pml4Physical != NULL) {
            FreePhysicalPage(Pml4Physical);
        }
        ReleaseRegionSetup(&LowRegion);
        ReleaseRegionSetup(&KernelRegion);
        ReleaseRegionSetup(&TaskRunnerRegion);
        return NULL;
    }

    DEBUG(TEXT("[AllocPageDirectory] Exit"));
    return Pml4Physical;
}

/************************************************************************/

/**
 * @brief Allocate a new page directory for userland processes.
 * @return Physical address of the page directory or NULL on failure.
 */
PHYSICAL AllocUserPageDirectory(void) {
    REGION_SETUP LowRegion;
    REGION_SETUP KernelRegion;
    REGION_SETUP TaskRunnerRegion;
    PHYSICAL Pml4Physical = NULL;
    BOOL Success = FALSE;
    BOOL TaskRunnerReused = FALSE;

    DEBUG(TEXT("[AllocUserPageDirectory] Enter"));

    if (EnsureCurrentStackSpace(N_32KB) == FALSE) {
        ERROR(TEXT("[AllocUserPageDirectory] Unable to ensure stack availability"));
        return NULL;
    }

    ResetRegionSetup(&LowRegion);
    ResetRegionSetup(&KernelRegion);
    ResetRegionSetup(&TaskRunnerRegion);

    UINT LowPml4Index = GetPml4Entry(0);
    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);
    UINT TaskRunnerPml4Index = GetPml4Entry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerTableIndex = GetTableEntry((U64)VMA_TASK_RUNNER);

    if (SetupLowRegion(&LowRegion, USERLAND_SEEDED_TABLES) == FALSE) goto Out;
    DEBUG(TEXT("[AllocUserPageDirectory] Low region tables=%u"), LowRegion.TableCount);

    Pml4Physical = AllocPhysicalPage();

    if (Pml4Physical == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] Out of physical pages"));
        goto Out;
    }

    LINEAR Pml4Linear = MapTemporaryPhysicalPage1(Pml4Physical);

    if (Pml4Linear == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] MapTemporaryPhysicalPage1 failed on PML4"));
        goto Out;
    }

    LPPAGE_DIRECTORY Pml4 = (LPPAGE_DIRECTORY)Pml4Linear;
    MemorySet(Pml4, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocUserPageDirectory] PML4 mapped at %p"), Pml4);

    LPPML4 CurrentPml4 = GetCurrentPml4VA();
    if (CurrentPml4 == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] Current PML4 pointer is NULL"));
        goto Out;
    }

    WritePageDirectoryEntryValue(
        Pml4,
        LowPml4Index,
        MakePageDirectoryEntryValue(
            LowRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            LowRegion.Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    UINT KernelBaseIndex = PML4_ENTRY_COUNT / 2u;
    UINT ClonedKernelEntries = 0u;
    for (UINT Index = KernelBaseIndex; Index < PML4_ENTRY_COUNT; Index++) {
        if (Index == PML4_RECURSIVE_SLOT) continue;

        U64 EntryValue = ReadPageDirectoryEntryValue(CurrentPml4, Index);
        if ((EntryValue & PAGE_FLAG_PRESENT) == 0) continue;

        WritePageDirectoryEntryValue(Pml4, Index, EntryValue);
        ClonedKernelEntries++;
    }

    if (ClonedKernelEntries == 0u) {
        ERROR(TEXT("[AllocUserPageDirectory] No kernel PML4 entries copied from current directory"));
        goto Out;
    }

    DEBUG(TEXT("[AllocUserPageDirectory] Cloned %u kernel PML4 entries from index %u"),
        ClonedKernelEntries,
        KernelBaseIndex);

    U64 TaskRunnerEntryValue = ReadPageDirectoryEntryValue(CurrentPml4, TaskRunnerPml4Index);
    if ((TaskRunnerEntryValue & PAGE_FLAG_PRESENT) != 0 && (TaskRunnerEntryValue & PAGE_FLAG_USER) != 0) {
        TaskRunnerReused = TRUE;
        DEBUG(TEXT("[AllocUserPageDirectory] Reusing existing task runner entry %p from current CR3"),
            (LINEAR)TaskRunnerEntryValue);
    } else {
        LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
        PHYSICAL TaskRunnerPhysical = KernelToPhysical(TaskRunnerLinear);

        DEBUG(TEXT("[AllocUserPageDirectory] TaskRunnerPhysical = %p + (%p - %p) = %p"),
            KernelStartup.KernelPhysicalBase,
            TaskRunnerLinear,
            VMA_KERNEL,
            TaskRunnerPhysical);

        if (SetupTaskRunnerRegion(&TaskRunnerRegion, TaskRunnerPhysical, TaskRunnerTableIndex) == FALSE) goto Out;
        DEBUG(TEXT("[AllocUserPageDirectory] TaskRunner tables=%u"), TaskRunnerRegion.TableCount);
        DEBUG(TEXT("[AllocUserPageDirectory] Regions low(pdpt=%p dir=%p priv=%u tables=%u) kernel(reuse existing) task(pdpt=%p dir=%p)"),
            LowRegion.PdptPhysical,
            LowRegion.DirectoryPhysical,
            LowRegion.Privilege,
            LowRegion.TableCount,
            TaskRunnerRegion.PdptPhysical,
            TaskRunnerRegion.DirectoryPhysical);

        TaskRunnerEntryValue = MakePageDirectoryEntryValue(
            TaskRunnerRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1);
    }

    WritePageDirectoryEntryValue(Pml4, TaskRunnerPml4Index, TaskRunnerEntryValue);

    if (!TaskRunnerReused) {
        LINEAR TaskRunnerDirectoryLinear = MapTemporaryPhysicalPage2(TaskRunnerRegion.DirectoryPhysical);
        LINEAR TaskRunnerTableLinear = MapTemporaryPhysicalPage3(TaskRunnerRegion.Tables[0].Physical);

        if (TaskRunnerDirectoryLinear != NULL && TaskRunnerTableLinear != NULL) {
            UINT TaskRunnerDirectoryIndex = GetDirectoryEntry((U64)VMA_TASK_RUNNER);
            U64 TaskDirectoryEntry =
                ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)TaskRunnerDirectoryLinear, TaskRunnerDirectoryIndex);
            U64 TaskTableEntry = ReadPageTableEntryValue((LPPAGE_TABLE)TaskRunnerTableLinear, TaskRunnerTableIndex);

            DEBUG(TEXT("[AllocUserPageDirectory] TaskRunner PDE[%u]=%p PTE[%u]=%p"),
                TaskRunnerDirectoryIndex,
                (LINEAR)TaskDirectoryEntry,
                TaskRunnerTableIndex,
                (LINEAR)TaskTableEntry);
        } else {
            ERROR(TEXT("[AllocUserPageDirectory] Unable to map TaskRunner directory/table snapshot"));
        }
    } else {
        DEBUG(TEXT("[AllocUserPageDirectory] Task runner entry reused without rebuilding tables"));
    }

    WritePageDirectoryEntryValue(
        Pml4,
        PML4_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            Pml4Physical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    U64 LowEntry = ReadPageDirectoryEntryValue(Pml4, LowPml4Index);
    U64 KernelEntry = ReadPageDirectoryEntryValue(Pml4, KernelPml4Index);
    U64 TaskEntry = ReadPageDirectoryEntryValue(Pml4, TaskRunnerPml4Index);
    U64 RecursiveEntry = ReadPageDirectoryEntryValue(Pml4, PML4_RECURSIVE_SLOT);

    DEBUG(TEXT("[AllocUserPageDirectory] PML4 entries set (low=%p, kernel=%p, task=%p, recursive=%p)"),
        (LINEAR)LowEntry,
        (LINEAR)KernelEntry,
        (LINEAR)TaskEntry,
        (LINEAR)RecursiveEntry);

    LogPageDirectory64(Pml4Physical);

    FlushTLB();

    Success = TRUE;

Out:
    if (!Success) {
        if (Pml4Physical != NULL) {
            FreePhysicalPage(Pml4Physical);
        }
        ReleaseRegionSetup(&LowRegion);
        ReleaseRegionSetup(&KernelRegion);
        ReleaseRegionSetup(&TaskRunnerRegion);
        return NULL;
    }

    DEBUG(TEXT("[AllocUserPageDirectory] Exit"));
    return Pml4Physical;
}

/************************************************************************/

/**
 * @brief Initialize a long mode segment descriptor for code or data.
 * @param This Descriptor to configure.
 * @param Executable TRUE for a code segment, FALSE for data.
 * @param Privilege Descriptor privilege level.
 */
static void InitLongModeSegmentDescriptor(LPSEGMENT_DESCRIPTOR This, BOOL Executable, U32 Privilege) {
    MemorySet(This, 0, sizeof(SEGMENT_DESCRIPTOR));

    This->Limit_00_15 = 0xFFFF;
    This->Base_00_15 = 0x0000;
    This->Base_16_23 = 0x00;
    This->Accessed = 0;
    This->CanWrite = 1;
    This->ConformExpand = 0;
    This->Code = Executable;
    This->S = 1;
    This->Privilege = Privilege;
    This->Present = 1;
    This->Limit_16_19 = 0x0F;
    This->Available = 0;
    This->LongMode = 1;
    This->DefaultSize = 0;
    This->Granularity = 1;
    This->Base_24_31 = 0x00;
}

/***************************************************************************/

/**
 * @brief Initialize a 32-bit legacy segment descriptor for compatibility gates.
 * @param This Descriptor to configure.
 * @param Executable TRUE for a code segment, FALSE for data.
 */
static void InitLegacySegmentDescriptor(LPSEGMENT_DESCRIPTOR This, BOOL Executable) {
    MemorySet(This, 0, sizeof(SEGMENT_DESCRIPTOR));

    This->Limit_00_15 = 0xFFFF;
    This->Limit_16_19 = 0x0F;
    This->Base_00_15 = 0x0000;
    This->Base_16_23 = 0x00;
    This->Base_24_31 = 0x00;
    This->Accessed = 0;
    This->CanWrite = 1;
    This->ConformExpand = 0;
    This->Code = Executable;
    This->S = 1;
    This->Privilege = PRIVILEGE_KERNEL;
    This->Present = 1;
    This->Available = 0;
    This->LongMode = 0;
    This->DefaultSize = 0;
    This->Granularity = 0;
}

/***************************************************************************/

/**
 * @brief Populate the shared GDT with long mode and compatibility segments.
 * @param Table Pointer to the descriptor table buffer.
 */
static void InitializeGlobalDescriptorTable(LPSEGMENT_DESCRIPTOR Table) {
    DEBUG(TEXT("[InitializeGlobalDescriptorTable] Enter"));

    MemorySet(Table, 0, GDT_SIZE);

    InitLongModeSegmentDescriptor(&Table[1], TRUE, PRIVILEGE_KERNEL);
    InitLongModeSegmentDescriptor(&Table[2], FALSE, PRIVILEGE_KERNEL);
    InitLongModeSegmentDescriptor(&Table[3], TRUE, PRIVILEGE_USER);
    InitLongModeSegmentDescriptor(&Table[4], FALSE, PRIVILEGE_USER);
    InitLegacySegmentDescriptor(&Table[5], TRUE);
    InitLegacySegmentDescriptor(&Table[6], FALSE);

    DEBUG(TEXT("[InitializeGlobalDescriptorTable] Exit"));
}

/***************************************************************************/

/**
 * @brief Allocate and initialize the architecture task-state segment.
 */
void InitializeTaskSegments(void) {
    DEBUG(TEXT("[InitializeTaskSegments] Enter"));

    UINT TssSize = sizeof(X86_64_TASK_STATE_SEGMENT);

    Kernel_i386.TSS = (LPX86_64_TASK_STATE_SEGMENT)AllocKernelRegion(
        0, TssSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Kernel_i386.TSS == NULL) {
        ERROR(TEXT("[InitializeTaskSegments] AllocKernelRegion for TSS failed"));
        ConsolePanic(TEXT("AllocKernelRegion for TSS failed"));
    }

    MemorySet(Kernel_i386.TSS, 0, TssSize);
    Kernel_i386.TSS->IOMapBase = (U16)TssSize;

    LINEAR CurrentRsp;
    GetESP(CurrentRsp);
    Kernel_i386.TSS->RSP0 = (U64)CurrentRsp;
    Kernel_i386.TSS->IST1 = (U64)CurrentRsp;

    LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR Descriptor =
        (LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR)((LPSEGMENT_DESCRIPTOR)Kernel_i386.GDT + GDT_TSS_INDEX);

    MemorySet(Descriptor, 0, sizeof(X86_64_SYSTEM_SEGMENT_DESCRIPTOR));

    SetSystemSegmentDescriptorLimit(Descriptor, TssSize - 1);
    SetSystemSegmentDescriptorBase(Descriptor, (UINT)Kernel_i386.TSS);

    Descriptor->Accessed = 1;
    Descriptor->Code = 1;
    Descriptor->S = 0;
    Descriptor->Privilege = PRIVILEGE_KERNEL;
    Descriptor->Present = 1;
    Descriptor->Limit_16_19 = (U8)(Descriptor->Limit_16_19 & 0x0F);
    Descriptor->Available = 0;
    Descriptor->LongMode = 0;
    Descriptor->DefaultSize = 0;
    Descriptor->Granularity = 0;
    Descriptor->Reserved = 0;

    DEBUG(TEXT("[InitializeTaskSegments] TSS = %p"), Kernel_i386.TSS);
    DEBUG(TEXT("[InitializeTaskSegments] Loading task register"));

    LoadInitialTaskRegister(SELECTOR_TSS);

    DEBUG(TEXT("[InitializeTaskSegments] Exit"));
}

/***************************************************************************/

/**
 * @brief Initialize the architecture-specific context for a task.
 *
 * Allocates kernel and user stacks for the task, clears the interrupt frame,
 * and seeds the register snapshot so the generic scheduler can operate while
 * the long mode context-switching code is under construction.
 */
BOOL SetupTask(struct tag_TASK* Task, struct tag_PROCESS* Process, struct tag_TASKINFO* Info) {
    LINEAR BaseVMA = VMA_KERNEL;
    SELECTOR CodeSelector = SELECTOR_KERNEL_CODE;
    SELECTOR DataSelector = SELECTOR_KERNEL_DATA;
    LINEAR StackTop;
    LINEAR SysStackTop;
    LINEAR BootStackTop;
    LINEAR RSP, RBP;
    U64 CR4;

    DEBUG(TEXT("[SetupTask] Enter"));

    if (Process->Privilege == PRIVILEGE_USER) {
        BaseVMA = VMA_USER;
        CodeSelector = SELECTOR_USER_CODE;
        DataSelector = SELECTOR_USER_DATA;
    }

    Task->Arch.Stack.Size = Info->StackSize;
    Task->Arch.SysStack.Size = TASK_MINIMUM_SYSTEM_STACK_SIZE;
    Task->Arch.Ist1Stack.Size = TASK_MINIMUM_SYSTEM_STACK_SIZE;

    Task->Arch.Stack.Base = AllocRegion(BaseVMA, 0, Task->Arch.Stack.Size,
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER);
    Task->Arch.SysStack.Base =
        AllocKernelRegion(0, Task->Arch.SysStack.Size, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
    Task->Arch.Ist1Stack.Base =
        AllocKernelRegion(0, Task->Arch.Ist1Stack.Size, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    DEBUG(TEXT("[SetupTask] BaseVMA=%p, Requested StackBase at BaseVMA"), BaseVMA);
    DEBUG(TEXT("[SetupTask] Actually got StackBase=%p"), Task->Arch.Stack.Base);

    if (Task->Arch.Stack.Base == NULL || Task->Arch.SysStack.Base == NULL || Task->Arch.Ist1Stack.Base == NULL) {
        if (Task->Arch.Stack.Base != NULL) {
            FreeRegion(Task->Arch.Stack.Base, Task->Arch.Stack.Size);
            Task->Arch.Stack.Base = 0;
            Task->Arch.Stack.Size = 0;
        }

        if (Task->Arch.SysStack.Base != NULL) {
            FreeRegion(Task->Arch.SysStack.Base, Task->Arch.SysStack.Size);
            Task->Arch.SysStack.Base = 0;
            Task->Arch.SysStack.Size = 0;
        }

        if (Task->Arch.Ist1Stack.Base != NULL) {
            FreeRegion(Task->Arch.Ist1Stack.Base, Task->Arch.Ist1Stack.Size);
            Task->Arch.Ist1Stack.Base = 0;
            Task->Arch.Ist1Stack.Size = 0;
        }

        ERROR(TEXT("[SetupTask] Stack or system stack allocation failed"));
        return FALSE;
    }

    DEBUG(TEXT("[SetupTask] Stack (%u bytes) allocated at %p"), Task->Arch.Stack.Size, Task->Arch.Stack.Base);
    DEBUG(TEXT("[SetupTask] System stack (%u bytes) allocated at %p"), Task->Arch.SysStack.Size,
        Task->Arch.SysStack.Base);
    DEBUG(TEXT("[SetupTask] IST1 stack (%u bytes) allocated at %p"), Task->Arch.Ist1Stack.Size,
        Task->Arch.Ist1Stack.Base);

    MemorySet((LPVOID)(Task->Arch.Stack.Base), 0, Task->Arch.Stack.Size);
    MemorySet((LPVOID)(Task->Arch.SysStack.Base), 0, Task->Arch.SysStack.Size);
    MemorySet((LPVOID)(Task->Arch.Ist1Stack.Base), 0, Task->Arch.Ist1Stack.Size);
    MemorySet(&(Task->Arch.Context), 0, sizeof(Task->Arch.Context));

    GetCR4(CR4);

    Task->Arch.Context.Registers.RAX = (UINT)Task->Parameter;
    Task->Arch.Context.Registers.RBX = (LINEAR)Task->Function;
    Task->Arch.Context.Registers.RCX = 0;
    Task->Arch.Context.Registers.RDX = 0;
    Task->Arch.Context.Registers.RSI = 0;
    Task->Arch.Context.Registers.RDI = 0;
    Task->Arch.Context.Registers.R8 = 0;
    Task->Arch.Context.Registers.R9 = 0;
    Task->Arch.Context.Registers.R10 = 0;
    Task->Arch.Context.Registers.R11 = 0;
    Task->Arch.Context.Registers.R12 = 0;
    Task->Arch.Context.Registers.R13 = 0;
    Task->Arch.Context.Registers.R14 = 0;
    Task->Arch.Context.Registers.R15 = 0x0000DEADBEEF0000;
    Task->Arch.Context.Registers.CS = CodeSelector;
    Task->Arch.Context.Registers.DS = DataSelector;
    Task->Arch.Context.Registers.ES = DataSelector;
    Task->Arch.Context.Registers.FS = DataSelector;
    Task->Arch.Context.Registers.GS = DataSelector;
    Task->Arch.Context.Registers.SS = DataSelector;
    Task->Arch.Context.Registers.RFlags = RFLAGS_IF | RFLAGS_ALWAYS_1;
    Task->Arch.Context.Registers.CR3 = Process->PageDirectory;
    Task->Arch.Context.Registers.CR4 = CR4;

    StackTop = Task->Arch.Stack.Base + Task->Arch.Stack.Size;
    SysStackTop = Task->Arch.SysStack.Base + Task->Arch.SysStack.Size;

    if (Process->Privilege == PRIVILEGE_KERNEL) {
        DEBUG(TEXT("[SetupTask] Setting kernel privilege (ring 0)"));
        Task->Arch.Context.Registers.RIP = (LINEAR)TaskRunner;
        Task->Arch.Context.Registers.RSP = StackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.RBP = StackTop - STACK_SAFETY_MARGIN;
    } else {
        DEBUG(TEXT("[SetupTask] Setting user privilege (ring 3)"));
        Task->Arch.Context.Registers.RIP = VMA_TASK_RUNNER;
        Task->Arch.Context.Registers.RSP = SysStackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.RBP = SysStackTop - STACK_SAFETY_MARGIN;
    }

    if (Info->Flags & TASK_CREATE_MAIN_KERNEL) {
        Task->Status = TASK_STATUS_RUNNING;

        Task->Arch.Context.SS0 = SELECTOR_KERNEL_DATA;
        Task->Arch.Context.RSP0 = SysStackTop - STACK_SAFETY_MARGIN;

        BootStackTop = (LINEAR)KernelStartup.StackTop;

        GetESP(RSP);
        UINT StackUsed = (BootStackTop - RSP) + 256;

        DEBUG(TEXT("[SetupTask] BootStackTop = %p"), BootStackTop);
        DEBUG(TEXT("[SetupTask] StackTop = %p"), StackTop);
        DEBUG(TEXT("[SetupTask] StackUsed = %u"), StackUsed);
        DEBUG(TEXT("[SetupTask] Switching to new stack..."));

        if (SwitchStack(StackTop, BootStackTop, StackUsed) == TRUE) {
            Task->Arch.Context.Registers.RSP = 0;
            GetEBP(RBP);
            Task->Arch.Context.Registers.RBP = RBP;
            DEBUG(TEXT("[SetupTask] Main task stack switched successfully"));
        } else {
            ERROR(TEXT("[SetupTask] Stack switch failed"));
        }
    }

    DEBUG(TEXT("[SetupTask] Exit"));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Prepare architectural state ahead of a context switch.
 * @param CurrentTask Task that is currently running (may be NULL).
 * @param NextTask Task that will become active.
 */
void PrepareNextTaskSwitch(struct tag_TASK* CurrentTask, struct tag_TASK* NextTask) {
    SAFE_USE(NextTask) {
        FINE_DEBUG(TEXT("[PrepareNextTaskSwitch] CurrentTask = %p (%s), NextTask = %p (%s)"),
            CurrentTask, CurrentTask->Name, NextTask, NextTask->Name);

        LINEAR NextSysStackTop = NextTask->Arch.SysStack.Base + NextTask->Arch.SysStack.Size;
        LINEAR NextIst1StackTop = NextTask->Arch.Ist1Stack.Base + NextTask->Arch.Ist1Stack.Size;

        FINE_DEBUG(TEXT("[PrepareNextTaskSwitch] NextSysStackTop = %p"), NextSysStackTop);
        FINE_DEBUG(TEXT("[PrepareNextTaskSwitch] NextIst1StackTop = %p"), NextIst1StackTop);

        Kernel_i386.TSS->RSP0 = NextSysStackTop - STACK_SAFETY_MARGIN;
        Kernel_i386.TSS->IST1 = NextIst1StackTop - STACK_SAFETY_MARGIN;
        Kernel_i386.TSS->IOMapBase = (U16)sizeof(X86_64_TASK_STATE_SEGMENT);

        SAFE_USE(CurrentTask) {
            GetFS(CurrentTask->Arch.Context.Registers.FS);
            GetGS(CurrentTask->Arch.Context.Registers.GS);
            SaveFPU(&(CurrentTask->Arch.Context.FPURegisters));
        }

        LoadPageDirectory(NextTask->Process->PageDirectory);

        // SetSS(NextTask->Arch.Context.Registers.SS);
        // SetDS(NextTask->Arch.Context.Registers.DS);
        // SetES(NextTask->Arch.Context.Registers.ES);
        SetFS(NextTask->Arch.Context.Registers.FS);
        SetGS(NextTask->Arch.Context.Registers.GS);

        RestoreFPU(&(NextTask->Arch.Context.FPURegisters));
    }
}

/***************************************************************************/

/**
 * @brief Architecture-specific memory manager initialization for x86-64.
 */
void InitializeMemoryManager(void) {
    DEBUG(TEXT("[InitializeMemoryManager] Enter"));

    DEBUG(TEXT("[InitializeMemoryManager] Temp pages reserved: %p, %p, %p"),
        (LPVOID)(LINEAR)X86_64_TEMP_LINEAR_PAGE_1,
        (LPVOID)(LINEAR)X86_64_TEMP_LINEAR_PAGE_2,
        (LPVOID)(LINEAR)X86_64_TEMP_LINEAR_PAGE_3);

    UpdateKernelMemoryMetricsFromMultibootMap();

    if (KernelStartup.PageCount == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    UINT BitmapBytes = (KernelStartup.PageCount + 7) >> MUL_8;
    UINT BitmapBytesAligned = (UINT)PAGE_ALIGN(BitmapBytes);

    U64 KernelSpan = (U64)KernelStartup.KernelSize + (U64)N_512KB;
    PHYSICAL MapSize = (PHYSICAL)PAGE_ALIGN(KernelSpan);
    U64 TotalPages = (MapSize + PAGE_SIZE - 1) >> PAGE_SIZE_MUL;
    U64 TablesRequired = (TotalPages + (U64)PAGE_TABLE_NUM_ENTRIES - 1) / (U64)PAGE_TABLE_NUM_ENTRIES;
    PHYSICAL TablesSize = (PHYSICAL)(TablesRequired * (U64)PAGE_TABLE_SIZE);
    PHYSICAL LoaderReservedEnd = KernelStartup.KernelPhysicalBase + MapSize + TablesSize;
    PHYSICAL PpbPhysical = PAGE_ALIGN(LoaderReservedEnd);

    Kernel.PPB = (LPPAGEBITMAP)(UINT)PpbPhysical;
    Kernel.PPBSize = BitmapBytesAligned;

    DEBUG(TEXT("[InitializeMemoryManager] Kernel.PPB physical base: %p"), (LINEAR)Kernel.PPB);
    DEBUG(TEXT("[InitializeMemoryManager] Kernel.PPB size: %x"), Kernel.PPBSize);

    MemorySet(Kernel.PPB, 0, Kernel.PPBSize);

    MarkUsedPhysicalMemory();

    if (KernelStartup.MemorySize == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    PHYSICAL NewPageDirectory = AllocPageDirectory();

    DEBUG(TEXT("[InitializeMemoryManager] New page directory: %p"), (LINEAR)NewPageDirectory);

    if (NewPageDirectory == NULL) {
        ERROR(TEXT("[InitializeMemoryManager] AllocPageDirectory failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    LoadPageDirectory(NewPageDirectory);

    FlushTLB();

    LogPageDirectory64(NewPageDirectory);

    DEBUG(TEXT("[InitializeMemoryManager] TLB flushed"));

    Kernel_i386.GDT = (LPVOID)AllocKernelRegion(0, GDT_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Kernel_i386.GDT == NULL) {
        ERROR(TEXT("[InitializeMemoryManager] AllocRegion for GDT failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    InitializeGlobalDescriptorTable((LPSEGMENT_DESCRIPTOR)Kernel_i386.GDT);

    LogGlobalDescriptorTable((LPSEGMENT_DESCRIPTOR)Kernel_i386.GDT, 10);

    DEBUG(TEXT("[InitializeMemoryManager] Loading GDT"));

    LoadGlobalDescriptorTable((PHYSICAL)Kernel_i386.GDT, GDT_SIZE - 1);

    DEBUG(TEXT("[InitializeMemoryManager] Exit"));
}

/************************************************************************/

/**
 * @brief Translate a linear address to its physical counterpart (page-level granularity).
 * @param Address Linear address.
 * @return Physical address or 0 when unmapped.
 */
PHYSICAL MapLinearToPhysical(LINEAR Address) {
    Address = CanonicalizeLinearAddress(Address);

    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Address);
    UINT Pml4Index = MemoryPageIteratorGetPml4Index(&Iterator);
    UINT PdptIndex = MemoryPageIteratorGetPdptIndex(&Iterator);
    UINT DirIndex = MemoryPageIteratorGetDirectoryIndex(&Iterator);
    UINT TabIndex = MemoryPageIteratorGetTableIndex(&Iterator);

    LPPML4 Pml4 = GetCurrentPml4VA();
    U64 Pml4EntryValue = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)Pml4, Pml4Index);
    if ((Pml4EntryValue & PAGE_FLAG_PRESENT) == 0) return 0;

    PHYSICAL PdptPhysical = (PHYSICAL)(Pml4EntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY PdptLinear = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(PdptPhysical);
    U64 PdptEntryValue = ReadPageDirectoryEntryValue(PdptLinear, PdptIndex);
    if ((PdptEntryValue & PAGE_FLAG_PRESENT) == 0) return 0;

    if ((PdptEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
        PHYSICAL LargeBase = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
        return (PHYSICAL)(LargeBase | (Address & (N_1GB - 1)));
    }

    PHYSICAL DirectoryPhysical = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY DirectoryLinear = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(DirectoryPhysical);
    U64 DirectoryEntryValue = ReadPageDirectoryEntryValue(DirectoryLinear, DirIndex);
    if ((DirectoryEntryValue & PAGE_FLAG_PRESENT) == 0) return 0;

    if ((DirectoryEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
        PHYSICAL LargeBase = (PHYSICAL)(DirectoryEntryValue & PAGE_MASK);
        return (PHYSICAL)(LargeBase | (Address & (N_2MB - 1)));
    }

    LPPAGE_TABLE Table = MemoryPageIteratorGetTable(&Iterator);
    if (!PageTableEntryIsPresent(Table, TabIndex)) return 0;

    PHYSICAL PagePhysical = PageTableEntryGetPhysical(Table, TabIndex);
    if (PagePhysical == 0) return 0;

    return (PHYSICAL)(PagePhysical | (Address & (PAGE_SIZE - 1)));
}

/************************************************************************/

/**
 * @brief Check if a linear address is mapped and accessible.
 * @param Address Linear address to test.
 * @return TRUE if the address resolves to a present page table entry.
 */
BOOL IsValidMemory(LINEAR Address) {
    LINEAR Canonical = CanonicalizeLinearAddress(Address);

    if (Canonical != Address) {
        return FALSE;
    }

    return MapLinearToPhysical(Canonical) != 0;
}

/************************************************************************/

/**
 * @brief Perform architecture-specific pre-initialization.
 */
void PreInitializeKernel(void) {
    GDT_REGISTER Gdtr;
    U64 Cr0;
    U64 Cr4;

    ReadGlobalDescriptorTable(&Gdtr);
    Kernel_i386.GDT = (LPVOID)(LINEAR)Gdtr.Base;

    __asm__ volatile("mov %%cr0, %0" : "=r"(Cr0));
    Cr0 |= (U64)CR0_COPROCESSOR;
    Cr0 &= ~(U64)CR0_EMULATION;
    __asm__ volatile("mov %0, %%cr0" : : "r"(Cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(Cr4));
    Cr4 |= (U64)(CR4_OSFXSR | CR4_OSXMMEXCPT);
    __asm__ volatile("mov %0, %%cr4" : : "r"(Cr4));

    DEBUG(TEXT("[PreInitializeKernel] Enabled SSE support"));
}

/************************************************************************/

/**
 * @brief Configure MSRs required for SYSCALL/SYSRET transitions.
 */
void InitializeSystemCall(void) {
    U64 StarValue;
    U64 EntryPoint;
    U64 MaskValue;
    U64 EferValue;

    StarValue = ((U64)SELECTOR_USER_CODE << 48) | ((U64)SELECTOR_KERNEL_CODE << 32);
    WriteMSR64(IA32_STAR_MSR, (U32)(StarValue & 0xFFFFFFFF), (U32)(StarValue >> 32));

    EntryPoint = (U64)(LINEAR)Interrupt_SystemCall;
    WriteMSR64(IA32_LSTAR_MSR, (U32)(EntryPoint & 0xFFFFFFFF), (U32)(EntryPoint >> 32));

    MaskValue = RFLAGS_TF | RFLAGS_IF | RFLAGS_DF;
    WriteMSR64(IA32_FMASK_MSR, (U32)(MaskValue & 0xFFFFFFFF), (U32)(MaskValue >> 32));

    EferValue = ReadMSR64Local(IA32_EFER_MSR);
    EferValue |= IA32_EFER_SCE;
    WriteMSR64(IA32_EFER_MSR, (U32)(EferValue & 0xFFFFFFFF), (U32)(EferValue >> 32));
}

/************************************************************************/

/**
 * @brief Log syscall stack frame information before returning to userland.
 * @param SaveArea Base address of the saved register block on the user stack.
 * @param FunctionId System call identifier that is about to complete.
 */
BOOL DebugReadLinearBytes(LINEAR Address, U8* Buffer, UINT Length) {
    UINT Index;

    if (Buffer == NULL) {
        return FALSE;
    }

    for (Index = 0u; Index < Length; Index++) {
        LINEAR ByteAddress = Address + (LINEAR)Index;

        if (!IsValidMemory(ByteAddress)) {
            return FALSE;
        }

        Buffer[Index] = *((U8*)ByteAddress);
    }

    return TRUE;
}

/************************************************************************/

BOOL DebugFormatPrintableAscii(const U8* Buffer, UINT Length, LPSTR Output, UINT OutputLength) {
    UINT Index;

    if (Buffer == NULL || Output == NULL || OutputLength == 0u) {
        return FALSE;
    }

    if (Length + 1u > OutputLength) {
        return FALSE;
    }

    for (Index = 0u; Index < Length; Index++) {
        U8 Byte = Buffer[Index];
        STR Character = (STR)'.';

        if (Byte >= 0x20u && Byte <= 0x7Eu) {
            Character = (STR)Byte;
        }

        Output[Index] = Character;
    }

    Output[Length] = STR_NULL;

    return TRUE;
}

/************************************************************************/

BOOL DebugLoadPrintableAscii(LINEAR Address, UINT Length, LPSTR Output, UINT OutputLength) {
    const UINT ScratchSize = 64u;
    U8 Scratch[64];

    if (Length == 0u) {
        if (Output != NULL && OutputLength > 0u) {
            Output[0] = STR_NULL;
            return TRUE;
        }

        return FALSE;
    }

    if (Length > ScratchSize) {
        return FALSE;
    }

    if (!DebugReadLinearBytes(Address, Scratch, Length)) {
        return FALSE;
    }

    return DebugFormatPrintableAscii(Scratch, Length, Output, OutputLength);
}

/************************************************************************/

void DebugLogSyscallFrame(LINEAR SaveArea, UINT FunctionId) {
    const UINT UserStackEntriesToLog = 8u;
    const UINT ReturnBytesToLog = 2u;
    const UINT RbxDataEntriesToLog = 4u;
    const UINT AsciiChunkLength = (UINT)sizeof(U64);
    STR AsciiBuffer[9];
    STR RbxSample[33];
    U8* SavePtr;
    U64* SavedRegisters;
    LINEAR UserStackPointer;
    LINEAR ReturnAddress;
    LINEAR SavedFlags;
    LINEAR RbxPointer;
    UINT Index;

    if (SaveArea == (LINEAR)0) {
        DEBUG(TEXT("[DebugLogSyscallFrame] SaveArea missing for Function=%u"), FunctionId);
        return;
    }

    SavePtr = (U8*)(SaveArea);
    SavedRegisters = (U64*)SavePtr;
    UserStackPointer = (LINEAR)(SaveArea + (LINEAR)SYSCALL_SAVE_AREA_SIZE);
    ReturnAddress = (LINEAR)SavedRegisters[SYSCALL_SAVE_OFFSET_RCX];
    SavedFlags = (LINEAR)SavedRegisters[SYSCALL_SAVE_OFFSET_R11];
    RbxPointer = (LINEAR)SavedRegisters[SYSCALL_SAVE_OFFSET_RBX];

    DEBUG(TEXT("[DebugLogSyscallFrame] Function=%u SaveArea=%p UserStack=%p Return=%p Flags=%p"), FunctionId,
          (LPVOID)SaveArea, (LPVOID)UserStackPointer, (LPVOID)ReturnAddress, (LPVOID)SavedFlags);
    DEBUG(TEXT("[DebugLogSyscallFrame] SavedRAX=%p SavedRBX=%p SavedRCX=%p SavedRDX=%p"),
          (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_RAX], (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_RBX],
          (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_RCX], (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_RDX]);
    DEBUG(TEXT("[DebugLogSyscallFrame] SavedRBP=%p SavedRSI=%p SavedRDI=%p SavedR8=%p"),
          (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_RBP], (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_RSI],
          (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_RDI], (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_R8]);
    DEBUG(TEXT("[DebugLogSyscallFrame] SavedR9=%p SavedR10=%p SavedR11=%p SavedR12=%p"),
          (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_R9], (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_R10],
          (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_R11], (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_R12]);
    DEBUG(TEXT("[DebugLogSyscallFrame] SavedR13=%p SavedR14=%p SavedR15=%p"),
          (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_R13], (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_R14],
          (LPVOID)SavedRegisters[SYSCALL_SAVE_OFFSET_R15]);

    for (Index = 0u; Index < 9u; Index++) {
        AsciiBuffer[Index] = STR_NULL;
    }

    for (Index = 0u; Index < 33u; Index++) {
        RbxSample[Index] = STR_NULL;
    }

    for (Index = 0u; Index < UserStackEntriesToLog; Index++) {
        LINEAR EntryAddress = UserStackPointer + (LINEAR)(Index * sizeof(LINEAR));
        LINEAR Value = 0;

        if (!IsValidMemory(EntryAddress)) {
            DEBUG(TEXT("[DebugLogSyscallFrame] UserStack[%u] @ %p invalid"), Index, (LPVOID)EntryAddress);
            break;
        }

        Value = *((LINEAR*)EntryAddress);
        DEBUG(TEXT("[DebugLogSyscallFrame] UserStack[%u] @ %p = %p"), Index, (LPVOID)EntryAddress, (LPVOID)Value);
    }

    if (RbxPointer != (LINEAR)0) {
        if (DebugLoadPrintableAscii(RbxPointer, 32u, RbxSample, sizeof RbxSample)) {
            DEBUG(TEXT("[DebugLogSyscallFrame] RBXData sample = \"%s\""), (LPCSTR)RbxSample);
        }

        for (Index = 0u; Index < RbxDataEntriesToLog; Index++) {
            LINEAR DataAddress = RbxPointer + (LINEAR)(Index * sizeof(U64));
            U64 DataValue = 0;

            if (!IsValidMemory(DataAddress)) {
                DEBUG(TEXT("[DebugLogSyscallFrame] RBXData[%u] @ %p invalid"), Index, (LPVOID)DataAddress);
                break;
            }

            DataValue = *((U64*)DataAddress);
            DEBUG(TEXT("[DebugLogSyscallFrame] RBXData[%u] @ %p = %p"), Index, (LPVOID)DataAddress, (LPVOID)DataValue);

            if (DebugLoadPrintableAscii(DataAddress, AsciiChunkLength, AsciiBuffer, sizeof AsciiBuffer)) {
                DEBUG(TEXT("[DebugLogSyscallFrame] RBXData[%u] ascii = \"%s\""), Index, (LPCSTR)AsciiBuffer);
            }
        }
    }

    for (Index = 0u; Index < ReturnBytesToLog; Index++) {
        LINEAR InstructionAddress = ReturnAddress + (LINEAR)(Index * sizeof(U64));
        U64 InstructionValue = 0;

        if (!IsValidMemory(InstructionAddress)) {
            DEBUG(TEXT("[DebugLogSyscallFrame] ReturnBytes[%u] @ %p invalid"), Index, (LPVOID)InstructionAddress);
            break;
        }

        InstructionValue = *((U64*)InstructionAddress);
        DEBUG(TEXT("[DebugLogSyscallFrame] ReturnBytes[%u] @ %p = %p"), Index, (LPVOID)InstructionAddress,
              (LPVOID)InstructionValue);

        if (DebugLoadPrintableAscii(InstructionAddress, AsciiChunkLength, AsciiBuffer, sizeof AsciiBuffer)) {
            DEBUG(TEXT("[DebugLogSyscallFrame] ReturnBytes[%u] ascii = \"%s\""), Index, (LPCSTR)AsciiBuffer);
        }
    }
}

/************************************************************************/
