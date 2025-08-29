
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\************************************************************************/

#include "../include/Console.h"
#include "../include/I386.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Memory.h"
#include "../include/Process.h"
#include "../include/String.h"
#include "../include/System.h"
#include "../include/Text.h"

/************************************************************************/
// Fault logging helpers

/**
 * @brief Log a segment selector extracted from an error code.
 * @param Prefix Text prefix for the log entry.
 * @param Err Raw error code containing a selector.
 */
static void LogSelectorFromErrorCode(LPCSTR Prefix, U32 Err) {
    U16 sel = (U16)(Err & 0xFFFFu);
    U16 idx = SELECTOR_INDEX(sel);
    U16 ti = SELECTOR_TI(sel);
    U16 rpl = SELECTOR_RPL(sel);

    KernelLogText(
        LOG_ERROR, TEXT("%s error code=%X  selector=%X  index=%u  TI=%u  RPL=%u"), Prefix, (U32)Err, (U32)sel, (U32)idx,
        (U32)ti, (U32)rpl);
}

/************************************************************************/

/**
 * @brief Dump descriptor and TSS information for a selector.
 * @param Prefix Text prefix for the log entry.
 * @param Sel Segment selector value.
 */
static void LogDescriptorAndTSSFromSelector(LPCSTR Prefix, U16 Sel) {
    U16 ti = SELECTOR_TI(Sel);
    U16 idx = SELECTOR_INDEX(Sel);

    if (ti != 0) {
        KernelLogText(LOG_ERROR, TEXT("%s selector points to LDT (TI=1); no dump available"), Prefix);
        return;
    }

    if (idx < GDT_NUM_BASE_DESCRIPTORS) {
        KernelLogText(LOG_ERROR, TEXT("%s selector index %u is below base descriptors"), Prefix, (U32)idx);
        return;
    }

    U32 table = idx;
    LogTSSDescriptor(LOG_ERROR, (const TSSDESCRIPTOR*)&Kernel_i386.GDT[table]);
    LogTaskStateSegment(LOG_ERROR, (const TASKSTATESEGMENT*)Kernel_i386.TSS);
}

/************************************************************************/

/**
 * @brief Log register state for a task at fault.
 * @param Frame Interrupt frame with register snapshot.
 */
static void DumpFrame(LPINTERRUPTFRAME Frame) {
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

/************************************************************************/

/**
 * @brief Terminate the current task and halt if necessary.
 */
static void Die(void) {
    LPTASK Task;

    Task = GetCurrentTask();

    if (Task != NULL) {
        LockMutex(MUTEX_KERNEL, INFINITY);
        LockMutex(MUTEX_MEMORY, INFINITY);
        LockMutex(MUTEX_CONSOLE, INFINITY);

        FreezeScheduler();

        KillTask(Task);

        UnlockMutex(MUTEX_KERNEL);
        UnlockMutex(MUTEX_MEMORY);
        UnlockMutex(MUTEX_CONSOLE);

        UnfreezeScheduler();

        EnableInterrupts();
    }

    // Wait forever
    DO_THE_SLEEPING_BEAUTY;
}

/************************************************************************/

/**
 * @brief Validate an instruction pointer and terminate on failure.
 * @param Address Linear address to check.
 */
void ValidateEIPOrDie(LINEAR Address) {
    if (IsValidMemory(Address) == FALSE) {
        Die();
    }
}

/************************************************************************/

/**
 * @brief Handle unknown interrupts.
 * @param Frame Interrupt frame context.
 */
void DefaultHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Unknown interrupt"));
    DumpFrame(Frame);
    Die();
}

/************************************************************************/

/**
 * @brief Handle divide-by-zero faults.
 * @param Frame Interrupt frame context.
 */
void DivideErrorHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Divide error"));
    DumpFrame(Frame);
    Die();
}

/************************************************************************/

/**
 * @brief Handle debug exceptions and log diagnostic information.
 * @param Frame Interrupt frame context.
 */
void DebugExceptionHandler(LPINTERRUPTFRAME Frame) {
    LPTASK Task = GetCurrentTask();

    KernelLogText(LOG_ERROR, TEXT("Debug exception"));

    ConsolePrint(TEXT("Debug exception !\n"));
    ConsolePrint(TEXT("The current task (%X) triggered a debug exception "), Task ? Task : 0);
    ConsolePrint(TEXT("at EIP : %X\n"), Frame->Registers.EIP);

    SELECTOR tr = GetTaskRegister();
    LogDescriptorAndTSSFromSelector(TEXT("[#DB]"), tr);

    DumpFrame(Frame);
    Die();
}

/************************************************************************/

/**
 * @brief Handle non-maskable interrupts.
 * @param Frame Interrupt frame context.
 */
void NMIHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Non-maskable interrupt"));
    DumpFrame(Frame);
}

/***************************************************************************/

/**
 * @brief Handle breakpoint exceptions.
 * @param Frame Interrupt frame context.
 */
void BreakPointHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Breakpoint"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle overflow exceptions.
 * @param Frame Interrupt frame context.
 */
void OverflowHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Overflow"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle bound range exceeded faults.
 * @param Frame Interrupt frame context.
 */
void BoundRangeHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Bound range fault"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle invalid opcode faults.
 * @param Frame Interrupt frame context.
 */
void InvalidOpcodeHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Invalid opcode"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle device-not-available faults.
 * @param Frame Interrupt frame context.
 */
void DeviceNotAvailHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Device not available"));
    DumpFrame(Frame);
}

/***************************************************************************/

/**
 * @brief Handle double fault exceptions.
 * @param Frame Interrupt frame context.
 */
void DoubleFaultHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Double fault"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle math overflow exceptions.
 * @param Frame Interrupt frame context.
 */
void MathOverflowHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Math overflow"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle invalid TSS faults.
 * @param Frame Interrupt frame context.
 */
void InvalidTSSHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Invalid TSS"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle segment not present faults.
 * @param Frame Interrupt frame context.
 */
void SegmentFaultHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Segment fault"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle stack fault exceptions.
 * @param Frame Interrupt frame context.
 */
void StackFaultHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Stack fault"));
    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle general protection faults.
 * @param Frame Interrupt frame context.
 */
void GeneralProtectionHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("General protection fault"));

    LPTASK Task = GetCurrentTask();

    ConsolePrint(TEXT("General protection fault !\n"));
    ConsolePrint(TEXT("The current thread (%X) triggered a general protection "), Task ? Task : 0);
    ConsolePrint(TEXT("fault with error code : %X, at EIP : %X\n"), Frame->ErrCode, Frame->Registers.EIP);
    ConsolePrint(TEXT("Since this error is unrecoverable, the task will be shutdown now.\n"));
    ConsolePrint(TEXT("Halting"));

    DumpFrame(Frame);
    Die();
}

/***************************************************************************/

/**
 * @brief Handle page fault exceptions.
 * @param ErrorCode Fault error code.
 * @param Address Faulting linear address.
 * @param Eip Instruction pointer where fault occurred.
 */
void PageFaultHandler(U32 ErrorCode, LINEAR Address, U32 Eip) {
    INTEL386REGISTERS Regs;
    SaveRegisters(&Regs);

    LPTASK Task = GetCurrentTask();

    ConsolePrint(TEXT("Page fault !\n"));
    ConsolePrint(TEXT("The current thread (%X) did an unauthorized access "), Task ? Task : 0);
    ConsolePrint(TEXT("at linear address : %X, error code : %X, EIP : %X\n"), Address, ErrorCode, Eip);
    ConsolePrint(TEXT("Since this error is unrecoverable, the task will be shutdown now.\n"));
    ConsolePrint(TEXT("Halting"));

    KernelLogText(LOG_ERROR, TEXT("Page fault at %X (EIP %X)"), Address, Eip);

    Regs.EIP = Eip;
    LogRegisters(&Regs);

    Die();
}

/***************************************************************************/

/**
 * @brief Handle alignment check faults.
 * @param Frame Interrupt frame context.
 */
void AlignmentCheckHandler(LPINTERRUPTFRAME Frame) {
    KernelLogText(LOG_ERROR, TEXT("Alignment check fault"));
    DumpFrame(Frame);
    Die();
}
