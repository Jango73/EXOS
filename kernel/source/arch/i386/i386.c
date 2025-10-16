
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
#include "Process.h"
#include "arch/i386/i386-Log.h"
#include "Stack.h"
#include "CoreString.h"
#include "System.h"
#include "Task.h"
#include "Text.h"
#include "Kernel.h"

/************************************************************************/
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
    PHYSICAL TaskRunnerPhysical = PhysBaseKernel + (PHYSICAL)(TaskRunnerLinear - VMA_KERNEL);

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
BOOL ArchSetupTask(struct tag_TASK* Task, struct tag_PROCESS* Process, struct tag_TASKINFO* Info) {
    LINEAR BaseVMA = VMA_KERNEL;
    SELECTOR CodeSelector = SELECTOR_KERNEL_CODE;
    SELECTOR DataSelector = SELECTOR_KERNEL_DATA;

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

    DEBUG(TEXT("[ArchSetupTask] BaseVMA=%X, Requested StackBase at BaseVMA"), BaseVMA);
    DEBUG(TEXT("[ArchSetupTask] Actually got StackBase=%X"), Task->Arch.StackBase);

    if (Task->Arch.StackBase == NULL || Task->Arch.SysStackBase == NULL) {
        if (Task->Arch.StackBase) {
            FreeRegion(Task->Arch.StackBase, Task->Arch.StackSize);
            Task->Arch.StackBase = NULL;
            Task->Arch.StackSize = 0;
        }

        if (Task->Arch.SysStackBase) {
            FreeRegion(Task->Arch.SysStackBase, Task->Arch.SysStackSize);
            Task->Arch.SysStackBase = NULL;
            Task->Arch.SysStackSize = 0;
        }

        ERROR(TEXT("[ArchSetupTask] Stack or system stack allocation failed"));
        return FALSE;
    }

    DEBUG(TEXT("[ArchSetupTask] Stack (%X bytes) allocated at %X"), Task->Arch.StackSize, Task->Arch.StackBase);
    DEBUG(TEXT("[ArchSetupTask] System stack (%X bytes) allocated at %X"), Task->Arch.SysStackSize, Task->Arch.SysStackBase);

    MemorySet((void*)(Task->Arch.StackBase), 0, Task->Arch.StackSize);
    MemorySet((void*)(Task->Arch.SysStackBase), 0, Task->Arch.SysStackSize);

    MemorySet(&(Task->Arch.Context), 0, sizeof(INTERRUPT_FRAME));

    Task->Arch.Context.Registers.EAX = (U32)Task->Parameter;
    Task->Arch.Context.Registers.EBX = (U32)Task->Function;
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
    U32 ControlRegister4;
    GetCR4(ControlRegister4);
    Task->Arch.Context.Registers.CR4 = ControlRegister4;
    Task->Arch.Context.Registers.EIP = VMA_TASK_RUNNER;

    LINEAR StackTop = Task->Arch.StackBase + Task->Arch.StackSize;
    LINEAR SysStackTop = Task->Arch.SysStackBase + Task->Arch.SysStackSize;

    if (Process->Privilege == PRIVILEGE_KERNEL) {
        DEBUG(TEXT("[ArchSetupTask] Setting kernel privilege (ring 0)"));
        Task->Arch.Context.Registers.ESP = StackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.EBP = StackTop - STACK_SAFETY_MARGIN;
    } else {
        DEBUG(TEXT("[ArchSetupTask] Setting user privilege (ring 3)"));
        Task->Arch.Context.Registers.ESP = SysStackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.EBP = SysStackTop - STACK_SAFETY_MARGIN;
    }

    if (Info->Flags & TASK_CREATE_MAIN_KERNEL) {
        Task->Status = TASK_STATUS_RUNNING;

        Kernel_i386.TSS->ESP0 = SysStackTop - STACK_SAFETY_MARGIN;

        LINEAR BootStackTop = KernelStartup.StackTop;
        LINEAR ESP;
        GetESP(ESP);
        UINT StackUsed = (BootStackTop - ESP) + 256;

        DEBUG(TEXT("[ArchSetupTask] BootStackTop = %X"), BootStackTop);
        DEBUG(TEXT("[ArchSetupTask] StackTop = %X"), StackTop);
        DEBUG(TEXT("[ArchSetupTask] StackUsed = %X"), StackUsed);
        DEBUG(TEXT("[ArchSetupTask] Switching to new stack..."));

        if (SwitchStack(StackTop, BootStackTop, StackUsed)) {
            Task->Arch.Context.Registers.ESP = 0;  // Not used for main task
            LINEAR CurrentEbp;
            GetEBP(CurrentEbp);
            Task->Arch.Context.Registers.EBP = CurrentEbp;
            DEBUG(TEXT("[ArchSetupTask] Main task stack switched successfully"));
        } else {
            ERROR(TEXT("[ArchSetupTask] Stack switch failed"));
        }
    }

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
void ArchPrepareNextTaskSwitch(struct tag_TASK* CurrentTask, struct tag_TASK* NextTask) {
    LINEAR NextSysStackTop = NextTask->Arch.SysStackBase + NextTask->Arch.SysStackSize;

    Kernel_i386.TSS->SS0 = SELECTOR_KERNEL_DATA;
    Kernel_i386.TSS->ESP0 = NextSysStackTop - STACK_SAFETY_MARGIN;

    GetFS(CurrentTask->Arch.Context.Registers.FS);
    GetGS(CurrentTask->Arch.Context.Registers.GS);

    SaveFPU((LPVOID)&(CurrentTask->Arch.Context.FPURegisters));

    LoadPageDirectory(NextTask->Process->PageDirectory);

    SetDS(NextTask->Arch.Context.Registers.DS);
    SetES(NextTask->Arch.Context.Registers.ES);
    SetFS(NextTask->Arch.Context.Registers.FS);
    SetGS(NextTask->Arch.Context.Registers.GS);

    RestoreFPU(&(NextTask->Arch.Context.FPURegisters));
}

/************************************************************************/

/**
 * @brief Architecture-specific memory manager initialization for i386.
 */
void ArchInitializeMemoryManager(void) {
    DEBUG(TEXT("[ArchInitializeMemoryManager] Enter"));

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

    DEBUG(TEXT("[ArchInitializeMemoryManager] Kernel.PPB physical base: %p"), (LPVOID)PpbPhysical);
    DEBUG(TEXT("[ArchInitializeMemoryManager] Kernel.PPB bytes (aligned): %lX"), BitmapBytesAligned);

    MemorySet(Kernel.PPB, 0, Kernel.PPBSize);

    MarkUsedPhysicalMemory();

    if (KernelStartup.MemorySize == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    LINEAR TempLinear1 = I386_TEMP_LINEAR_PAGE_1;
    LINEAR TempLinear2 = I386_TEMP_LINEAR_PAGE_2;
    LINEAR TempLinear3 = I386_TEMP_LINEAR_PAGE_3;

    MemorySetTemporaryLinearPages(TempLinear1, TempLinear2, TempLinear3);

    DEBUG(TEXT("[ArchInitializeMemoryManager] Temp pages reserved: %p, %p, %p"), TempLinear1, TempLinear2, TempLinear3);

    PHYSICAL NewPageDirectory = AllocPageDirectory();

    LogPageDirectory(NewPageDirectory);

    DEBUG(TEXT("[ArchInitializeMemoryManager] Page directory ready"));

    if (NewPageDirectory == NULL) {
        ERROR(TEXT("[ArchInitializeMemoryManager] AllocPageDirectory failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    DEBUG(TEXT("[ArchInitializeMemoryManager] New page directory: %p"), NewPageDirectory);

    LoadPageDirectory(NewPageDirectory);

    DEBUG(TEXT("[ArchInitializeMemoryManager] Page directory set: %p"), NewPageDirectory);

    FlushTLB();

    DEBUG(TEXT("[ArchInitializeMemoryManager] TLB flushed"));

    if (TempLinear1 == 0 || TempLinear2 == 0) {
        ERROR(TEXT("[ArchInitializeMemoryManager] Failed to reserve temp linear pages"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    Kernel_i386.GDT = (LPSEGMENT_DESCRIPTOR)AllocKernelRegion(0, GDT_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Kernel_i386.GDT == NULL) {
        ERROR(TEXT("[ArchInitializeMemoryManager] AllocRegion for GDT failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    InitializeGlobalDescriptorTable(Kernel_i386.GDT);

    DEBUG(TEXT("[ArchInitializeMemoryManager] Loading GDT"));

    LoadGlobalDescriptorTable((PHYSICAL)Kernel_i386.GDT, GDT_SIZE - 1);

    for (UINT Index = 0; Index < 10; Index++) {
        DEBUG(TEXT("[ArchInitializeMemoryManager] GDT[%u]=%x %x"), Index, ((U32*)(Kernel_i386.GDT))[Index * 2 + 1],
            ((U32*)(Kernel_i386.GDT))[Index * 2]);
    }

    DEBUG(TEXT("[ArchInitializeMemoryManager] Exit"));
}
