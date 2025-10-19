;-------------------------------------------------------------------------
;
;   EXOS Kernel
;   Copyright (c) 1999-2025 Jango73
;
;   This program is free software: you can redistribute it and/or modify
;   it under the terms of the GNU General Public License as published by
;   the Free Software Foundation, either version 3 of the License, or
;   (at your option) any later version.
;
;   This program is distributed in the hope that it will be useful,
;   but WITHOUT ANY WARRANTY; without even the implied warranty of
;   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;   GNU General Public License for more details.
;
;   You should have received a copy of the GNU General Public License
;   along with this program.  If not, see <https://www.gnu.org/licenses/>.
;
;
;   Interrupt stubs (x86-64)
;
;-------------------------------------------------------------------------

%include "x86-64.inc"

BITS 64

extern BuildInterruptFrame

INTERRUPT_FRAME_size          equ 366
INTERRUPT_FRAME_PADDING       equ 16
INTERRUPT_FRAME_storage_size  equ (INTERRUPT_FRAME_size + INTERRUPT_FRAME_PADDING)

section .text

    global Interrupt_Default
    global Interrupt_DivideError
    global Interrupt_DebugException
    global Interrupt_NMI
    global Interrupt_BreakPoint
    global Interrupt_Overflow
    global Interrupt_BoundRange
    global Interrupt_InvalidOpcode
    global Interrupt_DeviceNotAvail
    global Interrupt_DoubleFault
    global Interrupt_MathOverflow
    global Interrupt_InvalidTSS
    global Interrupt_SegmentFault
    global Interrupt_StackFault
    global Interrupt_GeneralProtection
    global Interrupt_PageFault
    global Interrupt_AlignmentCheck
    global Interrupt_MachineCheck
    global Interrupt_FloatingPoint

    global Interrupt_Clock
    global Interrupt_Clock_Iret
    global Interrupt_Keyboard
    global Interrupt_PIC2
    global Interrupt_COM2
    global Interrupt_COM1
    global Interrupt_RTC
    global Interrupt_PCI
    global Interrupt_Mouse
    global Interrupt_FPU
    global Interrupt_HardDrive
    global Interrupt_SystemCall
    global Interrupt_DriverCall
    global EnterKernel

%macro ISR_HANDLER 3
    push    rax
    push    rcx
    push    rdx
    push    rbx
    push    rsp
    push    rbp
    push    rsi
    push    rdi
    push    r8
    push    r9
    push    r10
    push    r11
    push    r12
    push    r13
    push    r14
    push    r15

    mov     ax, ds
    movzx   eax, ax
    push    rax

    mov     ax, es
    movzx   eax, ax
    push    rax

    mov     ax, fs
    movzx   eax, ax
    push    rax

    mov     ax, gs
    movzx   eax, ax
    push    rax

    push    rbp
    mov     rbp, rsp

    mov     ax, ss
    movzx   eax, ax
    push    rax

    call    EnterKernel

    sub     rsp, INTERRUPT_FRAME_storage_size
    lea     r11, [rsp + INTERRUPT_FRAME_PADDING]

    xor     r10d, r10d
    mov     rax, rsp
    and     rax, 0x0F
    jz      %%build_aligned
    mov     r10, rax
    sub     rsp, r10
%%build_aligned:

    mov     rdx, r11
    mov     esi, %3
    mov     edi, %1
    call    BuildInterruptFrame

    add     rsp, r10
    mov     rdi, rax

    xor     r10d, r10d
    mov     rax, rsp
    and     rax, 0x0F
    jz      %%handler_aligned
    mov     r10, rax
    sub     rsp, r10
%%handler_aligned:
    call    %2
    add     rsp, r10

    add     rsp, INTERRUPT_FRAME_storage_size

    add     rsp, 8
    pop     rbp

    pop     rax
    mov     gs, ax
    pop     rax
    mov     fs, ax
    pop     rax
    mov     es, ax
    pop     rax
    mov     ds, ax

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     r11
    pop     r10
    pop     r9
    pop     r8
    pop     rdi
    pop     rsi
    pop     rbp
    add     rsp, 8
    pop     rbx
    pop     rdx
    pop     rcx
    pop     rax

    iretq
%endmacro

%macro ISR_HANDLER_NOERR 2
    ISR_HANDLER %1, %2, 0
%endmacro

%macro ISR_HANDLER_ERR 2
    ISR_HANDLER %1, %2, 1
%endmacro

FUNC_HEADER
Interrupt_Default:
    ISR_HANDLER_NOERR 0xFFFF, DefaultHandler

FUNC_HEADER
Interrupt_DivideError:
    ISR_HANDLER_NOERR 0, DivideErrorHandler

FUNC_HEADER
Interrupt_DebugException:
    ISR_HANDLER_NOERR 1, DebugExceptionHandler

FUNC_HEADER
Interrupt_NMI:
    ISR_HANDLER_NOERR 2, NMIHandler

FUNC_HEADER
Interrupt_BreakPoint:
    ISR_HANDLER_NOERR 3, BreakPointHandler

FUNC_HEADER
Interrupt_Overflow:
    ISR_HANDLER_NOERR 4, OverflowHandler

FUNC_HEADER
Interrupt_BoundRange:
    ISR_HANDLER_NOERR 5, BoundRangeHandler

FUNC_HEADER
Interrupt_InvalidOpcode:
    ISR_HANDLER_NOERR 6, InvalidOpcodeHandler

FUNC_HEADER
Interrupt_DeviceNotAvail:
    ISR_HANDLER_NOERR 7, DeviceNotAvailHandler

FUNC_HEADER
Interrupt_DoubleFault:
    ISR_HANDLER_ERR 8, DoubleFaultHandler

FUNC_HEADER
Interrupt_MathOverflow:
    ISR_HANDLER_NOERR 9, MathOverflowHandler

FUNC_HEADER
Interrupt_InvalidTSS:
    ISR_HANDLER_ERR 10, InvalidTSSHandler

FUNC_HEADER
Interrupt_SegmentFault:
    ISR_HANDLER_ERR 11, SegmentFaultHandler

FUNC_HEADER
Interrupt_StackFault:
    ISR_HANDLER_ERR 12, StackFaultHandler

FUNC_HEADER
Interrupt_GeneralProtection:
    ISR_HANDLER_ERR 13, GeneralProtectionHandler

FUNC_HEADER
Interrupt_PageFault:
    ISR_HANDLER_ERR 14, PageFaultHandler

FUNC_HEADER
Interrupt_AlignmentCheck:
    ISR_HANDLER_ERR 17, AlignmentCheckHandler

FUNC_HEADER
Interrupt_MachineCheck:
    ISR_HANDLER_ERR 18, MachineCheckHandler

FUNC_HEADER
Interrupt_FloatingPoint:
    ISR_HANDLER_ERR 19, FloatingPointHandler

%macro STUB_ISR 1
%1:
    cli
    ud2
%%halt:
    hlt
    jmp     %%halt
%endmacro

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

FUNC_HEADER
EnterKernel:
    push    rax
    mov     ax, SELECTOR_KERNEL_DATA
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    pop     rax
    ret

section .note.GNU-stack noalloc noexec nowrite align=1
