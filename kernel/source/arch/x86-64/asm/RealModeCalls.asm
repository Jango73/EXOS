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
;   Real mode call function (x86-64 System V ABI)
;
;-------------------------------------------------------------------------

%include "x86-64.inc"

extern Kernel_i386
extern SwitchToPICForRealMode
extern RestoreIOAPICAfterRealMode

%define IA32_EFER               0xC0000080
%define IA32_EFER_LME           0x00000100
%define CR4_PAE                 0x00000020

;----------------------------------------------------------------------------
;
; Helper values to access function parameters and local variables
;
%define LOCAL_STACK_SIZE        0x60
%define LOCAL_SAVE_RAX          0x08
%define LOCAL_SAVE_RBX          0x10
%define LOCAL_SAVE_RCX          0x18
%define LOCAL_SAVE_RDX          0x20
%define LOCAL_SAVE_RSI          0x28
%define LOCAL_SAVE_RDI          0x30
%define LOCAL_SAVE_RFLAGS       0x38
%define LOCAL_SAVE_DS           0x40
%define LOCAL_SAVE_ES           0x42
%define LOCAL_SAVE_FS           0x44
%define LOCAL_SAVE_GS           0x46
%define LOCAL_PARAM_INT         0x48
%define LOCAL_PARAM_PTR         0x50

;----------------------------------------------------------------------------

section .text
bits 64

    global RealModeCall
    global RealModeCallTest
    global RMCJump1
    global RMCJump16
    global RMCIntCall

;--------------------------------------
;
; void RealModeCall (U32 IntOrAddress, LPINTEL_X86_REGISTERS Registers)
;
FUNC_HEADER
RealModeCall:

    ;--------------------------------------
    ; Disable interrupts

    cli

    push    rbp
    mov     rbp, rsp
    sub     rsp, LOCAL_STACK_SIZE

    ;--------------------------------------
    ; Save all registers and CPU state

    mov     [rbp-LOCAL_SAVE_RAX], rax
    mov     [rbp-LOCAL_SAVE_RBX], rbx
    mov     [rbp-LOCAL_SAVE_RCX], rcx
    mov     [rbp-LOCAL_SAVE_RDX], rdx
    mov     [rbp-LOCAL_SAVE_RSI], rsi
    mov     [rbp-LOCAL_SAVE_RDI], rdi

    pushfq
    pop     qword [rbp-LOCAL_SAVE_RFLAGS]

    mov     ax, ds
    mov     [rbp-LOCAL_SAVE_DS], ax
    mov     ax, es
    mov     [rbp-LOCAL_SAVE_ES], ax
    mov     ax, fs
    mov     [rbp-LOCAL_SAVE_FS], ax
    mov     ax, gs
    mov     [rbp-LOCAL_SAVE_GS], ax

    mov     dword [rbp-LOCAL_PARAM_INT], edi
    mov     [rbp-LOCAL_PARAM_PTR], rsi

    ;--------------------------------------
    ; Switch to PIC mode for real mode call

    call    SwitchToPICForRealMode

    ;--------------------------------------
    ; Set the address of the RMC code

    mov     rbx, LOW_MEMORY_PAGE_5

    ;--------------------------------------
    ; Copy the RMC code

    lea     rsi, [rel RMCSetup]
    mov     rdi, rbx
    mov     ecx, RMCSetupEnd - RMCSetup
    cld
    rep     movsb

    ;--------------------------------------
    ; Copy the GDT at RMC code + 16

    mov     rsi, [rel Kernel_i386 + KERNELDATA_X86_64.GDT]
    mov     rdi, rbx
    add     rdi, 0x10
    mov     ecx, 8 * SEGMENT_DESCRIPTOR_SIZE
    cld
    rep     movsb

    ;--------------------------------------
    ; Compute relocations

    ; 64-bit far pointer for initial jump to the trampoline copy
    lea     rsi, [rbx + RMCJump1Pointer - RMCSetup]
    mov     qword [rsi], rbx

    ; 64-bit offset for GDT label
    lea     rsi, [rbx + Rel1 - RMCSetup]
    add     dword [rsi], ebx

    ; 64-bit offset for jump to 16-bit PM code
    lea     rsi, [rbx + Rel2 - RMCSetup]
    add     dword [rsi], ebx

    ; Patch Real_IDT_Label base with Kernel_i386.IDT
    mov     rsi, [rel Kernel_i386 + KERNELDATA_X86_64.IDT]
    lea     rdi, [rbx + Real_IDT_Label - RMCSetup + 2]
    mov     dword [rdi], esi

    ; Patch Real_GDT_Label base with Kernel_i386.GDT
    mov     rsi, [rel Kernel_i386 + KERNELDATA_X86_64.GDT]
    lea     rdi, [rbx + Real_GDT_Label - RMCSetup + 2]
    mov     dword [rdi], esi

    ; 16-bit segment for jump to real mode code
    lea     rsi, [rbx + Rel3 - RMCSetup]
    mov     eax, ebx
    shr     eax, 4
    add     word [rsi], ax

    ; 32-bit offset for jump to 16-bit PM code
    lea     rsi, [rbx + Rel4 - RMCSetup]
    add     dword [rsi], ebx

    ; 32-bit offset for jump to 32-bit PM code
    lea     rsi, [rbx + Rel5 - RMCSetup]
    add     dword [rsi], ebx

    ; 64-bit far pointer used when returning to long mode
    lea     rdi, [rbx + ReturnToLong - RMCSetup]
    lea     rax, [rbx + ReturnToLongStub - RMCSetup]
    mov     [rdi], rax

    ; 64-bit target address executed after the long mode stub restores state
    lea     rdi, [rbx + ReturnToLongTarget - RMCSetup]
    lea     rax, [rel RealModeCall_Back]
    mov     [rdi], rax

    ;--------------------------------------
    ; Transfer register parameters to low memory buffer

    mov     r8, [rbp-LOCAL_PARAM_PTR]
    test    r8, r8
    jz      .SkipParamCopy

    lea     rdi, [rbx + Param_REGS - RMCSetup]
    mov     rsi, r8
    mov     ecx, 36
    cld
    rep     movsb

.SkipParamCopy:

    ;--------------------------------------
    ; Setup arguments

    mov     eax, [rbp-LOCAL_PARAM_INT]
    mov     rcx, r8

    ;--------------------------------------
    ; Jump to code at rbx

RMCJump1:

    jmp     far [rbx + RMCJump1Pointer - RMCSetup]

RealModeCall_Back:

    ;--------------------------------------
    ; Restore IOAPIC mode

    call    RestoreIOAPICAfterRealMode

    ;--------------------------------------
    ; Transfer register parameters back to caller buffer

    mov     r8, [rbp-LOCAL_PARAM_PTR]
    test    r8, r8
    jz      .SkipParamRestore

    mov     rsi, LOW_MEMORY_PAGE_5
    add     rsi, Param_REGS - RMCSetup
    mov     rdi, r8
    mov     ecx, 36
    cld
    rep     movsb

.SkipParamRestore:

    ;--------------------------------------
    ; Restore all registers

    mov     rbx, [rbp-LOCAL_SAVE_RBX]
    mov     rcx, [rbp-LOCAL_SAVE_RCX]
    mov     rdx, [rbp-LOCAL_SAVE_RDX]
    mov     rsi, [rbp-LOCAL_SAVE_RSI]
    mov     rdi, [rbp-LOCAL_SAVE_RDI]
    mov     rax, [rbp-LOCAL_SAVE_RAX]

    push    qword [rbp-LOCAL_SAVE_RFLAGS]
    popfq

    mov     ax, [rbp-LOCAL_SAVE_DS]
    mov     ds, ax
    mov     ax, [rbp-LOCAL_SAVE_ES]
    mov     es, ax
    mov     ax, [rbp-LOCAL_SAVE_FS]
    mov     fs, ax
    mov     ax, [rbp-LOCAL_SAVE_GS]
    mov     gs, ax

    add     rsp, LOCAL_STACK_SIZE
    pop     rbp
    ret

;----------------------------------------------------------------------------

; This code executes at address in rbx
; Register contents upon entry :
; EAX : Interrupt number
; ECX : Pointer to regs or segment:offset func pointer
; EDX : Real mode IRQ masks

bits 64

RMCSetup:

    jmp     RMCEntry64

RMCJump1Pointer:
    dq 0
    dw SELECTOR_KERNEL_CODE

    db 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

GDT:

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

Temp_IDT_Label:
    dw 1023                                ; Limit (1024 entries - 1)
    dd 0x00000000                         ; Base address for real mode IVT

Temp_GDT_Label:
    dw (8 * SEGMENT_DESCRIPTOR_SIZE) - 1
Rel1:
    dd GDT - RMCSetup

Real_IDT_Label:
    dw  (IDT_SIZE - 1)
    dd 0

Real_GDT_Label:
    dw  (GDT_SIZE - 1)
    dd 0

Save_REG:
Save_SEG: dw 0, 0, 0, 0, 0           ; ds, ss, es, fs, gs
    align 8
Save_RSP: dq 0
Save_RBP: dq 0
Save_CR0: dq 0
Save_CR3: dq 0
Save_CR4: dq 0
Save_EFER: dq 0
Save_INT: dd 0
    dd 0                              ; padding to keep 8-byte alignment
    align 8
Save_PRM: dq 0
Save_IRQ: dd 0
    dd 0                              ; padding

ReturnToLongTarget:
    dq 0

Param_REGS:
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

RMCEntry64:

    ;--------------------------------------
    ; Save arguments and CPU state required to restore long mode

    lea     rsi, [rbx + Save_INT - RMCSetup]
    mov     [rsi], eax

    lea     rsi, [rbx + Save_PRM - RMCSetup]
    mov     [rsi], rcx

    lea     rsi, [rbx + Save_IRQ - RMCSetup]
    mov     [rsi], rdx

    lea     rsi, [rbx + Save_SEG - RMCSetup]
    mov     ax, ds
    mov     [rsi + 0], ax
    mov     ax, ss
    mov     [rsi + 2], ax
    mov     ax, es
    mov     [rsi + 4], ax
    mov     ax, fs
    mov     [rsi + 6], ax
    mov     ax, gs
    mov     [rsi + 8], ax

    mov     [rbx + Save_RSP - RMCSetup], rsp
    mov     [rbx + Save_RBP - RMCSetup], rbp

    mov     rax, cr0
    mov     [rbx + Save_CR0 - RMCSetup], rax
    mov     rax, cr3
    mov     [rbx + Save_CR3 - RMCSetup], rax
    mov     rax, cr4
    mov     [rbx + Save_CR4 - RMCSetup], rax

    mov     ecx, IA32_EFER
    rdmsr
    shl     rdx, 32
    or      rax, rdx
    mov     [rbx + Save_EFER - RMCSetup], rax

    jmp     Start

bits 32

Start:

    ;--------------------------------------
    ; Disable paging and flush TLB

    mov     eax, [ebx + Save_CR0 - RMCSetup]
    and     eax, ~CR0_PAGING
    mov     cr0, eax
    xor     eax, eax
    mov     cr3, eax

    ;--------------------------------------
    ; Disable IA-32e extensions

    mov     ecx, IA32_EFER
    mov     eax, [ebx + Save_EFER - RMCSetup]
    mov     edx, [ebx + Save_EFER - RMCSetup + 4]
    and     eax, ~IA32_EFER_LME
    wrmsr

    mov     eax, [ebx + Save_CR4 - RMCSetup]
    and     eax, ~CR4_PAE
    mov     cr4, eax

    ;--------------------------------------
    ; Load the real mode IDT (IVT at 0x0000:0x0000)
    ; In real mode, the IVT is always at physical address 0

    lea     esi, [ebx + Temp_IDT_Label - RMCSetup]
    lidt    [esi]

    ;--------------------------------------
    ; Load the temporary GDT

    lea     esi, [ebx + Temp_GDT_Label - RMCSetup]
    lgdt    [esi]

    ;--------------------------------------
    ; Jump to next instruction with 16-bit code selector

RMCJump16:

    db      0xEA                       ; jmp far
Rel2:
    dd      L1 - RMCSetup
    dw      SELECTOR_REAL_CODE

L1:

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
Rel3:
    dw      0

L2:

bits 16

    ;--------------------------------------
    ; Disable interrupts

    cli

    ;--------------------------------------
    ; Setup real-mode segment registers

    mov     ax, cs                     ; Get code segment
    mov     ds, ax                     ; Data segment
    mov     es, ax                     ; Extra segment 1
    mov     fs, ax                     ; Extra segment 2
    mov     gs, ax                     ; Extra segment 3

    xor     ax, ax
    mov     ss, ax                     ; Stack segment

    ;--------------------------------------
    ; Setup the stack

    mov     sp, LOW_MEMORY_PAGE_5

    ;--------------------------------------
    ; Save our base address

    push    ebx

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

DoInterrupt_Jump:

    jmp     DoInterrupt

DoFarCall:

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

    db      0x9A                       ; call far

FarAddress:

    dd      0x00000000                 ; Far 16-bit address

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

DoInterrupt:

    ;--------------------------------------
    ; Adjust interrupt number

    mov     esi, Save_INT - RMCSetup
    mov     eax, [esi]
    mov     esi, (RMCIntCall - RMCSetup) + 1
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

RMCIntCall:
    int     0x00

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

ReturnFromCall:

    ;--------------------------------------
    ; Set the A20 line

    call    Set_A20_Line

    ;--------------------------------------
    ; Get back our base address

    pop     ebx

    ;--------------------------------------
    ; Restore protected mode but not paging
    ; Paging would produce effective addresses
    ; out of the 16-bit range

    mov     eax, CR0_PROTECTED_MODE
    mov     cr0, eax

    ;--------------------------------------
    ; Jump to 16-bit code
    ; We want to do a 32-bit far jump

    db      0x66                       ; operand size override
    db      0xEA                       ; jmp far
Rel4:
    dd      L5 - RMCSetup
    dw      SELECTOR_REAL_CODE

L5:

bits 16

    ;--------------------------------------
    ; Jump to 32-bit code
    ; We want to do a 32-bit far jump

    db      0x66                       ; operand size override
    db      0xEA                       ; jmp far
Rel5:
    dd      L6 - RMCSetup
    dw      SELECTOR_KERNEL_CODE

L6:

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
    ; Restore CR4, IA32_EFER, CR3 and CR0

    mov     eax, [ebx + Save_CR4 - RMCSetup]
    mov     cr4, eax

    mov     ecx, IA32_EFER
    mov     eax, [ebx + Save_EFER - RMCSetup]
    mov     edx, [ebx + Save_EFER - RMCSetup + 4]
    wrmsr

    mov     eax, [ebx + Save_CR3 - RMCSetup]
    mov     cr3, eax

    mov     eax, [ebx + Save_CR0 - RMCSetup]
    mov     cr0, eax

    ;--------------------------------------
    ; Load the real IDT

    lea     esi, [ebx + Real_IDT_Label - RMCSetup]
    lidt    [esi]

    ;--------------------------------------
    ; Load the real GDT

    lea     esi, [ebx + Real_GDT_Label - RMCSetup]
    lgdt    [esi]

    ;--------------------------------------
    ; Restore registers

    lea     esi, [ebx + Save_SEG - RMCSetup]

    mov     ax, [esi + 0]
    mov     ds, ax
    mov     ax, [esi + 2]
    mov     ss, ax
    mov     ax, [esi + 4]
    mov     es, ax
    mov     ax, [esi + 6]
    mov     fs, ax
    mov     ax, [esi + 8]
    mov     gs, ax

    ;--------------------------------------
    ; Return to kernel code

    db      0xEA                       ; jmp far
ReturnToLong:
    dq      0
    dw      SELECTOR_KERNEL_CODE

    ;--------------------------------------

bits 64

ReturnToLongStub:

    mov     rsp, [rbx + Save_RSP - RMCSetup]
    mov     rbp, [rbx + Save_RBP - RMCSetup]
    mov     rax, [rbx + ReturnToLongTarget - RMCSetup]
    jmp     rax

bits 32

RMCSetup_Hang:

    jmp     RMCSetup_Hang

    ;--------------------------------------

bits 16

Set_A20_Line:

    push    ax
    mov     ah, 0xDF
    call    Gate_A20
    pop     ax
    ret

    ;--------------------------------------

bits 16

Clear_A20_Line:

    push    ax
    mov     ah, 0xDD
    call    Gate_A20
    pop     ax
    ret

    ;--------------------------------------

bits 16

Gate_A20:

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

Gate_A20_Loop:

    out     0xED, al                   ; IO delay
    loop    Gate_A20_Loop
    pop     cx

    popf
    ret

    ;--------------------------------------

bits 16

Empty_8042:

    out     0xED, al                   ; IO delay

    in      al, KEYBOARD_STATUS
    test    al, KEYBOARD_STATUS_OUT_FULL
    jz      Empty_8042_NoOutput

    out     0xED, al                   ; IO delay

    in      al, KEYBOARD_CONTROL
    jmp     Empty_8042

Empty_8042_NoOutput:

    test    al, KEYBOARD_STATUS_IN_FULL
    jnz     Empty_8042
    ret

    ;--------------------------------------

Delay_RM    : jmp Delay_RM_L1
Delay_RM_L1 : jmp Delay_RM_L2
Delay_RM_L2 : ret

    ;--------------------------------------

RMCSetupEnd:

;-------------------------------------------------------------------------

bits 32

Delay    : jmp Delay_L1
Delay_L1 : jmp Delay_L2
Delay_L2 : ret

;-------------------------------------------------------------------------

bits 64

FUNC_HEADER
RealModeCallTest:

    push    rbp
    mov     rbp, rsp
    sub     rsp, 0x20

    push    rsi
    push    rdi

    lea     rsi, [rel TestFarCall]
    mov     rdi, 0x70000
    mov     ecx, TestFarCallEnd - TestFarCall
    cld
    rep     movsb

    lea     rsi, [rel _RealModeCallTest_Data]
    mov     edi, 0x70000000
    call    RealModeCall

    pop     rdi
    pop     rsi

    add     rsp, 0x20
    pop     rbp
    ret

;-------------------------------------------------------------------------

bits 32

_RealModeCallTest_Data:

    times 64 db 0

bits 16

TestFarCall:

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

bits 16

Text:

    db 'F', 7, 'a', 7, 'r', 7, ' ', 7, 'c', 7, 'a', 7, 'l', 7, 'l', 7

TestFarCallEnd:

;-------------------------------------------------------------------------

section .note.GNU-stack noalloc noexec nowrite align=1
