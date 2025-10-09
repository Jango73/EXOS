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

extern KernelStartup

PRIVILEGE_KERNEL        equ 0x00
SELECTOR_GLOBAL         equ 0x00
SELECTOR_KERNEL_CODE    equ (0x08 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_KERNEL_DATA    equ (0x10 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)

CR0_TASKSWITCH          equ 0x0000000000000008

KERNELSTARTUP_IRQMASK_21_PM equ 24
KERNELSTARTUP_IRQMASK_A1_PM equ 28

CONSOLE_TEXT_BUFFER_BASE    equ 0x00000000000B8000

REG_OFF_RFLAGS equ 0
REG_OFF_RAX    equ 8
REG_OFF_RBX    equ 16
REG_OFF_RCX    equ 24
REG_OFF_RDX    equ 32
REG_OFF_RSI    equ 40
REG_OFF_RDI    equ 48
REG_OFF_RBP    equ 56
REG_OFF_RSP    equ 64
REG_OFF_R8     equ 72
REG_OFF_R9     equ 80
REG_OFF_R10    equ 88
REG_OFF_R11    equ 96
REG_OFF_R12    equ 104
REG_OFF_R13    equ 112
REG_OFF_R14    equ 120
REG_OFF_R15    equ 128
REG_OFF_RIP    equ 136
REG_OFF_CS     equ 144
REG_OFF_DS     equ 146
REG_OFF_SS     equ 148
REG_OFF_ES     equ 150
REG_OFF_FS     equ 152
REG_OFF_GS     equ 154
REG_OFF_CR0    equ 156
REG_OFF_CR2    equ 164
REG_OFF_CR3    equ 172
REG_OFF_CR4    equ 180
REG_OFF_CR8    equ 188
REG_OFF_DR0    equ 196
REG_OFF_DR1    equ 204
REG_OFF_DR2    equ 212
REG_OFF_DR3    equ 220
REG_OFF_DR6    equ 228
REG_OFF_DR7    equ 236

;----------------------------------------------------------------------------

section .data
BITS 64

    global DeadBeef

;--------------------------------------

DeadBeef      dq 0x00000000DEADBEEF

;--------------------------------------

section .text.stub
BITS 64

global start
extern KernelMain
extern KernelLogText

stub_base:

    jmp     start

    times (4 - ($ - $$)) db 0

Magic : db 'EXOS'

FUNC_HEADER
start:
    ud2
    hlt
    jmp     start

;----------------------------------------------------------------------------

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

CR0_PAGING  equ 0x0000000080000000

STUB_FUNC DoSystemCall
STUB_FUNC IdleCPU
STUB_FUNC DeadCPU
STUB_FUNC Reboot
STUB_FUNC TaskRunner

;----------------------------------------------------------------------------

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
    or      rax, CR0_PAGING
    mov     cr0, rax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN DisableInterrupts
    cli
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN EnableInterrupts
    sti
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN SaveFlags
    pushfq
    pop     rax
    mov     dword [rdi], eax
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN RestoreFlags
    mov     eax, dword [rdi]
    push    rax
    popfq
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN SaveFPU
    fsave   [rdi]
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN RestoreFPU
    frstor  [rdi]
    xor     eax, eax
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

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN MaskIRQ
    mov     ecx, edi
    and     ecx, 0x07
    mov     eax, 1
    shl     eax, cl
    cmp     edi, 8
    jb      .primary

    mov     edx, dword [rel KernelStartup + KERNELSTARTUP_IRQMASK_A1_PM]
    or      edx, eax
    mov     dword [rel KernelStartup + KERNELSTARTUP_IRQMASK_A1_PM], edx
    mov     eax, edx
    mov     dx, PIC2_DATA
    out     dx, al
    jmp     .done

.primary:
    mov     edx, dword [rel KernelStartup + KERNELSTARTUP_IRQMASK_21_PM]
    or      edx, eax
    mov     dword [rel KernelStartup + KERNELSTARTUP_IRQMASK_21_PM], edx
    mov     eax, edx
    mov     dx, PIC1_DATA
    out     dx, al

.done:
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN UnmaskIRQ
    mov     ecx, edi
    and     ecx, 0x07
    mov     eax, 1
    shl     eax, cl
    not     eax
    cmp     edi, 8
    jb      .primary

    mov     edx, dword [rel KernelStartup + KERNELSTARTUP_IRQMASK_A1_PM]
    and     edx, eax
    mov     dword [rel KernelStartup + KERNELSTARTUP_IRQMASK_A1_PM], edx
    mov     eax, edx
    mov     dx, PIC2_DATA
    out     dx, al
    jmp     .done

.primary:
    mov     edx, dword [rel KernelStartup + KERNELSTARTUP_IRQMASK_21_PM]
    and     edx, eax
    mov     dword [rel KernelStartup + KERNELSTARTUP_IRQMASK_21_PM], edx
    mov     eax, edx
    mov     dx, PIC1_DATA
    out     dx, al

.done:
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN DisableIRQ
    pushfq
    cli
    call    MaskIRQ
    popfq
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN EnableIRQ
    pushfq
    cli
    call    UnmaskIRQ
    popfq
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN LoadGlobalDescriptorTable
    sub     rsp, 16
    mov     word [rsp], si
    mov     qword [rsp + 2], rdi
    lgdt    [rsp]
    jmp     SELECTOR_KERNEL_CODE:.flush

.flush:
    mov     ax, SELECTOR_KERNEL_DATA
    mov     ss, ax
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    add     rsp, 16
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN ReadGlobalDescriptorTable
    sgdt    [rdi]
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN LoadLocalDescriptorTable
    cli
    sub     rsp, 16
    mov     word [rsp], si
    mov     qword [rsp + 2], rdi
    lldt    [rsp]
    add     rsp, 16
    sti
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN LoadInterruptDescriptorTable
    pushfq
    cli
    sub     rsp, 16
    mov     word [rsp], si
    mov     qword [rsp + 2], rdi
    lidt    [rsp]
    add     rsp, 16
    popfq
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN LoadPageDirectory
    mov     rax, rdi
    mov     rdx, cr3
    cmp     rax, rdx
    je      .out
    mov     cr3, rax

.out:
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN LoadInitialTaskRegister
    mov     ax, di
    ltr     ax
    pushfq
    pop     rax
    mov     rdx, EFLAGS_NT
    not     rdx
    and     rax, rdx
    push    rax
    popfq
    clts
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN GetTaskRegister
    xor     eax, eax
    str     ax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN GetPageDirectory
    mov     rax, cr3
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN InvalidatePage
    invlpg  byte [rdi]
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN FlushTLB
    mov     rax, cr3
    mov     cr3, rax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN SwitchToTask
    sub     rsp, 16
    xor     eax, eax
    mov     dr6, rax
    mov     dr7, rax
    mov     [rsp], rax
    mov     [rsp + 8], rax
    mov     word [rsp + 8], di
    jmp     far [rsp]
    add     rsp, 16
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN SetTaskState
    mov     rax, cr0
    or      rax, CR0_TASKSWITCH
    mov     cr0, rax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN ClearTaskState
    clts
SYS_FUNC_END

;----------------------------------------------------------------------------

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

SYS_FUNC_BEGIN MemorySet
    mov     rcx, rdx
    mov     eax, esi
    cld
    rep     stosb
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN MemoryCopy
    mov     rcx, rdx
    cld
    rep     movsb
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN MemoryCompare
    mov     rcx, rdx
    cld
    repe    cmpsb
    je      .equal
    ja      .greater
    mov     eax, -1
    jmp     .done

.greater:
    mov     eax, 1
    jmp     .done

.equal:
    xor     eax, eax

.done:
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN MemoryMove
    mov     r8, rdi
    mov     rcx, rdx
    test    rcx, rcx
    jz      .exit
    cmp     rdi, rsi
    je      .exit

    mov     rax, rsi
    add     rax, rcx
    cmp     rdi, rax
    jae     .forward

    lea     rsi, [rsi + rcx - 1]
    lea     rdi, [rdi + rcx - 1]
    std
    rep     movsb
    cld
    jmp     .finish

.forward:
    cld
    rep     movsb

.finish:
    mov     rax, r8
    jmp     .exit

.exit:
    mov     rax, r8
SYS_FUNC_END

;----------------------------------------------------------------------------

section .bss
align 16

    global Stack
Stack:
    resq 1

;----------------------------------------------------------------------------
