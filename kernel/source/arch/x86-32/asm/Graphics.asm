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
;   Graphics functions (x86-32)
;
;-------------------------------------------------------------------------

%include "x86-32.inc"
%include "System.inc"

section .text
BITS 32

    global BlitMemoryAsm
    global DrawScanlineAsm
    global DrawHorizontalGradientScanlineAsm
    global FillVerticalGradientRectAsm

BlitMemoryAsm :

    push    ebp
    mov     ebp, esp
    push    esi
    push    edi

    mov     edi, [ebp + PBN]
    mov     esi, [ebp + PBN + 4]
    mov     ecx, [ebp + PBN + 8]

    test    edi, edi
    jz      .fail
    test    esi, esi
    jz      .fail
    test    ecx, ecx
    jz      .success
    cmp     edi, esi
    je      .success
    cmp     ecx, 16
    jb      .tail

    sub     esp, 16
    movdqu  [esp], xmm0
    mov     edx, ecx
    shr     edx, 4
.loop:
    movdqu  xmm0, [esi]
    movdqu  [edi], xmm0
    add     esi, 16
    add     edi, 16
    dec     edx
    jnz     .loop
    movdqu  xmm0, [esp]
    add     esp, 16
.tail:
    and     ecx, 15
    cld
    rep     movsb
.success:
    mov     eax, 1
    jmp     .done2
.fail:
    xor     eax, eax
.done2:
    pop     edi
    pop     esi
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
DrawScanlineAsm :

    push    ebp
    mov     ebp, esp
    push    esi
    push    edi

    mov     edi, [ebp + PBN]
    mov     esi, [ebp + PBN + 4]
    mov     edx, [ebp + PBN + 8]
    mov     ecx, [ebp + PBN + 12]
    mov     eax, [ebp + PBN + 16]

    test    edi, edi
    jz      .fail
    test    esi, esi
    jz      .fail
    cmp     eax, [ebp + PBN + 20]
    jne     .gradient_dispatch

    cmp     edx, 32
    je      .draw32
    cmp     edx, 24
    je      .draw24
    cmp     edx, 16
    je      .draw16
    jmp     .fail

.draw32:
    cmp     ecx, 0x0001
    jne     .loop32
    cmp     esi, 4
    jb      .loop32
    sub     esp, 16
    movdqu  [esp], xmm0
    movd    xmm0, eax
    pshufd  xmm0, xmm0, 0
    mov     edx, esi
    shr     edx, 2
.sse32_loop:
    movdqu  [edi], xmm0
    add     edi, 16
    dec     edx
    jnz     .sse32_loop
    and     esi, 3
    movdqu  xmm0, [esp]
    add     esp, 16
    test    esi, esi
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
    mov     dword [edi], eax
    jmp     .next32
.xor32:
    xor     dword [edi], eax
    jmp     .next32
.or32:
    or      dword [edi], eax
    jmp     .next32
.and32:
    and     dword [edi], eax
.next32:
    add     edi, 4
    dec     esi
    jnz     .loop32
    mov     eax, 1
    jmp     .done
.draw32_done:
    mov     eax, 1
    jmp     .done

.draw24:
    push    ebx
    mov     ebx, eax
    shr     ebx, 16
    mov     edx, eax
    shr     edx, 8
.loop24:
    cmp     ecx, 0x0001
    je      .set24
    cmp     ecx, 0x0004
    je      .xor24
    cmp     ecx, 0x0003
    je      .or24
    cmp     ecx, 0x0002
    je      .and24
    pop     ebx
    jmp     .fail
.set24:
    mov     [edi], bl
    mov     [edi + 1], dl
    mov     [edi + 2], al
    jmp     .next24
.xor24:
    xor     byte [edi], bl
    xor     byte [edi + 1], dl
    xor     byte [edi + 2], al
    jmp     .next24
.or24:
    or      byte [edi], bl
    or      byte [edi + 1], dl
    or      byte [edi + 2], al
    jmp     .next24
.and24:
    and     byte [edi], bl
    and     byte [edi + 1], dl
    and     byte [edi + 2], al
.next24:
    add     edi, 3
    dec     esi
    jnz     .loop24
    pop     ebx
    mov     eax, 1
    jmp     .done

.draw16:
    cmp     ecx, 0x0001
    jne     .loop16
    cmp     esi, 8
    jb      .loop16
    sub     esp, 16
    movdqu  [esp], xmm0
    movd    xmm0, eax
    pshuflw xmm0, xmm0, 0
    punpcklqdq xmm0, xmm0
    mov     edx, esi
    shr     edx, 3
.sse16_loop:
    movdqu  [edi], xmm0
    add     edi, 16
    dec     edx
    jnz     .sse16_loop
    and     esi, 7
    movdqu  xmm0, [esp]
    add     esp, 16
    test    esi, esi
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
    mov     word [edi], ax
    jmp     .next16
.xor16:
    xor     word [edi], ax
    jmp     .next16
.or16:
    or      word [edi], ax
    jmp     .next16
.and16:
    and     word [edi], ax
.next16:
    add     edi, 2
    dec     esi
    jnz     .loop16
    mov     eax, 1
    jmp     .done
.draw16_done:
    mov     eax, 1
    jmp     .done

.gradient_dispatch:
    push    dword [ebp + PBN + 20]
    push    dword [ebp + PBN + 16]
    push    dword [ebp + PBN + 12]
    push    dword [ebp + PBN + 8]
    push    dword [ebp + PBN + 4]
    push    dword [ebp + PBN]
    call    DrawHorizontalGradientScanlineAsm
    add     esp, 24
    jmp     .done

.fail:
    xor     eax, eax

.done:
    pop     edi
    pop     esi
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
DrawHorizontalGradientScanlineAsm :

    push    ebp
    mov     ebp, esp
    push    ebx
    push    esi
    push    edi
    sub     esp, 44

    mov     edi, [ebp + PBN]
    mov     esi, [ebp + PBN + 4]
    test    edi, edi
    jz      .gradient_fail
    test    esi, esi
    jz      .gradient_fail

    mov     eax, [ebp + PBN + 16]
    cmp     eax, [ebp + PBN + 20]
    jne     .gradient_prepare

    push    eax
    push    eax
    push    dword [ebp + PBN + 12]
    push    dword [ebp + PBN + 8]
    push    dword [ebp + PBN + 4]
    push    dword [ebp + PBN]
    call    DrawScanlineAsm
    add     esp, 24
    jmp     .gradient_done

.gradient_prepare:
    cmp     esi, 1
    jne     .gradient_setup

    mov     eax, [ebp + PBN + 16]
    push    eax
    push    eax
    push    dword [ebp + PBN + 12]
    push    dword [ebp + PBN + 8]
    push    dword [ebp + PBN + 4]
    push    dword [ebp + PBN]
    call    DrawScanlineAsm
    add     esp, 24
    jmp     .gradient_done

.gradient_setup:
    mov     eax, esi
    dec     eax
    mov     [ebp - 4], eax
    mov     eax, [ebp + PBN + 8]
    mov     [ebp - 40], eax
    mov     eax, [ebp + PBN + 12]
    mov     [ebp - 44], eax

    mov     eax, [ebp + PBN + 16]
    shr     eax, 24
    mov     [ebp - 8], eax
    mov     eax, [ebp + PBN + 16]
    shr     eax, 16
    and     eax, 0xFF
    mov     [ebp - 12], eax
    mov     eax, [ebp + PBN + 16]
    shr     eax, 8
    and     eax, 0xFF
    mov     [ebp - 16], eax
    mov     eax, [ebp + PBN + 16]
    and     eax, 0xFF
    mov     [ebp - 20], eax

    mov     eax, [ebp + PBN + 20]
    shr     eax, 24
    sub     eax, [ebp - 8]
    mov     [ebp - 24], eax
    mov     eax, [ebp + PBN + 20]
    shr     eax, 16
    and     eax, 0xFF
    sub     eax, [ebp - 12]
    mov     [ebp - 28], eax
    mov     eax, [ebp + PBN + 20]
    shr     eax, 8
    and     eax, 0xFF
    sub     eax, [ebp - 16]
    mov     [ebp - 32], eax
    mov     eax, [ebp + PBN + 20]
    and     eax, 0xFF
    sub     eax, [ebp - 20]
    mov     [ebp - 36], eax

    xor     ebx, ebx

.gradient_loop:
    mov     eax, [ebp - 24]
    imul    ebx
    cdq
    idiv    dword [ebp - 4]
    add     eax, [ebp - 8]
    shl     eax, 24
    mov     ecx, eax

    mov     eax, [ebp - 28]
    imul    ebx
    cdq
    idiv    dword [ebp - 4]
    add     eax, [ebp - 12]
    shl     eax, 16
    or      ecx, eax

    mov     eax, [ebp - 32]
    imul    ebx
    cdq
    idiv    dword [ebp - 4]
    add     eax, [ebp - 16]
    shl     eax, 8
    or      ecx, eax

    mov     eax, [ebp - 36]
    imul    ebx
    cdq
    idiv    dword [ebp - 4]
    add     eax, [ebp - 20]
    or      ecx, eax

    mov     eax, [ebp - 40]
    cmp     eax, 32
    je      .gradient_32
    cmp     eax, 24
    je      .gradient_24
    cmp     eax, 16
    je      .gradient_16
    jmp     .gradient_fail

.gradient_32:
    mov     eax, [ebp - 44]
    cmp     eax, 0x0001
    je      .gradient_set32
    cmp     eax, 0x0004
    je      .gradient_xor32
    cmp     eax, 0x0003
    je      .gradient_or32
    cmp     eax, 0x0002
    je      .gradient_and32
    jmp     .gradient_fail
.gradient_set32:
    mov     [edi], ecx
    jmp     .gradient_next32
.gradient_xor32:
    xor     dword [edi], ecx
    jmp     .gradient_next32
.gradient_or32:
    or      dword [edi], ecx
    jmp     .gradient_next32
.gradient_and32:
    and     dword [edi], ecx
.gradient_next32:
    add     edi, 4
    jmp     .gradient_next

.gradient_24:
    push    ecx
    mov     eax, [esp]
    shr     eax, 16
    mov     edx, [esp]
    shr     edx, 8
    mov     ecx, [esp]
    cmp     dword [ebp - 44], 0x0001
    je      .gradient_set24
    cmp     dword [ebp - 44], 0x0004
    je      .gradient_xor24
    cmp     dword [ebp - 44], 0x0003
    je      .gradient_or24
    cmp     dword [ebp - 44], 0x0002
    je      .gradient_and24
    pop     ecx
    jmp     .gradient_fail
.gradient_set24:
    mov     [edi], al
    mov     [edi + 1], dl
    mov     [edi + 2], cl
    jmp     .gradient_next24
.gradient_xor24:
    xor     byte [edi], al
    xor     byte [edi + 1], dl
    xor     byte [edi + 2], cl
    jmp     .gradient_next24
.gradient_or24:
    or      byte [edi], al
    or      byte [edi + 1], dl
    or      byte [edi + 2], cl
    jmp     .gradient_next24
.gradient_and24:
    and     byte [edi], al
    and     byte [edi + 1], dl
    and     byte [edi + 2], cl
.gradient_next24:
    pop     ecx
    add     edi, 3
    jmp     .gradient_next

.gradient_16:
    mov     eax, ecx
    mov     ecx, [ebp - 44]
    cmp     ecx, 0x0001
    je      .gradient_set16
    cmp     ecx, 0x0004
    je      .gradient_xor16
    cmp     ecx, 0x0003
    je      .gradient_or16
    cmp     ecx, 0x0002
    je      .gradient_and16
    jmp     .gradient_fail
.gradient_set16:
    mov     word [edi], ax
    jmp     .gradient_next16
.gradient_xor16:
    xor     word [edi], ax
    jmp     .gradient_next16
.gradient_or16:
    or      word [edi], ax
    jmp     .gradient_next16
.gradient_and16:
    and     word [edi], ax
.gradient_next16:
    add     edi, 2

.gradient_next:
    inc     ebx
    dec     esi
    jnz     .gradient_loop
    mov     eax, 1
    jmp     .gradient_done

.gradient_fail:
    xor     eax, eax

.gradient_done:
    add     esp, 44
    pop     edi
    pop     esi
    pop     ebx
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
FillVerticalGradientRectAsm :

    push    ebp
    mov     ebp, esp
    push    ebx
    push    esi
    push    edi
    sub     esp, 60

    mov     edi, [ebp + PBN]
    mov     eax, [ebp + PBN + 4]
    mov     esi, [ebp + PBN + 8]
    test    edi, edi
    jz      .vertical_fail
    test    eax, eax
    jz      .vertical_fail
    test    esi, esi
    jz      .vertical_fail

    mov     [ebp - 56], eax
    mov     eax, [ebp + PBN + 16]
    mov     [ebp - 4], eax
    mov     eax, [ebp + PBN + 20]
    mov     [ebp - 8], eax
    mov     eax, [ebp + PBN + 24]
    mov     [ebp - 12], eax
    mov     eax, [ebp + PBN + 28]
    mov     [ebp - 16], eax
    mov     eax, [ebp + PBN + 12]
    mov     [ebp - 60], eax

    mov     eax, esi
    dec     eax
    mov     [ebp - 20], eax

    mov     eax, [ebp - 12]
    shr     eax, 24
    mov     [ebp - 24], eax
    mov     eax, [ebp - 12]
    shr     eax, 16
    and     eax, 0xFF
    mov     [ebp - 28], eax
    mov     eax, [ebp - 12]
    shr     eax, 8
    and     eax, 0xFF
    mov     [ebp - 32], eax
    mov     eax, [ebp - 12]
    and     eax, 0xFF
    mov     [ebp - 36], eax

    mov     eax, [ebp - 16]
    shr     eax, 24
    sub     eax, [ebp - 24]
    mov     [ebp - 40], eax
    mov     eax, [ebp - 16]
    shr     eax, 16
    and     eax, 0xFF
    sub     eax, [ebp - 28]
    mov     [ebp - 44], eax
    mov     eax, [ebp - 16]
    shr     eax, 8
    and     eax, 0xFF
    sub     eax, [ebp - 32]
    mov     [ebp - 48], eax
    mov     eax, [ebp - 16]
    and     eax, 0xFF
    sub     eax, [ebp - 36]
    mov     [ebp - 52], eax

    xor     ebx, ebx

.vertical_loop:
    cmp     dword [ebp - 20], 0
    jg      .vertical_interpolate
    mov     ecx, [ebp - 12]
    jmp     .vertical_draw

.vertical_interpolate:
    mov     eax, [ebp - 40]
    imul    ebx
    cdq
    idiv    dword [ebp - 20]
    add     eax, [ebp - 24]
    shl     eax, 24
    mov     ecx, eax

    mov     eax, [ebp - 44]
    imul    ebx
    cdq
    idiv    dword [ebp - 20]
    add     eax, [ebp - 28]
    shl     eax, 16
    or      ecx, eax

    mov     eax, [ebp - 48]
    imul    ebx
    cdq
    idiv    dword [ebp - 20]
    add     eax, [ebp - 32]
    shl     eax, 8
    or      ecx, eax

    mov     eax, [ebp - 52]
    imul    ebx
    cdq
    idiv    dword [ebp - 20]
    add     eax, [ebp - 36]
    or      ecx, eax

.vertical_draw:
    push    ecx
    push    ecx
    push    dword [ebp - 8]
    push    dword [ebp - 60]
    push    dword [ebp - 56]
    push    edi
    call    DrawScanlineAsm
    add     esp, 24
    test    eax, eax
    jz      .vertical_fail

    add     edi, [ebp - 4]
    inc     ebx
    dec     esi
    jnz     .vertical_loop

    mov     eax, 1
    jmp     .vertical_done

.vertical_fail:
    xor     eax, eax

.vertical_done:
    add     esp, 60
    pop     edi
    pop     esi
    pop     ebx
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
