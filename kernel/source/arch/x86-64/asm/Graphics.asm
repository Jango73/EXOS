;-------------------------------------------------------------------------
;
;   EXOS Kernel
;   Copyright (c) 1999-2026 Jango73
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
;   Graphics functions (x86-64)
;
;-------------------------------------------------------------------------

%include "x86-64.inc"
%include "System.inc"

section .text
BITS 64

%macro SYS_FUNC_BEGIN 1
    FUNC_HEADER
    global %1
%1:
%endmacro

%macro SYS_FUNC_END 0
    ret
%endmacro

SYS_FUNC_BEGIN BlitMemoryAsm
    test    rdi, rdi
    jz      .fail
    test    rsi, rsi
    jz      .fail
    test    rdx, rdx
    jz      .success
    cmp     rdi, rsi
    je      .success
    cmp     rdx, 16
    jb      .tail

    sub     rsp, 16
    movdqu  [rsp], xmm0
    mov     rcx, rdx
    shr     rcx, 4
.loop:
    movdqu  xmm0, [rsi]
    movdqu  [rdi], xmm0
    add     rsi, 16
    add     rdi, 16
    dec     rcx
    jnz     .loop
    movdqu  xmm0, [rsp]
    add     rsp, 16
.tail:
    mov     rcx, rdx
    and     rcx, 15
    cld
    rep     movsb
.success:
    mov     eax, 1
    ret
.fail:
    xor     eax, eax
    ret

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN DrawScanlineAsm
    test    rdi, rdi
    jz      .fail
    test    rsi, rsi
    jz      .fail
    cmp     r8d, r9d
    jne     DrawHorizontalGradientScanlineAsm

    cmp     edx, 32
    je      .draw32
    cmp     edx, 24
    je      .draw24
    cmp     edx, 16
    je      .draw16
    jmp     .fail

.draw32:
    mov     eax, r8d
    cmp     ecx, 0x0001
    jne     .loop32
    cmp     rsi, 4
    jb      .loop32
    sub     rsp, 16
    movdqu  [rsp], xmm0
    movd    xmm0, eax
    pshufd  xmm0, xmm0, 0
    mov     r10, rsi
    shr     r10, 2
.sse32_loop:
    movdqu  [rdi], xmm0
    add     rdi, 16
    dec     r10
    jnz     .sse32_loop
    and     rsi, 3
    movdqu  xmm0, [rsp]
    add     rsp, 16
    test    rsi, rsi
    jz      .draw32_done
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
.draw32_done:
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
    cmp     ecx, 0x0001
    jne     .loop16
    cmp     rsi, 8
    jb      .loop16
    sub     rsp, 16
    movdqu  [rsp], xmm0
    movd    xmm0, eax
    pshuflw xmm0, xmm0, 0
    punpcklqdq xmm0, xmm0
    mov     r10, rsi
    shr     r10, 3
.sse16_loop:
    movdqu  [rdi], xmm0
    add     rdi, 16
    dec     r10
    jnz     .sse16_loop
    and     rsi, 7
    movdqu  xmm0, [rsp]
    add     rsp, 16
    test    rsi, rsi
    jz      .draw16_done
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
.draw16_done:
    mov     eax, 1
    ret

.fail:
    xor     eax, eax
    ret

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN DrawHorizontalGradientScanlineAsm
    test    rdi, rdi
    jz      .gradient_fail
    test    rsi, rsi
    jz      .gradient_fail
    cmp     r8d, r9d
    je      DrawScanlineAsm
    cmp     rsi, 1
    jne     .gradient_setup
    mov     r9d, r8d
    jmp     DrawScanlineAsm

.gradient_setup:
    push    rbp
    mov     rbp, rsp
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 64

    mov     r12d, esi
    dec     r12d
    mov     dword [rbp - 4], r12d
    mov     dword [rbp - 40], edx
    mov     dword [rbp - 44], ecx

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
    cdq
    idiv    dword [rbp - 4]
    add     eax, dword [rbp - 8]
    shl     eax, 24
    mov     ebx, eax

    mov     eax, dword [rbp - 28]
    imul    r13d
    cdq
    idiv    dword [rbp - 4]
    add     eax, dword [rbp - 12]
    shl     eax, 16
    or      ebx, eax

    mov     eax, dword [rbp - 32]
    imul    r13d
    cdq
    idiv    dword [rbp - 4]
    add     eax, dword [rbp - 16]
    shl     eax, 8
    or      ebx, eax

    mov     eax, dword [rbp - 36]
    imul    r13d
    cdq
    idiv    dword [rbp - 4]
    add     eax, dword [rbp - 20]
    or      ebx, eax

    cmp     dword [rbp - 40], 32
    je      .gradient_32
    cmp     dword [rbp - 40], 24
    je      .gradient_24
    cmp     dword [rbp - 40], 16
    je      .gradient_16
    jmp     .gradient_cleanup_fail

.gradient_32:
    cmp     dword [rbp - 44], 0x0001
    je      .gradient_set32
    cmp     dword [rbp - 44], 0x0004
    je      .gradient_xor32
    cmp     dword [rbp - 44], 0x0003
    je      .gradient_or32
    cmp     dword [rbp - 44], 0x0002
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
    cmp     dword [rbp - 44], 0x0001
    je      .gradient_set24
    cmp     dword [rbp - 44], 0x0004
    je      .gradient_xor24
    cmp     dword [rbp - 44], 0x0003
    je      .gradient_or24
    cmp     dword [rbp - 44], 0x0002
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
    cmp     dword [rbp - 44], 0x0001
    je      .gradient_set16
    cmp     dword [rbp - 44], 0x0004
    je      .gradient_xor16
    cmp     dword [rbp - 44], 0x0003
    je      .gradient_or16
    cmp     dword [rbp - 44], 0x0002
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
    add     rsp, 48
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

SYS_FUNC_BEGIN FillVerticalGradientRectAsm
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
    cdq
    idiv    dword [rbp - 20]
    add     eax, dword [rbp - 24]
    shl     eax, 24
    mov     r10d, eax

    mov     eax, dword [rbp - 44]
    imul    ebx
    cdq
    idiv    dword [rbp - 20]
    add     eax, dword [rbp - 28]
    shl     eax, 16
    or      r10d, eax

    mov     eax, dword [rbp - 48]
    imul    ebx
    cdq
    idiv    dword [rbp - 20]
    add     eax, dword [rbp - 32]
    shl     eax, 8
    or      r10d, eax

    mov     eax, dword [rbp - 52]
    imul    ebx
    cdq
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
    call    DrawScanlineAsm
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
