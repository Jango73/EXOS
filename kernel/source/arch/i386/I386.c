
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

#include "arch/i386/I386.h"

#include "Log.h"
#include "Memory.h"
#include "Process.h"
#include "Stack.h"
#include "String.h"
#include "System.h"
#include "Task.h"
#include "Text.h"
#include "Kernel.h"

/************************************************************************/

KERNELDATA_I386 SECTION(".data") Kernel_i386 = {.GDT = 0, .TSS = 0, .PPB = (U8*)0};

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

    DEBUG(TEXT("[SetupTask] BaseVMA=%X, Requested StackBase at BaseVMA"), BaseVMA);
    DEBUG(TEXT("[SetupTask] Actually got StackBase=%X"), Task->Arch.StackBase);

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

        ERROR(TEXT("[SetupTask] Stack or system stack allocation failed"));
        return FALSE;
    }

    DEBUG(TEXT("[SetupTask] Stack (%X bytes) allocated at %X"), Task->Arch.StackSize, Task->Arch.StackBase);
    DEBUG(TEXT("[SetupTask] System stack (%X bytes) allocated at %X"), Task->Arch.SysStackSize, Task->Arch.SysStackBase);

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
    Task->Arch.Context.Registers.CR4 = GetCR4();
    Task->Arch.Context.Registers.EIP = VMA_TASK_RUNNER;

    LINEAR StackTop = Task->Arch.StackBase + Task->Arch.StackSize;
    LINEAR SysStackTop = Task->Arch.SysStackBase + Task->Arch.SysStackSize;

    if (Process->Privilege == PRIVILEGE_KERNEL) {
        DEBUG(TEXT("[SetupTask] Setting kernel privilege (ring 0)"));
        Task->Arch.Context.Registers.ESP = StackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.EBP = StackTop - STACK_SAFETY_MARGIN;
    } else {
        DEBUG(TEXT("[SetupTask] Setting user privilege (ring 3)"));
        Task->Arch.Context.Registers.ESP = SysStackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.EBP = SysStackTop - STACK_SAFETY_MARGIN;
    }

    if (Info->Flags & TASK_CREATE_MAIN_KERNEL) {
        Task->Status = TASK_STATUS_RUNNING;

        Kernel_i386.TSS->ESP0 = SysStackTop - STACK_SAFETY_MARGIN;

        LINEAR BootStackTop = KernelStartup.StackTop;
        LINEAR ESP = GetESP();
        UINT StackUsed = (BootStackTop - ESP) + 256;

        DEBUG(TEXT("[SetupTask] BootStackTop = %X"), BootStackTop);
        DEBUG(TEXT("[SetupTask] StackTop = %X"), StackTop);
        DEBUG(TEXT("[SetupTask] StackUsed = %X"), StackUsed);
        DEBUG(TEXT("[SetupTask] Switching to new stack..."));

        if (SwitchStack(StackTop, BootStackTop, StackUsed)) {
            Task->Arch.Context.Registers.ESP = 0;  // Not used for main task
            Task->Arch.Context.Registers.EBP = GetEBP();
            DEBUG(TEXT("[SetupTask] Main task stack switched successfully"));
        } else {
            ERROR(TEXT("[SetupTask] Stack switch failed"));
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
