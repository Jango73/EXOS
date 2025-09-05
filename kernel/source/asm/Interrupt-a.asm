
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
;   Interrupt stubs
;
;-------------------------------------------------------------------------

BITS 32

;----------------------------------------------------------------------------

%include "./Kernel.inc"

;----------------------------------------------------------------------------

extern DisableIRQ
extern EnableIRQ
extern BuildInterruptFrame
extern RestoreFromInterruptFrame

;----------------------------------------------------------------------------
; Helper values to access function parameters

PBN equ 0x08
PBF equ 0x0A

;----------------------------------------------------------------------------
; Macros

%macro ISR_HANDLER_NOERR 2
    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    push        ebp
    mov         ebp, esp

    mov         eax, ss                     ; push SS
    push        eax

    call        EnterKernel

    sub         esp, INTERRUPT_FRAME_size   ; Space used by frame

    push        esp                         ; cdecl arg 3
    push        0                           ; cdecl arg 2
    push        %1                          ; cdecl arg 1
    call        BuildInterruptFrame
    add         esp, 12                     ; cdecl clear args

    push        eax                         ; cdecl arg 1
    call        %2
    add         esp, 4                      ; cdecl clear args

    add         esp, INTERRUPT_FRAME_size   ; Space used by frame
    add         esp, 4                      ; Space used by SS
    pop         ebp

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad
    iretd
%endmacro

%macro ISR_HANDLER_ERR 2
    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    push        ebp
    mov         ebp, esp

    mov         eax, ss
    push        eax

    call        EnterKernel

    sub         esp, INTERRUPT_FRAME_size   ; Space used by frame

    push        esp                         ; cdecl arg 3
    push        1                           ; cdecl arg 2
    push        %1                          ; cdecl arg 1
    call        BuildInterruptFrame
    add         esp, 12                     ; cdecl clear args

    push        eax                         ; cdecl arg 1
    call        %2
    add         esp, 4                      ; cdecl clear args

    add         esp, INTERRUPT_FRAME_size   ; Space used by frame
    add         esp, 4                      ; Space used by SS
    pop         ebp

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iretd
%endmacro

%macro ISR_PANIC_HALT 0
    cli
%%hang:
    hlt
    jmp         %%hang
%endmacro

;----------------------------------------------------------------------------

section .text

    global EXOS_Start
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
    global Interrupt_Clock
    global Interrupt_Keyboard
    global Interrupt_Mouse
    global Interrupt_HardDrive
    global Interrupt_SystemCall
    global Interrupt_DriverCall
    global EnterKernel

EXOS_Start :

;--------------------------------------
; Error code : No

Interrupt_Default :
    ISR_HANDLER_NOERR 0xFFFF, DefaultHandler

;--------------------------------------
; Int 0      : Divide error (#DE)
; Class      : fault
; Error code : No

Interrupt_DivideError :
    ISR_HANDLER_NOERR 0, DivideErrorHandler

;--------------------------------------
; Int 1      : Debug exception (#DB)
; Class      : Trap or fault
; Error code : No

Interrupt_DebugException :
    ISR_HANDLER_NOERR 1, DebugExceptionHandler

;--------------------------------------
; Int 2      : Non-maskable interrupt
; Class      : Not applicable
; Error code : Not applicable

Interrupt_NMI :
    ISR_HANDLER_NOERR 2, NMIHandler

;--------------------------------------
; Int 3      : Breakpoint exception (#BP)
; Class      : Trap
; Error code : No

Interrupt_BreakPoint :
    ISR_HANDLER_NOERR 3, BreakPointHandler

;--------------------------------------
; Int 4      : Overflow exception (#OF)
; Class      : Trap
; Error code : No

Interrupt_Overflow :
    ISR_HANDLER_NOERR 4, OverflowHandler

;--------------------------------------
; Int 5      : Bound range exceeded exception (#BR)
; Class      : Fault
; Error code : No

Interrupt_BoundRange :
    ISR_HANDLER_NOERR 5, BoundRangeHandler

;--------------------------------------
; Int 6      : Invalid opcode exception (#UD)
; Class      : Fault
; Error code : No

Interrupt_InvalidOpcode:
    ISR_HANDLER_NOERR 6, InvalidOpcodeHandler

;--------------------------------------
; Int 7      : Device not available exception (#NM)
; Class      : Fault
; Error code : No

Interrupt_DeviceNotAvail :
    ISR_HANDLER_NOERR 7, DeviceNotAvailHandler

;--------------------------------------
; Int 8      : Double fault exception (#DF)
; Class      : Abort
; Error code : Yes, always 0

Interrupt_DoubleFault :
    ISR_HANDLER_ERR 8, DoubleFaultHandler

;--------------------------------------
; Int 9      : Coprocessor Segment Overrun
; Class      : Abort
; Error code : No

Interrupt_MathOverflow :
    ISR_HANDLER_NOERR 9, MathOverflowHandler

;--------------------------------------
; Int 10     : Invalid TSS Exception (#TS)
; Class      : Fault
; Error code : Yes

Interrupt_InvalidTSS :
    ISR_HANDLER_ERR 10, InvalidTSSHandler

;--------------------------------------
; Int 11     : Segment Not Present (#NP)
; Class      : Fault
; Error code : Yes

Interrupt_SegmentFault :
    ISR_HANDLER_ERR 11, SegmentFaultHandler

;--------------------------------------
; Int 12     : Stack Fault Exception (#SS)
; Class      : Fault
; Error code : Yes

Interrupt_StackFault :
    ISR_HANDLER_ERR 12, StackFaultHandler

;--------------------------------------
; Int 13     : General Protection Exception (#GP)
; Class      : Fault
; Error code : Yes

Interrupt_GeneralProtection :
    ISR_HANDLER_ERR 13, GeneralProtectionHandler

;--------------------------------------
; Int 14     : Page Fault Exception (#PF)
; Class      : Fault
; Error code : Yes

Interrupt_PageFault:
    ISR_HANDLER_ERR 14, PageFaultHandler

;--------------------------------------
; Int 16     : Floating-Point Error Exception (#MF)
; Class      : Fault
; Error code : No

;--------------------------------------
; Int 17     : Alignment Check Exception (#AC)
; Class      : Fault
; Error code : Yes, always 0

Interrupt_AlignmentCheck :
    ISR_HANDLER_ERR 17, AlignmentCheckHandler

;--------------------------------------
; Int 18     : Machine-Check Exception (#MC)
; Class      : Abort
; Error code : No

;--------------------------------------
; Int 32     : Clock
; Class      : Trap
; Error code : No

Interrupt_Clock:
    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    push        ebp
    mov         ebp, esp
    
    mov         eax, ss                     ; push ss
    push        eax

    call        EnterKernel

    sub         esp, INTERRUPT_FRAME_size   ; Space used by frame

    push        esp                         ; cdecl arg 3
    push        0                           ; cdecl arg 2
    push        32                          ; cdecl arg 1
    call        BuildInterruptFrame
    add         esp, 12                     ; cdecl clear args

    push        eax                         ; cdecl arg 1
    call        ClockHandler
    add         esp, 4                      ; cdecl clear args

    push        eax
    mov         al, INTERRUPT_DONE
    out         INTERRUPT_CONTROL, al
    pop         eax
    
    test        eax, eax                    ; If return = null
    jz          .NoSwitch                   ; Don't switch

    push        esp                         ; cdecl arg 2
    push        eax                         ; cdecl arg 1
    call        RestoreFromInterruptFrame
    add         esp, 8                      ; cdecl clear args
    
.NoSwitch:
    add         esp, INTERRUPT_FRAME_size   ; Space used by frame
    add         esp, 4                      ; Space used by SS

    pop         ebp

    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad

    iretd

;--------------------------------------

Interrupt_Keyboard :

    push    ds
    push    es
    push    fs
    push    gs
    push    eax
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi

    call    EnterKernel

    call    KeyboardHandler

    mov     al, INTERRUPT_DONE
    out     INTERRUPT_CONTROL, al

    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    pop     eax
    pop     gs
    pop     fs
    pop     es
    pop     ds

    iretd

;--------------------------------------

Interrupt_Mouse :

    push    ds
    push    es
    push    fs
    push    gs
    push    eax
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi

    call    EnterKernel

    mov     eax, 4
    push    eax
    call    DisableIRQ
    add     esp, 4

    call    MouseHandler

    mov     al, INTERRUPT_DONE
    out     INTERRUPT_CONTROL, al

    mov     eax, 4
    push    eax
    call    EnableIRQ
    add     esp, 4

    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    pop     eax
    pop     gs
    pop     fs
    pop     es
    pop     ds

    iretd

;--------------------------------------

Interrupt_HardDrive :

    push    ds
    push    es
    push    fs
    push    gs
    push    eax
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi

    call    EnterKernel

    call    HardDriveHandler

    mov     al, INTERRUPT_DONE
    out     INTERRUPT_CONTROL, al

    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    pop     eax
    pop     gs
    pop     fs
    pop     es
    pop     ds

    iretd

;--------------------------------------

Interrupt_SystemCall :

    push    ds
    push    es
    push    fs
    push    gs
    push    ecx
    push    edx
    push    esi
    push    edi

    call    EnterKernel

    push    ebx
    push    eax
    call    SystemCallHandler
    add     esp, 8

    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     gs
    pop     fs
    pop     es
    pop     ds

    iretd

;--------------------------------------

Interrupt_DriverCall :

    push    ds
    push    es
    push    fs
    push    gs
    push    ecx
    push    edx
    push    esi
    push    edi

    call    EnterKernel

    push    ebx
    push    eax
    call    DriverCallHandler
    add     esp, 8

    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     gs
    pop     fs
    pop     es
    pop     ds

    iretd

;--------------------------------------

EnterKernel :

    push        eax
    mov         ax,  SELECTOR_KERNEL_DATA
    mov         ds,  ax
    mov         es,  ax
    mov         fs,  ax
    mov         gs,  ax
    pop         eax
    ret

;--------------------------------------

Delay :

    dw      0x00EB                     ; jmp $+2
    dw      0x00EB                     ; jmp $+2
    ret

section .data
debug_msg db "[Scheduler returned: %X]", 0

;----------------------------------------------------------------------------
