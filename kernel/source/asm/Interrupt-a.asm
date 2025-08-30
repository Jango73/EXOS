
BITS 32

;----------------------------------------------------------------------------

%include "./Kernel.inc"
%include "./Interrupt-a.inc"

;----------------------------------------------------------------------------

extern DisableIRQ
extern EnableIRQ

;----------------------------------------------------------------------------

; Helper values to access function parameters

PBN equ 0x08
PBF equ 0x0A

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

    ISR_BUILD_FRAME_NOERR_AND_CALL 0xFFFF, DefaultHandler
    ISR_RETURN

;--------------------------------------
; Int 0      : Divide error (#DE)
; Class      : fault
; Error code : No

Interrupt_DivideError :

    ISR_BUILD_FRAME_NOERR_AND_CALL 0, DivideErrorHandler
    ISR_RETURN

;--------------------------------------
; Int 1      : Debug exception (#DB)
; Class      : Trap or fault
; Error code : No

Interrupt_DebugException :

    ISR_BUILD_FRAME_NOERR_AND_CALL 1, DebugExceptionHandler
    ISR_RETURN

;--------------------------------------
; Int 2      : Non-maskable interrupt
; Class      : Not applicable
; Error code : Not applicable

Interrupt_NMI :

    ISR_BUILD_FRAME_NOERR_AND_CALL 2, NMIHandler
    ISR_RETURN

;--------------------------------------
; Int 3      : Breakpoint exception (#BP)
; Class      : Trap
; Error code : No

Interrupt_BreakPoint :

    ISR_BUILD_FRAME_NOERR_AND_CALL 3, BreakPointHandler
    ISR_RETURN

;--------------------------------------
; Int 4      : Overflow exception (#OF)
; Class      : Trap
; Error code : No

Interrupt_Overflow :

    ISR_BUILD_FRAME_NOERR_AND_CALL 4, OverflowHandler
    ISR_RETURN

;--------------------------------------
; Int 5      : Bound range exceeded exception (#BR)
; Class      : Fault
; Error code : No

Interrupt_BoundRange :

    ISR_BUILD_FRAME_NOERR_AND_CALL 5, BoundRangeHandler
    ISR_RETURN

;--------------------------------------
; Int 6      : Invalid opcode exception (#UD)
; Class      : Fault
; Error code : No

Interrupt_InvalidOpcode:

    ISR_BUILD_FRAME_NOERR_AND_CALL 6, InvalidOpcodeHandler
    ISR_RETURN

;--------------------------------------
; Int 7      : Device not available exception (#NM)
; Class      : Fault
; Error code : No

Interrupt_DeviceNotAvail :

    ISR_BUILD_FRAME_NOERR_AND_CALL 7, DeviceNotAvailHandler
    ISR_RETURN

;--------------------------------------
; Int 8      : Double fault exception (#DF)
; Class      : Abort
; Error code : Yes, always 0

Interrupt_DoubleFault :

    ISR_BUILD_FRAME_NOERR_AND_CALL 8, DoubleFaultHandler
    ISR_RETURN

;--------------------------------------
; Int 9      : Coprocessor Segment Overrun
; Class      : Abort
; Error code : No

Interrupt_MathOverflow :

    ISR_BUILD_FRAME_NOERR_AND_CALL 9, MathOverflowHandler
    ISR_RETURN

;--------------------------------------
; Int 10     : Invalid TSS Exception (#TS)
; Class      : Fault
; Error code : Yes

Interrupt_InvalidTSS :

    ISR_BUILD_FRAME_ERR_AND_CALL 10, InvalidTSSHandler
    ISR_RETURN_ERR

;--------------------------------------
; Int 11     : Segment Not Present (#NP)
; Class      : Fault
; Error code : Yes

Interrupt_SegmentFault :

    ISR_BUILD_FRAME_ERR_AND_CALL 11, SegmentFaultHandler
    ISR_RETURN_ERR

;--------------------------------------
; Int 12     : Stack Fault Exception (#SS)
; Class      : Fault
; Error code : Yes

Interrupt_StackFault :

    ISR_BUILD_FRAME_ERR_AND_CALL 12, StackFaultHandler
    ISR_RETURN_ERR

;--------------------------------------
; Int 13     : General Protection Exception (#GP)
; Class      : Fault
; Error code : Yes

Interrupt_GeneralProtection :

    ISR_BUILD_FRAME_ERR_AND_CALL 13, GeneralProtectionHandler
    ISR_RETURN_ERR

;--------------------------------------
; Int 14     : Page Fault Exception (#PF)
; Class      : Fault
; Error code : Yes

Interrupt_PageFault:
    ; Stack on entry (top -> bottom):
    ; [esp+0]=error, [esp+4]=EIP, [esp+8]=CS, [esp+12]=EFLAGS, (and possibly [old ESP], [old SS])

    push    eax                    ; save original EAX
    push    ecx                    ; save original ECX

    mov     ecx, [esp+8]           ; ECX = error code (after 2 pushes)
    mov     eax, [esp+12]          ; EAX = faulting EIP (after 2 pushes)
    mov     ebx, cr2               ; EBX = faulting linear address (read ASAP)

    pusha                          ; save all GPRs (incl. our EAX/ECX/EBX copies)

    call    EnterKernel            ; enter kernel context (segments, etc.)

    ; Args: (cdecl) push in reverse order of prototype if needed
    push    eax                    ; EIP
    push    ebx                    ; linear addr (CR2)
    push    ecx                    ; error code
    call    PageFaultHandler
    add     esp, 12

    popa
    pop     ecx                    ; restore original ECX
    pop     eax                    ; restore original EAX

    add     esp, 4                 ; drop CPU-pushed error code
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

    ISR_BUILD_FRAME_ERR_AND_CALL 17, AlignmentCheckHandler
    ISR_RETURN_ERR

;--------------------------------------
; Int 18     : Machine-Check Exception (#MC)
; Class      : Abort
; Error code : No

;--------------------------------------
; Int 32     : Clock
; Class      : Trap
; Error code : No

Interrupt_Clock :
    push    dword 0                 ; Error code
    push    dword 32                ; Trap number
    push    ds
    push    es
    push    fs
    push    gs
    pushad

    call    ClockHandler

    mov     eax, esp
    push    eax
    call    Scheduler
    add     esp, 4
    test    eax, eax                ; Did we switch tasks ?
    jz      .NoSwitch
    mov     esp, eax                ; Load new task stack
.NoSwitch:
    mov     al, INTERRUPT_DONE
    out     INTERRUPT_CONTROL, al

    popad
    pop     gs
    pop     fs
    pop     es
    pop     ds
    add     esp, 8                  ; Remove trap number and error code
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

;----------------------------------------------------------------------------
