
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "Base.h"
#include "Kernel.h"

// Functions in Interrupt-a.asm

extern void Interrupt_Default();
extern void Interrupt_DivideError();
extern void Interrupt_DebugException();
extern void Interrupt_NMI();
extern void Interrupt_BreakPoint();
extern void Interrupt_Overflow();
extern void Interrupt_BoundRange();
extern void Interrupt_InvalidOpcode();
extern void Interrupt_DeviceNotAvail();
extern void Interrupt_DoubleFault();
extern void Interrupt_MathOverflow();
extern void Interrupt_InvalidTSS();
extern void Interrupt_SegmentFault();
extern void Interrupt_StackFault();
extern void Interrupt_GeneralProtection();
extern void Interrupt_PageFault();
extern void Interrupt_AlignmentCheck();
extern void Interrupt_Clock();
extern void Interrupt_Keyboard();
extern void Interrupt_Mouse();
extern void Interrupt_HardDrive();
extern void Interrupt_SystemCall();
extern void Interrupt_DriverCall();

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
    Interrupt_Default,            // 18
    Interrupt_Default,            // 19
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
    Interrupt_Clock,              // 32
    Interrupt_Keyboard,           // 33  0x01
    Interrupt_Default,            // 34  0x02
    Interrupt_Default,            // 35  0x03
    Interrupt_Mouse,              // 36  0x04
    Interrupt_Default,            // 37  0x05
    Interrupt_Default,            // 38  0x06
    Interrupt_Default,            // 39  0x07
    Interrupt_Default,            // 40  0x08
    Interrupt_Default,            // 41  0x09
    Interrupt_Default,            // 42  0x0A
    Interrupt_Default,            // 43  0x0B
    Interrupt_Default,            // 44  0x0C
    Interrupt_Default,            // 45  0x0D
    Interrupt_HardDrive,          // 46  0x0E
    Interrupt_Default,            // 47  0x0F
};

/***************************************************************************/

void InitializeInterrupts() {
    U32 Index = 0;

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
    IDT[Index].Privilege = PRIVILEGE_KERNEL;
    IDT[Index].Present = 1;

    SetGateDescriptorOffset(IDT + Index, (U32)Interrupt_SystemCall);

    //-------------------------------------
    // Set driver call interrupt

    Index = EXOS_DRIVER_CALL;

    IDT[Index].Selector = SELECTOR_KERNEL_CODE;
    IDT[Index].Reserved = 0;
    IDT[Index].Type = GATE_TYPE_386_TRAP;
    IDT[Index].Privilege = PRIVILEGE_KERNEL;
    IDT[Index].Present = 1;

    SetGateDescriptorOffset(IDT + Index, (U32)Interrupt_DriverCall);

    //-------------------------------------

    LoadInterruptDescriptorTable(LA_IDT, IDT_SIZE - 1);
}
