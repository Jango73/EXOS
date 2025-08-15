; Stub for the 2nd part of the VBR
; Registers at start
; DL : Boot drive
; EAX : Partition Start LBA

BITS 16

section .start
global _start
global BiosReadSectors

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

    mov         si, Text_Jumping
    call        PrintString

    mov         eax, [DAP_Start_LBA_Low]
    push        eax                         ; Param 2 : Partition LBA
    xor         eax, eax
    mov         al, dl
    push        eax                         ; Param 1 : Drive
    push        word 0                      ; Add 16 bits bacause of 32 bits call
    call        BootMain
    add         esp, 8

    hlt
    jmp         $

;--------------------------------------------------------------------------
; BiosReadSectors cdecl
; In : EBP+8 = Drive number
;      EBP+12 = Start LBA
;      EBP+16 = Sector count
;      EBP+20 = Buffer address (far pointer SEG:OFS)

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

.return
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


;--------------------------------------------------------------------------
; PrintChar
; In : AL = character to write

PrintChar:
    push        bx
    mov         bx, 1
    mov         ah, 0x0E
    int         0x10
    pop         bx
    ret

;--------------------------------------------------------------------------
PrintString:
    lodsb
    or          al, al
    jz          .done
    mov         ah, 0x0E
    int         0x10
    jmp         PrintString
.done:
    ret

;--------------------------------------------------------------------------
; PrintHex32
; In : EAX = value to write
; Uses : EAX, EBX, ECX

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
    cmp     bl, 9
    jbe     .digit
    add     bl, 7
.digit:
    add     bl, '0'
    mov     al, bl
    call    PrintChar
    ret

;--------------------------------------------------------------------------
; Read-only data

section .rodata
align 16

Text_Jumping: db "[VBR C Stub] Jumping to BootMain",10,13,0
; Text_ReadBiosSectors: db "[VBR C Stub] Reading BIOS sectors",10,13,0
; Text_BiosReadSectorsDone: db "[VBR C Stub] BIOS sectors read",10,13,0
Text_Params: db "[VBR C Stub] Params : ",0
Text_NewLine: db 10,13,0

;--------------------------------------------------------------------------
; Data

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

