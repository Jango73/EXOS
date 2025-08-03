
BITS 32

;----------------------------------------------------------------------------

%include "./Kernel.inc"

;----------------------------------------------------------------------------

extern DisableIRQ
extern EnableIRQ

;----------------------------------------------------------------------------

; Helper values to access function parameters

PBN equ 0x08
PBF equ 0x0A

;----------------------------------------------------------------------------

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
    global Interrupt_Clock
    global Interrupt_Keyboard
    global Interrupt_Mouse
    global Interrupt_HardDrive
    global Interrupt_SystemCall
    global Interrupt_DriverCall
    global EnterKernel

;--------------------------------------
; Error code : No

Interrupt_Default :

    pusha
    call EnterKernel
    call DefaultHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 0      : Divide error (#DE)
; Class      : fault
; Error code : No

Interrupt_DivideError :

    pusha
    call    EnterKernel
    call    DivideErrorHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 1      : Debug exception (#DB)
; Class      : Trap or fault
; Error code : No

Interrupt_DebugException :

    pusha
    call    EnterKernel
    call    DebugExceptionHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 2      : Non-maskable interrupt
; Class      : Not applicable
; Error code : Not applicable

Interrupt_NMI :

    pusha
    call    EnterKernel
    call    NMIHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 3      : Breakpoint exception (#BP)
; Class      : Trap
; Error code : No

Interrupt_BreakPoint :

    pusha
    call    EnterKernel
    call    BreakPointHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 4      : Overflow exception (#OF)
; Class      : Trap
; Error code : No

Interrupt_Overflow :

    pusha
    call    EnterKernel
    call    OverflowHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 5      : Bound range exceeded exception (#BR)
; Class      : Fault
; Error code : No

Interrupt_BoundRange :

    pusha
    call    EnterKernel
    call    BoundRangeHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 6      : Invalid opcode exception (#UD)
; Class      : Fault
; Error code : No

Interrupt_InvalidOpcode :

    pusha
    call    EnterKernel
    call    InvalidOpcodeHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 7      : Device not available exception (#NM)
; Class      : Fault
; Error code : No

Interrupt_DeviceNotAvail :

    pusha
    call    EnterKernel
    call    DeviceNotAvailHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 8      : Double fault exception (#DF)
; Class      : Abort
; Error code : Yes, always 0

Interrupt_DoubleFault :

    pusha
    call    EnterKernel
    call    DoubleFaultHandler
    hlt
    popa
    add     esp, 4                        ; Remove error code
    iretd

;--------------------------------------
; Int 9      : Coprocessor Segment Overrun
; Class      : Abort
; Error code : No

Interrupt_MathOverflow :

    pusha
    call    EnterKernel
    call    MathOverflowHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 10     : Invalid TSS Exception (#TS)
; Class      : Fault
; Error code : Yes

Interrupt_InvalidTSS :

    pusha
    call    EnterKernel
    call    InvalidTSSHandler
    hlt
    popa
    add     esp, 4                        ; Remove error code
    iretd

;--------------------------------------
; Int 11     : Segment Not Present (#NP)
; Class      : Fault
; Error code : Yes

Interrupt_SegmentFault :

    pusha
    call    EnterKernel
    call    SegmentFaultHandler
    hlt
    popa
    add     esp, 4                        ; Remove error code
    iretd

;--------------------------------------
; Int 12     : Stack Fault Exception (#SS)
; Class      : Fault
; Error code : Yes

Interrupt_StackFault :

    pusha
    call    EnterKernel
    call    StackFaultHandler
    hlt
    popa
    add     esp, 4                        ; Remove error code
    iretd

;--------------------------------------
; Int 13     : General Protection Exception (#GP)
; Class      : Fault
; Error code : Yes

Interrupt_GeneralProtection :

    pusha
    cli
    call    EnterKernel
    call    GeneralProtectionHandler
    hlt
    sti
    popa
    add     esp, 4                     ; Remove error code
    iretd

;--------------------------------------
; Int 14     : Page Fault Exception (#PF)
; Class      : Fault
; Error code : Yes

Interrupt_PageFault :

    push    eax
    mov     eax, [esp+4]
    pusha

    call    EnterKernel

    mov     ebx, cr2
    push    ebx
    push    eax
    call    PageFaultHandler
    add     esp, 8

    popa
    pop     eax

    add     esp, 4                     ; Remove error code
    iretd

;--------------------------------------
; Int 16     : Floating-Point Error Exception (#MF)
; Class      : Fault
; Error code : No

;--------------------------------------
; Int 17     : Alignment Check Exception (#AC)
; Class      : Fault
; Error code : Yes, always 0

Interrupt_AlignmentCheck :

    pusha
    call    EnterKernel
    call    AlignmentCheckHandler
    hlt
    popa
    add     esp, 4                        ; Remove error code
    iretd

;--------------------------------------
; Int 18     : Machine-Check Exception (#MC)
; Class      : Abort
; Error code : No

;--------------------------------------
; Int 32     : Clock
; Class      : Trap
; Error code : No

Interrupt_Clock :

    pusha

    mov     al, INTERRUPT_DONE
    out     INTERRUPT_CONTROL, al

    call    EnterKernel
    call    ClockHandler

    popa
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

    mov     eax, 4
    push    eax
    call    DisableIRQ
    add     esp, 4

    call    EnterKernel
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

    push    eax
    mov     ax,  SELECTOR_KERNEL_DATA
    mov     ds,  ax
    mov     es,  ax
    mov     fs,  ax
    mov     gs,  ax
    pop     eax
    ret

;--------------------------------------

Delay :

    dw      0x00EB                     ; jmp $+2
    dw      0x00EB                     ; jmp $+2
    ret

;----------------------------------------------------------------------------
