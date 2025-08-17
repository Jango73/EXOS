
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Console.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Memory.h"
#include "../include/Process.h"
#include "../include/String.h"
#include "../include/Text.h"

/***************************************************************************/

static void DumpFrame(LPINTERRUPTFRAME Frame) {
    INTEL386REGISTERS Regs;
    LPPROCESS Process;
    LPTASK Task;

    Task = GetCurrentTask();

    if (Task != NULL) {
        Process = Task->Process;

        if (Process != NULL) {
            KernelLogText(LOG_VERBOSE, TEXT("Image : %s"), Process->FileName);
            KernelLogText(LOG_VERBOSE, Text_Registers);

            LogRegisters(&(Frame->Registers));
        }
    }
}

/***************************************************************************/

static void PrintFaultDetails() {
    INTEL386REGISTERS Regs;
    LPPROCESS Process;
    LPTASK Task;

    Task = GetCurrentTask();

    if (Task != NULL) {
        Process = Task->Process;

        if (Process != NULL) {
            KernelLogText(LOG_VERBOSE, TEXT("Image : %s"), Process->FileName);
            KernelLogText(LOG_VERBOSE, Text_Registers);

            SaveRegisters(&Regs);
            LogRegisters(&Regs);
        }
    }

    // LINEAR Table = MapPhysicalPage(Process->PageDirectory);
    // LogPageDirectory(LOG_DEBUG, Table);
}

/***************************************************************************/

static void Die() {
    LPTASK Task;

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
    DO_THE_SLEEPING_BEAUTY;
}

/***************************************************************************/

void DefaultHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Unknown interrupt\n"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void DivideErrorHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Divide error !\n"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void DebugExceptionHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Debug exception !\n"));
    DumpFrame(Frame);
}

/***************************************************************************/

void NMIHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Non-maskable interrupt !\n"));
    DumpFrame(Frame);
}

/***************************************************************************/

void BreakPointHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Breakpoint !\n"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void OverflowHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Overflow !\n"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void BoundRangeHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Bound range fault !\n"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void InvalidOpcodeHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Invalid opcode !\n"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void DeviceNotAvailHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Device not available !\n"));
    DumpFrame(Frame);
}

/***************************************************************************/

void DoubleFaultHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Double fault !\n"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void MathOverflowHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Math overflow !\n"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void InvalidTSSHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Invalid TSS !\n"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void SegmentFaultHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Segment fault !\n"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void StackFaultHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Stack fault !\n"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

void GeneralProtectionHandler(U32 Code) {
    UNUSED(Code);

    INTEL386REGISTERS Regs;

    ConsolePrint(Text_NewLine);
    ConsolePrint(TEXT("General protection fault !\n"));

    KernelLogText(LOG_ERROR, Text_NewLine);
    KernelLogText(LOG_ERROR, TEXT("General protection fault\n"));
    PrintFaultDetails();
    KernelLogText(LOG_ERROR, Text_Registers);
    KernelLogText(LOG_ERROR, Text_NewLine);

    SaveRegisters(&Regs);
    LogRegisters(&Regs);

    Die();
}

/***************************************************************************/

void PageFaultHandler(U32 ErrorCode, LINEAR Address, U32 Eip) {
    LPTASK Task = GetCurrentTask();
    INTEL386REGISTERS Regs;

    ConsolePrint(Text_NewLine);
    ConsolePrint(TEXT("Page fault !\n"));
    ConsolePrint(TEXT("The current task (%X) did an unauthorized access "), Task ? Task : 0);
    ConsolePrint(TEXT("at linear address : %X, error code : %X, EIP : %X\n"), Address, ErrorCode, Eip);
    ConsolePrint(TEXT("Since this error is unrecoverable, the task will be shutdown now.\n"));
    ConsolePrint(TEXT("Halting"));

    KernelLogText(LOG_ERROR, TEXT("Page fault at %X (EIP %X)"), Address, Eip);

    SaveRegisters(&Regs);
    Regs.EIP = Eip;
    LogRegisters(&Regs);

    Die();
}

/***************************************************************************/

void AlignmentCheckHandler() {
    KernelLogText(LOG_ERROR, TEXT("Alignment check fault !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/
