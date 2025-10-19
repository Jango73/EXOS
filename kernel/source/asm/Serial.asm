
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
;   Serial port output
;
;-------------------------------------------------------------------------

%include "i386.inc"

COMPort_Debug equ 0x2F8

section .text.serial64
bits 64

    global SerialWriteChar64
    global SerialWriteString64

;--------------------------------------
; Write a single character to COM (64-bit)
; DIL - character to send
SerialWriteChar64:
    push    rdx
    push    rax

    movzx   eax, dil
    mov     dl, al

.wait:
    mov     dx, COMPort_Debug + 5
    in      al, dx
    test    al, 0x20
    jz      .wait

    mov     dx, COMPort_Debug
    mov     al, dl
    out     dx, al

    pop     rax
    pop     rdx
    ret

;--------------------------------------
; Write a zero-terminated string to COM (64-bit)
; RDI - pointer to string
SerialWriteString64:
    push    rax
    push    rdx
    push    r8

.loop:
    mov     al, [rdi]
    test    al, al
    jz      .done

    mov     r8b, al

.wait_tx:
    mov     dx, COMPort_Debug + 5
    in      al, dx
    test    al, 0x20
    jz      .wait_tx

    mov     dx, COMPort_Debug
    mov     al, r8b
    out     dx, al

    inc     rdi
    jmp     .loop

.done:
    pop     r8
    pop     rdx
    pop     rax
    ret

;--------------------------------------

section .text.stub2
bits 32

    global SerialInit
    global SerialWriteChar
    global SerialWriteSpace
    global SerialWriteNewLine
    global SerialWriteString
    global SerialWriteHex32

;--------------------------------------
; Initialize COM1 for 38400 8N1
SerialInit:
    mov     dx, COMPort_Debug + 1
    mov     al, 0x00
    out     dx, al                ; Disable interrupts

    mov     dx, COMPort_Debug + 3
    mov     al, 0x80
    out     dx, al                ; Enable DLAB

    mov     dx, COMPort_Debug + 0
    mov     al, 0x03
    out     dx, al                ; Set baud rate divisor (low byte)

    mov     dx, COMPort_Debug + 1
    mov     al, 0x00
    out     dx, al                ; High byte divisor

    mov     dx, COMPort_Debug + 3
    mov     al, 0x03
    out     dx, al                ; 8 bits, no parity, 1 stop bit

    mov     dx, COMPort_Debug + 2
    mov     al, 0xC7
    out     dx, al                ; Enable FIFO

    mov     dx, COMPort_Debug + 4
    mov     al, 0x0B
    out     dx, al                ; IRQs enabled, RTS/DSR set
    ret

;--------------------------------------
; Write a single character to COM
; AL - character to send
SerialWriteChar:
    mov     ah, al                ; Save character

.wait:
    mov     dx, COMPort_Debug + 5
    in      al, dx
    test    al, 0x20
    jz      .wait

    mov     dx, COMPort_Debug
    mov     al, ah                ; Restore character
    out     dx, al
    ret

;--------------------------------------
; Write a space to COM
SerialWriteSpace:
    mov     al, ' '
    call    SerialWriteChar
    ret

;--------------------------------------
; Write a new line to COM
SerialWriteNewLine:
    mov     al, 10
    call    SerialWriteChar
    ret

;--------------------------------------
; Write a zero-terminated string to COM
; ESI - pointer to string
SerialWriteString:
.loop:
    lodsb
    test    al, al
    jz      .done
    call    SerialWriteChar
    jmp     .loop
.done:
    ret

;--------------------------------------
SerialWriteHex32:
    ; Entrée : EAX contient la valeur 32 bits à écrire en hexadécimal
    ; Utilise : ECX, EBX, EAX (modifié)
    ; Appelle : SerialWriteChar pour écrire chaque caractère

SerialWriteHex32:
    mov     ecx, eax

    mov     ebx, ecx
    shr     ebx, 28
    and     bl, 0xF
    call    PrintHexNibble

    mov     ebx, ecx
    shr     ebx, 24
    and     bl, 0xF
    call    PrintHexNibble

    mov     ebx, ecx
    shr     ebx, 20
    and     bl, 0xF
    call    PrintHexNibble

    mov     ebx, ecx
    shr     ebx, 16
    and     bl, 0xF
    call    PrintHexNibble

    mov     ebx, ecx
    shr     ebx, 12
    and     bl, 0xF
    call    PrintHexNibble

    mov     ebx, ecx
    shr     ebx, 8
    and     bl, 0xF
    call    PrintHexNibble

    mov     ebx, ecx
    shr     ebx, 4
    and     bl, 0xF
    call    PrintHexNibble

    mov     ebx, ecx
    and     bl, 0xF
    call    PrintHexNibble

    ret

PrintHexNibble:
    cmp     bl, 9
    jbe     .digit
    add     bl, 7
.digit:
    add     bl, '0'
    mov     al, bl
    call    SerialWriteChar
    ret

;----------------------------------------------------------------------------

section .text.stub16
bits 16

    global SerialWriteChar16
    global SerialWriteString16

;--------------------------------------
; Write a single character to COM (16-bit)
; AL - character to send
SerialWriteChar16:
    push    ax
    push    dx

    mov     ah, al

.wait16:
    mov     dx, COMPort_Debug + 5
    in      al, dx
    test    al, 0x20
    jz      .wait16

    mov     dx, COMPort_Debug
    mov     al, ah
    out     dx, al

    pop     dx
    pop     ax
    ret

;--------------------------------------
; Write a zero-terminated string to COM (16-bit)
; DS:SI - pointer to string
SerialWriteString16:
    push    ax
    push    dx
    push    si

.loop16:
    lodsb
    test    al, al
    jz      .done16

    call    SerialWriteChar16
    jmp     .loop16

.done16:
    pop     si
    pop     dx
    pop     ax
    ret

;----------------------------------------------------------------------------

section .note.GNU-stack noalloc noexec nowrite align=1
