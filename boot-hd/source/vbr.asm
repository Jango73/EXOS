
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
;   Volume Boot Record
;
;   Registers on entry
;   DL : Boot drive
;   EAX : Partition Start LBA
;
;-------------------------------------------------------------------------

VBR_LOAD_OFFSET     equ (0x7E00 + 0x005A)
VBR_STACK_OFFSET    equ 0x9000

%ifndef PAYLOAD_ADDRESS
%error "PAYLOAD_ADDRESS is not defined"
%endif

%include "vbr-address.inc"

PAYLOAD_TARGET_SEGMENT equ LINEAR_TO_SEGMENT(PAYLOAD_ADDRESS)
PAYLOAD_TARGET_OFFSET  equ LINEAR_TO_OFFSET(PAYLOAD_ADDRESS)

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

;-------------------------------------------------------------------------

BITS 16
ORG VBR_LOAD_OFFSET

    jmp         Start
    db          'VBR1'

Start:
    mov         [PartitionStartLBA], eax    ; Save Partition Start LBA

    xor         ecx, ecx
    mov         cx, [DAP_NumSectors]
    cmp         eax, ecx
    jae         .payload_ok
    jmp         BootFailed

.payload_ok:
    sub         eax, ecx
    mov         [DAP_Start_LBA_Low], eax    ; Read payload from the gap before the partition
    mov         dword [DAP_Start_LBA_High], 0

    cli                                     ; Disable interrupts
    mov         ax, cs
    mov         ds, ax
    mov         ss, ax
    mov         sp, VBR_STACK_OFFSET        ; Use dedicated VBR stack within this segment
    mov         ax, 0xB800
    mov         es, ax
    sti                                     ; Enable interrupts

%if DEBUG_OUTPUT = 2
    call        InitSerial
%endif

    DebugPrint  Text_Loading

    ; DL already has the drive
    mov         ax, PAYLOAD_TARGET_SEGMENT
    mov         [DAP_Buffer_Segment], ax
    mov         ax, PAYLOAD_TARGET_OFFSET
    mov         [DAP_Buffer_Offset], ax
    mov         ah, 0x42                    ; Extended Read (LBA)
    mov         si, DAP                     ; DAP address
    int         0x13
    jc          BootFailed

    DebugPrint  Text_Jumping

    ; Jump to loaded sector at PAYLOAD_ADDRESS with partition start in EAX
    mov         eax, [PartitionStartLBA]
    push        word PAYLOAD_TARGET_SEGMENT
    push        word PAYLOAD_TARGET_OFFSET
    retf

BootFailed:
    ErrorPrint  Text_Failed

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
DAP_NumSectors : dw RESERVED_SECTORS
DAP_Buffer_Offset : dw PAYLOAD_TARGET_OFFSET
DAP_Buffer_Segment : dw PAYLOAD_TARGET_SEGMENT
DAP_Start_LBA_Low : dd 0
DAP_Start_LBA_High : dd 0

PartitionStartLBA : dd 0

Text_Loading: db "Loading payload...",13,10,0
Text_Jumping: db "Jumping to VBR-2 code...",13,10,0
Text_Failed: db "Payload boot failed.",13,10,0
