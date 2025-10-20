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


    Fault handling stubs (x86-64)

\************************************************************************/

#include "Arch.h"
#include "Kernel.h"
#include "Log.h"
#include "Schedule.h"
#include "System.h"
#include "Text.h"
#include "arch/x86-64/x86-64-Log.h"

/************************************************************************/

#define DEFINE_FATAL_HANDLER(FunctionName, Description)                                       \
    void FunctionName(LPINTERRUPT_FRAME Frame) {                                               \
        ERROR(TEXT("[" #FunctionName "] %s"), TEXT(Description));                            \
        LogCPUState(Frame);                                                                    \
        Die();                                                                                 \
    }

/************************************************************************/

void LogCPUState(LPINTERRUPT_FRAME Frame) {
    if (Frame == NULL) {
        ERROR(TEXT("[LogCPUState] No interrupt frame available"));
        return;
    }

    LogFrame(NULL, Frame);
    BacktraceFrom(Frame->Registers.RBP, 10u);
}

/************************************************************************/

void Die(void) {
    LPTASK Task;

    DEBUG(TEXT("[Die] Enter"));

    Task = GetCurrentTask();

    SAFE_USE(Task) {
        LockMutex(MUTEX_KERNEL, INFINITY);
        LockMutex(MUTEX_MEMORY, INFINITY);
        LockMutex(MUTEX_CONSOLE, INFINITY);

        FreezeScheduler();

        KillTask(Task);

        UnlockMutex(MUTEX_CONSOLE);
        UnlockMutex(MUTEX_MEMORY);
        UnlockMutex(MUTEX_KERNEL);

        UnfreezeScheduler();

        EnableInterrupts();
    }

    // Wait forever
    do {
        __asm__ __volatile__(
            "1:\n\t"
            "hlt\n\t"
            "jmp 1b\n\t"
            :
            :
            : "memory");
    } while (0);
}

/************************************************************************/

void DefaultHandler(LPINTERRUPT_FRAME Frame) {
    UNUSED(Frame);
}

/************************************************************************/

/**
 * @brief Handle divide-by-zero faults.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(DivideErrorHandler, "Divide error fault")

/************************************************************************/

/**
 * @brief Handle debug exceptions.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(DebugExceptionHandler, "Debug exception fault")

/************************************************************************/

/**
 * @brief Handle non-maskable interrupts.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(NMIHandler, "Non-maskable interrupt")

/************************************************************************/

/**
 * @brief Handle breakpoint exceptions.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(BreakPointHandler, "Breakpoint fault")

/************************************************************************/

/**
 * @brief Handle overflow exceptions.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(OverflowHandler, "Overflow fault")

/************************************************************************/

/**
 * @brief Handle bound range exceeded faults.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(BoundRangeHandler, "Bound range fault")

/************************************************************************/

/**
 * @brief Handle invalid opcode faults.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(InvalidOpcodeHandler, "Invalid opcode fault")

/************************************************************************/

/**
 * @brief Handle device-not-available faults.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(DeviceNotAvailHandler, "Device not available fault")

/************************************************************************/

/**
 * @brief Handle double fault exceptions.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(DoubleFaultHandler, "Double fault")

/************************************************************************/

/**
 * @brief Handle math overflow exceptions.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(MathOverflowHandler, "Math overflow fault")

/************************************************************************/

/**
 * @brief Handle invalid TSS faults.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(InvalidTSSHandler, "Invalid TSS fault")

/************************************************************************/

/**
 * @brief Handle segment not present faults.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(SegmentFaultHandler, "Segment not present fault")

/************************************************************************/

/**
 * @brief Handle stack fault exceptions.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(StackFaultHandler, "Stack fault")

/************************************************************************/

/**
 * @brief Handle general protection faults.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(GeneralProtectionHandler, "General protection fault")

/************************************************************************/

void PageFaultHandler(LPINTERRUPT_FRAME Frame) {
    U64 FaultAddress = 0;

    __asm__ __volatile__("mov %%cr2, %0" : "=r"(FaultAddress));
    ERROR(TEXT("[PageFaultHandler] Page fault at %p"), (LINEAR)FaultAddress);
    LogCPUState(Frame);
    Die();
}

/************************************************************************/

/**
 * @brief Handle alignment check faults.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(AlignmentCheckHandler, "Alignment check fault")

/************************************************************************/

/**
 * @brief Handle machine check exceptions.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(MachineCheckHandler, "Machine check fault")

/************************************************************************/

/**
 * @brief Handle floating point exceptions.
 * @param Frame Interrupt frame context.
 */
DEFINE_FATAL_HANDLER(FloatingPointHandler, "Floating point fault")
