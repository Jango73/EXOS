; FAT32 VBR loader, loads some sectors @0x8000 and jumps
; Registers at start
; DL : Boot drive
; EAX : Partition Start LBA

BITS 16
ORG (0x7E00 + 0x005A)

PAYLOAD_OFFSET equ 0x8000

    jmp         Start
    db          'VBR1'

Start:
    mov         [DAP_Start_LBA_Low], eax    ; Save Partition Start LBA
    add         dword [DAP_Start_LBA_Low], 1

    cli                                     ; Disable interrupts
    xor         ax, ax
    mov         ss, ax
    mov         sp, PAYLOAD_OFFSET          ; Place stack juste below PAYLOAD
    mov         ax, 0xB800
    mov         es, ax
    sti                                     ; Enable interrupts

    mov         si, Text_Loading
    call        PrintString

    ; DL already has the drive
    mov         ax, ds
    mov         [DAP_Buffer_Segment], ax
    mov         ah, 0x42                    ; Extended Read (LBA)
    mov         si, DAP                     ; DAP address
    int         0x13
    jc          BootFailed

    mov         si, Text_Jumping
    call        PrintString

    ; Jump to loaded sector at PAYLOAD_OFFSET
    mov         eax, [DAP_Start_LBA_Low]
    sub         eax, 1                      ; We added a 1 sector offset before
    jmp         PAYLOAD_OFFSET

BootFailed:
    mov         si, Text_Failed
    call        PrintString

    ; Hang
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

DAP :
DAP_Size : db 16
DAP_Reserved : db 0
DAP_NumSectors : dw 32
DAP_Buffer_Offset : dw PAYLOAD_OFFSET
DAP_Buffer_Segment : dw 0x0000
DAP_Start_LBA_Low : dd 0
DAP_Start_LBA_High : dd 0

Text_Loading: db "Loading payload...",13,10,0
Text_Jumping: db "Jumping to VBR-2 code...",13,10,0
Text_Failed: db "Payload boot failed.",13,10,0
