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
#include "System.h"
#include "Text.h"

/************************************************************************/

static void HaltOnFault(LPINTERRUPT_FRAME Frame, LPCSTR Reason) {
    U64 Rip = 0;
    U64 Rsp = 0;
    U32 ErrCode = 0;

    if (Frame != NULL) {
        Rip = Frame->Registers.RIP;
        Rsp = Frame->Registers.RSP;
        ErrCode = Frame->ErrCode;
    }

    ERROR(TEXT("[Fault64] %s"), Reason);
    ERROR(TEXT("[Fault64] RIP=%llX RSP=%llX ERR=%X"),
        (unsigned long long)Rip,
        (unsigned long long)Rsp,
        ErrCode);

    DisableInterrupts();

    for (;;) {
        __asm__ __volatile__("hlt" : : : "memory");
    }
}

/************************************************************************/

void LogCPUState(LPINTERRUPT_FRAME Frame) {
    if (Frame == NULL) {
        ERROR(TEXT("[Fault64] No interrupt frame available"));
        return;
    }

    ERROR(TEXT("[Fault64] RAX=%llX RBX=%llX RCX=%llX RDX=%llX"),
        (unsigned long long)Frame->Registers.RAX,
        (unsigned long long)Frame->Registers.RBX,
        (unsigned long long)Frame->Registers.RCX,
        (unsigned long long)Frame->Registers.RDX);
    ERROR(TEXT("[Fault64] RSI=%llX RDI=%llX RBP=%llX RSP=%llX"),
        (unsigned long long)Frame->Registers.RSI,
        (unsigned long long)Frame->Registers.RDI,
        (unsigned long long)Frame->Registers.RBP,
        (unsigned long long)Frame->Registers.RSP);
    ERROR(TEXT("[Fault64] R8=%llX R9=%llX R10=%llX R11=%llX"),
        (unsigned long long)Frame->Registers.R8,
        (unsigned long long)Frame->Registers.R9,
        (unsigned long long)Frame->Registers.R10,
        (unsigned long long)Frame->Registers.R11);
    ERROR(TEXT("[Fault64] R12=%llX R13=%llX R14=%llX R15=%llX"),
        (unsigned long long)Frame->Registers.R12,
        (unsigned long long)Frame->Registers.R13,
        (unsigned long long)Frame->Registers.R14,
        (unsigned long long)Frame->Registers.R15);
    ERROR(TEXT("[Fault64] RIP=%llX RFLAGS=%llX CR2 unavailable"),
        (unsigned long long)Frame->Registers.RIP,
        (unsigned long long)Frame->Registers.RFlags);
}

/************************************************************************/

void Die(void) {
    HaltOnFault(NULL, TEXT("Die() invoked"));
}

/************************************************************************/

void DefaultHandler(LPINTERRUPT_FRAME Frame) {
    UNUSED(Frame);
}

/************************************************************************/

#define DEFINE_FATAL_HANDLER(FunctionName, Description) \
    void FunctionName(LPINTERRUPT_FRAME Frame) { \
        HaltOnFault(Frame, TEXT(Description)); \
    }

DEFINE_FATAL_HANDLER(DivideErrorHandler, "Divide error fault")
DEFINE_FATAL_HANDLER(DebugExceptionHandler, "Debug exception fault")
DEFINE_FATAL_HANDLER(NMIHandler, "Non-maskable interrupt")
DEFINE_FATAL_HANDLER(BreakPointHandler, "Breakpoint fault")
DEFINE_FATAL_HANDLER(OverflowHandler, "Overflow fault")
DEFINE_FATAL_HANDLER(BoundRangeHandler, "BOUND range fault")
DEFINE_FATAL_HANDLER(InvalidOpcodeHandler, "Invalid opcode fault")
DEFINE_FATAL_HANDLER(DeviceNotAvailHandler, "Device not available fault")
DEFINE_FATAL_HANDLER(DoubleFaultHandler, "Double fault")
DEFINE_FATAL_HANDLER(MathOverflowHandler, "Coprocessor segment overrun")
DEFINE_FATAL_HANDLER(InvalidTSSHandler, "Invalid TSS fault")
DEFINE_FATAL_HANDLER(SegmentFaultHandler, "Segment not present fault")
DEFINE_FATAL_HANDLER(StackFaultHandler, "Stack fault")
DEFINE_FATAL_HANDLER(GeneralProtectionHandler, "General protection fault")

/************************************************************************/

void PageFaultHandler(LPINTERRUPT_FRAME Frame) {
    U64 FaultAddress = 0;

    __asm__ __volatile__("mov %%cr2, %0" : "=r"(FaultAddress));
    ERROR(TEXT("[Fault64] Page fault at %llX"), (unsigned long long)FaultAddress);
    HaltOnFault(Frame, TEXT("Page fault"));
}

/************************************************************************/

DEFINE_FATAL_HANDLER(AlignmentCheckHandler, "Alignment check fault")
DEFINE_FATAL_HANDLER(MachineCheckHandler, "Machine check fault")
DEFINE_FATAL_HANDLER(FloatingPointHandler, "Floating point fault")

/************************************************************************/

