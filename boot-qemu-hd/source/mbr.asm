; Minimal MBR that boots the active partition

BITS 16
ORG 0x7C00

VBR_OFFSET equ 0x7E00

; Clear interrupts
    cli
    xor         ax, ax
    mov         ss, ax
    mov         sp, 0x7C00
    mov         ax, 0xB800
    mov         es, ax
    sti

; BIOS parameter block offset: skip table entries
; Partition table starts at offset 0x1BE; BIOS vector at 0x1FE
; We'll search for active partition entry

    mov         si, Text_Loading
    call        PrintString

    mov         si, Partition
    mov         cx, 4                       ; up to 4 partition entries

FindActive:
    lodsb                                   ; first byte is boot flag
    cmp         al, 0x80
    je          ActivePartitionFound
    add         si, 15                      ; skip rest of entry
    loop FindActive

NoActive:
    jmp         BootFailed

ActivePartitionFound:
    sub         si, 1
    mov         [ActivePartition], si

    mov         ax, [si + (Partition_Start_LBA - Partition)]
    mov         [DAP_Start_LBA_Low], ax

    ; DL already has the drive
    mov         ax, ds
    mov         [DAP_Buffer_Segment], ax
    mov         word [DAP_NumSectors], 1    ; Sectors to read
    mov         ah, 0x42                    ; Extended Read (LBA)
    mov         si, DAP                     ; DAP address
    int         0x13
    jc          BootFailed

    mov         si, Text_Jumping
    call        PrintString

    ; Jump to loaded sector at VBR_OFFSET
    jmp         VBR_OFFSET

BootFailed:
    mov         si, Text_Failed
    call        PrintString

    ; Hang
    hlt
    jmp BootFailed

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
DAP_NumSectors : dw 0
DAP_Buffer_Offset : dw VBR_OFFSET
DAP_Buffer_Segment : dw 0x0000
DAP_Start_LBA_Low : dd 0
DAP_Start_LBA_High : dd 0

ActivePartition : db 0
Text_Loading: db "Loading VBR...",13,10,0
Text_Jumping: db "Jumping to VBR...",13,10,0
Text_Failed: db "VBR boot failed.",13,10,0

times 446-($-$$) db 0

; The following are placeholders, real data is already present in the image
Partition:
Partition_Status: db 0
Partition_Start_CHS_Head: db 0
Partition_Start_CHS_Sector: db 0
Partition_Start_CHS_Cylinder: db 0
Partition_Type: db 0
Partition_End_CHS_Head: db 0
Partition_End_CHS_Sector: db 0
Partition_End_CHS_Cylinder: db 0
Partition_Start_LBA: dd 0
Partition_Total_Sectors: dd 0
