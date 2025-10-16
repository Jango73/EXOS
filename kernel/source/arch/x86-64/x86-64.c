
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
#include "CoreString.h"
#include "System.h"
#include "Text.h"

/***************************************************************************/

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

/***************************************************************************/

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

/**
 * @brief Perform architecture-specific pre-initialization.
 */
void ArchPreInitializeKernel(void) {
    GDT_REGISTER Gdtr;

    ReadGlobalDescriptorTable(&Gdtr);
    Kernel_i386.GDT = (LPVOID)(LINEAR)Gdtr.Base;
}

/************************************************************************/

/**
 * @brief Allocate a new page directory.
 * @return Physical address of the page directory or NULL on failure.
 */
PHYSICAL AllocPageDirectory(void) {
    DEBUG(TEXT("[AllocPageDirectory] Enter"));

    PHYSICAL Pml4Physical = AllocPhysicalPage();

    if (Pml4Physical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Out of physical pages"));
        return NULL;
    }

    LINEAR VMA_Pml4 = MapTemporaryPhysicalPage1(Pml4Physical);

    if (VMA_Pml4 == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed on PML4"));
        FreePhysicalPage(Pml4Physical);
        return NULL;
    }

    LPPAGE_DIRECTORY NewPml4 = (LPPAGE_DIRECTORY)VMA_Pml4;
    LPPML4 CurrentPml4 = GetCurrentPml4VA();

    MemoryCopy(NewPml4, CurrentPml4, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        NewPml4,
        PML4_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            Pml4Physical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    FlushTLB();

    DEBUG(TEXT("[AllocPageDirectory] PML4 cloned at %p"), (LINEAR)Pml4Physical);

    return Pml4Physical;
}

/************************************************************************/

/**
 * @brief Allocate a new page directory for userland processes.
 * @return Physical address of the page directory or NULL on failure.
 */
PHYSICAL AllocUserPageDirectory(void) {
    DEBUG(TEXT("[AllocUserPageDirectory] Enter"));

    PHYSICAL PageDirectory = AllocPageDirectory();

    if (PageDirectory == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] AllocPageDirectory failed"));
        return NULL;
    }

    DEBUG(TEXT("[AllocUserPageDirectory] Exit"));
    return PageDirectory;
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
    U64 StackTop;
    U64 SysStackTop;
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

    StackTop = Task->Arch.StackBase + (U64)Task->Arch.StackSize;
    SysStackTop = Task->Arch.SysStackBase + (U64)Task->Arch.SysStackSize;

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
        WARNING(TEXT("[ArchSetupTask] Main kernel stack handoff not implemented on x86-64"));
    }

    DEBUG(TEXT("[ArchSetupTask] Exit"));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Architecture-specific memory manager initialization for x86-64.
 */
void ArchInitializeMemoryManager(void) {
    DEBUG(TEXT("[ArchInitializeMemoryManager] Enter"));

    LINEAR TempLinear1 = (LINEAR)X86_64_TEMP_LINEAR_PAGE_1;
    LINEAR TempLinear2 = (LINEAR)X86_64_TEMP_LINEAR_PAGE_2;
    LINEAR TempLinear3 = (LINEAR)X86_64_TEMP_LINEAR_PAGE_3;

    MemorySetTemporaryLinearPages(TempLinear1, TempLinear2, TempLinear3);

    DEBUG(TEXT("[ArchInitializeMemoryManager] Temp pages reserved: %p, %p, %p"),
        (LPVOID)TempLinear1,
        (LPVOID)TempLinear2,
        (LPVOID)TempLinear3);

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

    LogPageDirectory64(NewPageDirectory);

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
