
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

typedef struct tag_InterruptFrame
{
    U32 EDI;
    U32 ESI;
    U32 EBP;
    U32 ESP;
    U32 EBX;
    U32 EDX;
    U32 ECX;
    U32 EAX;

    U32 Error;         // Always present (dummy 0 if no real error code)

    U32 EIP;
    U32 CS;
    U32 EFlags;

    // Only present if privilege level changed (user -> kernel)
    U32 ESP_Fault;     // (optional)
    U32 SS_Fault;      // (optional)
    U32 FaultAddress;
} InterruptFrame, *LPInterruptFrame;

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

void DefaultHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Unknown interrupt\n"));
    PrintFaultDetails();
}

/***************************************************************************/

void DivideErrorHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Divide error !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void DebugExceptionHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Debug exception !\n"));
    PrintFaultDetails();
}

/***************************************************************************/

void NMIHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Non-maskable interrupt !\n"));
    PrintFaultDetails();
}

/***************************************************************************/

void BreakPointHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Breakpoint !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void OverflowHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Overflow !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void BoundRangeHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Bound range fault !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void InvalidOpcodeHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Invalid opcode !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void DeviceNotAvailHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Device not available !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void DoubleFaultHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Double fault !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void MathOverflowHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Math overflow !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void InvalidTSSHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Invalid TSS !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void SegmentFaultHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Segment fault !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void StackFaultHandler(LPInterruptFrame) {
    KernelLogText(LOG_ERROR, TEXT("Stack fault !\n"));
    PrintFaultDetails();
    Die();
}

/***************************************************************************/

void GeneralProtectionHandler(LPInterruptFrame) {
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

void PageFaultHandler(LPInterruptFrame Frame) {
    STR Num[16];
    INTEL386REGISTERS Regs;

    KernelLogText(LOG_ERROR, "Page fault !\n");

    KernelLogText(LOG_ERROR, TEXT("The current task did an unauthorized access\n"));
    KernelLogText(LOG_ERROR, TEXT("at linear address : "));
    U32ToHexString(Frame->FaultAddress, Num);
    KernelLogText(LOG_ERROR, Num);
    KernelLogText(LOG_ERROR, Text_NewLine);
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
