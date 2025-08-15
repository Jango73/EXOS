
;----------------------------------------------------------------------------
;  EXOS
;  Copyright (c) 1999-2025 Jango73
;  All rights reserved
;----------------------------------------------------------------------------

%include "./Kernel.inc"

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

%if DEBUG_LOGS = 1

    mov     dx, 0x3F8 + 1
    mov     al, 0x00
    out     dx, al                ; Disable interrupts

    mov     dx, 0x3F8 + 3
    mov     al, 0x80
    out     dx, al                ; Enable DLAB

    mov     dx, 0x3F8 + 0
    mov     al, 0x03
    out     dx, al                ; Set baud rate divisor (low byte)

    mov     dx, 0x3F8 + 1
    mov     al, 0x00
    out     dx, al                ; High byte divisor

    mov     dx, 0x3F8 + 3
    mov     al, 0x03
    out     dx, al                ; 8 bits, no parity, 1 stop bit

    mov     dx, 0x3F8 + 2
    mov     al, 0xC7
    out     dx, al                ; Enable FIFO

    mov     dx, 0x3F8 + 4
    mov     al, 0x0B
    out     dx, al                ; IRQs enabled, RTS/DSR set

%endif

    ret

;--------------------------------------
; Write a single character to COM1
; AL - character to send
SerialWriteChar:

%if DEBUG_LOGS = 1

    mov     ah, al                ; Save character
.wait:
    mov     dx, 0x3F8 + 5
    in      al, dx
    test    al, 0x20
    jz      .wait

    mov     dx, 0x3F8
    mov     al, ah                ; Restore character
    out     dx, al

%endif

    ret

;--------------------------------------
; Write a space to COM1
SerialWriteSpace:

%if DEBUG_LOGS = 1

    mov     al, ' '
    call    SerialWriteChar

%endif

    ret

;--------------------------------------
; Write a new line to COM1
SerialWriteNewLine:

%if DEBUG_LOGS = 1

    mov     al, 10
    call    SerialWriteChar

%endif

    ret

;--------------------------------------
; Write a zero-terminated string to COM1
; ESI - pointer to string
SerialWriteString:

%if DEBUG_LOGS = 1

.loop:
    lodsb
    test    al, al
    jz      .done
    call    SerialWriteChar
    jmp     .loop
.done:

%endif

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

;--------------------------------------
