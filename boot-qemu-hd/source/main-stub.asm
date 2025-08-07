BITS 16

section .start
global _start

extern BootMain

ORIGIN equ 0x8000

_start:
    cli

    mov         ax, cs
    mov         ds, ax
    mov         ss, ax

    ; Setup a 32-bit stack for C
    xor         eax, eax
    mov         ax, ds
    shl         eax, 4
    add         eax, ORIGIN
    mov         esp, eax
    mov         ebp, eax

    sti

    mov         si, Text_Jumping
    call        PrintString

    xor         ax, ax
    mov         al, dl
    push        ax
    call        BootMain

    hlt
    jmp         $

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
