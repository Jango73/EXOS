
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
;   VBR Payload Stub
;
;-------------------------------------------------------------------------

; Registers at start
; DL : Boot drive
; EAX : Partition Start LBA

BITS 16
ORIGIN equ 0x8000
KERNEL_LOAD_ADDRESS      equ 0x00020000

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

section .start
global _start
global BiosReadSectors
global MemorySet
global MemoryCopy
global StubJumpToImage
global BiosGetMemoryMap

extern BootMain

PBN                         equ 0x08        ; Param base near
PBF                         equ 0x0A        ; Param base far
CR0_PROTECTED_MODE          equ 0x00000001  ; Protected mode on/off
CR0_COPROCESSOR             equ 0x00000002  ; Math present
CR0_MONITOR_COPROCESSOR     equ 0x00000004  ; Emulate co-processor
CR0_TASKSWITCH              equ 0x00000008  ; Set on task switch
CR0_80387                   equ 0x00000010  ; Type of co-processor
CR0_PAGING                  equ 0x80000000  ; Paging on/off

_start:
    jmp         Start
    db          'VBR2'

Start:
    mov         [DAP_Start_LBA_Low], eax    ; Save Partition Start LBA

    cli                                     ; Disable interrupts
    mov         ax, cs
    mov         ds, ax
    mov         es, ax
    mov         ss, ax

    ; Setup a 32-bit stack
    ; Just below 0x8000, don't care about this data
    ; We won't be returning to MBR and VBR
    mov         sp, ORIGIN
    xor         eax, eax
    mov         ax, sp
    mov         esp, eax
    mov         ebp, eax

    sti                                     ; Enable interrupts

%if DEBUG_OUTPUT = 2
    call        InitSerial
%endif

    DebugPrint  Text_Jumping

    mov         eax, [DAP_Start_LBA_Low]
    push        eax                         ; Param 2 : Partition LBA
    xor         eax, eax
    mov         al, dl
    push        eax                         ; Param 1 : Drive
    push        word 0                      ; Add 16 bits bacause of 32 bits call
                                            ; MANDATORY to jump to 32 bit C code
    call        BootMain
    add         esp, 8

    hlt
    jmp         $

;-------------------------------------------------------------------------
; BiosReadSectors cdecl
; In : EBP+8 = Drive number
;      EBP+12 = Start LBA
;      EBP+16 = Sector count
;      EBP+20 = Buffer address (far pointer SEG:OFS)
;-------------------------------------------------------------------------

BiosReadSectors:
    push        ebp
    mov         ebp, esp
    push        ebx
    push        ecx
    push        edx
    push        esi
    push        edi

;    mov         si, Text_ReadBiosSectors
;    call        PrintString

;    mov         si, Text_Params
;    call        PrintString

;    mov         eax, [ebp+8]
;    call        PrintHex32
;    mov         al, ' '
;    call        PrintChar
;    mov         eax, [ebp+12]
;    call        PrintHex32
;    mov         al, ' '
;    call        PrintChar
;    mov         eax, [ebp+16]
;    call        PrintHex32
;    mov         al, ' '
;    call        PrintChar
;    mov         eax, [ebp+20]
;    call        PrintHex32

;    mov         si, Text_NewLine
;    call        PrintString

; Setup DAP
    mov         eax, [ebp+8]
    mov         dl, al
    mov         eax, [ebp+12]
    mov         [DAP_Start_LBA_Low], eax
    mov         eax, [ebp+16]
    mov         [DAP_NumSectors], ax

    mov         eax, [ebp+20]
    mov         [DAP_Buffer_Offset], ax
    shr         eax, 16
    mov         [DAP_Buffer_Segment], ax

    call        BiosReadSectors_16

;    mov         si, Text_BiosReadSectorsDone
;    call        PrintString

    xor         eax, eax
    jnc         .return
    mov         eax, 1

.return:
    pop         edi
    pop         esi
    pop         edx
    pop         ecx
    pop         ebx
    pop         ebp
    ret

BiosReadSectors_16:
    push        ax
    push        bx
    push        cx
    push        dx
    push        si
    push        di
    push        ds
    push        es

    push        cs
    pop         ds

    xor         ax, ax
    mov         ah, 0x42                    ; Extended Read (LBA)
    mov         si, DAP                     ; DAP address
    int         0x13                        ; BIOS disk operation

    pop         es
    pop         ds
    pop         di
    pop         si
    pop         dx
    pop         cx
    pop         bx
    pop         ax
    ret

;-------------------------------------------------------------------------

MemorySet :

    push        ebp
    mov         ebp, esp

    push        ecx
    push        edi
    push        es

    push        ds
    pop         es

    mov         edi, [ebp+(PBN+0)]
    mov         eax, [ebp+(PBN+4)]
    mov         ecx, [ebp+(PBN+8)]
    cld
    rep         stosb

    pop         es
    pop         edi
    pop         ecx

    pop         ebp
    ret

;--------------------------------------

MemoryCopy :

    push    ebp
    mov     ebp, esp

    push    ecx
    push    esi
    push    edi
    push    es

    push    ds
    pop     es

    mov     edi, [ebp+(PBN+0)]
    mov     esi, [ebp+(PBN+4)]
    mov     ecx, [ebp+(PBN+8)]
    cld
    rep     movsb

    pop     es
    pop     edi
    pop     esi
    pop     ecx

    pop     ebp
    ret

;-------------------------------------------------------------------------
; BiosGetMemoryMap
; In : EBP+8 = buffer (seg:ofs packed)
;      EBP+12 = max entries
; Out: EAX = entry count
;-------------------------------------------------------------------------

BiosGetMemoryMap:
    push    ebp
    mov     ebp, esp
    push    esi
    push    edi
    push    ebx
    push    ecx
    push    edx

    mov     eax, [ebp+8]
    mov     di, ax
    shr     eax, 16
    mov     es, ax
    mov     esi, [ebp+12]
    xor     ebx, ebx

.loop:
    mov     eax, 0xE820
    mov     edx, 0x534D4150
    mov     ecx, 24
    int     0x15
    jc      .error
    cmp     eax, 0x534D4150
    jne     .error
    add     di, 24
    dec     esi
    jz      .done
    cmp     ebx, 0
    jne     .loop

.done:
    mov     eax, [ebp+12]
    sub     eax, esi
    jmp     .return

.error:
    mov     si, Text_E820Error
    call    PrintString
    cli
    hlt
    jmp     .error

.return:
    pop     edx
    pop     ecx
    pop     ebx
    pop     edi
    pop     esi
    pop     ebp
    ret

;-------------------------------------------------------------------------
; PrintChar
; In : AL = character to write
;-------------------------------------------------------------------------

PrintChar:
    push        bx
    mov         bx, 1
    mov         ah, 0x0E
    int         0x10
    pop         bx
    ret

;-------------------------------------------------------------------------
; PrintString
;-------------------------------------------------------------------------

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

;-------------------------------------------------------------------------
; PrintHex32
; In : EAX = value to write
; Uses : EAX, EBX, ECX
;-------------------------------------------------------------------------

PrintHex32:
    mov     ecx, eax

    mov     ebx, ecx
    shr     ebx, 28
    and     bl, 0xF
    call    PrintHex32Nibble

    mov     ebx, ecx
    shr     ebx, 24
    and     bl, 0xF
    call    PrintHex32Nibble

    mov     ebx, ecx
    shr     ebx, 20
    and     bl, 0xF
    call    PrintHex32Nibble

    mov     ebx, ecx
    shr     ebx, 16
    and     bl, 0xF
    call    PrintHex32Nibble

    mov     ebx, ecx
    shr     ebx, 12
    and     bl, 0xF
    call    PrintHex32Nibble

    mov     ebx, ecx
    shr     ebx, 8
    and     bl, 0xF
    call    PrintHex32Nibble

    mov     ebx, ecx
    shr     ebx, 4
    and     bl, 0xF
    call    PrintHex32Nibble

    mov     ebx, ecx
    and     bl, 0xF
    call    PrintHex32Nibble

    ret

PrintHex32Nibble:
    cmp         bl, 9
    jbe         .digit
    add         bl, 7
.digit:
    add         bl, '0'
    mov         al, bl
    call        PrintChar
    ret

;-------------------------------------------------------------------------
; StubJumpToImage : switches to protected mode, enables paging
; and jumps into the kernel at 0xC0000000
; Param 1 : GDTR
; Param 2 : PageDirectory (physical)
; Param 3 : KernelEntryVA (virtual)
;-------------------------------------------------------------------------

StubJumpToImage:
    push        ebp
    mov         ebp, esp

    cli

    DebugPrint  Text_JumpingToPM

    xor         ebx, ebx                    ; Prepare EBX and select page 0
    mov         ah, 0x03                    ; BIOS get cursor position
    int         0x10
    mov         bx, dx                      ; BL = X, BH = Y

    mov         eax, [ebp + 8]              ; GDTR
    lgdt        [eax]

    mov         eax, [ebp + 12]             ; PageDirectory (cr3)
    mov         cr3, eax

    ; Activate protected mode
    mov         eax, cr0
    or          eax, CR0_PROTECTED_MODE
    mov         cr0, eax

    ; Jump far to 32 bits
    jmp         0x08:ProtectedEntryPoint

[BITS 32]
ProtectedEntryPoint:
    mov         ax, 0x10
    mov         ds, ax
    mov         es, ax
    mov         ss, ax
    mov         esp, 0x200000               ; Set stack halfway through low 4mb

    ; Activate paging
    mov         eax, cr0
    or          eax, CR0_PAGING
    mov         cr0, eax
    jmp         $+2                         ; Pipeline flush

    mov         edi, 0x200000               ; Top of stack
    mov         edx, [ebp + 16]             ; KernelEntryVA
    mov         esi, [ebp + 20]             ; E820 map pointer
    mov         ecx, [ebp + 24]             ; E820 entry count
    mov         eax, KERNEL_LOAD_ADDRESS
    jmp         edx

.hang:
    cli
    hlt
    jmp         .hang

;-------------------------------------------------------------------------
; Read-only data
;-------------------------------------------------------------------------

section .rodata
align 16

Text_Jumping: db "[VBR C Stub] Jumping to BootMain",10,13,0
Text_ReadBiosSectors: db "[VBR C Stub] Reading BIOS sectors",10,13,0
Text_BiosReadSectorsDone: db "[VBR C Stub] BIOS sectors read",10,13,0
Text_Params: db "[VBR C Stub] Params : ",0
Text_JumpingToPM: db "[VBR C Stub] Jumping to protected mode",0
Text_JumpingToImage: db "[VBR C Stub] Jumping to imaage",0
Text_E820Error: db "[VBR C Stub] E820 call failed",0
Text_NewLine: db 10,13,0

;-------------------------------------------------------------------------
; Data
;-------------------------------------------------------------------------

section .data
align 16

DAP :
DAP_Size : db 16
DAP_Reserved : db 0
DAP_NumSectors : dw 0
DAP_Buffer_Offset : dw 0
DAP_Buffer_Segment : dw 0
DAP_Start_LBA_Low : dd 0
DAP_Start_LBA_High : dd 0
