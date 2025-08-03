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

; Macros for interrupt handling
%macro SAVE_REGISTERS 0
    push edi
    push esi
    push ebp
    push esp
    push ebx
    push edx
    push ecx
    push eax
%endmacro

%macro RESTORE_REGISTERS 0
    pop eax
    pop ecx
    pop edx
    pop ebx
    pop esp
    pop ebp
    pop esi
    pop edi
%endmacro

%macro PUSH_DUMMY_ERROR 0
    push dword 0        ; Push dummy error code
%endmacro

%macro HANDLE_INTERRUPT 2
    SAVE_REGISTERS
    ; Check if privilege level changed (Ring 3 -> Ring 0)
    mov eax, [esp + 32]     ; Get CS from stack (after registers)
    and eax, 3              ; Extract DPL (bits 0-1)
    cmp eax, 3              ; Is it Ring 3?
    je %%ring3
    ; Ring 0: Push dummy ESP_Fault and SS
    push dword 0            ; Dummy SS
    push dword 0            ; Dummy ESP_Fault
    jmp %%continue
%%ring3:
    ; Ring 3: Copy SS and ESP_Fault from stack
    mov eax, [esp + 44]     ; Get SS
    push eax
    mov eax, [esp + 44]     ; Get ESP_Fault
    push eax
%%continue:
    push dword [esp + 48]   ; Push EFlags
    push dword [esp + 48]   ; Push CS
    push dword [esp + 48]   ; Push EIP
    %if %2
        push dword [esp + 48]   ; Push error code from stack
    %else
        PUSH_DUMMY_ERROR        ; Push dummy error code
    %endif
    push dword 0            ; Push dummy FaultAddress
    push esp                ; Push pointer to InterruptFrame
    call EnterKernel
    call %1
    add esp, 12             ; Remove InterruptFrame pointer, FaultAddress, error code
    add esp, 12             ; Remove EIP, CS, EFlags
    add esp, 8              ; Remove ESP_Fault, SS
    RESTORE_REGISTERS
    %if %2
        add esp, 4          ; Remove error code from stack
    %endif
    iretd
%endmacro

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

Interrupt_Default:
    HANDLE_INTERRUPT DefaultHandler, 0

;--------------------------------------
; Int 0      : Divide error (#DE)
; Class      : fault
; Error code : No

Interrupt_DivideError:
    HANDLE_INTERRUPT DivideErrorHandler, 0

;--------------------------------------
; Int 1      : Debug exception (#DB)
; Class      : Trap or fault
; Error code : No

Interrupt_DebugException:
    HANDLE_INTERRUPT DebugExceptionHandler, 0

;--------------------------------------
; Int 2      : Non-maskable interrupt
; Class      : Not applicable
; Error code : Not applicable

Interrupt_NMI:
    HANDLE_INTERRUPT NMIHandler, 0

;--------------------------------------
; Int 3      : Breakpoint exception (#BP)
; Class      : Trap
; Error code : No

Interrupt_BreakPoint:
    HANDLE_INTERRUPT BreakPointHandler, 0

;--------------------------------------
; Int 4      : Overflow exception (#OF)
; Class      : Trap
; Error code : No

Interrupt_Overflow:
    HANDLE_INTERRUPT OverflowHandler, 0

;--------------------------------------
; Int 5      : Bound range exceeded exception (#BR)
; Class      : Fault
; Error code : No

Interrupt_BoundRange:
    HANDLE_INTERRUPT BoundRangeHandler, 0

;--------------------------------------
; Int 6      : Invalid opcode exception (#UD)
; Class      : Fault
; Error code : No

Interrupt_InvalidOpcode:
    HANDLE_INTERRUPT InvalidOpcodeHandler, 0

;--------------------------------------
; Int 7      : Device not available exception (#NM)
; Class      : Fault
; Error code : No

Interrupt_DeviceNotAvail:
    HANDLE_INTERRUPT DeviceNotAvailHandler, 0

;--------------------------------------
; Int 8      : Double fault exception (#DF)
; Class      : Abort
; Error code : Yes, always 0

Interrupt_DoubleFault:
    HANDLE_INTERRUPT DoubleFaultHandler, 1

;--------------------------------------
; Int 9      : Coprocessor Segment Overrun
; Class      : Abort
; Error code : No

Interrupt_MathOverflow:
    HANDLE_INTERRUPT MathOverflowHandler, 0

;--------------------------------------
; Int 10     : Invalid TSS Exception (#TS)
; Class      : Fault
; Error code : Yes

Interrupt_InvalidTSS:
    HANDLE_INTERRUPT InvalidTSSHandler, 1

;--------------------------------------
; Int 11     : Segment Not Present (#NP)
; Class      : Fault
; Error code : Yes

Interrupt_SegmentFault:
    HANDLE_INTERRUPT SegmentFaultHandler, 1

;--------------------------------------
; Int 12     : Stack Fault Exception (#SS)
; Class      : Fault
; Error code : Yes

Interrupt_StackFault:
    HANDLE_INTERRUPT StackFaultHandler, 1

;--------------------------------------
; Int 13     : General Protection Exception (#GP)
; Class      : Fault
; Error code : Yes

Interrupt_GeneralProtection:
    cli
    HANDLE_INTERRUPT GeneralProtectionHandler, 1
    sti

;--------------------------------------
; Int 14     : Page Fault Exception (#PF)
; Class      : Fault
; Error code : Yes

Interrupt_PageFault:
    push eax
    mov eax, [esp + 4]      ; Get error code
    SAVE_REGISTERS
    ; Check if privilege level changed (Ring 3 -> Ring 0)
    mov ebx, [esp + 32]     ; Get CS from stack
    and ebx, 3              ; Extract DPL
    cmp ebx, 3
    je .ring3
    ; Ring 0: Push dummy ESP_Fault and SS
    push dword 0
    push dword 0
    jmp .continue
.ring3:
    ; Ring 3: Copy SS and ESP_Fault
    mov ebx, [esp + 44]     ; Get SS
    push ebx
    mov ebx, [esp + 44]     ; Get ESP_Fault
    push ebx
.continue:
    push dword [esp + 48]   ; Push EFlags
    push dword [esp + 48]   ; Push CS
    push dword [esp + 48]   ; Push EIP
    push eax                ; Push error code as Error
    mov ebx, cr2
    push ebx                ; Push CR2 as FaultAddress
    push esp                ; Push pointer to InterruptFrame
    call EnterKernel
    call PageFaultHandler
    add esp, 12             ; Remove InterruptFrame pointer, FaultAddress, Error
    add esp, 12             ; Remove EIP, CS, EFlags
    add esp, 8              ; Remove ESP_Fault, SS
    RESTORE_REGISTERS
    pop eax
    add esp, 4              ; Remove error code from stack
    iretd

;--------------------------------------
; Int 17     : Alignment Check Exception (#AC)
; Class      : Fault
; Error code : Yes, always 0

Interrupt_AlignmentCheck:
    HANDLE_INTERRUPT AlignmentCheckHandler, 1

;--------------------------------------
; Int 32     : Clock
; Class      : Trap
; Error code : No

Interrupt_Clock:
    mov al, INTERRUPT_DONE
    out INTERRUPT_CONTROL, al
    HANDLE_INTERRUPT ClockHandler, 0

;--------------------------------------

Interrupt_Keyboard :

    push    ds
    push    es
    push    fs
    push    gs
    pusha

    call    EnterKernel
    call    KeyboardHandler

    mov     al, INTERRUPT_DONE
    out     INTERRUPT_CONTROL, al

    popa
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
    pusha

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

    popa
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
    pusha

    call    EnterKernel
    call    HardDriveHandler

    mov     al, INTERRUPT_DONE
    out     INTERRUPT_CONTROL, al

    popa
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
