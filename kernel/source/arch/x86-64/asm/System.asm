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

extern SystemCallHandler
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

SYS_FUNC_BEGIN MemoryCopy
    test    rdx, rdx
    jz      .done
    cmp     rdi, rsi
    je      .done

    mov     rcx, rdx
    shr     rcx, 3
    cld
    rep     movsq

    mov     rcx, rdx
    and     rcx, 7
    rep     movsb
.done:
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN MemoryMove
    test    rdx, rdx
    jz      .done
    cmp     rdi, rsi
    je      .done

    lea     rax, [rsi + rdx]
    cmp     rdi, rsi
    jb      .forward
    cmp     rdi, rax
    jae     .forward

    std
    lea     rsi, [rsi + rdx - 1]
    lea     rdi, [rdi + rdx - 1]
    mov     rcx, rdx
    rep     movsb
    cld
    jmp     .done
.forward:
    cld
    mov     rcx, rdx
    rep     movsb
.done:
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN GraphicsDrawScanlineAsm
    test    rdi, rdi
    jz      .fail
    test    rsi, rsi
    jz      .fail
    cmp     r8d, r9d
    jne     GraphicsDrawHorizontalGradientScanlineAsm

    cmp     edx, 32
    je      .draw32
    cmp     edx, 24
    je      .draw24
    cmp     edx, 16
    je      .draw16
    jmp     .fail

.draw32:
    mov     eax, r8d
.loop32:
    cmp     ecx, 0x0001
    je      .set32
    cmp     ecx, 0x0004
    je      .xor32
    cmp     ecx, 0x0003
    je      .or32
    cmp     ecx, 0x0002
    je      .and32
    jmp     .fail
.set32:
    mov     dword [rdi], eax
    jmp     .next32
.xor32:
    xor     dword [rdi], eax
    jmp     .next32
.or32:
    or      dword [rdi], eax
    jmp     .next32
.and32:
    and     dword [rdi], eax
.next32:
    add     rdi, 4
    dec     rsi
    jnz     .loop32
    mov     eax, 1
    ret

.draw24:
    mov     eax, r8d
    mov     r10d, eax
    shr     r10d, 16
    mov     r11d, eax
    shr     r11d, 8
.loop24:
    cmp     ecx, 0x0001
    je      .set24
    cmp     ecx, 0x0004
    je      .xor24
    cmp     ecx, 0x0003
    je      .or24
    cmp     ecx, 0x0002
    je      .and24
    jmp     .fail
.set24:
    mov     byte [rdi], r10b
    mov     byte [rdi + 1], r11b
    mov     byte [rdi + 2], al
    jmp     .next24
.xor24:
    xor     byte [rdi], r10b
    xor     byte [rdi + 1], r11b
    xor     byte [rdi + 2], al
    jmp     .next24
.or24:
    or      byte [rdi], r10b
    or      byte [rdi + 1], r11b
    or      byte [rdi + 2], al
    jmp     .next24
.and24:
    and     byte [rdi], r10b
    and     byte [rdi + 1], r11b
    and     byte [rdi + 2], al
.next24:
    add     rdi, 3
    dec     rsi
    jnz     .loop24
    mov     eax, 1
    ret

.draw16:
    mov     eax, r8d
.loop16:
    cmp     ecx, 0x0001
    je      .set16
    cmp     ecx, 0x0004
    je      .xor16
    cmp     ecx, 0x0003
    je      .or16
    cmp     ecx, 0x0002
    je      .and16
    jmp     .fail
.set16:
    mov     word [rdi], ax
    jmp     .next16
.xor16:
    xor     word [rdi], ax
    jmp     .next16
.or16:
    or      word [rdi], ax
    jmp     .next16
.and16:
    and     word [rdi], ax
.next16:
    add     rdi, 2
    dec     rsi
    jnz     .loop16
    mov     eax, 1
    ret

.fail:
    xor     eax, eax
    ret

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN GraphicsDrawHorizontalGradientScanlineAsm
    test    rdi, rdi
    jz      .gradient_fail
    test    rsi, rsi
    jz      .gradient_fail
    cmp     r8d, r9d
    je      GraphicsDrawScanlineAsm
    cmp     rsi, 1
    jne     .gradient_setup
    mov     r9d, r8d
    jmp     GraphicsDrawScanlineAsm

.gradient_setup:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 56

    mov     r12d, esi
    dec     r12d
    mov     dword [rbp - 4], r12d

    mov     eax, r8d
    shr     eax, 24
    mov     dword [rbp - 8], eax
    mov     eax, r8d
    shr     eax, 16
    and     eax, 0xFF
    mov     dword [rbp - 12], eax
    mov     eax, r8d
    shr     eax, 8
    and     eax, 0xFF
    mov     dword [rbp - 16], eax
    mov     eax, r8d
    and     eax, 0xFF
    mov     dword [rbp - 20], eax

    mov     eax, r9d
    shr     eax, 24
    sub     eax, dword [rbp - 8]
    mov     dword [rbp - 24], eax
    mov     eax, r9d
    shr     eax, 16
    and     eax, 0xFF
    sub     eax, dword [rbp - 12]
    mov     dword [rbp - 28], eax
    mov     eax, r9d
    shr     eax, 8
    and     eax, 0xFF
    sub     eax, dword [rbp - 16]
    mov     dword [rbp - 32], eax
    mov     eax, r9d
    and     eax, 0xFF
    sub     eax, dword [rbp - 20]
    mov     dword [rbp - 36], eax

    xor     r13d, r13d
    mov     r14, rdi
    mov     r15, rsi

.gradient_loop:
    mov     eax, dword [rbp - 24]
    imul    r13d
    idiv    dword [rbp - 4]
    add     eax, dword [rbp - 8]
    shl     eax, 24
    mov     ebx, eax

    mov     eax, dword [rbp - 28]
    imul    r13d
    idiv    dword [rbp - 4]
    add     eax, dword [rbp - 12]
    shl     eax, 16
    or      ebx, eax

    mov     eax, dword [rbp - 32]
    imul    r13d
    idiv    dword [rbp - 4]
    add     eax, dword [rbp - 16]
    shl     eax, 8
    or      ebx, eax

    mov     eax, dword [rbp - 36]
    imul    r13d
    idiv    dword [rbp - 4]
    add     eax, dword [rbp - 20]
    or      ebx, eax

    cmp     edx, 32
    je      .gradient_32
    cmp     edx, 24
    je      .gradient_24
    cmp     edx, 16
    je      .gradient_16
    jmp     .gradient_cleanup_fail

.gradient_32:
    cmp     ecx, 0x0001
    je      .gradient_set32
    cmp     ecx, 0x0004
    je      .gradient_xor32
    cmp     ecx, 0x0003
    je      .gradient_or32
    cmp     ecx, 0x0002
    je      .gradient_and32
    jmp     .gradient_cleanup_fail
.gradient_set32:
    mov     dword [r14], ebx
    jmp     .gradient_next32
.gradient_xor32:
    xor     dword [r14], ebx
    jmp     .gradient_next32
.gradient_or32:
    or      dword [r14], ebx
    jmp     .gradient_next32
.gradient_and32:
    and     dword [r14], ebx
.gradient_next32:
    add     r14, 4
    jmp     .gradient_next_pixel

.gradient_24:
    mov     eax, ebx
    shr     eax, 16
    mov     r10d, ebx
    shr     r10d, 8
    cmp     ecx, 0x0001
    je      .gradient_set24
    cmp     ecx, 0x0004
    je      .gradient_xor24
    cmp     ecx, 0x0003
    je      .gradient_or24
    cmp     ecx, 0x0002
    je      .gradient_and24
    jmp     .gradient_cleanup_fail
.gradient_set24:
    mov     byte [r14], al
    mov     byte [r14 + 1], r10b
    mov     byte [r14 + 2], bl
    jmp     .gradient_next24
.gradient_xor24:
    xor     byte [r14], al
    xor     byte [r14 + 1], r10b
    xor     byte [r14 + 2], bl
    jmp     .gradient_next24
.gradient_or24:
    or      byte [r14], al
    or      byte [r14 + 1], r10b
    or      byte [r14 + 2], bl
    jmp     .gradient_next24
.gradient_and24:
    and     byte [r14], al
    and     byte [r14 + 1], r10b
    and     byte [r14 + 2], bl
.gradient_next24:
    add     r14, 3
    jmp     .gradient_next_pixel

.gradient_16:
    mov     eax, ebx
    cmp     ecx, 0x0001
    je      .gradient_set16
    cmp     ecx, 0x0004
    je      .gradient_xor16
    cmp     ecx, 0x0003
    je      .gradient_or16
    cmp     ecx, 0x0002
    je      .gradient_and16
    jmp     .gradient_cleanup_fail
.gradient_set16:
    mov     word [r14], ax
    jmp     .gradient_next16
.gradient_xor16:
    xor     word [r14], ax
    jmp     .gradient_next16
.gradient_or16:
    or      word [r14], ax
    jmp     .gradient_next16
.gradient_and16:
    and     word [r14], ax
.gradient_next16:
    add     r14, 2

.gradient_next_pixel:
    inc     r13d
    dec     r15
    jnz     .gradient_loop
    mov     eax, 1
    jmp     .gradient_cleanup_done

.gradient_cleanup_fail:
    xor     eax, eax

.gradient_cleanup_done:
    add     rsp, 40
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    leave
    ret

.gradient_fail:
    xor     eax, eax
    ret

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN GraphicsFillVerticalGradientRectAsm
    test    rdi, rdi
    jz      .vertical_fail
    test    rsi, rsi
    jz      .vertical_fail
    test    rdx, rdx
    jz      .vertical_fail

    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 40

    mov     r14, rdi
    mov     r15, rsi
    mov     r13, rdx
    mov     r12d, ecx
    mov     dword [rbp - 4], r8d
    mov     dword [rbp - 8], r9d
    mov     eax, dword [rbp + 16]
    mov     dword [rbp - 12], eax
    mov     eax, dword [rbp + 24]
    mov     dword [rbp - 16], eax

    mov     eax, r13d
    dec     eax
    mov     dword [rbp - 20], eax

    mov     eax, dword [rbp - 12]
    shr     eax, 24
    mov     dword [rbp - 24], eax
    mov     eax, dword [rbp - 12]
    shr     eax, 16
    and     eax, 0xFF
    mov     dword [rbp - 28], eax
    mov     eax, dword [rbp - 12]
    shr     eax, 8
    and     eax, 0xFF
    mov     dword [rbp - 32], eax
    mov     eax, dword [rbp - 12]
    and     eax, 0xFF
    mov     dword [rbp - 36], eax

    mov     eax, dword [rbp - 16]
    shr     eax, 24
    sub     eax, dword [rbp - 24]
    mov     dword [rbp - 40], eax
    mov     eax, dword [rbp - 16]
    shr     eax, 16
    and     eax, 0xFF
    sub     eax, dword [rbp - 28]
    mov     dword [rbp - 44], eax
    mov     eax, dword [rbp - 16]
    shr     eax, 8
    and     eax, 0xFF
    sub     eax, dword [rbp - 32]
    mov     dword [rbp - 48], eax
    mov     eax, dword [rbp - 16]
    and     eax, 0xFF
    sub     eax, dword [rbp - 36]
    mov     dword [rbp - 52], eax

    xor     ebx, ebx

.vertical_loop:
    cmp     dword [rbp - 20], 0
    jg      .vertical_interpolate
    mov     r10d, dword [rbp - 12]
    jmp     .vertical_draw

.vertical_interpolate:
    mov     eax, dword [rbp - 40]
    imul    ebx
    idiv    dword [rbp - 20]
    add     eax, dword [rbp - 24]
    shl     eax, 24
    mov     r10d, eax

    mov     eax, dword [rbp - 44]
    imul    ebx
    idiv    dword [rbp - 20]
    add     eax, dword [rbp - 28]
    shl     eax, 16
    or      r10d, eax

    mov     eax, dword [rbp - 48]
    imul    ebx
    idiv    dword [rbp - 20]
    add     eax, dword [rbp - 32]
    shl     eax, 8
    or      r10d, eax

    mov     eax, dword [rbp - 52]
    imul    ebx
    idiv    dword [rbp - 20]
    add     eax, dword [rbp - 36]
    or      r10d, eax

.vertical_draw:
    mov     rdi, r14
    mov     rsi, r15
    mov     edx, r12d
    mov     ecx, dword [rbp - 8]
    mov     r8d, r10d
    mov     r9d, r10d
    call    GraphicsDrawScanlineAsm
    test    eax, eax
    jz      .vertical_cleanup_fail

    mov     eax, dword [rbp - 4]
    add     r14, rax
    inc     ebx
    dec     r13
    jnz     .vertical_loop

    mov     eax, 1
    jmp     .vertical_cleanup_done

.vertical_cleanup_fail:
    xor     eax, eax

.vertical_cleanup_done:
    add     rsp, 56
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    leave
    ret

.vertical_fail:
    xor     eax, eax
    ret

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
    sub     rsp, 32
    mov     word [rsp], si
    mov     qword [rsp + 2], rdi
    lgdt    [rsp]

    lea     rax, [rel .flush]
    mov     qword [rsp + 16], rax
    mov     word [rsp + 24], SELECTOR_KERNEL_CODE
    jmp     far [rsp + 16]

.flush:
    add     rsp, 32
    mov     ax, SELECTOR_KERNEL_DATA
    mov     ss, ax
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN ReadGlobalDescriptorTable
    sgdt    [rdi]
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
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN FlushTLB
    mov     rax, cr3
    mov     cr3, rax
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

;-------------------------------------------------------------------------

SYS_FUNC_BEGIN DoSystemCall
    call    SystemCallHandler
SYS_FUNC_END

;-------------------------------------------------------------------------
; DON'T call this one outside of :
; Sleep, WaitForMessage and LockMutex
; It will trigger random crashes

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

;-------------------------------------------------------------------------

section .shared_text
BITS 64

SYS_FUNC_BEGIN TaskRunner

    STACK_ALIGN_16_ENTER

    ;--------------------------------------
    ; RDI contains the function pointer passed by the kernel
    ; RSI contains the task parameter

    test    rdi, rdi
    je      .exit

    ; Clear registers
    xor     rbp, rbp
    xor     rax, rax
    xor     rcx, rcx
    xor     rdx, rdx
    xor     r8, r8
    xor     r9, r9
    xor     r10, r10
    xor     r11, r11
    xor     r12, r12
    xor     r13, r13
    xor     r14, r14
    mov     r15, r15

    mov     rbx, rdi
    mov     rdi, rsi            ; SysV ABI: first C argument = task parameter
    call    rbx                 ; Call task function

.exit:
    mov     rbx, rax            ; Preserve task exit code in RBX
    mov     rdi, 0x33           ; SYSCALL_Exit
    mov     rsi, rbx            ; Pass exit code as the syscall parameter
%if USE_SYSCALL
    syscall
%else
    int     EXOS_USER_CALL
%endif

.sleep:
    mov     rdi, 0x0F           ; SYSCALL_Sleep
    mov     rsi, MAX_UINT       ; Sleep forever while we wait for the scheduler
%if USE_SYSCALL
    syscall
%else
    int     EXOS_USER_CALL
%endif
    jmp     .sleep

    STACK_ALIGN_16_LEAVE
SYS_FUNC_END

;----------------------------------------------------------------------------

section .bss
align 16

    global Stack
Stack:
    resq 1

;----------------------------------------------------------------------------

section .note.GNU-stack noalloc noexec nowrite align=1
