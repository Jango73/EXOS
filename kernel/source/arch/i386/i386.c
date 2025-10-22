
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


    I386

\************************************************************************/

#include "arch/i386/i386.h"

#include "Log.h"
#include "Memory.h"
#include "Console.h"
#include "process/Process.h"
#include "arch/i386/i386-Log.h"
#include "Stack.h"
#include "CoreString.h"
#include "System.h"
#include "process/Task.h"
#include "Text.h"
#include "Kernel.h"

/************************************************************************\

    Virtual Address Space (32-bit)
    ┌──────────────────────────────────────────────────────────────────────────┐
    │ 0x00000000 .................................................. 0xBFFFFFFF │
    │                [User space]  (PDE 0..KernelDir-1)                        │
    ├──────────────────────────────────────────────────────────────────────────┤
    │ 0xC0000000 .................................................. 0xFFFFEFFF │
    │                [Kernel space] (PDE KernelDir .. 1022)                    │
    ├──────────────────────────────────────────────────────────────────────────┤
    │ 0xFFFFF000 .................................................. 0xFFFFFFFF │
    │                [Self-map window]                                         │
    │                0xFFFFF000 = PD_VA (Page Directory as an array of PDEs)   │
    │                0xFFC00000 = PT_BASE_VA (all Page Tables visible)         │
    └──────────────────────────────────────────────────────────────────────────┘

    Page Directory (1024 PDEs, each 4B)
    dir = (VMA >> 22)
    tab = (VMA >> 12) & 0x3FF
    ofs =  VMA & 0xFFF

                      PDE index
            ┌────────────┬────────────┬────────────┬────────────┬─────────────┐
            │     0      │     1      │   ...      │ KernelDir  │   1023      │
            ├────────────┼────────────┼────────────┼────────────┼─────────── ─┤
    points→ │  Low PT    │   PT #1    │   ...      │ Kernel PT  │  SELF-MAP   │
    to PA   │ (0..4MB)   │            │            │ (VMA_KERNEL)│ (PD itself)│
            └────────────┴────────────┴────────────┴────────────┴─────────────┘
                                                              ^
                                                              |
                                         PDE[1023] -> PD physical page (recursive)
                                                              |
                                                              v
    PD_VA = 0xFFFFF000 ----------------------------------> Page Directory (VA alias)


    All Page Tables via the recursive window:
    PT_BASE_VA = 0xFFC00000
    PT for PDE = D is at:   PT_VA(D) = 0xFFC00000 + D * 0x1000

    Examples:
    - PT of PDE 0:        0xFFC00000
    - PT of KernelDir:    0xFFC00000 + KernelDir*0x1000
    - PT of PDE 1023:     0xFFC00000 + 1023*0x1000  (not used for mappings)


    Resolution path for any VMA:
           VMA
            │
       dir = VMA>>22  ------>  PD_VA[dir] (PDE)  ------>  PT_VA(dir)[tab] (PTE)  ------>  PA + ofs

    Kernel mappings installed at init:
    - PDE[0]         -> Low PT (identity map 0..4MB)
    - PDE[KernelDir] -> Kernel PT (maps VMA_KERNEL .. VMA_KERNEL+4MB-1)
    - PDE[1023]      -> PD itself (self-map)


    Temporary mapping mechanism (MapTemporaryPhysicalPage1):
    1) Two VAs reserved dynamically (e.g., G_TempLinear1, G_TempLinear2).
    2) To map a physical frame P into G_TempLinear1:
       - Compute dir/tab of G_TempLinear1
       - Write the PTE via the PT window:
           PT_VA(dir) = PT_BASE_VA + dir*0x1000, entry [tab]
       - Execute `invlpg [G_TempLinear1]`
       - The physical frame P is now accessible via the VA G_TempLinear1

    Simplified view of the two temporary pages:

                         (reserved via AllocRegion, not present by default)
    G_TempLinear1  -\    ┌────────────────────────────────────────────┐
                    |-─> │ PTE < (Present=1, RW=1, ..., Address=P>>12)│  map/unmap to chosen PA
    G_TempLinear2  -/    └────────────────────────────────────────────┘
                                   ^
                                   │ (written through) PT_VA(dir(G_TempLinearX)) = PT_BASE_VA + dir*0x1000
                                   │
                              PD self-map (PD_VA, PT_BASE_VA)

    PDE[1023] points to the Page Directory itself.
    PD_VA = 0xFFFFF000 gives access to the current PD (as PTE-like entries).
    PT_BASE_VA = 0xFFC00000 provides a window for Page Tables:
    PT for directory index D is at PT_BASE_VA + (D * PAGE_SIZE).

    Temporary physical access is done by remapping two reserved
    linear pages (G_TempLinear1, G_TempLinear2, G_TempLinear3) on demand.

    =================================================================

    PCI BAR mapping process (example: Intel E1000 NIC)

    ┌───────────────────────────┐
    │  PCI Configuration Space  │
    │  (accessed via PCI config │
    │   reads/writes)           │
    └───────────┬───────────────┘
                │
                │ Read BAR0 (Base Address Register #0)
                ▼
    ┌────────────────────────────────┐
    │ BAR0 value = Physical address  │
    │ of device registers (MMIO)     │
    │ + resource size                │
    └───────────┬────────────────────┘
                │
                │ Map physical MMIO region into
                │ kernel virtual space
                │ (uncached for DMA safety)
                ▼
    ┌───────────────────────────┐
    │ AllocRegion(Base=0,       │
    │   Target=BAR0,            │
    │   Size=MMIO size,         │
    │   Flags=ALLOC_PAGES_COMMIT│
    │         | ALLOC_PAGES_UC) │
    └───────────┬───────────────┘
                │
                │ Returns Linear (VMA) address
                │ where the driver can access MMIO
                ▼
    ┌───────────────────────────────┐
    │ Driver reads/writes registers │
    │ via *(volatile U32*)(VMA+ofs) │
    │ Example: E1000_CTRL register  │
    └───────────────────────────────┘

    NOTES:
    - MMIO (Memory-Mapped I/O) must be UNCACHED (UC) to avoid
     stale data and incorrect ordering.
    - BARs can also point to I/O port ranges instead of MMIO.
    - PCI devices can have multiple BARs for different resources.

\************************************************************************/

// Uncomment below to mark BIOS memory pages "not present" in the page tables

// #define PROTECT_BIOS
#define PROTECTED_ZONE_START 0xC0000
#define PROTECTED_ZONE_END 0xFFFFF

/************************************************************************/

KERNELDATA_I386 SECTION(".data") Kernel_i386 = {
    .IDT = NULL,
    .GDT = NULL,
    .TSS = NULL
};

/************************************************************************/

/**
 * @brief Set the handler address for an IDT gate descriptor.
 * @param Descriptor IDT entry to update.
 * @param Handler Linear address of the interrupt handler.
 */
void SetGateDescriptorOffset(LPGATE_DESCRIPTOR Descriptor, LINEAR Handler) {
    U32 Offset = (U32)Handler;

    Descriptor->Offset_00_15 = (U16)(Offset & 0x0000FFFFu);
    Descriptor->Offset_16_31 = (U16)((Offset >> 16) & 0x0000FFFFu);
}

/***************************************************************************/

/**
 * @brief Initialize an IDT gate descriptor.
 * @param Descriptor IDT entry to configure.
 * @param Handler Linear address of the interrupt handler.
 * @param Type Gate type to install.
 * @param Privilege Descriptor privilege level.
 */
void InitializeGateDescriptor(
    LPGATE_DESCRIPTOR Descriptor,
    LINEAR Handler,
    U16 Type,
    U16 Privilege) {
    Descriptor->Selector = SELECTOR_KERNEL_CODE;
    Descriptor->Reserved = 0;
    Descriptor->Type = Type;
    Descriptor->Privilege = Privilege;
    Descriptor->Present = 1;

    SetGateDescriptorOffset(Descriptor, Handler);
}

/************************************************************************/

/**
 * @brief Perform architecture-specific pre-initialization.
 */
void ArchPreInitializeKernel(void) {
    GDT_REGISTER Gdtr;

    ReadGlobalDescriptorTable(&Gdtr);
    Kernel_i386.GDT = (LPSEGMENT_DESCRIPTOR)(LINEAR)Gdtr.Base;
}

/************************************************************************/

/**
 * @brief Allocate a new page directory.
 * @return Physical address of the page directory or MAX_U32 on failure.
 */
PHYSICAL AllocPageDirectory(void) {
    PHYSICAL PMA_Directory = NULL;
    PHYSICAL PMA_LowTable = NULL;
    PHYSICAL PMA_KernelTable = NULL;
    PHYSICAL PMA_TaskRunnerTable = NULL;

    LPPAGE_DIRECTORY Directory = NULL;
    LPPAGE_TABLE LowTable = NULL;
    LPPAGE_TABLE KernelTable = NULL;
    LPPAGE_TABLE TaskRunnerTable = NULL;

    DEBUG(TEXT("[AllocPageDirectory] Enter"));

    UINT DirKernel = (VMA_KERNEL >> PAGE_TABLE_CAPACITY_MUL);           // 4MB directory slot for VMA_KERNEL
    UINT DirTaskRunner = (VMA_TASK_RUNNER >> PAGE_TABLE_CAPACITY_MUL);  // 4MB directory slot for VMA_TASK_RUNNER
    PHYSICAL PhysBaseKernel = KernelStartup.KernelPhysicalBase;                // Kernel physical base
    UINT Index;

    // Allocate required physical pages (PD + 3 PTs)
    PMA_Directory = AllocPhysicalPage();
    PMA_LowTable = AllocPhysicalPage();
    PMA_KernelTable = AllocPhysicalPage();
    PMA_TaskRunnerTable = AllocPhysicalPage();

    if (PMA_Directory == NULL || PMA_LowTable == NULL || PMA_KernelTable == NULL || PMA_TaskRunnerTable == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Out of physical pages"));
        goto Out_Error;
    }

    // Clear and prepare the Page Directory
    LINEAR VMA_PD = MapTemporaryPhysicalPage1(PMA_Directory);
    if (VMA_PD == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed on Directory"));
        goto Out_Error;
    }
    Directory = (LPPAGE_DIRECTORY)VMA_PD;
    MemorySet(Directory, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocPageDirectory] Page directory cleared"));

    // Directory[0] -> identity map 0..4MB via PMA_LowTable
    WritePageDirectoryEntryValue(
        Directory,
        0,
        MakePageDirectoryEntryValue(
            PMA_LowTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // Directory[DirKernel] -> map VMA_KERNEL..VMA_KERNEL+4MB-1 to KERNEL_PHYSICAL_ORIGIN..+4MB-1
    WritePageDirectoryEntryValue(
        Directory,
        DirKernel,
        MakePageDirectoryEntryValue(
            PMA_KernelTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // Directory[DirTaskRunner] -> map VMA_TASK_RUNNER (one page) to TaskRunner physical location
    WritePageDirectoryEntryValue(
        Directory,
        DirTaskRunner,
        MakePageDirectoryEntryValue(
            PMA_TaskRunnerTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // Install recursive mapping: PDE[1023] = PD
    WritePageDirectoryEntryValue(
        Directory,
        PD_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            PMA_Directory,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // Fill identity-mapped low table (0..4MB)
    LINEAR VMA_PT = MapTemporaryPhysicalPage2(PMA_LowTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed on LowTable"));
        goto Out_Error;
    }
    LowTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(LowTable, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocPageDirectory] Low memory table cleared"));

    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = (PHYSICAL)Index << PAGE_SIZE_MUL;

#ifdef PROTECT_BIOS
        BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);
#else
        BOOL Protected = FALSE;
#endif

        if (Protected) {
            ClearPageTableEntry(LowTable, Index);
        } else {
            WritePageTableEntryValue(
                LowTable,
                Index,
                MakePageTableEntryValue(
                    Physical,
                    /*ReadWrite*/ 1,
                    PAGE_PRIVILEGE_KERNEL,
                    /*WriteThrough*/ 0,
                    /*CacheDisabled*/ 0,
                    /*Global*/ 0,
                    /*Fixed*/ 1));
        }
    }

    // Fill kernel mapping table by copying the current kernel PT
    VMA_PT = MapTemporaryPhysicalPage2(PMA_KernelTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed on KernelTable"));
        goto Out_Error;
    }
    KernelTable = (LPPAGE_TABLE)VMA_PT;

    MemorySet(KernelTable, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocPageDirectory] Kernel table cleared"));

    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = PhysBaseKernel + ((PHYSICAL)Index << PAGE_SIZE_MUL);
        WritePageTableEntryValue(
            KernelTable,
            Index,
            MakePageTableEntryValue(
                Physical,
                /*ReadWrite*/ 1,
                PAGE_PRIVILEGE_KERNEL,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                /*Global*/ 0,
                /*Fixed*/ 1));
    }

    // Fill TaskRunner page table - only map the first page where TaskRunner is located
    VMA_PT = MapTemporaryPhysicalPage2(PMA_TaskRunnerTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed on TaskRunnerTable"));
        goto Out_Error;
    }
    TaskRunnerTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(TaskRunnerTable, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocPageDirectory] TaskRunner table cleared"));

    LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = KernelToPhysical(TaskRunnerLinear);

    DEBUG(TEXT("[AllocPageDirectory] TaskRunnerPhysical = %x + (%x - %x) = %x"),
        (UINT)PhysBaseKernel, (UINT)TaskRunnerLinear, (UINT)VMA_KERNEL, (UINT)TaskRunnerPhysical);

    UINT TaskRunnerTableIndex = GetTableEntry(VMA_TASK_RUNNER);

    WritePageTableEntryValue(
        TaskRunnerTable,
        TaskRunnerTableIndex,
        MakePageTableEntryValue(
            TaskRunnerPhysical,
            /*ReadWrite*/ 0,  // Read-only for user
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    // TLB sync before returning
    FlushTLB();

    DEBUG(TEXT("[AllocPageDirectory] PDE[0]=%x, PDE[768]=%x, PDE[%u]=%x, PDE[1023]=%x"),
        ReadPageDirectoryEntryValue(Directory, 0), ReadPageDirectoryEntryValue(Directory, 768), DirTaskRunner,
        ReadPageDirectoryEntryValue(Directory, DirTaskRunner), ReadPageDirectoryEntryValue(Directory, 1023));
    DEBUG(TEXT("[AllocPageDirectory] LowTable[0]=%x, KernelTable[0]=%x, TaskRunnerTable[%u]=%x"),
        ReadPageTableEntryValue(LowTable, 0), ReadPageTableEntryValue(KernelTable, 0), TaskRunnerTableIndex,
        ReadPageTableEntryValue(TaskRunnerTable, TaskRunnerTableIndex));
    DEBUG(TEXT("[AllocPageDirectory] TaskRunner VMA=%x -> Physical=%x"), VMA_TASK_RUNNER, TaskRunnerPhysical);

    DEBUG(TEXT("[AllocPageDirectory] Exit"));
    return PMA_Directory;

Out_Error:

    if (PMA_Directory) FreePhysicalPage(PMA_Directory);
    if (PMA_LowTable) FreePhysicalPage(PMA_LowTable);
    if (PMA_KernelTable) FreePhysicalPage(PMA_KernelTable);
    if (PMA_TaskRunnerTable) FreePhysicalPage(PMA_TaskRunnerTable);

    return NULL;
}

/************************************************************************/

/**
 * @brief Allocate a new page directory for userland processes.
 * @return Physical address of the page directory or NULL on failure.
 */
PHYSICAL AllocUserPageDirectory(void) {
    PHYSICAL PMA_Directory = NULL;
    PHYSICAL PMA_LowTable = NULL;
    PHYSICAL PMA_KernelTable = NULL;

    LPPAGE_DIRECTORY Directory = NULL;
    LPPAGE_TABLE LowTable = NULL;
    LPPAGE_TABLE KernelTable = NULL;
    LPPAGE_DIRECTORY CurrentPD = (LPPAGE_DIRECTORY)PD_VA;

    DEBUG(TEXT("[AllocUserPageDirectory] Enter"));

    UINT DirKernel = (VMA_KERNEL >> PAGE_TABLE_CAPACITY_MUL);
    PHYSICAL PhysBaseKernel = KernelStartup.KernelPhysicalBase;
    UINT Index;

    PMA_Directory = AllocPhysicalPage();
    PMA_LowTable = AllocPhysicalPage();
    PMA_KernelTable = AllocPhysicalPage();

    if (PMA_Directory == NULL || PMA_LowTable == NULL || PMA_KernelTable == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] Out of physical pages"));
        goto Out_Error;
    }

    LINEAR VMA_PD = MapTemporaryPhysicalPage1(PMA_Directory);
    if (VMA_PD == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] MapTemporaryPhysicalPage1 failed on Directory"));
        goto Out_Error;
    }
    Directory = (LPPAGE_DIRECTORY)VMA_PD;
    MemorySet(Directory, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocUserPageDirectory] Page directory cleared"));

    WritePageDirectoryEntryValue(
        Directory,
        0,
        MakePageDirectoryEntryValue(
            PMA_LowTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Directory,
        DirKernel,
        MakePageDirectoryEntryValue(
            PMA_KernelTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    UNUSED(VMA_TASK_RUNNER);
    UINT UserStartPDE = GetDirectoryEntry(VMA_USER);
    UINT UserEndPDE = GetDirectoryEntry(VMA_LIBRARY - 1) - 1;
    for (Index = 1; Index < 1023; Index++) {
        if (PageDirectoryEntryIsPresent(CurrentPD, Index) && Index != DirKernel) {
            if (Index >= UserStartPDE && Index <= UserEndPDE) {
                DEBUG(TEXT("[AllocUserPageDirectory] Skipped user space PDE[%u]"), Index);
                continue;
            }
            WritePageDirectoryEntryValue(Directory, Index, ReadPageDirectoryEntryValue(CurrentPD, Index));
            DEBUG(TEXT("[AllocUserPageDirectory] Copied PDE[%u]"), Index);
        }
    }

    WritePageDirectoryEntryValue(
        Directory,
        PD_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            PMA_Directory,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    LINEAR VMA_PT = MapTemporaryPhysicalPage2(PMA_LowTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] MapTemporaryPhysicalPage2 failed on LowTable"));
        goto Out_Error;
    }
    LowTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(LowTable, 0, PAGE_SIZE);

    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = (PHYSICAL)Index << PAGE_SIZE_MUL;

#ifdef PROTECT_BIOS
        BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);
#else
        BOOL Protected = FALSE;
#endif

        if (Protected) {
            ClearPageTableEntry(LowTable, Index);
        } else {
            WritePageTableEntryValue(
                LowTable,
                Index,
                MakePageTableEntryValue(
                    Physical,
                    /*ReadWrite*/ 1,
                    PAGE_PRIVILEGE_KERNEL,
                    /*WriteThrough*/ 0,
                    /*CacheDisabled*/ 0,
                    /*Global*/ 0,
                    /*Fixed*/ 1));
        }
    }

    DEBUG(TEXT("[AllocUserPageDirectory] Low memory table copied from current"));

    VMA_PT = MapTemporaryPhysicalPage2(PMA_KernelTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] MapTemporaryPhysicalPage2 failed on KernelTable"));
        goto Out_Error;
    }
    KernelTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(KernelTable, 0, PAGE_SIZE);

    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = PhysBaseKernel + ((PHYSICAL)Index << PAGE_SIZE_MUL);
        WritePageTableEntryValue(
            KernelTable,
            Index,
            MakePageTableEntryValue(
                Physical,
                /*ReadWrite*/ 1,
                PAGE_PRIVILEGE_KERNEL,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                /*Global*/ 0,
                /*Fixed*/ 1));
    }

    DEBUG(TEXT("[AllocUserPageDirectory] Basic kernel mapping created"));

    FlushTLB();

    DEBUG(TEXT("[AllocUserPageDirectory] PDE[0]=%x, PDE[768]=%x, PDE[1023]=%x"),
        ReadPageDirectoryEntryValue(Directory, 0),
        ReadPageDirectoryEntryValue(Directory, 768),
        ReadPageDirectoryEntryValue(Directory, 1023));
    DEBUG(TEXT("[AllocUserPageDirectory] LowTable[0]=%x, KernelTable[0]=%x"),
        ReadPageTableEntryValue(LowTable, 0),
        ReadPageTableEntryValue(KernelTable, 0));

    DEBUG(TEXT("[AllocUserPageDirectory] Exit"));
    return PMA_Directory;

Out_Error:

    if (PMA_Directory) FreePhysicalPage(PMA_Directory);
    if (PMA_LowTable) FreePhysicalPage(PMA_LowTable);
    if (PMA_KernelTable) FreePhysicalPage(PMA_KernelTable);

    return NULL;
}

/************************************************************************/

void InitSegmentDescriptor(LPSEGMENT_DESCRIPTOR This, U32 Type) {
    MemorySet(This, 0, sizeof(SEGMENT_DESCRIPTOR));

    This->Limit_00_15 = 0xFFFF;
    This->Base_00_15 = 0x0000;
    This->Base_16_23 = 0x00;
    This->Accessed = 0;
    This->CanWrite = 1;
    This->ConformExpand = 0;  // Expand-up for data, Conforming for code
    This->Type = Type;
    This->Segment = 1;
    This->Privilege = PRIVILEGE_USER;
    This->Present = 1;
    This->Limit_16_19 = 0x0F;
    This->Available = 0;
    This->OperandSize = 1;
    This->Granularity = GDT_GRANULAR_4KB;
    This->Base_24_31 = 0x00;
}

/***************************************************************************/

void InitializeGlobalDescriptorTable(LPSEGMENT_DESCRIPTOR Table) {
    DEBUG(TEXT("[InitializeGlobalDescriptorTable] Enter"));

    DEBUG(TEXT("[InitializeGlobalDescriptorTable] GDT address = %X"), (U32)Table);

    MemorySet(Table, 0, GDT_SIZE);

    InitSegmentDescriptor(&Table[1], GDT_TYPE_CODE);
    Table[1].Privilege = GDT_PRIVILEGE_KERNEL;

    InitSegmentDescriptor(&Table[2], GDT_TYPE_DATA);
    Table[2].Privilege = GDT_PRIVILEGE_KERNEL;

    InitSegmentDescriptor(&Table[3], GDT_TYPE_CODE);
    Table[3].Privilege = GDT_PRIVILEGE_USER;

    InitSegmentDescriptor(&Table[4], GDT_TYPE_DATA);
    Table[4].Privilege = GDT_PRIVILEGE_USER;

    InitSegmentDescriptor(&Table[5], GDT_TYPE_CODE);
    Table[5].Privilege = GDT_PRIVILEGE_KERNEL;
    Table[5].OperandSize = GDT_OPERANDSIZE_16;
    Table[5].Granularity = GDT_GRANULAR_1B;
    SetSegmentDescriptorLimit(&Table[5], N_1MB_M1);

    InitSegmentDescriptor(&Table[6], GDT_TYPE_DATA);
    Table[6].Privilege = GDT_PRIVILEGE_KERNEL;
    Table[6].OperandSize = GDT_OPERANDSIZE_16;
    Table[6].Granularity = GDT_GRANULAR_1B;
    SetSegmentDescriptorLimit(&Table[6], N_1MB_M1);

    DEBUG(TEXT("[InitializeGlobalDescriptorTable] Exit"));
}

/***************************************************************************/

void InitializeTaskSegments(void) {
    DEBUG(TEXT("[InitializeTaskSegments] Enter"));

    U32 TssSize = sizeof(TASK_STATE_SEGMENT);

    Kernel_i386.TSS = (LPTASK_STATE_SEGMENT)AllocKernelRegion(0, TssSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Kernel_i386.TSS == NULL) {
        ERROR(TEXT("[InitializeTaskSegments] AllocRegion for TSS failed"));
        DO_THE_SLEEPING_BEAUTY;
    }

    MemorySet(Kernel_i386.TSS, 0, TssSize);

    LPTSS_DESCRIPTOR Desc = (LPTSS_DESCRIPTOR)(Kernel_i386.GDT + GDT_TSS_INDEX);
    Desc->Type = GATE_TYPE_386_TSS_AVAIL;
    Desc->Privilege = GDT_PRIVILEGE_USER;
    Desc->Present = 1;
    Desc->Granularity = GDT_GRANULAR_1B;
    SetTSSDescriptorBase(Desc, (U32)Kernel_i386.TSS);
    SetTSSDescriptorLimit(Desc, sizeof(TASK_STATE_SEGMENT) - 1);

    DEBUG(TEXT("[InitializeTaskSegments] TSS = %X"), Kernel_i386.TSS);
    DEBUG(TEXT("[InitializeTaskSegments] Exit"));
}

/***************************************************************************/

void SetSegmentDescriptorBase(LPSEGMENT_DESCRIPTOR This, U32 Base) {
    This->Base_00_15 = (Base & (U32)0x0000FFFF) >> 0x00;
    This->Base_16_23 = (Base & (U32)0x00FF0000) >> 0x10;
    This->Base_24_31 = (Base & (U32)0xFF000000) >> 0x18;
}

/***************************************************************************/

void SetSegmentDescriptorLimit(LPSEGMENT_DESCRIPTOR This, U32 Limit) {
    This->Limit_00_15 = (Limit >> 0x00) & 0x0000FFFF;
    This->Limit_16_19 = (Limit >> 0x10) & 0x0000000F;
}

/***************************************************************************/

void SetTSSDescriptorBase(LPTSS_DESCRIPTOR This, U32 Base) {
    SetSegmentDescriptorBase((LPSEGMENT_DESCRIPTOR)This, Base);
}

/***************************************************************************/

void SetTSSDescriptorLimit(LPTSS_DESCRIPTOR This, U32 Limit) {
    SetSegmentDescriptorLimit((LPSEGMENT_DESCRIPTOR)This, Limit);
}

/************************************************************************/

BOOL GetSegmentInfo(LPSEGMENT_DESCRIPTOR This, LPSEGMENT_INFO Info) {
    if (Info) {
        Info->Base = SEGMENTBASE(This);
        Info->Limit = SEGMENTLIMIT(This);
        Info->Type = This->Type;
        Info->Privilege = This->Privilege;
        Info->Granularity = SEGMENTGRANULAR(This);
        Info->CanWrite = This->CanWrite;
        Info->OperandSize = This->OperandSize ? 32 : 16;
        Info->Conforming = This->ConformExpand;
        Info->Present = This->Present;

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

BOOL SegmentInfoToString(LPSEGMENT_INFO This, LPSTR Text) {
    if (This && Text) {
        STR Temp[64];

        Text[0] = STR_NULL;

        StringConcat(Text, TEXT("Segment"));
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Base           : "));
        U32ToHexString(This->Base, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Limit          : "));
        U32ToHexString(This->Limit, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Type           : "));
        StringConcat(Text, This->Type ? TEXT("Code") : TEXT("Data"));
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Privilege      : "));
        U32ToHexString(This->Privilege, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Granularity    : "));
        U32ToHexString(This->Granularity, Temp);
        StringConcat(Text, Temp);
        StringConcat(Text, Text_NewLine);

        StringConcat(Text, TEXT("Can write      : "));
        StringConcat(Text, This->CanWrite ? TEXT("True") : TEXT("False"));
        StringConcat(Text, Text_NewLine);

        /*
            StringConcat(Text, "Operand Size   : ");
            StringConcat(Text, This->OperandSize ? "32-bit" : "16-bit");
            StringConcat(Text, Text_NewLine);

            StringConcat(Text, "Conforming     : ");
            StringConcat(Text, This->Conforming ? "True" : "False");
            StringConcat(Text, Text_NewLine);

            StringConcat(Text, "Present        : ");
            StringConcat(Text, This->Present ? "True" : "False");
            StringConcat(Text, Text_NewLine);
        */

        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Perform i386-specific initialisation for a freshly created task.
 *
 * Allocates and clears the user and system stacks, seeds the interrupt frame
 * with the correct segment selectors, and configures the boot-time stack when
 * creating the main kernel task. The generic CreateTask routine handles the
 * architecture-neutral bookkeeping and delegates the hardware specific work to
 * this helper.
 */
BOOL SetupTask(struct tag_TASK* Task, struct tag_PROCESS* Process, struct tag_TASKINFO* Info) {
    LINEAR BaseVMA = VMA_KERNEL;
    SELECTOR CodeSelector = SELECTOR_KERNEL_CODE;
    SELECTOR DataSelector = SELECTOR_KERNEL_DATA;
    LINEAR StackTop;
    LINEAR SysStackTop;
    LINEAR BootStackTop;
    LINEAR ESP, EBP;
    U32 CR4;

    DEBUG(TEXT("[SetupTask] Enter"));

    if (Process->Privilege == PRIVILEGE_USER) {
        BaseVMA = VMA_USER;
        CodeSelector = SELECTOR_USER_CODE;
        DataSelector = SELECTOR_USER_DATA;
    }

    Task->Arch.StackSize = Info->StackSize;
    Task->Arch.SysStackSize = TASK_SYSTEM_STACK_SIZE * 4;

    Task->Arch.StackBase =
        AllocRegion(BaseVMA, 0, Task->Arch.StackSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER);
    Task->Arch.SysStackBase =
        AllocKernelRegion(0, Task->Arch.SysStackSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    DEBUG(TEXT("[SetupTask] BaseVMA=%p, Requested StackBase at BaseVMA"), BaseVMA);
    DEBUG(TEXT("[SetupTask] Actually got StackBase=%p"), Task->Arch.StackBase);

    if (Task->Arch.StackBase == NULL || Task->Arch.SysStackBase == NULL) {
        if (Task->Arch.StackBase != NULL) {
            FreeRegion(Task->Arch.StackBase, Task->Arch.StackSize);
            Task->Arch.StackBase = NULL;
            Task->Arch.StackSize = 0;
        }

        if (Task->Arch.SysStackBase != NULL) {
            FreeRegion(Task->Arch.SysStackBase, Task->Arch.SysStackSize);
            Task->Arch.SysStackBase = NULL;
            Task->Arch.SysStackSize = 0;
        }

        ERROR(TEXT("[SetupTask] Stack or system stack allocation failed"));
        return FALSE;
    }

    DEBUG(TEXT("[SetupTask] Stack (%u bytes) allocated at %p"), Task->Arch.StackSize, Task->Arch.StackBase);
    DEBUG(TEXT("[SetupTask] System stack (%u bytes) allocated at %p"), Task->Arch.SysStackSize, Task->Arch.SysStackBase);

    MemorySet((LPVOID)(Task->Arch.StackBase), 0, Task->Arch.StackSize);
    MemorySet((LPVOID)(Task->Arch.SysStackBase), 0, Task->Arch.SysStackSize);
    MemorySet(&(Task->Arch.Context), 0, sizeof(Task->Arch.Context));

    GetCR4(CR4);

    Task->Arch.Context.Registers.EAX = (UINT)Task->Parameter;
    Task->Arch.Context.Registers.EBX = (LINEAR)Task->Function;
    Task->Arch.Context.Registers.ECX = 0;
    Task->Arch.Context.Registers.EDX = 0;
    Task->Arch.Context.Registers.CS = CodeSelector;
    Task->Arch.Context.Registers.DS = DataSelector;
    Task->Arch.Context.Registers.ES = DataSelector;
    Task->Arch.Context.Registers.FS = DataSelector;
    Task->Arch.Context.Registers.GS = DataSelector;
    Task->Arch.Context.Registers.SS = DataSelector;
    Task->Arch.Context.Registers.EFlags = EFLAGS_IF | EFLAGS_A1;
    Task->Arch.Context.Registers.CR3 = Process->PageDirectory;
    Task->Arch.Context.Registers.CR4 = CR4;

    StackTop = Task->Arch.StackBase + Task->Arch.StackSize;
    SysStackTop = Task->Arch.SysStackBase + Task->Arch.SysStackSize;

    if (Process->Privilege == PRIVILEGE_KERNEL) {
        DEBUG(TEXT("[SetupTask] Setting kernel privilege (ring 0)"));
        Task->Arch.Context.Registers.EIP = (LINEAR)TaskRunner;
        Task->Arch.Context.Registers.ESP = StackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.EBP = StackTop - STACK_SAFETY_MARGIN;
    } else {
        DEBUG(TEXT("[SetupTask] Setting user privilege (ring 3)"));
        Task->Arch.Context.Registers.EIP = VMA_TASK_RUNNER;
        Task->Arch.Context.Registers.ESP = SysStackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.EBP = SysStackTop - STACK_SAFETY_MARGIN;
    }

    if (Info->Flags & TASK_CREATE_MAIN_KERNEL) {
        Task->Status = TASK_STATUS_RUNNING;

        Kernel_i386.TSS->ESP0 = SysStackTop - STACK_SAFETY_MARGIN;

        BootStackTop = KernelStartup.StackTop;

        GetESP(ESP);
        UINT StackUsed = (BootStackTop - ESP) + 256;

        DEBUG(TEXT("[SetupTask] BootStackTop = %p"), BootStackTop);
        DEBUG(TEXT("[SetupTask] StackTop = %p"), StackTop);
        DEBUG(TEXT("[SetupTask] StackUsed = %u"), StackUsed);
        DEBUG(TEXT("[SetupTask] Switching to new stack..."));

        if (SwitchStack(StackTop, BootStackTop, StackUsed) == TRUE) {
            Task->Arch.Context.Registers.ESP = 0;
            GetEBP(EBP);
            Task->Arch.Context.Registers.EBP = EBP;
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
 * @brief Prepares architecture-specific state for the next task switch.
 *
 * Saves the current task's segment and FPU state, configures the TSS and
 * kernel stack for the next task, loads its address space and restores its
 * segment and FPU state so that SwitchToNextTask_3 can perform the generic
 * scheduling steps.
 */
void PrepareNextTaskSwitch(struct tag_TASK* CurrentTask, struct tag_TASK* NextTask) {
    SAFE_USE(NextTask) {
        LINEAR NextSysStackTop = NextTask->Arch.SysStackBase + NextTask->Arch.SysStackSize;

        Kernel_i386.TSS->SS0 = SELECTOR_KERNEL_DATA;
        Kernel_i386.TSS->ESP0 = NextSysStackTop - STACK_SAFETY_MARGIN;

        SAFE_USE(CurrentTask) {
            GetFS(CurrentTask->Arch.Context.Registers.FS);
            GetGS(CurrentTask->Arch.Context.Registers.GS);
            SaveFPU((LPVOID)&(CurrentTask->Arch.Context.FPURegisters));
        }

        LoadPageDirectory(NextTask->Process->PageDirectory);

        SetDS(NextTask->Arch.Context.Registers.DS);
        SetES(NextTask->Arch.Context.Registers.ES);
        SetFS(NextTask->Arch.Context.Registers.FS);
        SetGS(NextTask->Arch.Context.Registers.GS);

        RestoreFPU(&(NextTask->Arch.Context.FPURegisters));
    }
}

/************************************************************************/

/**
 * @brief Architecture-specific memory manager initialization for i386.
 */
void InitializeMemoryManager(void) {
    DEBUG(TEXT("[InitializeMemoryManager] Enter"));

    UpdateKernelMemoryMetricsFromMultibootMap();

    if (KernelStartup.PageCount == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    UINT BitmapBytes = (KernelStartup.PageCount + 7u) >> MUL_8;
    UINT BitmapBytesAligned = (UINT)PAGE_ALIGN(BitmapBytes);

    PHYSICAL KernelSpan = (PHYSICAL)KernelStartup.KernelSize + (PHYSICAL)N_512KB;
    PHYSICAL MapSize = (PHYSICAL)PAGE_ALIGN(KernelSpan);
    PHYSICAL LoaderReservedEnd = KernelStartup.KernelPhysicalBase + MapSize;
    PHYSICAL PpbPhysical = PAGE_ALIGN(LoaderReservedEnd);

    Kernel.PPB = (LPPAGEBITMAP)(UINT)PpbPhysical;
    Kernel.PPBSize = BitmapBytesAligned;

    DEBUG(TEXT("[InitializeMemoryManager] Kernel.PPB physical base: %p"), (LPVOID)PpbPhysical);
    DEBUG(TEXT("[InitializeMemoryManager] Kernel.PPB bytes (aligned): %lX"), BitmapBytesAligned);

    MemorySet(Kernel.PPB, 0, Kernel.PPBSize);

    MarkUsedPhysicalMemory();

    if (KernelStartup.MemorySize == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    DEBUG(TEXT("[InitializeMemoryManager] Temp pages reserved: %p, %p, %p"),
        I386_TEMP_LINEAR_PAGE_1,
        I386_TEMP_LINEAR_PAGE_2,
        I386_TEMP_LINEAR_PAGE_3);

    PHYSICAL NewPageDirectory = AllocPageDirectory();

    LogPageDirectory(NewPageDirectory);

    DEBUG(TEXT("[InitializeMemoryManager] Page directory ready"));

    if (NewPageDirectory == NULL) {
        ERROR(TEXT("[InitializeMemoryManager] AllocPageDirectory failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    DEBUG(TEXT("[InitializeMemoryManager] New page directory: %p"), NewPageDirectory);

    LoadPageDirectory(NewPageDirectory);

    DEBUG(TEXT("[InitializeMemoryManager] Page directory set: %p"), NewPageDirectory);

    FlushTLB();

    DEBUG(TEXT("[InitializeMemoryManager] TLB flushed"));

    Kernel_i386.GDT = (LPSEGMENT_DESCRIPTOR)AllocKernelRegion(0, GDT_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Kernel_i386.GDT == NULL) {
        ERROR(TEXT("[InitializeMemoryManager] AllocRegion for GDT failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    InitializeGlobalDescriptorTable(Kernel_i386.GDT);

    DEBUG(TEXT("[InitializeMemoryManager] Loading GDT"));

    LoadGlobalDescriptorTable((PHYSICAL)Kernel_i386.GDT, GDT_SIZE - 1);

    for (UINT Index = 0; Index < 10; Index++) {
        DEBUG(TEXT("[InitializeMemoryManager] GDT[%u]=%x %x"), Index, ((U32*)(Kernel_i386.GDT))[Index * 2 + 1],
            ((U32*)(Kernel_i386.GDT))[Index * 2]);
    }

    DEBUG(TEXT("[InitializeMemoryManager] Exit"));
}

/************************************************************************/

/**
 * @brief Translate a linear address to its physical counterpart (page-level granularity).
 * @param Address Linear address.
 * @return Physical address or 0 when unmapped.
 */
PHYSICAL MapLinearToPhysical(LINEAR Address) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Address);
    UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(&Iterator);
    UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);

    if (!PageDirectoryEntryIsPresent(Directory, DirEntry)) return 0;

    LPPAGE_TABLE Table = MemoryPageIteratorGetTable(&Iterator);
    if (!PageTableEntryIsPresent(Table, TabEntry)) return 0;

    PHYSICAL PagePhysical = PageTableEntryGetPhysical(Table, TabEntry);
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
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    UINT DirectoryIndex = GetDirectoryEntry(Address);
    UINT TableIndex = GetTableEntry(Address);

    if (Directory == NULL) return FALSE;
    if (DirectoryIndex >= PAGE_TABLE_NUM_ENTRIES) return FALSE;
    if (TableIndex >= PAGE_TABLE_NUM_ENTRIES) return FALSE;

    if (PageDirectoryEntryIsPresent(Directory, DirectoryIndex) == FALSE) return FALSE;

    LPPAGE_TABLE Table = GetPageTableVAFor(Address);
    if (Table == NULL) return FALSE;
    if (PageTableEntryIsPresent(Table, TableIndex) == FALSE) return FALSE;

    return TRUE;
}

/************************************************************************/
