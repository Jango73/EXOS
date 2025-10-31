
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
#include "Interrupt.h"
#include "SYSCall.h"

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

extern GATE_DESCRIPTOR IDT[];
extern void Interrupt_SystemCall(void);
extern VOIDFUNC InterruptTable[];

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
    U16 Privilege,
    U8 InterruptStackTable) {
    UNUSED(InterruptStackTable);
    Descriptor->Selector = SELECTOR_KERNEL_CODE;
    Descriptor->Reserved = 0;
    Descriptor->Type = Type;
    Descriptor->Privilege = Privilege;
    Descriptor->Present = 1;

    SetGateDescriptorOffset(Descriptor, Handler);
}

void InitializeInterrupts(void) {
    Kernel_i386.IDT = IDT;

    for (U32 Index = 0; Index < NUM_INTERRUPTS; Index++) {
        InitializeGateDescriptor(
            IDT + Index,
            (LINEAR)(InterruptTable[Index]),
            GATE_TYPE_386_INT,
            PRIVILEGE_KERNEL,
            0u);
    }

    InitializeSystemCall();

    LoadInterruptDescriptorTable((LINEAR)IDT, IDT_SIZE - 1u);

    ClearDR7();

    InitializeSystemCallTable();
}

void InitSegmentDescriptor(LPSEGMENT_DESCRIPTOR This, U32 Type) {
    MemorySet(This, 0, sizeof(SEGMENT_DESCRIPTOR));

    This->Limit_00_15 = 0xFFFF;
    This->Base_00_15 = 0x0000;
    This->Base_16_23 = 0x00;
    This->Accessed = 0;
    This->CanWrite = 1;
    This->ConformExpand = 0;
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

    DEBUG(TEXT("[InitializeTaskSegments] TSS = %p"), Kernel_i386.TSS);
    DEBUG(TEXT("[InitializeTaskSegments] Loading task register"));

    LoadInitialTaskRegister(SELECTOR_TSS);

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

    Task->Arch.Stack.Size = Info->StackSize;
    Task->Arch.SysStack.Size = TASK_MINIMUM_SYSTEM_STACK_SIZE;

    Task->Arch.Stack.Base = AllocRegion(BaseVMA, 0, Task->Arch.Stack.Size,
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER);
    Task->Arch.SysStack.Base =
        AllocKernelRegion(0, Task->Arch.SysStack.Size, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    DEBUG(TEXT("[SetupTask] BaseVMA=%p, Requested StackBase at BaseVMA"), BaseVMA);
    DEBUG(TEXT("[SetupTask] Actually got StackBase=%p"), Task->Arch.Stack.Base);

    if (Task->Arch.Stack.Base == NULL || Task->Arch.SysStack.Base == NULL) {
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

        ERROR(TEXT("[SetupTask] Stack or system stack allocation failed"));
        return FALSE;
    }

    DEBUG(TEXT("[SetupTask] Stack (%u bytes) allocated at %p"), Task->Arch.Stack.Size, Task->Arch.Stack.Base);
    DEBUG(TEXT("[SetupTask] System stack (%u bytes) allocated at %p"), Task->Arch.SysStack.Size,
        Task->Arch.SysStack.Base);

    MemorySet((LPVOID)(Task->Arch.Stack.Base), 0, Task->Arch.Stack.Size);
    MemorySet((LPVOID)(Task->Arch.SysStack.Base), 0, Task->Arch.SysStack.Size);
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

    StackTop = Task->Arch.Stack.Base + Task->Arch.Stack.Size;
    SysStackTop = Task->Arch.SysStack.Base + Task->Arch.SysStack.Size;

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
        LINEAR NextSysStackTop = NextTask->Arch.SysStack.Base + NextTask->Arch.SysStack.Size;

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

    LogGlobalDescriptorTable(Kernel_i386.GDT, 10);

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

/**
 * @brief Perform architecture-specific pre-initialization.
 */
void PreInitializeKernel(void) {
    GDT_REGISTER Gdtr;

    ReadGlobalDescriptorTable(&Gdtr);
    Kernel_i386.GDT = (LPSEGMENT_DESCRIPTOR)(LINEAR)Gdtr.Base;
}

/***************************************************************************/

void InitializeSystemCall(void) {
    InitializeGateDescriptor(
        IDT + EXOS_USER_CALL,
        (LINEAR)Interrupt_SystemCall,
        GATE_TYPE_386_TRAP,
        PRIVILEGE_USER,
        0u);
}

/************************************************************************/
