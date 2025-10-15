;-------------------------------------------------------------------------
;
;   EXOS Kernel
;
;   Interrupt stubs (x86-64)
;
;   The long mode interrupt handling layer is not implemented yet.  To
;   allow the x86-64 build to complete we expose placeholder handlers that
;   simply trigger an invalid opcode and halt the processor.  Once the
;   actual interrupt entry/exit paths are implemented these stubs must be
;   replaced with the real logic.
;
;-------------------------------------------------------------------------

%include "x86-64.inc"

BITS 64

section .text

%macro STUB_ISR 1
    global %1
%1:
    cli
    ud2
%%halt:
    hlt
    jmp     %%halt
%endmacro

; CPU exceptions
STUB_ISR Interrupt_Default
STUB_ISR Interrupt_DivideError
STUB_ISR Interrupt_DebugException
STUB_ISR Interrupt_NMI
STUB_ISR Interrupt_BreakPoint
STUB_ISR Interrupt_Overflow
STUB_ISR Interrupt_BoundRange
STUB_ISR Interrupt_InvalidOpcode
STUB_ISR Interrupt_DeviceNotAvail
STUB_ISR Interrupt_DoubleFault
STUB_ISR Interrupt_MathOverflow
STUB_ISR Interrupt_InvalidTSS
STUB_ISR Interrupt_SegmentFault
STUB_ISR Interrupt_StackFault
STUB_ISR Interrupt_GeneralProtection
STUB_ISR Interrupt_PageFault
STUB_ISR Interrupt_AlignmentCheck
STUB_ISR Interrupt_MachineCheck
STUB_ISR Interrupt_FloatingPoint

; Hardware interrupts and system calls
STUB_ISR Interrupt_Clock
STUB_ISR Interrupt_Clock_Iret
STUB_ISR Interrupt_Keyboard
STUB_ISR Interrupt_PIC2
STUB_ISR Interrupt_COM2
STUB_ISR Interrupt_COM1
STUB_ISR Interrupt_RTC
STUB_ISR Interrupt_PCI
STUB_ISR Interrupt_Mouse
STUB_ISR Interrupt_FPU
STUB_ISR Interrupt_HardDrive
STUB_ISR Interrupt_SystemCall
STUB_ISR Interrupt_DriverCall

;----------------------------------------------------------------------------

section .note.GNU-stack noalloc noexec nowrite align=1
