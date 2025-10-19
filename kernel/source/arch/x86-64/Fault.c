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

    LPTASK Task = GetCurrentTask();

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        LogFrame(Task, Frame);
        BacktraceFrom(Frame->Registers.RBP, 10u);
    }
}

/************************************************************************/

void Die(void) {
    LPTASK Task;

    DEBUG(TEXT("[DIE] Enter"));

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

DEFINE_FATAL_HANDLER(DivideErrorHandler, "Divide error fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(DebugExceptionHandler, "Debug exception fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(NMIHandler, "Non-maskable interrupt")

/************************************************************************/

DEFINE_FATAL_HANDLER(BreakPointHandler, "Breakpoint fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(OverflowHandler, "Overflow fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(BoundRangeHandler, "BOUND range fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(InvalidOpcodeHandler, "Invalid opcode fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(DeviceNotAvailHandler, "Device not available fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(DoubleFaultHandler, "Double fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(MathOverflowHandler, "Coprocessor segment overrun")

/************************************************************************/

DEFINE_FATAL_HANDLER(InvalidTSSHandler, "Invalid TSS fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(SegmentFaultHandler, "Segment not present fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(StackFaultHandler, "Stack fault")

/************************************************************************/

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

DEFINE_FATAL_HANDLER(AlignmentCheckHandler, "Alignment check fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(MachineCheckHandler, "Machine check fault")

/************************************************************************/

DEFINE_FATAL_HANDLER(FloatingPointHandler, "Floating point fault")
