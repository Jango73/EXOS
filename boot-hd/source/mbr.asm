
;-------------------------------------------------------------------------
;
;   EXOS Bootloader
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
;   Master Boot Record
;
;-------------------------------------------------------------------------

; Minimal MBR that boots the active partition
; Registers at start
; DL : Boot drive

BITS 16
ORG 0x7C00

VBR_OFFSET equ 0x7E00

%macro DebugPrint 1
%if DEBUG_OUTPUT
    mov         si, %1
%if DEBUG_OUTPUT = 2
    call        SerialPrintString
%else
    call        PrintString
%endif
%endif
%endmacro

%macro ErrorPrint 1
    mov         si, %1
%if DEBUG_OUTPUT = 2
    call        SerialPrintString
%else
    call        PrintString
%endif
%endmacro

    jmp         Start
    db          'MBR0'

Start:
    cli                                     ; Disable interrupts
    xor         ax, ax
    mov         ss, ax
    mov         sp, 0x7C00
    mov         ax, 0xB800
    mov         es, ax
    sti                                     ; Enable interrupts

%if DEBUG_OUTPUT = 2
    call        InitSerial
%endif

; BIOS parameter block offset: skip table entries
; Partition table starts at offset 0x1BE; BIOS vector at 0x1FE
; We'll search for active partition entry

    DebugPrint  Text_Loading

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

    mov         eax, [si + (Partition_Start_LBA - Partition)]
    mov         [DAP_Start_LBA_Low], eax

    ; DL already has the drive
    mov         ax, ds
    mov         [DAP_Buffer_Segment], ax
    mov         ah, 0x42                    ; Extended Read (LBA)
    mov         si, DAP                     ; DAP address
    int         0x13
    jc          BootFailed

    DebugPrint  Text_Jumping

    ; Transfer Partition Start LBA to VBR
    mov         si, [ActivePartition]
    mov         eax, [si + (Partition_Start_LBA - Partition)]

    ; Jump to loaded sector at VBR_OFFSET
    jmp         VBR_OFFSET

BootFailed:
    ErrorPrint  Text_Failed

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

SerialPrintString:
    lodsb
    or          al, al
    jz          .sdone
    push        ax
.wait:
    mov         dx, 0x3FD
    in          al, dx
    test        al, 0x20
    jz          .wait
    pop         ax
    mov         dx, 0x3F8
    out         dx, al
    jmp         SerialPrintString
.sdone:
    ret

InitSerial:
    mov         dx, 0x3F8 + 1
    mov         al, 0x00
    out         dx, al
    mov         dx, 0x3F8 + 3
    mov         al, 0x80
    out         dx, al
    mov         dx, 0x3F8
    mov         al, 0x03
    out         dx, al
    mov         dx, 0x3F8 + 1
    mov         al, 0x00
    out         dx, al
    mov         dx, 0x3F8 + 3
    mov         al, 0x03
    out         dx, al
    mov         dx, 0x3F8 + 2
    mov         al, 0xC7
    out         dx, al
    mov         dx, 0x3F8 + 4
    mov         al, 0x0B
    out         dx, al
    ret

DAP :
DAP_Size : db 16
DAP_Reserved : db 0
DAP_NumSectors : dw 1
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
