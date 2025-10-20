
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

KERNELDATA_X86_64 SECTION(".data") Kernel_i386 = {
    .IDT = NULL,
    .GDT = NULL,
    .TSS = NULL,
};

/************************************************************************/

/**
 * @brief Set the handler address for a 64-bit IDT gate descriptor.
 * @param Descriptor IDT entry to update.
 * @param Handler Linear address of the interrupt handler.
 */
void SetGateDescriptorOffset(LPX86_64_IDT_ENTRY Descriptor, LINEAR Handler) {
    U64 Offset = (U64)Handler;

    Descriptor->Offset_00_15 = (U16)(Offset & 0x0000FFFFull);
    Descriptor->Offset_16_31 = (U16)((Offset >> 16) & 0x0000FFFFull);
    Descriptor->Offset_32_63 = (U32)((Offset >> 32) & 0xFFFFFFFFull);
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
    LPX86_64_IDT_ENTRY Descriptor,
    LINEAR Handler,
    U16 Type,
    U16 Privilege) {
    Descriptor->Selector = SELECTOR_KERNEL_CODE;
    Descriptor->InterruptStackTable = 0;
    Descriptor->Reserved_0 = 0;
    Descriptor->Type = Type;
    Descriptor->Privilege = Privilege;
    Descriptor->Present = 1;
    Descriptor->Reserved_1 = 0;

    SetGateDescriptorOffset(Descriptor, Handler);
}

/************************************************************************/

static void SetSystemSegmentDescriptorLimit(LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR Descriptor, U32 Limit) {
    Descriptor->Limit_00_15 = (U16)(Limit & 0xFFFFu);
    Descriptor->Limit_16_19 = (U8)((Limit >> 16) & 0x0Fu);
}

/************************************************************************/

static void SetSystemSegmentDescriptorBase(LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR Descriptor, U64 Base) {
    Descriptor->Base_00_15 = (U16)(Base & 0xFFFFu);
    Descriptor->Base_16_23 = (U8)((Base >> 16) & 0xFFu);
    Descriptor->Base_24_31 = (U8)((Base >> 24) & 0xFFu);
    Descriptor->Base_32_63 = (U32)((Base >> 32) & 0xFFFFFFFFu);
}

/************************************************************************/

/**
 * @brief Perform architecture-specific pre-initialization.
 */
void ArchPreInitializeKernel(void) {
    GDT_REGISTER Gdtr;

    ReadGlobalDescriptorTable(&Gdtr);
    Kernel_i386.GDT = (LPVOID)(LINEAR)Gdtr.Base;
}

/************************************************************************/

static void ResetRegionSetup(REGION_SETUP* Region) {
    MemorySet(Region, 0, sizeof(REGION_SETUP));
}

/************************************************************************/

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

static BOOL AllocateTableAndPopulate(
    REGION_SETUP* Region,
    PAGE_TABLE_SETUP* Table,
    LPPAGE_DIRECTORY Directory) {
    Table->Physical = AllocPhysicalPage();

    if (Table->Physical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] %s region out of physical pages"), Region->Label);
        return FALSE;
    }

    LINEAR TableLinear = MapTemporaryPhysicalPage3(Table->Physical);

    if (TableLinear == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage3 failed for %s table"), Region->Label);
        FreePhysicalPage(Table->Physical);
        Table->Physical = NULL;
        return FALSE;
    }

    LPPAGE_TABLE TableVA = (LPPAGE_TABLE)TableLinear;
    MemorySet(TableVA, 0, PAGE_SIZE);

    UINT ProbeIndex = 0u;
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
        ProbeIndex = Table->Data.Single.TableIndex;
        break;

    case PAGE_TABLE_POPULATE_EMPTY:
    default:
        ProbeIndex = 0u;
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

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] table ready at %p"),
        Region->Label,
        Table->DirectoryIndex,
        Table->Physical);

    return TRUE;
}

/************************************************************************/

static BOOL SetupLowRegion(REGION_SETUP* Region, UINT UserSeedTables) {
    ResetRegionSetup(Region);

    Region->Label = TEXT("Low");
    Region->PdptIndex = GetPdptEntry(0);
    Region->ReadWrite = 1;
    Region->Privilege = PAGE_PRIVILEGE_KERNEL;
    Region->Global = 0;

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();

    DEBUG(TEXT("[SetupLowRegion] PDPT %p, directory %p"), Region->PdptPhysical, Region->DirectoryPhysical);

    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Low region out of physical pages"));
        return FALSE;
    }

    LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(Region->PdptPhysical);

    if (Pdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed for low PDPT"));
        return FALSE;
    }

    MemorySet(Pdpt, 0, PAGE_SIZE);

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(Region->DirectoryPhysical);

    if (Directory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed for low directory"));
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
    DEBUG(TEXT("[SetupLowRegion] PDPT[%u] -> %p"), Region->PdptIndex, Region->DirectoryPhysical);

    UINT LowDirectoryIndex = GetDirectoryEntry(0);

    Region->Tables[Region->TableCount].DirectoryIndex = LowDirectoryIndex;
    Region->Tables[Region->TableCount].ReadWrite = 1;
    Region->Tables[Region->TableCount].Privilege = PAGE_PRIVILEGE_KERNEL;
    Region->Tables[Region->TableCount].Global = 0;
    Region->Tables[Region->TableCount].Mode = PAGE_TABLE_POPULATE_IDENTITY;
    Region->Tables[Region->TableCount].Data.Identity.PhysicalBase = 0;
    Region->Tables[Region->TableCount].Data.Identity.ProtectBios = TRUE;
    if (AllocateTableAndPopulate(Region, &Region->Tables[Region->TableCount], Directory) == FALSE) return FALSE;
    Region->TableCount++;

    Region->Tables[Region->TableCount].DirectoryIndex = LowDirectoryIndex + 1u;
    Region->Tables[Region->TableCount].ReadWrite = 1;
    Region->Tables[Region->TableCount].Privilege = PAGE_PRIVILEGE_KERNEL;
    Region->Tables[Region->TableCount].Global = 0;
    Region->Tables[Region->TableCount].Mode = PAGE_TABLE_POPULATE_IDENTITY;
    Region->Tables[Region->TableCount].Data.Identity.PhysicalBase = ((PHYSICAL)PAGE_TABLE_NUM_ENTRIES << PAGE_SIZE_MUL);
    Region->Tables[Region->TableCount].Data.Identity.ProtectBios = FALSE;
    if (AllocateTableAndPopulate(Region, &Region->Tables[Region->TableCount], Directory) == FALSE) return FALSE;
    Region->TableCount++;

    if (UserSeedTables != 0u) {
        UINT BaseDirectory = GetDirectoryEntry((U64)VMA_USER);

        for (UINT Index = 0; Index < UserSeedTables; Index++) {
            PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];
            Table->DirectoryIndex = BaseDirectory + Index;
            Table->ReadWrite = 1;
            Table->Privilege = PAGE_PRIVILEGE_USER;
            Table->Global = 0;
            Table->Mode = PAGE_TABLE_POPULATE_EMPTY;
            if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) return FALSE;
            Region->TableCount++;
        }
    }

    return TRUE;
}

/************************************************************************/

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

    return TRUE;
}

/************************************************************************/

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
    return TRUE;
}

/************************************************************************/

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
    if (SetupKernelRegion(&KernelRegion, KernelTableCount) == FALSE) goto Out;

    LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = KernelStartup.KernelPhysicalBase +
        (PHYSICAL)(TaskRunnerLinear - (LINEAR)VMA_KERNEL);

    DEBUG(TEXT("[AllocPageDirectory] TaskRunnerPhysical = %p + (%p - %p) = %p"),
        KernelStartup.KernelPhysicalBase,
        TaskRunnerLinear,
        VMA_KERNEL,
        TaskRunnerPhysical);

    if (SetupTaskRunnerRegion(&TaskRunnerRegion, TaskRunnerPhysical, TaskRunnerTableIndex) == FALSE) goto Out;

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
            PAGE_PRIVILEGE_KERNEL,
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

    DEBUG(TEXT("[AllocUserPageDirectory] Enter"));

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

    if (SetupLowRegion(&LowRegion, USERLAND_SEEDED_TABLES) == FALSE) goto Out;
    if (SetupKernelRegion(&KernelRegion, KernelTableCount) == FALSE) goto Out;

    LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = KernelStartup.KernelPhysicalBase +
        (PHYSICAL)(TaskRunnerLinear - (LINEAR)VMA_KERNEL);

    DEBUG(TEXT("[AllocUserPageDirectory] TaskRunnerPhysical = %p + (%p - %p) = %p"),
        KernelStartup.KernelPhysicalBase,
        TaskRunnerLinear,
        VMA_KERNEL,
        TaskRunnerPhysical);

    if (SetupTaskRunnerRegion(&TaskRunnerRegion, TaskRunnerPhysical, TaskRunnerTableIndex) == FALSE) goto Out;

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
    DEBUG(TEXT("[AllocUserPageDirectory] PML4 mapped"));

    WritePageDirectoryEntryValue(
        Pml4,
        LowPml4Index,
        MakePageDirectoryEntryValue(
            LowRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
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

    DEBUG(TEXT("[AllocUserPageDirectory] PML4 entries set (low=%p, kernel=%p, task=%p, recursive=%p)"),
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

    DEBUG(TEXT("[AllocUserPageDirectory] Exit"));
    return Pml4Physical;
}

/************************************************************************/

static void InitLongModeSegmentDescriptor(LPSEGMENT_DESCRIPTOR Descriptor, BOOL Executable, U32 Privilege) {
    MemorySet(Descriptor, 0, sizeof(SEGMENT_DESCRIPTOR));

    Descriptor->Limit_00_15 = 0xFFFFu;
    Descriptor->Base_00_15 = 0x0000u;
    Descriptor->Base_16_23 = 0x00u;
    Descriptor->Accessed = 0u;
    Descriptor->CanWrite = 1u;
    Descriptor->ConformExpand = 0u;
    Descriptor->Type = Executable ? 1u : 0u;
    Descriptor->Segment = 1u;
    Descriptor->Privilege = Privilege;
    Descriptor->Present = 1u;
    Descriptor->Limit_16_19 = 0x0Fu;
    Descriptor->Available = 0u;
    Descriptor->Unused = Executable ? 1u : 0u;
    Descriptor->OperandSize = Executable ? 0u : 1u;
    Descriptor->Granularity = 1u;
    Descriptor->Base_24_31 = 0x00u;
}

/***************************************************************************/

static void InitLongModeDataDescriptor(LPSEGMENT_DESCRIPTOR Descriptor, U32 Privilege) {
    InitLongModeSegmentDescriptor(Descriptor, FALSE, Privilege);
    Descriptor->Unused = 0u;
    Descriptor->OperandSize = 1u;
}

/***************************************************************************/

static void InitLegacySegmentDescriptor(LPSEGMENT_DESCRIPTOR Descriptor, BOOL Executable) {
    MemorySet(Descriptor, 0, sizeof(SEGMENT_DESCRIPTOR));

    Descriptor->Limit_00_15 = 0xFFFFu;
    Descriptor->Limit_16_19 = 0x0Fu;
    Descriptor->Base_00_15 = 0x0000u;
    Descriptor->Base_16_23 = 0x00u;
    Descriptor->Base_24_31 = 0x00u;
    Descriptor->Accessed = 0u;
    Descriptor->CanWrite = 1u;
    Descriptor->ConformExpand = 0u;
    Descriptor->Type = Executable ? 1u : 0u;
    Descriptor->Segment = 1u;
    Descriptor->Privilege = PRIVILEGE_KERNEL;
    Descriptor->Present = 1u;
    Descriptor->Available = 0u;
    Descriptor->Unused = 0u;
    Descriptor->OperandSize = 0u;
    Descriptor->Granularity = 0u;
}

/***************************************************************************/

static void InitializeGlobalDescriptorTable(LPSEGMENT_DESCRIPTOR Table) {
    DEBUG(TEXT("[InitializeGlobalDescriptorTable] Enter"));

    MemorySet(Table, 0, GDT_SIZE);

    InitLongModeSegmentDescriptor(&Table[1], TRUE, PRIVILEGE_KERNEL);
    InitLongModeDataDescriptor(&Table[2], PRIVILEGE_KERNEL);
    InitLongModeSegmentDescriptor(&Table[3], TRUE, PRIVILEGE_USER);
    InitLongModeDataDescriptor(&Table[4], PRIVILEGE_USER);
    InitLegacySegmentDescriptor(&Table[5], TRUE);
    InitLegacySegmentDescriptor(&Table[6], FALSE);

    DEBUG(TEXT("[InitializeGlobalDescriptorTable] Exit"));
}

/***************************************************************************/

void InitializeTaskSegments(void) {
    DEBUG(TEXT("[InitializeTaskSegments] Enter"));

    UINT TssSize = sizeof(X86_64_TASK_STATE_SEGMENT);

    Kernel_i386.TSS = (LPX86_64_TASK_STATE_SEGMENT)AllocKernelRegion(
        0, TssSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Kernel_i386.TSS == NULL) {
        ERROR(TEXT("[InitializeTaskSegments] AllocKernelRegion for TSS failed"));
        DO_THE_SLEEPING_BEAUTY;
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
    SetSystemSegmentDescriptorLimit(Descriptor, TssSize - 1u);
    SetSystemSegmentDescriptorBase(Descriptor, (U64)(UINT)Kernel_i386.TSS);

    Descriptor->Type = GDT_TYPE_TSS_AVAILABLE;
    Descriptor->Zero0 = 0u;
    Descriptor->Privilege = PRIVILEGE_KERNEL;
    Descriptor->Present = 1u;
    Descriptor->Limit_16_19 = (U8)(Descriptor->Limit_16_19 & 0x0Fu);
    Descriptor->Available = 0u;
    Descriptor->Zero1 = 0u;
    Descriptor->Granularity = 0u;
    Descriptor->Reserved = 0u;

    DEBUG(TEXT("[InitializeTaskSegments] TSS = %p"), (LPVOID)(UINT)Kernel_i386.TSS);

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
BOOL ArchSetupTask(struct tag_TASK* Task, struct tag_PROCESS* Process, struct tag_TASKINFO* Info) {
    LINEAR BaseVMA = VMA_KERNEL;
    SELECTOR CodeSelector = SELECTOR_KERNEL_CODE;
    SELECTOR DataSelector = SELECTOR_KERNEL_DATA;
    LINEAR StackTop;
    LINEAR SysStackTop;
    U64 ControlRegister4 = 0;

    DEBUG(TEXT("[ArchSetupTask] Enter"));

    if (Process->Privilege == PRIVILEGE_USER) {
        BaseVMA = VMA_USER;
        CodeSelector = SELECTOR_USER_CODE;
        DataSelector = SELECTOR_USER_DATA;
    }

    Task->Arch.StackSize = Info->StackSize;
    Task->Arch.SysStackSize = TASK_SYSTEM_STACK_SIZE * 4u;

    Task->Arch.StackBase = AllocRegion(BaseVMA, 0, Task->Arch.StackSize,
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER);
    Task->Arch.SysStackBase = AllocKernelRegion(
        0, Task->Arch.SysStackSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Task->Arch.StackBase == 0 || Task->Arch.SysStackBase == 0) {
        if (Task->Arch.StackBase != 0) {
            FreeRegion(Task->Arch.StackBase, Task->Arch.StackSize);
            Task->Arch.StackBase = 0;
            Task->Arch.StackSize = 0;
        }

        if (Task->Arch.SysStackBase != 0) {
            FreeRegion(Task->Arch.SysStackBase, Task->Arch.SysStackSize);
            Task->Arch.SysStackBase = 0;
            Task->Arch.SysStackSize = 0;
        }

        ERROR(TEXT("[ArchSetupTask] Stack allocation failed"));
        return FALSE;
    }

    DEBUG(TEXT("[ArchSetupTask] Stack (%x bytes) allocated at %p"), Task->Arch.StackSize,
        (LINEAR)Task->Arch.StackBase);
    DEBUG(TEXT("[ArchSetupTask] System stack (%x bytes) allocated at %p"), Task->Arch.SysStackSize,
        (LINEAR)Task->Arch.SysStackBase);

    MemorySet((void*)Task->Arch.StackBase, 0, Task->Arch.StackSize);
    MemorySet((void*)Task->Arch.SysStackBase, 0, Task->Arch.SysStackSize);
    MemorySet(&(Task->Arch.Context), 0, sizeof(Task->Arch.Context));

    Task->Arch.Context.Registers.RAX = (U64)(UINT)Task->Parameter;
    Task->Arch.Context.Registers.RBX = (U64)(UINT)Task->Function;
    Task->Arch.Context.Registers.CS = CodeSelector;
    Task->Arch.Context.Registers.DS = DataSelector;
    Task->Arch.Context.Registers.ES = DataSelector;
    Task->Arch.Context.Registers.FS = DataSelector;
    Task->Arch.Context.Registers.GS = DataSelector;
    Task->Arch.Context.Registers.SS = DataSelector;
    Task->Arch.Context.Registers.RFlags = RFLAGS_IF | RFLAGS_ALWAYS_1;
    Task->Arch.Context.Registers.CR3 = (U64)Process->PageDirectory;

    GetCR4(ControlRegister4);
    Task->Arch.Context.Registers.CR4 = ControlRegister4;
    Task->Arch.Context.Registers.RIP = (U64)VMA_TASK_RUNNER;

    StackTop = (LINEAR)(Task->Arch.StackBase + (U64)Task->Arch.StackSize);
    SysStackTop = (LINEAR)(Task->Arch.SysStackBase + (U64)Task->Arch.SysStackSize);

    if (Process->Privilege == PRIVILEGE_KERNEL) {
        Task->Arch.Context.Registers.RSP = StackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.RBP = StackTop - STACK_SAFETY_MARGIN;
    } else {
        Task->Arch.Context.Registers.RSP = SysStackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.RBP = SysStackTop - STACK_SAFETY_MARGIN;
    }

    Task->Arch.Context.SS0 = SELECTOR_KERNEL_DATA;
    Task->Arch.Context.RSP0 = SysStackTop - STACK_SAFETY_MARGIN;

    if ((Info->Flags & TASK_CREATE_MAIN_KERNEL) != 0u) {
        Task->Status = TASK_STATUS_RUNNING;

        LINEAR BootStackTop = (LINEAR)KernelStartup.StackTop;
        LINEAR CurrentRsp;
        LINEAR StackUsedLinear;
        U32 StackUsed;

        GetESP(CurrentRsp);
        if (CurrentRsp > BootStackTop) {
            StackUsedLinear = 0x100u;
        } else {
            StackUsedLinear = (BootStackTop - CurrentRsp) + 0x100u;
        }

        if (StackUsedLinear > (LINEAR)MAX_U32) {
            StackUsed = MAX_U32;
        } else {
            StackUsed = (U32)StackUsedLinear;
        }

        DEBUG(TEXT("[ArchSetupTask] BootStackTop = %p"), (LPVOID)(UINT)BootStackTop);
        DEBUG(TEXT("[ArchSetupTask] StackTop = %p"), (LPVOID)(UINT)StackTop);
        DEBUG(TEXT("[ArchSetupTask] StackUsed = %u"), StackUsed);
        DEBUG(TEXT("[ArchSetupTask] Switching to new stack..."));

        if (SwitchStack(StackTop, BootStackTop, StackUsed) == TRUE) {
            LINEAR CurrentRbp;

            Task->Arch.Context.Registers.RSP = 0;
            GetEBP(CurrentRbp);
            Task->Arch.Context.Registers.RBP = CurrentRbp;

            DEBUG(TEXT("[ArchSetupTask] Main task stack switched successfully"));
        } else {
            ERROR(TEXT("[ArchSetupTask] Stack switch failed"));
        }
    }

    DEBUG(TEXT("[ArchSetupTask] Exit"));
    return TRUE;
}

/***************************************************************************/

void PrepareNextTaskSwitch(struct tag_TASK* CurrentTask, struct tag_TASK* NextTask) {
#if SCHEDULING_DEBUG_OUTPUT == 1
    DEBUG(TEXT("[PrepareNextTaskSwitch] Enter"));
#endif

    if (NextTask == NULL) {
        return;
    }

    if (CurrentTask != NULL) {
        GetFS(CurrentTask->Arch.Context.Registers.FS);
        GetGS(CurrentTask->Arch.Context.Registers.GS);
        SaveFPU(&(CurrentTask->Arch.Context.FPURegisters));
    }

    LPX86_64_TASK_STATE_SEGMENT Tss = Kernel_i386.TSS;

    if (Tss != NULL) {
        Tss->RSP0 = NextTask->Arch.Context.RSP0;
        Tss->IST1 = NextTask->Arch.Context.RSP0;
        Tss->IOMapBase = (U16)sizeof(X86_64_TASK_STATE_SEGMENT);
    }

#if SCHEDULING_DEBUG_OUTPUT == 1
    DEBUG(TEXT("[PrepareNextTaskSwitch] LoadPageDirectory"));
#endif

    LoadPageDirectory(NextTask->Process->PageDirectory);

    SetDS(NextTask->Arch.Context.Registers.DS);
    SetES(NextTask->Arch.Context.Registers.ES);
    SetFS(NextTask->Arch.Context.Registers.FS);
    SetGS(NextTask->Arch.Context.Registers.GS);

    RestoreFPU(&(NextTask->Arch.Context.FPURegisters));

#if SCHEDULING_DEBUG_OUTPUT == 1
    DEBUG(TEXT("[PrepareNextTaskSwitch] Exit"));
#endif
}

/***************************************************************************/

/**
 * @brief Architecture-specific memory manager initialization for x86-64.
 */
void ArchInitializeMemoryManager(void) {
    DEBUG(TEXT("[ArchInitializeMemoryManager] Enter"));

    DEBUG(TEXT("[ArchInitializeMemoryManager] Temp pages reserved: %p, %p, %p"),
        (LPVOID)(LINEAR)X86_64_TEMP_LINEAR_PAGE_1,
        (LPVOID)(LINEAR)X86_64_TEMP_LINEAR_PAGE_2,
        (LPVOID)(LINEAR)X86_64_TEMP_LINEAR_PAGE_3);

    UpdateKernelMemoryMetricsFromMultibootMap();

    if (KernelStartup.PageCount == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    UINT BitmapBytes = (KernelStartup.PageCount + 7u) >> MUL_8;
    UINT BitmapBytesAligned = (UINT)PAGE_ALIGN(BitmapBytes);

    U64 KernelSpan = (U64)KernelStartup.KernelSize + (U64)N_512KB;
    PHYSICAL MapSize = (PHYSICAL)PAGE_ALIGN(KernelSpan);
    U64 TotalPages = (MapSize + PAGE_SIZE - 1ull) >> PAGE_SIZE_MUL;
    U64 TablesRequired = (TotalPages + (U64)PAGE_TABLE_NUM_ENTRIES - 1ull) / (U64)PAGE_TABLE_NUM_ENTRIES;
    PHYSICAL TablesSize = (PHYSICAL)(TablesRequired * (U64)PAGE_TABLE_SIZE);
    PHYSICAL LoaderReservedEnd = KernelStartup.KernelPhysicalBase + MapSize + TablesSize;
    PHYSICAL PpbPhysical = PAGE_ALIGN(LoaderReservedEnd);

    Kernel.PPB = (LPPAGEBITMAP)(UINT)PpbPhysical;
    Kernel.PPBSize = BitmapBytesAligned;

    DEBUG(TEXT("[ArchInitializeMemoryManager] Kernel.PPB physical base: %p"), (LINEAR)Kernel.PPB);
    DEBUG(TEXT("[ArchInitializeMemoryManager] Kernel.PPB size: %x"), Kernel.PPBSize);

    MemorySet(Kernel.PPB, 0, Kernel.PPBSize);

    MarkUsedPhysicalMemory();

    if (KernelStartup.MemorySize == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    PHYSICAL NewPageDirectory = AllocPageDirectory();

    DEBUG(TEXT("[ArchInitializeMemoryManager] New page directory: %p"), (LINEAR)NewPageDirectory);

    if (NewPageDirectory == NULL) {
        ERROR(TEXT("[ArchInitializeMemoryManager] AllocPageDirectory failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    LoadPageDirectory(NewPageDirectory);

    FlushTLB();

    DEBUG(TEXT("[ArchInitializeMemoryManager] TLB flushed"));

    Kernel_i386.GDT = (LPVOID)AllocKernelRegion(0, GDT_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Kernel_i386.GDT == NULL) {
        ERROR(TEXT("[ArchInitializeMemoryManager] AllocRegion for GDT failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    InitializeGlobalDescriptorTable((LPSEGMENT_DESCRIPTOR)Kernel_i386.GDT);

    DEBUG(TEXT("[ArchInitializeMemoryManager] Loading GDT"));

    LoadGlobalDescriptorTable((PHYSICAL)Kernel_i386.GDT, GDT_SIZE - 1);

    U64* RawEntries = (U64*)Kernel_i386.GDT;
    for (UINT Index = 0; Index < 10; Index++) {
        U64 Low = RawEntries[Index * 2u];
        U64 High = RawEntries[Index * 2u + 1u];
        DEBUG(TEXT("[InitializeMemoryManager] GDT[%u]=%p %p"), Index, (LPVOID)High, (LPVOID)Low);
    }

    DEBUG(TEXT("[ArchInitializeMemoryManager] Exit"));
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
