%include "./Kernel.inc"

section .text
bits 32

    global SerialInit
    global SerialWriteChar
    global SerialWriteString
    global SerialWriteHex32

;--------------------------------------
; Initialize COM1 for 38400 8N1
SerialInit:
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
    ret

;--------------------------------------
; Write a single character to COM1
; AL - character to send
SerialWriteChar:
.wait:
    mov     dx, 0x3F8 + 5
    in      al, dx
    test    al, 0x20
    jz      .wait

    mov     dx, 0x3F8
    out     dx, al
    ret

;--------------------------------------
; Write a zero-terminated string to COM1
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
; Write a 32-bit value in hexadecimal to COM1
; EAX - value
SerialWriteHex32:
    mov     ecx, 8
.hex_loop:
    mov     edx, eax
    shr     edx, 28
    add     dl, '0'
    cmp     dl, '9'
    jbe     .digit
    add     dl, 7
.digit:
    mov     al, dl
    call    SerialWriteChar
    shl     eax, 4
    loop    .hex_loop
    ret

;--------------------------------------
