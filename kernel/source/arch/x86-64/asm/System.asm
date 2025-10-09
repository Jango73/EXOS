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
extern KernelStartup

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

SYS_FUNC_BEGIN MaskIRQ
    mov     ecx, edi
    and     ecx, 0x07
    mov     eax, 1
    shl     eax, cl

    mov     ecx, edi
    cmp     ecx, 8
    jge     .high

    mov     edx, dword [rel KernelStartup + KernelStartupInfo64.IRQMask_21_PM]
    or      edx, eax
    mov     dword [rel KernelStartup + KernelStartupInfo64.IRQMask_21_PM], edx
    mov     eax, edx
    out     PIC1_DATA, al
    ret

.high:
    mov     edx, dword [rel KernelStartup + KernelStartupInfo64.IRQMask_A1_PM]
    or      edx, eax
    mov     dword [rel KernelStartup + KernelStartupInfo64.IRQMask_A1_PM], edx
    mov     eax, edx
    out     PIC2_DATA, al
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN UnmaskIRQ
    mov     ecx, edi
    and     ecx, 0x07
    mov     eax, 1
    shl     eax, cl
    not     eax

    mov     ecx, edi
    cmp     ecx, 8
    jge     .high

    mov     edx, dword [rel KernelStartup + KernelStartupInfo64.IRQMask_21_PM]
    and     edx, eax
    mov     dword [rel KernelStartup + KernelStartupInfo64.IRQMask_21_PM], edx
    mov     eax, edx
    out     PIC1_DATA, al
    ret

.high:
    mov     edx, dword [rel KernelStartup + KernelStartupInfo64.IRQMask_A1_PM]
    and     edx, eax
    mov     dword [rel KernelStartup + KernelStartupInfo64.IRQMask_A1_PM], edx
    mov     eax, edx
    out     PIC2_DATA, al
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
    mov     dword [rsp + 10], 0
    mov     word [rsp + 14], 0

    lgdt    [rsp]

    mov     ax, SELECTOR_KERNEL_CODE
    push    rax
    lea     rax, [rel .flush]
    push    rax
    retfq

.flush:
    mov     ax, SELECTOR_KERNEL_DATA
    mov     ss, ax
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax

    add     rsp, 16
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN ReadGlobalDescriptorTable
    sgdt    [rdi]
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN LoadLocalDescriptorTable
    cli
    mov     ax, si
    lldt    ax
    sti
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN LoadInterruptDescriptorTable
    pushfq
    cli

    sub     rsp, 16
    mov     word [rsp], si
    mov     qword [rsp + 2], rdi
    mov     dword [rsp + 10], 0
    mov     word [rsp + 14], 0

    lidt    [rsp]
    add     rsp, 16

    popfq
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN LoadPageDirectory
    mov     rax, rdi
    mov     rdx, cr3
    cmp     rax, rdx
    je      .done
    mov     cr3, rax
.done:
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
    xor     eax, eax
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
    invlpg  [rdi]
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN FlushTLB
    mov     rax, cr3
    mov     cr3, rax
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN SwitchToTask
    xor     eax, eax
    mov     dr6, rax
    mov     dr7, rax

    sub     rsp, 16
    mov     qword [rsp], 0
    mov     ax, di
    mov     word [rsp + 8], ax
    mov     dword [rsp + 10], 0
    mov     word [rsp + 14], 0
    jmp     far [rsp]
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN SetTaskState
    mov     rax, cr0
    or      rax, CR0_TASKSWITCH
    mov     cr0, rax
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN ClearTaskState
    clts
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN PeekConsoleWord
    lea     rax, [rdi + CONSOLE_TEXT_BUFFER]
    movzx   eax, word [rax]
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN PokeConsoleWord
    lea     rax, [rdi + CONSOLE_TEXT_BUFFER]
    mov     word [rax], si
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN SaveRegisters
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.RAX], rax
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.RBX], rbx
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.RCX], rcx
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.RDX], rdx
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.RSI], rsi
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.RDI], rdi
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.RBP], rbp
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.R8], r8
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.R9], r9
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.R10], r10
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.R11], r11
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.R12], r12
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.R13], r13
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.R14], r14
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.R15], r15

    lea     rax, [rsp + 8]
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.RSP], rax
    mov     rax, [rsp]
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.RIP], rax

    pushfq
    cli
    mov     rax, [rsp]
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.RFlags], rax

    mov     ax, cs
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.CS], ax
    mov     ax, ds
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.DS], ax
    mov     ax, ss
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.SS], ax
    mov     ax, es
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.ES], ax
    mov     ax, fs
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.FS], ax
    mov     ax, gs
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.GS], ax

    mov     rax, cr0
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.CR0], rax
    mov     rax, cr2
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.CR2], rax
    mov     rax, cr3
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.CR3], rax
    mov     rax, cr4
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.CR4], rax
    mov     rax, cr8
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.CR8], rax

    mov     rax, dr0
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.DR0], rax
    mov     rax, dr1
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.DR1], rax
    mov     rax, dr2
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.DR2], rax
    mov     rax, dr3
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.DR3], rax
    mov     rax, dr6
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.DR6], rax
    mov     rax, dr7
    mov     [rdi + INTEL_64_GENERAL_REGISTERS.DR7], rax

    popfq
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN MemorySet
    mov     r8, rdi
    mov     eax, esi
    mov     rcx, rdx
    cld
    rep     stosb
    mov     rax, r8
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN MemoryCopy
    mov     r8, rdi
    mov     rcx, rdx
    cld
    rep     movsb
    mov     rax, r8
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN MemoryCompare
    mov     rcx, rdx
    cld
    repe    cmpsb
    je      .equal
    ja      .greater
    mov     eax, -1
    ret

.greater:
    mov     eax, 1
    ret

.equal:
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN MemoryMove
    mov     r8, rdi
    mov     rcx, rdx
    test    rcx, rcx
    jz      .done

    cmp     rdi, rsi
    je      .done
    lea     r9, [rsi + rcx]
    cmp     rdi, r9
    jae     .forward

    lea     rsi, [rsi + rcx]
    lea     rdi, [rdi + rcx]
    dec     rsi
    dec     rdi
    std
    rep     movsb
    cld
    jmp     .end

.forward:
    cld
    rep     movsb

.end:
.done:
    mov     rax, r8
SYS_FUNC_END
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

section .bss
align 16

    global Stack
Stack:
    resq 1

;----------------------------------------------------------------------------
