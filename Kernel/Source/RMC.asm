
; RMC.asm

;----------------------------------------------------------------------------
;  EXOS Kernel
;  Copyright (c) 1999 Exelsius
;  All rights reserved
;----------------------------------------------------------------------------

%include "./Kernel.inc"

extern _IRQMask_21
extern _IRQMask_A1
extern _IRQMask_21_RM
extern _IRQMask_A1_RM
extern _StubAddress

;----------------------------------------------------------------------------

; Helper values to access function parameters and local variables

PBN equ 0x08                           ; Param base near
PBF equ 0x0A                           ; Param base far
LBN equ 0x04                           ; Local base near
LBF equ 0x04                           ; Local base far

;----------------------------------------------------------------------------

RMC_STACK equ 0x1000

;----------------------------------------------------------------------------

segment .text

    global _RealModeCall
    global _Exit_EXOS
    global _RealModeCallTest

bits 32

;--------------------------------------

; void RealModeCall (U32 Int, LPX86REGS Param2)
; void RealModeCall (U32 Address, LPX86REGS Param2)

; If Param1 <= 0xFF, Param1 is an interrupt
; If Param1 >  0xFF, Param1 is a far function pointer

_RealModeCall :

    push    ebp
    mov     ebp, esp

    ;--------------------------------------
    ; Save all registers

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
    pushfd

    ;--------------------------------------
    ; Disable interrupts

    cli

    ;--------------------------------------
    ; Mask all IRQs except the NMI (IRQ 2)

    mov     al, 0xFB                   ; 8259-1
    out     PIC1_DATA, al
    call    Delay

    mov     al, 0xFF                   ; 8259-2
    out     PIC2_DATA, al
    call    Delay

    ;--------------------------------------
    ; Set the address of the RMC code

    mov     ebx, [_StubAddress]

    ;--------------------------------------
    ; Copy the RMC code

    mov     esi, RMCSetup
    mov     edi, ebx
    mov     ecx, RMCSetupEnd - RMCSetup
    cld
    rep     movsb

    ;--------------------------------------
    ; Copy the GDT at RMC code + 16

    mov     esi, LA_GDT
    mov     edi, ebx
    add     edi, 0x10
    mov     ecx, 8 * SEGMENT_DESCRIPTOR_SIZE
    cld
    rep     movsb

    ;--------------------------------------
    ; Compute relocations

    ; 32-bit offset for jump to RMC code
    mov     esi, RelJmp
    mov     dword [esi], ebx

    ; 32-bit offset for GDT label
    mov     esi, ebx
    add     esi, Rel1 - RMCSetup
    add     dword [esi], ebx

    ; 32-bit offset for jump to 16-bit PM code
    mov     esi, ebx
    add     esi, Rel2 - RMCSetup
    add     dword [esi], ebx

    ; 16-bit segment for jump to real mode code
    mov     esi, ebx
    add     esi, Rel3 - RMCSetup
    mov     eax, ebx
    shr     eax, 4
    add     word [esi], ax

    ; 32-bit offset for jump to 16-bit PM code
    mov     esi, ebx
    add     esi, Rel4 - RMCSetup
    add     dword [esi], ebx

    ; 32-bit offset for jump to 32-bit PM code
    mov     esi, ebx
    add     esi, Rel5 - RMCSetup
    add     dword [esi], ebx

    ;--------------------------------------
    ; Set the 8259 PICs for real mode

    call    SetupPIC_RM

    ;--------------------------------------
    ; Set the 8253 timer for real mode

    call    SetupTimer_RM

    ;--------------------------------------
    ; Put the real mode IRQ masks in edx
    ; Force masking of IRQ 0

    xor     edx, edx

    mov     eax, [_IRQMask_21_RM]
;    or      eax, 0x01
    shl     eax, 16
    or      edx, eax

    mov     eax, [_IRQMask_A1_RM]
    or      edx, eax

    ;--------------------------------------
    ; Setup arguments

    mov     eax, [ebp+(PBN+0)]
    mov     ecx, [ebp+(PBN+4)]

    ;--------------------------------------
    ; Jump to code at ebx

    db      0xEA                       ; jmp far
RelJmp :
    dd      0
    dw      SELECTOR_KERNEL_CODE

RealModeCall_Back :

    ;--------------------------------------
    ; Set the 8253 timer for real mode

    call    SetupTimer_PM

    ;--------------------------------------
    ; Set the 8259 PICs for protected mode

    call    SetupPIC_PM

    ;--------------------------------------
    ; Restore protected mode IRQs

    mov     eax, [_IRQMask_21]         ; 8259-1
    out     PIC1_DATA, al
    call    Delay

    mov     eax, [_IRQMask_A1]         ; 8259-2
    out     PIC2_DATA, al
    call    Delay

    ;--------------------------------------
    ; Restore all registers

    popfd
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

    pop     ebp
    ret

;----------------------------------------------------------------------------

; This code executes at address in ebx
; Register contents upon entry :
; EAX : Interrupt number
; ECX : Pointer to regs or segment:offset func pointer
; EDX : Real mode IRQ masks

bits 32

RMCSetup :

    jmp     Start

    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

GDT :

    ;--------------------------------------
    ; Reserve space for 8 selectors

    dd 0, 0                            ; NULL selector
    dd 0, 0                            ; Unused
    dd 0, 0                            ; Kernel code
    dd 0, 0                            ; Kernel data
    dd 0, 0                            ; User code
    dd 0, 0                            ; User data
    dd 0, 0                            ; Real code
    dd 0, 0                            ; Real data

Temp_IDT_Label :
    dw 1023
    dd 0

Temp_GDT_Label :
    dw (8 * SEGMENT_DESCRIPTOR_SIZE) - 1
Rel1 :
    dd GDT - RMCSetup

Real_IDT_Label :
    dw  (IDT_SIZE - 1)
    dd LA_IDT

Real_GDT_Label :
    dw  (GDT_SIZE - 1)
    dd LA_GDT

Save_REG :
Save_SEG : dw 0, 0, 0, 0, 0           ; ds, ss, es, fs, gs
Save_STR : dd 0, 0                    ; esp, ebp
Save_CTL : dd 0, 0                    ; cr0, cr3
Save_INT : dd 0
Save_PRM : dd 0
Save_IRQ : dd 0

Param_REGS :
Param_DS   : dw 0
Param_ES   : dw 0
Param_FS   : dw 0
Param_GS   : dw 0
Param_EAX  : dd 0
Param_EBX  : dd 0
Param_ECX  : dd 0
Param_EDX  : dd 0
Param_ESI  : dd 0
Param_EDI  : dd 0
Param_EFL  : dd 0

Start :

    ;--------------------------------------
    ; Save arguments and IRQ masks

    mov     esi, ebx
    add     esi, Save_INT - RMCSetup
    mov     [esi], eax

    mov     esi, ebx
    add     esi, Save_PRM - RMCSetup
    mov     [esi], ecx

    mov     esi, ebx
    add     esi, Save_IRQ - RMCSetup
    mov     [esi], edx

    ;--------------------------------------
    ; Save registers

    mov     esi, ebx
    add     esi, Save_REG - RMCSetup

    mov     ax, ds
    mov     [esi+0], ax
    mov     ax, ss
    mov     [esi+2], ax
    mov     ax, es
    mov     [esi+4], ax
    mov     ax, fs
    mov     [esi+6], ax
    mov     ax, gs
    mov     [esi+8], ax
    mov     [esi+10], esp
    mov     [esi+14], ebp
    mov     eax, cr0
    mov     [esi+18], eax
    mov     eax, cr3
    mov     [esi+22], eax

    ;--------------------------------------
    ; Transfer register parameters

    mov     esi, ebx
    add     esi, Save_PRM - RMCSetup
    mov     esi, [esi]
    mov     edi, ebx
    add     edi, Param_REGS - RMCSetup
    mov     ecx, 36
    cld
    rep     movsb

    ;--------------------------------------
    ; Disable paging and flush TLB

    mov     eax, cr0
    and     eax, ~CR0_PAGING
    mov     cr0, eax
    xor     eax, eax
    mov     cr3, eax

    ;--------------------------------------
    ; Load the temporary IDT

    mov     esi, ebx
    add     esi, Temp_IDT_Label - RMCSetup
    lidt    [esi]

    ;--------------------------------------
    ; Load the temporary GDT

    mov     esi, ebx
    add     esi, Temp_GDT_Label - RMCSetup
    lgdt    [esi]

    ;--------------------------------------
    ; Jump to next instruction with 16-bit code selector

    db      0xEA                       ; jmp far
Rel2 :
    dd      L1 - RMCSetup
    dw      SELECTOR_REAL_CODE

L1 :

bits 16

    mov     ax, SELECTOR_REAL_DATA
    mov     ds, ax
    mov     ss, ax
    mov     es, ax

    ;--------------------------------------
    ; Switch to real mode

    xor     eax, eax
    mov     cr0, eax

    ;--------------------------------------
    ; Jump to real mode segment-offset

    db      0xEA                       ; jmp far
    dw      L2 - RMCSetup
Rel3 :
    dw      0

L2 :

bits 16

    ;--------------------------------------
    ; Disable interrupts

    cli

    ;--------------------------------------
    ; Setup real-mode segment registers

    mov     ax, cs                     ; Get code segment
    mov     ss, ax                     ; Stack segment
    mov     ds, ax                     ; Data segment
    mov     es, ax                     ; Extra segment 1
    mov     fs, ax                     ; Extra segment 2
    mov     gs, ax                     ; Extra segment 3

    ;--------------------------------------
    ; Setup the stack

    mov     sp, RMC_STACK

    ;--------------------------------------
    ; Save our base address

    push    ebx

    ;--------------------------------------
    ; Unmask real mode IRQs

;    mov     esi, Save_IRQ - RMCSetup
;    mov     edx, [esi]

;    mov     eax, edx
;    shr     eax, 16
;    out     PIC1_DATA, al
;    call    Delay_RM

;    mov     eax, edx
;    out     PIC2_DATA, al
;    call    Delay_RM

    ;--------------------------------------
    ; Clear the A20 line

    call    Clear_A20_Line

    ;--------------------------------------
    ; Check if we are doing an interrupt call
    ; or a standard far call

    mov     esi, Save_INT - RMCSetup
    mov     eax, [esi]

    test    eax, 0xFFFFFF00
    jz      DoInterrupt_Jump
    jmp     DoFarCall

DoInterrupt_Jump :

    jmp     DoInterrupt

DoFarCall :

    ;--------------------------------------
    ; Set the segment:offset of the call

    mov     esi, Save_INT - RMCSetup
    mov     eax, [esi]
    mov     esi, FarAddress - RMCSetup
    mov     [esi], eax

    ;--------------------------------------
    ; Setup registers

    mov     ebp, Param_REGS - RMCSetup

    mov     ax, [ebp+0]
    mov     ds, ax
    mov     ax, [ebp+2]
    mov     es, ax
    mov     ax, [ebp+4]
    mov     fs, ax
    mov     ax, [ebp+6]
    mov     gs, ax
    mov     eax, [ebp+8]
    mov     ebx, [ebp+12]
    mov     ecx, [ebp+16]
    mov     edx, [ebp+20]
    mov     esi, [ebp+24]
    mov     edi, [ebp+28]
    mov     ebp, esp

    sti                                ; Enable interrupts

    db      0x9A                       ; call far

FarAddress :

    dd      0x00000000                 ; Far 16-bit address

    cli                                ; Disable interrupts

    ;--------------------------------------
    ; Write back registers

    mov     ebp, Param_REGS - RMCSetup

    mov     [ebp+8], eax
    mov     ax, ds
    mov     [ebp+0], ax
    mov     ax, es
    mov     [ebp+2], ax
    mov     ax, fs
    mov     [ebp+4], ax
    mov     ax, gs
    mov     [ebp+6], ax
    mov     [ebp+12], ebx
    mov     [ebp+16], ecx
    mov     [ebp+20], edx
    mov     [ebp+24], esi
    mov     [ebp+28], edi

    jmp     ReturnFromCall

DoInterrupt :

    ;--------------------------------------
    ; Adjust interrupt number

    mov     esi, Save_INT - RMCSetup
    mov     eax, [esi]
    mov     esi, (IntCall - RMCSetup) + 1
    mov     [esi], al

    ;--------------------------------------
    ; Setup registers before calling interrupt

    mov     ebp, Param_REGS - RMCSetup

    mov     ax, [ebp+0]
    mov     ds, ax
    mov     ax, [ebp+2]
    mov     es, ax
    mov     ax, [ebp+4]
    mov     fs, ax
    mov     ax, [ebp+6]
    mov     gs, ax
    mov     eax, [ebp+8]
    mov     ebx, [ebp+12]
    mov     ecx, [ebp+16]
    mov     edx, [ebp+20]
    mov     esi, [ebp+24]
    mov     edi, [ebp+28]

    mov     ebp, esp

    sti                                ; Enable interrupts

IntCall :
    int     0x00

    cli                                ; Disable interrupts

    ;--------------------------------------
    ; Write back registers after interrupt is done

    mov     ebp, Param_REGS - RMCSetup

    mov     [ebp+8], eax
    mov     ax, ds
    mov     [ebp+0], ax
    mov     ax, es
    mov     [ebp+2], ax
    mov     ax, fs
    mov     [ebp+4], ax
    mov     ax, gs
    mov     [ebp+6], ax
    mov     [ebp+12], ebx
    mov     [ebp+16], ecx
    mov     [ebp+20], edx
    mov     [ebp+24], esi
    mov     [ebp+28], edi

    ;--------------------------------------
    ; Interrupt or far call returns here

ReturnFromCall :

    ;--------------------------------------
    ; Set the A20 line

    call    Set_A20_Line

    ;--------------------------------------
    ; Mask real mode IRQs

;    mov     al, 0xFB
;    out     PIC1_DATA, al
;    call    Delay_RM

;    mov     al, 0xFF
;    out     PIC2_DATA, al
;    call    Delay_RM

    ;--------------------------------------
    ; Get back our base address

    pop     ebx

    ;--------------------------------------
    ; Restore protected mode but not paging
    ; Paging would produce effective addresses
    ; out of the 16-bit range

    mov     eax, CR0_PROTECTEDMODE
    mov     cr0, eax

    ;--------------------------------------
    ; Jump to 16-bit code
    ; We want to do a 32-bit far jump

    db      0x66                       ; operand size override
    db      0xEA                       ; jmp far
Rel4 :
    dd      L5 - RMCSetup
    dw      SELECTOR_REAL_CODE

L5 :

bits 16

    ;--------------------------------------
    ; Jump to 32-bit code
    ; We want to do a 32-bit far jump

    db      0x66                       ; operand size override
    db      0xEA                       ; jmp far
Rel5 :
    dd      L6 - RMCSetup
    dw      SELECTOR_KERNEL_CODE

L6 :

bits 32

    ;--------------------------------------
    ; Restore 32-bit segment registers

    mov     ax, SELECTOR_KERNEL_DATA
    mov     ds, ax
    mov     ss, ax
    mov     es, ax
    mov     gs, ax
    mov     fs, ax

    ;--------------------------------------
    ; Restore cr0 and cr3

    mov     esi, ebx
    add     esi, Save_REG - RMCSetup
    mov     eax, [esi+22]
    mov     cr3, eax
    mov     eax, [esi+18]
    mov     cr0, eax

    ;--------------------------------------
    ; Load the real IDT

    mov     esi, ebx
    add     esi, Real_IDT_Label - RMCSetup
    lidt    [esi]

    ;--------------------------------------
    ; Load the real GDT

    mov     esi, ebx
    add     esi, Real_GDT_Label - RMCSetup
    lgdt    [esi]

    ;--------------------------------------
    ; Transfer register parameters

    mov     esi, ebx
    add     esi, Param_REGS - RMCSetup
    mov     edi, ebx
    add     edi, Save_PRM - RMCSetup
    mov     edi, [edi]
    mov     ecx, 36
    cld
    rep     movsb

    ;--------------------------------------
    ; Restore registers

    mov     esi, ebx
    add     esi, Save_REG - RMCSetup

    mov     ax, [esi+0]
    mov     ds, ax
    mov     ax, [esi+2]
    mov     ss, ax
    mov     ax, [esi+4]
    mov     es, ax
    mov     ax, [esi+6]
    mov     fs, ax
    mov     ax, [esi+8]
    mov     gs, ax
    mov     esp, [esi+10]
    mov     ebp, [esi+14]

    ;--------------------------------------
    ; Return to kernel code

    db      0xEA                       ; jmp far
    dd      RealModeCall_Back
    dw      SELECTOR_KERNEL_CODE

    ;--------------------------------------

RMCSetup_Hang :

    jmp     RMCSetup_Hang

    ;--------------------------------------

bits 16

Set_A20_Line :

    push    ax
    mov     ah, 0xDF
    call    Gate_A20
    pop     ax
    ret

    ;--------------------------------------

bits 16

Clear_A20_Line :

    push    ax
    mov     ah, 0xDD
    call    Gate_A20
    pop     ax
    ret

    ;--------------------------------------

bits 16

Gate_A20 :

    pushf
    cli

    call    Empty_8042

    mov     al, 0xD1
    out     KEYBOARD_STATUS, al

    call    Empty_8042

    mov     al, ah
    out     KEYBOARD_CONTROL, al

    call    Empty_8042

    push    cx
    mov     cx, 0x14

Gate_A20_Loop :

    out     0xED, ax                   ; IO delay
    loop    Gate_A20_Loop
    pop     cx

    popf
    ret

    ;--------------------------------------

bits 16

Empty_8042 :

    out     0x00ED, ax                 ; IO delay

    in      al, KEYBOARD_STATUS
    test    al, KEYBOARD_STATUS_OUT_FULL
    jz      Empty_8042_NoOutput

    out     0x00ED, ax                 ; IO delay

    in      al, KEYBOARD_CONTROL
    jmp     Empty_8042

Empty_8042_NoOutput :

    test    al, KEYBOARD_STATUS_IN_FULL
    jnz     Empty_8042
    ret

    ;--------------------------------------

Delay_RM    : jmp Delay_RM_L1
Delay_RM_L1 : jmp Delay_RM_L2
Delay_RM_L2 : ret

    ;--------------------------------------

RMCSetupEnd :

;----------------------------------------------------------------------------

bits 32

SetupPIC_RM :

    push    eax

    mov     al, ICW1_INIT + ICW1_ICW4
    out     PIC1_CMD, al
    call    Delay
    mov     al, ICW1_INIT + ICW1_ICW4
    out     PIC2_CMD, al
    call    Delay

    mov     al, 0x08
    out     PIC1_DATA, al
    call    Delay
    mov     al, 0x70
    out     PIC2_DATA, al
    call    Delay

    mov     al, 0x04
    out     PIC1_DATA, al
    call    Delay
    mov     al, 0x02
    out     PIC2_DATA, al
    call    Delay

    mov     al, ICW4_8086
    out     PIC1_DATA, al
    call    Delay
    mov     al, ICW4_8086
    out     PIC2_DATA, al
    call    Delay

    pop     eax
    ret

;----------------------------------------------------------------------------

bits 32

SetupPIC_PM :

    push    eax

    mov  al, ICW1_INIT + ICW1_ICW4
    out  PIC1_CMD, al
    call Delay
    mov  al, ICW1_INIT + ICW1_ICW4
    out  PIC2_CMD, al
    call Delay

    mov  al, PIC1_VECTOR
    out  PIC1_DATA, al
    call Delay
    mov  al, PIC2_VECTOR
    out  PIC2_DATA, al
    call Delay

    mov     al, 0x04
    out     PIC1_DATA, al
    call    Delay
    mov     al, 0x02
    out     PIC2_DATA, al
    call    Delay

    mov     al, ICW4_8086
    out     PIC1_DATA, al
    call    Delay
    mov     al, ICW4_8086
    out     PIC2_DATA, al
    call    Delay

    pop     eax
    ret

;----------------------------------------------------------------------------

bits 32

SetupTimer_RM :

    push    eax

    mov     eax, 0x36
    out     CLOCK_COMMAND, al
    call    Delay

    mov     eax, CLOCK_18_2
    out     CLOCK_DATA, al
    call    Delay

    mov     eax, CLOCK_18_2
    shr     eax, 8
    out     CLOCK_DATA, al
    call    Delay

    pop     eax
    ret

;----------------------------------------------------------------------------

bits 32

SetupTimer_PM :

    push    eax

    mov     eax, 0x36
    out     CLOCK_COMMAND, al
    call    Delay

    mov     eax, CLOCK_100
    out     CLOCK_DATA, al
    call    Delay

    mov     eax, CLOCK_100
    shr     eax, 8
    out     CLOCK_DATA, al
    call    Delay

    pop     eax
    ret

;----------------------------------------------------------------------------

bits 32

Delay    : jmp Delay_L1
Delay_L1 : jmp Delay_L2
Delay_L2 : ret

;----------------------------------------------------------------------------

bits 32

_Exit_EXOS :

    jmp     _Exit_EXOS_Start

_Exit_EXOS_Data :

    EED_DS  : dw 0
    EED_ES  : dw 0
    EED_FS  : dw 0
    EED_GS  : dw 0
    EED_EAX : dd 0
    EED_EBX : dd 0
    EED_ECX : dd 0
    EED_EDX : dd 0
    EED_ESI : dd 0
    EED_EDI : dd 0
    EED_EFL : dd 0

_Exit_EXOS_Start :

    push    ebp
    mov     ebp, esp

    push    ebx
    push    ecx
    push    esi
    push    edi

    ;--------------------------------------
    ; Store loader stack in registers

    mov     eax, [ebp+(PBN+0)]
    mov     [EED_ESI], eax
    mov     eax, [ebp+(PBN+4)]
    mov     [EED_EDI], eax

    ;--------------------------------------
    ; We will put our real mode function at
    ; StubAddress + 4096

    mov     ebx, [_StubAddress]
    add     ebx, N_4KB

    mov     esi, Exit_EXOS_16
    mov     edi, ebx
    mov     ecx, Exit_EXOS_16_End - Exit_EXOS_16
    cld
    rep     movsb

    mov     eax, _Exit_EXOS_Data
    push    eax

    mov     ecx, ebx
    and     ecx, 0x0000000F
    mov     eax, ebx
    and     eax, 0xFFFFFFF0
    shr     eax, 4
    shl     eax, 16
    or      eax, ecx
    push    eax

    call    _RealModeCall

    add     esp, 8

    pop     edi
    pop     esi
    pop     ecx
    pop     ebx

    pop     ebp
    ret

;----------------------------------------------------------------------------

bits 16

Exit_EXOS_16 :

    ;--------------------------------------
    ; Here we are supposed to have the loader's
    ; SS in SI and it's SP in DI
    ; So all we do is setup the stack and make
    ; a far return

    cli
    mov     ss, si
    mov     sp, di
    sti
    retf

Exit_EXOS_16_End :

;----------------------------------------------------------------------------

bits 32

_RealModeCallTest :

    jmp     _RealModeCallTest_Start

_RealModeCallTest_Data :

    times 64 db 0

_RealModeCallTest_Start :

    push    ebp
    mov     ebp, esp

    push    ecx
    push    esi
    push    edi

    mov     esi, TestFarCall
    mov     edi, 0x70000
    mov     ecx, TestFarCallEnd - TestFarCall
    cld
    rep     movsb

    mov     eax, _RealModeCallTest_Data
    push    eax
    mov     eax, 0x70000000
    push    eax

    call    _RealModeCall

    add     esp, 8

    pop     edi
    pop     esi
    pop     ecx

    pop     ebp
    ret

;----------------------------------------------------------------------------

bits 16

TestFarCall :

    push    ds
    push    es
    push    ax
    push    bx
    push    cx
    push    si
    push    di

    mov     ax, cs
    mov     ds, ax
    mov     ax, 0xB800
    mov     es, ax
    mov     si, Text - TestFarCall
    xor     di, di
    mov     cx, 8
    cld
    rep     movsw

    pop     si
    pop     di
    pop     cx
    pop     bx
    pop     ax
    pop     es
    pop     ds

    retf

Text :

    db 'F', 7, 'a', 7, 'r', 7, ' ', 7, 'c', 7, 'a', 7, 'l', 7, 'l', 7

TestFarCallEnd :

;----------------------------------------------------------------------------
