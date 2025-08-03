
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Process.h"
#include "../include/String.h"
#include "../include/Text.h"

/***************************************************************************/

static void PrintFaultDetails() {
    INTEL386REGISTERS Regs;
    LPPROCESS Process;
    LPTASK Task;

    Task = GetCurrentTask();

    if (Task != NULL) {
        Process = Task->Process;

        if (Process != NULL) {
            KernelLogText(LOG_VERBOSE, Text_Image);
            KernelLogText(LOG_VERBOSE, Text_Space);
            KernelLogText(LOG_VERBOSE, Process->FileName);
            KernelLogText(LOG_VERBOSE, Text_NewLine);

            KernelLogText(LOG_VERBOSE, Text_Registers);
            KernelLogText(LOG_VERBOSE, Text_NewLine);

            SaveRegisters(&Regs);
            DumpRegisters(&Regs);
        }
    }
}

/***************************************************************************/

static void Die() {
    LPTASK Task;
    INTEL386REGISTERS Regs;

    Task = GetCurrentTask();

    if (Task != NULL && Task != &KernelTask) {
        LockMutex(MUTEX_KERNEL, INFINITY);
        LockMutex(MUTEX_MEMORY, INFINITY);
        LockMutex(MUTEX_CONSOLE, INFINITY);

        FreezeScheduler();

        KillTask(GetCurrentTask());

        UnlockMutex(MUTEX_KERNEL);
        UnlockMutex(MUTEX_MEMORY);
        UnlockMutex(MUTEX_CONSOLE);

        UnfreezeScheduler();

        EnableInterrupts();
    }

    // Wait forever
    SLEEPING_BEAUTY
}

/***************************************************************************/

void DefaultHandler() {
    KernelLogText(LOG_ERROR, TEXT("Unknown interrupt\n"));
    PrintFaultDetails();
}

/***************************************************************************/

void DivideErrorHandler() {
    KernelLogText(LOG_ERROR, TEXT("Divide error !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void DebugExceptionHandler() {
    KernelLogText(LOG_ERROR, TEXT("Debug exception !\n"));
    PrintFaultDetails();
}

/***************************************************************************/

void NMIHandler() {
    KernelLogText(LOG_ERROR, TEXT("Non-maskable interrupt !\n"));
    PrintFaultDetails();
}

/***************************************************************************/

void BreakPointHandler() {
    KernelLogText(LOG_ERROR, TEXT("Breakpoint !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void OverflowHandler() {
    KernelLogText(LOG_ERROR, TEXT("Overflow !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void BoundRangeHandler() {
    KernelLogText(LOG_ERROR, TEXT("Bound range fault !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void InvalidOpcodeHandler() {
    KernelLogText(LOG_ERROR, TEXT("Invalid opcode !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void DeviceNotAvailHandler() {
    KernelLogText(LOG_ERROR, TEXT("Device not available !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void DoubleFaultHandler() {
    KernelLogText(LOG_ERROR, TEXT("Double fault !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void MathOverflowHandler() {
    KernelLogText(LOG_ERROR, TEXT("Math overflow !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void InvalidTSSHandler() {
    KernelLogText(LOG_ERROR, TEXT("Invalid TSS !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void SegmentFaultHandler() {
    KernelLogText(LOG_ERROR, TEXT("Segment fault !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void StackFaultHandler() {
    KernelLogText(LOG_ERROR, TEXT("Stack fault !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void GeneralProtectionHandler(U32 Code) {
    STR Num[16];
    INTEL386REGISTERS Regs;

    KernelLogText(LOG_ERROR, Text_NewLine);
    KernelLogText(LOG_ERROR, TEXT("General protection fault !\n"));
    PrintFaultDetails();
    KernelLogText(LOG_ERROR, Text_Registers);
    KernelLogText(LOG_ERROR, Text_NewLine);

    SaveRegisters(&Regs);
    DumpRegisters(&Regs);

    Die();
}

/***************************************************************************/

void PageFaultHandler(U32 ErrorCode, LINEAR Address) {
    LPTASK Task = GetCurrentTask();
    INTEL386REGISTERS Regs;

    KernelLogText(LOG_ERROR, "Page fault !\n");

    KernelLogText(LOG_ERROR, TEXT("The current task (%X) did an unauthorized access\n"), Task ? Task : 0);
    KernelLogText(LOG_ERROR, TEXT("at linear address : %X\n"), Address);
    KernelLogText(LOG_ERROR, TEXT("Since this error is unrecoverable,\n"));
    KernelLogText(LOG_ERROR, TEXT("the task will be shutdown now.\n"));
    KernelLogText(LOG_ERROR, TEXT("Shutdown in progress...\n"));

    SaveRegisters(&Regs);
    DumpRegisters(&Regs);

    Die();
}

/***************************************************************************/

void AlignmentCheckHandler() {
    KernelLogText(LOG_ERROR, TEXT("Alignment check fault !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/
