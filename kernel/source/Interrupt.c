
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

#include "../include/Base.h"
#include "../include/Kernel.h"

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
extern void Interrupt_SystemCall(void);
extern void Interrupt_DriverCall(void);

/***************************************************************************/

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
};

GATEDESCRIPTOR IDT[IDT_SIZE / sizeof(GATEDESCRIPTOR)];

/***************************************************************************/

static void SetGateDescriptorOffset(LPGATEDESCRIPTOR This, U32 Offset) {
    This->Offset_00_15 = (Offset & (U32)0x0000FFFF) >> 0x00;
    This->Offset_16_31 = (Offset & (U32)0xFFFF0000) >> 0x10;
}

/***************************************************************************/

void InitializeInterrupts(void) {
    U32 Index = 0;

    Kernel_i386.IDT = IDT;

    //-------------------------------------
    // Set all used interrupts

    for (Index = 0; Index < NUM_INTERRUPTS; Index++) {
        IDT[Index].Selector = SELECTOR_KERNEL_CODE;
        IDT[Index].Reserved = 0;
        IDT[Index].Type = GATE_TYPE_386_INT;
        IDT[Index].Privilege = PRIVILEGE_KERNEL;
        IDT[Index].Present = 1;

        SetGateDescriptorOffset(IDT + Index, (U32)InterruptTable[Index]);
    }

    //-------------------------------------
    // Set system call interrupt

    Index = EXOS_USER_CALL;

    IDT[Index].Selector = SELECTOR_KERNEL_CODE;
    IDT[Index].Reserved = 0;
    IDT[Index].Type = GATE_TYPE_386_TRAP;
    IDT[Index].Privilege = PRIVILEGE_USER;
    IDT[Index].Present = 1;

    SetGateDescriptorOffset(IDT + Index, (U32)Interrupt_SystemCall);

    //-------------------------------------
    // Set driver call interrupt

    Index = EXOS_DRIVER_CALL;

    IDT[Index].Selector = SELECTOR_KERNEL_CODE;
    IDT[Index].Reserved = 0;
    IDT[Index].Type = GATE_TYPE_386_TRAP;
    IDT[Index].Privilege = PRIVILEGE_USER;
    IDT[Index].Present = 1;

    SetGateDescriptorOffset(IDT + Index, (U32)Interrupt_DriverCall);

    //-------------------------------------

    LoadInterruptDescriptorTable((PHYSICAL)IDT, sizeof(IDT) - 1);

    // Reset debug registers

    SetDR7(0);
}
