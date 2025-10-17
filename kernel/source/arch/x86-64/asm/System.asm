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
;   System functions (x86-64)
;
;-------------------------------------------------------------------------

%include "x86-64.inc"
%include "System.inc"

;-------------------------------------------------------------------------

section .data
BITS 32

    global DeadBeef

DeadBeef      dd 0xDEADBEEF

;-------------------------------------------------------------------------

section .text.stub
BITS 64

global start

stub_base:

    jmp     start

    times (4 - ($ - $$)) db 0

Magic : db 'EXOS'

FUNC_HEADER
start:

    mov         [KernelStartup + KernelStartupInfo.StackTop], rsp

    call        KernelMain

.hang:
    cli         ; Should not return here, hang
    hlt
    jmp .hang
    nop

;-------------------------------------------------------------------------

section .text
BITS 64

; Placeholder implementations until the long mode system layer is implemented.

%macro STUB_FUNC 1
    global %1
%1:
    ud2
    ret
%endmacro

%macro SYS_FUNC_BEGIN 1
    FUNC_HEADER
    global %1
%1:
%endmacro

%macro SYS_FUNC_END 0
    ret
%endmacro

SYS_FUNC_BEGIN GetCPUID
    push    rbx

    mov     eax, 0
    cpuid

    mov     dword [rdi + 0x00], eax
    mov     dword [rdi + 0x04], ebx
    mov     dword [rdi + 0x08], ecx
    mov     dword [rdi + 0x0C], edx

    mov     eax, 1
    cpuid

    mov     dword [rdi + 0x10], eax
    mov     dword [rdi + 0x14], ebx
    mov     dword [rdi + 0x18], ecx
    mov     dword [rdi + 0x1C], edx

    mov     eax, 1

    pop     rbx
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN DisablePaging
    mov     rax, cr0
    btr     rax, 31
    mov     cr0, rax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN EnablePaging
    mov     rax, cr0
    bts     rax, 31
    mov     cr0, rax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN InPortByte
    mov     dx, di
    xor     eax, eax
    in      al, dx
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN OutPortByte
    mov     dx, di
    mov     eax, esi
    out     dx, al
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN InPortWord
    mov     dx, di
    xor     eax, eax
    in      ax, dx
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN OutPortWord
    mov     dx, di
    mov     eax, esi
    out     dx, ax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN InPortLong
    mov     dx, di
    in      eax, dx
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN OutPortLong
    mov     dx, di
    mov     eax, esi
    out     dx, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN InPortStringWord
    mov     dx, di
    mov     rdi, rsi
    mov     rcx, rdx
    cld
    rep     insw
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN OutPortStringWord
    mov     dx, di
    mov     rcx, rdx
    cld
    rep     outsw
    xor     eax, eax
SYS_FUNC_END

SYS_FUNC_BEGIN PeekConsoleWord
    mov     rax, rdi
    add     rax, CONSOLE_TEXT_BUFFER_BASE
    movzx   eax, word [rax]
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN PokeConsoleWord
    mov     rax, rdi
    add     rax, CONSOLE_TEXT_BUFFER_BASE
    mov     word [rax], si
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN SaveRegisters
    push    rbp
    mov     rbp, rsp
    push    rax
    push    rbx
    push    rcx
    push    rdx
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
    pushfq
    cli

    mov     r11, [rbp - 48]

    mov     rax, [rbp - 120]
    mov     [r11 + REG_OFF_RFLAGS], rax

    mov     rax, [rbp - 8]
    mov     [r11 + REG_OFF_RAX], rax
    mov     rax, [rbp - 16]
    mov     [r11 + REG_OFF_RBX], rax
    mov     rax, [rbp - 24]
    mov     [r11 + REG_OFF_RCX], rax
    mov     rax, [rbp - 32]
    mov     [r11 + REG_OFF_RDX], rax
    mov     rax, [rbp - 40]
    mov     [r11 + REG_OFF_RSI], rax
    mov     rax, [rbp - 48]
    mov     [r11 + REG_OFF_RDI], rax

    mov     rax, [rbp]
    mov     [r11 + REG_OFF_RBP], rax
    mov     rax, [rbp]
    add     rax, 8
    mov     [r11 + REG_OFF_RSP], rax

    mov     rax, [rbp - 56]
    mov     [r11 + REG_OFF_R8], rax
    mov     rax, [rbp - 64]
    mov     [r11 + REG_OFF_R9], rax
    mov     rax, [rbp - 72]
    mov     [r11 + REG_OFF_R10], rax
    mov     rax, [rbp - 80]
    mov     [r11 + REG_OFF_R11], rax
    mov     rax, [rbp - 88]
    mov     [r11 + REG_OFF_R12], rax
    mov     rax, [rbp - 96]
    mov     [r11 + REG_OFF_R13], rax
    mov     rax, [rbp - 104]
    mov     [r11 + REG_OFF_R14], rax
    mov     rax, [rbp - 112]
    mov     [r11 + REG_OFF_R15], rax

    mov     rax, [rbp + 8]
    mov     [r11 + REG_OFF_RIP], rax

    mov     ax, cs
    mov     [r11 + REG_OFF_CS], ax
    mov     ax, ds
    mov     [r11 + REG_OFF_DS], ax
    mov     ax, ss
    mov     [r11 + REG_OFF_SS], ax
    mov     ax, es
    mov     [r11 + REG_OFF_ES], ax
    mov     ax, fs
    mov     [r11 + REG_OFF_FS], ax
    mov     ax, gs
    mov     [r11 + REG_OFF_GS], ax

    mov     rax, cr0
    mov     [r11 + REG_OFF_CR0], rax
    mov     rax, cr2
    mov     [r11 + REG_OFF_CR2], rax
    mov     rax, cr3
    mov     [r11 + REG_OFF_CR3], rax
    mov     rax, cr4
    mov     [r11 + REG_OFF_CR4], rax
    mov     rax, cr8
    mov     [r11 + REG_OFF_CR8], rax

    mov     rax, dr0
    mov     [r11 + REG_OFF_DR0], rax
    mov     rax, dr1
    mov     [r11 + REG_OFF_DR1], rax
    mov     rax, dr2
    mov     [r11 + REG_OFF_DR2], rax
    mov     rax, dr3
    mov     [r11 + REG_OFF_DR3], rax
    mov     rax, dr6
    mov     [r11 + REG_OFF_DR6], rax
    mov     rax, dr7
    mov     [r11 + REG_OFF_DR7], rax

    popfq
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
    pop     rdx
    pop     rcx
    pop     rbx
    pop     rax
    pop     rbp
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN DoSystemCall
    mov     r11, rbx
    mov     eax, edi
    mov     rbx, rsi
    int     EXOS_USER_CALL
    mov     rbx, r11
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN IdleCPU
    sti
    hlt
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN DeadCPU
.loop:
    sti
    hlt
    jmp     .loop
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN Reboot
    cli

.wait_input_clear:
    in      al, 0x64
    test    al, 0x02
    jnz     .wait_input_clear

    mov     al, 0xFE
    out     0x64, al

.hang:
    hlt
    jmp     .hang
SYS_FUNC_END

;----------------------------------------------------------------------------

section .shared_text
BITS 64

SYS_FUNC_BEGIN TaskRunner
    mov     r8, rax
    mov     r9, rbx

    xor     ecx, ecx
    xor     edx, edx
    xor     esi, esi
    xor     ebp, ebp
    xor     r10d, r10d
    xor     r11d, r11d
    xor     r12d, r12d
    xor     r13d, r13d
    xor     r14d, r14d
    xor     r15d, r15d

    mov     rbx, r9
    test    rbx, rbx
    je      _TaskRunner_Exit

    mov     rdi, r8
    call    rbx

_TaskRunner_Exit:
    mov     rbx, rax
    mov     eax, 0x33
    int     EXOS_USER_CALL

.sleep:
    mov     eax, 0x0E
    mov     rbx, MAX_UINT
    int     EXOS_USER_CALL
    jmp     .sleep
SYS_FUNC_END

;----------------------------------------------------------------------------

section .bss
align 16

    global Stack
Stack:
    resq 1

;----------------------------------------------------------------------------

section .note.GNU-stack noalloc noexec nowrite align=1
