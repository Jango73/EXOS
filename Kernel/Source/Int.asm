
; Int.asm

;----------------------------------------------------------------------------

%include "./Kernel.inc"

;----------------------------------------------------------------------------

extern _DisableIRQ
extern _EnableIRQ

;----------------------------------------------------------------------------

; Helper values to access function parameters

PBN equ 0x08
PBF equ 0x0A

;----------------------------------------------------------------------------

segment .text use32

    global _Interrupt_Default
    global _Interrupt_DivideError
    global _Interrupt_DebugException
    global _Interrupt_NMI
    global _Interrupt_BreakPoint
    global _Interrupt_Overflow
    global _Interrupt_BoundRange
    global _Interrupt_InvalidOpcode
    global _Interrupt_DeviceNotAvail
    global _Interrupt_DoubleFault
    global _Interrupt_MathOverflow
    global _Interrupt_InvalidTSS
    global _Interrupt_SegmentFault
    global _Interrupt_StackFault
    global _Interrupt_GeneralProtection
    global _Interrupt_PageFault
    global _Interrupt_AlignmentCheck
    global _Interrupt_Clock
    global _Interrupt_Keyboard
    global _Interrupt_Mouse
    global _Interrupt_HardDrive
    global _Interrupt_SystemCall
    global _Interrupt_DriverCall
    global _EnterKernel

;--------------------------------------
; Error code : No

_Interrupt_Default :

    pusha
    call    _EnterKernel
    call    _DefaultHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 0      : Divide error (#DE)
; Class      : fault
; Error code : No

_Interrupt_DivideError :

    pusha
;    call    _EnterKernel
    call    _DivideErrorHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 1      : Debug exception (#DB)
; Class      : Trap or fault
; Error code : No

_Interrupt_DebugException :

    pusha
;    call    _EnterKernel
    call    _DebugExceptionHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 2      : Non-maskable interrupt
; Class      : Not applicable
; Error code : Not applicable

_Interrupt_NMI :

    pusha
;    call    _EnterKernel
    call    _NMIHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 3      : Breakpoint exception (#BP)
; Class      : Trap
; Error code : No

_Interrupt_BreakPoint :

    pusha
;    call    _EnterKernel
    call    _BreakPointHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 4      : Overflow exception (#OF)
; Class      : Trap
; Error code : No

_Interrupt_Overflow :

    pusha
;    call    _EnterKernel
    call    _OverflowHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 5      : Bound range exceeded exception (#BR)
; Class      : Fault
; Error code : No

_Interrupt_BoundRange :

    pusha
;    call    _EnterKernel
    call    _BoundRangeHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 6      : Invalid opcode exception (#UD)
; Class      : Fault
; Error code : No

_Interrupt_InvalidOpcode :

    pusha
;    call    _EnterKernel
    call    _InvalidOpcodeHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 7      : Device not available exception (#NM)
; Class      : Fault
; Error code : No

_Interrupt_DeviceNotAvail :

    pusha
;    call    _EnterKernel
    call    _DeviceNotAvailHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 8      : Double fault exception (#DF)
; Class      : Abort
; Error code : Yes, always 0

_Interrupt_DoubleFault :

    pusha
;    call    _EnterKernel
    call    _DoubleFaultHandler
    hlt
    popa
    add     esp, 4                        ; Remove error code
    iretd

;--------------------------------------
; Int 9      : Coprocessor Segment Overrun
; Class      : Abort
; Error code : No

_Interrupt_MathOverflow :

    pusha
;    call    _EnterKernel
    call    _MathOverflowHandler
    hlt
    popa
    iretd

;--------------------------------------
; Int 10     : Invalid TSS Exception (#TS)
; Class      : Fault
; Error code : Yes

_Interrupt_InvalidTSS :

    pusha
;    call    _EnterKernel
    call    _InvalidTSSHandler
    hlt
    popa
    add     esp, 4                        ; Remove error code
    iretd

;--------------------------------------
; Int 11     : Segment Not Present (#NP)
; Class      : Fault
; Error code : Yes

_Interrupt_SegmentFault :

    pusha
;    call    _EnterKernel
    call    _SegmentFaultHandler
    hlt
    popa
    add     esp, 4                        ; Remove error code
    iretd

;--------------------------------------
; Int 12     : Stack Fault Exception (#SS)
; Class      : Fault
; Error code : Yes

_Interrupt_StackFault :

    pusha
;    call    _EnterKernel
    call    _StackFaultHandler
    hlt
    popa
    add     esp, 4                        ; Remove error code
    iretd

;--------------------------------------
; Int 13     : General Protection Exception (#GP)
; Class      : Fault
; Error code : Yes

_Interrupt_GeneralProtection :

    pusha
    cli
;    call    _EnterKernel
    call    _GeneralProtectionHandler
    hlt
    sti
    popa
    add     esp, 4                     ; Remove error code
    iretd

;--------------------------------------
; Int 14     : Page Fault Exception (#PF)
; Class      : Fault
; Error code : Yes

_Interrupt_PageFault :

    push    eax
    mov     eax, [esp+4]
    pusha

;    call    _EnterKernel

    mov     ebx, cr2
    push    ebx
    push    eax
    call    _PageFaultHandler
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

_Interrupt_AlignmentCheck :

    pusha
;    call    _EnterKernel
    call    _AlignmentCheckHandler
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

_Interrupt_Clock :

    pusha

    mov     al, INTERRUPT_DONE
    out     INTERRUPT_CONTROL, al

    call    _EnterKernel
    call    _ClockHandler

    popa
    iretd

;--------------------------------------

_Interrupt_Keyboard :

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

    call    _EnterKernel
    call    _KeyboardHandler

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

_Interrupt_Mouse :

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
    call    _DisableIRQ
    add     esp, 4

    call    _EnterKernel
    call    _MouseHandler

    mov     al, INTERRUPT_DONE
    out     INTERRUPT_CONTROL, al

    mov     eax, 4
    push    eax
    call    _EnableIRQ
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

_Interrupt_HardDrive :

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

    call    _EnterKernel
    call    _HardDriveHandler

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

_Interrupt_SystemCall :

    push    ds
    push    es
    push    fs
    push    gs
    push    ecx
    push    edx
    push    esi
    push    edi

    call    _EnterKernel

    push    ebx
    push    eax
    call    _SystemCallHandler
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

_Interrupt_DriverCall :

    push    ds
    push    es
    push    fs
    push    gs
    push    ecx
    push    edx
    push    esi
    push    edi

;    call    _EnterKernel

    push    ebx
    push    eax
    call    _DriverCallHandler
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

_EnterKernel :

    push    eax
    mov     ax,  SELECTOR_KERNEL_DATA
    mov     ds,  ax
    mov     es,  ax
    mov     fs,  ax
    mov     gs,  ax
    pop     eax
    ret

;--------------------------------------

_Delay :

    dw      0x00EB                     ; jmp $+2
    dw      0x00EB                     ; jmp $+2
   ret

;----------------------------------------------------------------------------
