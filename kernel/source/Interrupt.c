
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


    Interrupt

\************************************************************************/

#include "Base.h"
#include "Kernel.h"
#include "Arch.h"
#include "SYSCall.h"
#include "InterruptController.h"
#include "System.h"

/************************************************************************/
// Functions in Int.asm

extern void Interrupt_Default(void);
extern void Interrupt_DivideError(void);
extern void Interrupt_DebugException(void);
extern void Interrupt_NMI(void);
extern void Interrupt_BreakPoint(void);
extern void Interrupt_Overflow(void);
extern void Interrupt_BoundRange(void);
extern void Interrupt_InvalidOpcode(void);
extern void Interrupt_DeviceNotAvail(void);
extern void Interrupt_DoubleFault(void);
extern void Interrupt_MathOverflow(void);
extern void Interrupt_InvalidTSS(void);
extern void Interrupt_SegmentFault(void);
extern void Interrupt_StackFault(void);
extern void Interrupt_GeneralProtection(void);
extern void Interrupt_PageFault(void);
extern void Interrupt_AlignmentCheck(void);
extern void Interrupt_MachineCheck(void);
extern void Interrupt_FloatingPoint(void);

extern void Interrupt_Clock(void);
extern void Interrupt_Keyboard(void);
extern void Interrupt_PIC2(void);
extern void Interrupt_COM2(void);
extern void Interrupt_COM1(void);
extern void Interrupt_RTC(void);
extern void Interrupt_PCI(void);
extern void Interrupt_Mouse(void);
extern void Interrupt_FPU(void);
extern void Interrupt_HardDrive(void);

/************************************************************************/

#define DEVICE_INTERRUPT_SLOT_LIST(_)                                                     \
    _(0)  _(1)  _(2)  _(3)  _(4)  _(5)  _(6)  _(7)                                        \
    _(8)  _(9)  _(10) _(11) _(12) _(13) _(14) _(15)                                       \
    _(16) _(17) _(18) _(19) _(20) _(21) _(22) _(23)                                       \
    _(24) _(25) _(26) _(27) _(28) _(29) _(30) _(31)

#define DECLARE_DEVICE_INTERRUPT(n) extern void Interrupt_Device##n(void);
DEVICE_INTERRUPT_SLOT_LIST(DECLARE_DEVICE_INTERRUPT)
#undef DECLARE_DEVICE_INTERRUPT

/************************************************************************/

VOIDFUNC InterruptTable[] = {
    Interrupt_DivideError,        // 0
    Interrupt_DebugException,     // 1
    Interrupt_NMI,                // 2
    Interrupt_BreakPoint,         // 3
    Interrupt_Overflow,           // 4
    Interrupt_BoundRange,         // 5
    Interrupt_InvalidOpcode,      // 6
    Interrupt_DeviceNotAvail,     // 7
    Interrupt_DoubleFault,        // 8
    Interrupt_MathOverflow,       // 9
    Interrupt_InvalidTSS,         // 10
    Interrupt_SegmentFault,       // 11
    Interrupt_StackFault,         // 12
    Interrupt_GeneralProtection,  // 13
    Interrupt_PageFault,          // 14
    Interrupt_Default,            // 15
    Interrupt_Default,            // 16
    Interrupt_AlignmentCheck,     // 17
    Interrupt_MachineCheck,       // 18
    Interrupt_FloatingPoint,      // 19
    Interrupt_Default,            // 20
    Interrupt_Default,            // 21
    Interrupt_Default,            // 22
    Interrupt_Default,            // 23
    Interrupt_Default,            // 24
    Interrupt_Default,            // 25
    Interrupt_Default,            // 26
    Interrupt_Default,            // 27
    Interrupt_Default,            // 28
    Interrupt_Default,            // 29
    Interrupt_Default,            // 30
    Interrupt_Default,            // 31
    Interrupt_Clock,              // 32  0x00
    Interrupt_Keyboard,           // 33  0x01
    Interrupt_PIC2,               // 34  0x02
    Interrupt_COM2,               // 35  0x03
    Interrupt_COM1,               // 36  0x04
    Interrupt_Default,            // 37  0x05
    Interrupt_Default,            // 38  0x06
    Interrupt_Default,            // 39  0x07
    Interrupt_RTC,                // 40  0x08
    Interrupt_Default,            // 41  0x09
    Interrupt_PCI,                // 42  0x0A
    Interrupt_PCI,                // 43  0x0B
    Interrupt_Mouse,              // 44  0x0C
    Interrupt_FPU,                // 45  0x0D
    Interrupt_HardDrive,          // 46  0x0E
    Interrupt_HardDrive,          // 47  0x0F
#define IDT_DEVICE_ENTRY(n) Interrupt_Device##n,
    DEVICE_INTERRUPT_SLOT_LIST(IDT_DEVICE_ENTRY)
#undef IDT_DEVICE_ENTRY
};
#undef DEVICE_INTERRUPT_SLOT_LIST

GATE_DESCRIPTOR DATA_SECTION IDT[IDT_SIZE / sizeof(GATE_DESCRIPTOR)];

/***************************************************************************/

/**
 * @brief Send End of Interrupt (EOI) signal.
 *
 * Sends EOI to the appropriate interrupt controller (Local APIC or PIC).
 */
void SendEOI(void) {
    SendInterruptEOI();
}
#define DEVICE_INTERRUPT_SLOT_LIST(_)                                                     \
    _(0)  _(1)  _(2)  _(3)  _(4)  _(5)  _(6)  _(7)                                        \
    _(8)  _(9)  _(10) _(11) _(12) _(13) _(14) _(15)                                       \
    _(16) _(17) _(18) _(19) _(20) _(21) _(22) _(23)                                       \
    _(24) _(25) _(26) _(27) _(28) _(29) _(30) _(31)
// Keep DEVICE_INTERRUPT_SLOT_LIST in sync with DEVICE_INTERRUPT_VECTOR_MAX.
