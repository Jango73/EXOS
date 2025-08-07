; Stub for the 2nd part of the VBR
; Registers at start
; DL : Boot drive
; EAX : Partition Start LBA

BITS 16

section .start
global _start

extern BootMain

ORIGIN equ 0x8000

_start:
    jmp         Start
    db          'VBR2'

Start:
    mov         [DAP_Start_LBA_Low], eax    ; Save Partition Start LBA

    cli                                     ; Disable interrupts
    mov         ax, cs
    mov         ds, ax
    mov         ss, ax

    ; Setup a 32-bit stack for C
    ; Just below 0x8000, don't care about this data
    xor         eax, eax
    mov         ax, ds
    shl         eax, 4
    add         eax, ORIGIN
    mov         esp, eax
    mov         ebp, eax

    sti                                     ; Enable interrupts

    mov         si, Text_Jumping
    call        PrintString

    mov         eax, [DAP_Start_LBA_Low]
    push        eax
    xor         eax, eax
    mov         al, dl
    push        eax
    call        BootMain
    add         esp, 8

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

DAP_Start_LBA_Low : dd 0

Text_Jumping: db "Jumping to BootMain...",13,10,0
