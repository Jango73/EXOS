BITS 16

section .start
global _start

extern BootMain

_start:
    ; mov         si, Text_Jumping
    ; call        PrintString
    ; jmp $

    mov ax, 0xB800
    mov es, ax
    mov byte [es:0], 'C'
    hlt
    jmp $

    mov al, dl
    push ax
    call BootMain

PrintString:
    lodsb
    or          al, al
    jz          .done
    mov         ah, 0x0E
    int         0x10
    jmp         PrintString
.done:
    ret

Text_Jumping: db "Jumping to BootMain...",13,10,0
