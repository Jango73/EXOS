
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
%ifndef PAYLOAD_OFFSET
%error "PAYLOAD_OFFSET is not defined"
%endif

ORIGIN equ PAYLOAD_OFFSET
KERNEL_LOAD_ADDRESS      equ 0x00200000

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
global VESAGetModeInfo
global VESASetMode
global SetPixel24
global EnableA20
global EnterUnrealMode

extern BootMain

PBN                         equ 0x08        ; Param base near
PBF                         equ 0x0A        ; Param base far
CR0_PROTECTED_MODE          equ 0x00000001  ; Protected mode on/off
CR0_COPROCESSOR             equ 0x00000002  ; Math present
CR0_MONITOR_COPROCESSOR     equ 0x00000004  ; Emulate co-processor
CR0_TASKSWITCH              equ 0x00000008  ; Set on task switch
CR0_80387                   equ 0x00000010  ; Type of co-processor
CR0_PAGING                  equ 0x80000000  ; Paging on/off

%ifdef ARCH_X86_64
CR4_PAE                     equ 0x00000020
IA32_EFER                   equ 0xC0000080
EFER_LME                    equ 0x00000100
%endif

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
    ; Just below PAYLOAD_OFFSET, do not care about this data
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

/***************************************************************************/
; EnterUnrealMode
; Switch segment registers to descriptors with a 4GB limit while staying in
; real mode so that linear addresses above 1MB can be accessed safely.
/***************************************************************************/

EnterUnrealMode:
    push        eax
    push        ebx
    push        ecx
    push        edx
    pushf

    cli
    lgdt        [TempGDT]

    mov         eax, cr0
    or          eax, CR0_PROTECTED_MODE
    mov         cr0, eax

    mov         ax, 0x10
    mov         ds, ax
    mov         es, ax
    mov         fs, ax
    mov         gs, ax

    mov         eax, cr0
    and         eax, ~CR0_PROTECTED_MODE
    mov         cr0, eax
    jmp         $+2                         ; Flush prefetch queue

    popf
    pop         edx
    pop         ecx
    pop         ebx
    pop         eax
    ret

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
; VESAGetModeInfo
; In : EBP+8 = mode number
;      EBP+12 = buffer pointer (seg:ofs packed)
; Out: EAX = 0 on success, 1 on failure
;-------------------------------------------------------------------------

VESAGetModeInfo:
    push    ebp
    mov     ebp, esp
    push    esi
    push    edi
    push    ebx
    push    ecx
    push    edx

    mov     cx, [ebp+8]             ; Mode number
    mov     eax, [ebp+12]           ; Buffer address
    mov     di, ax                  ; Offset
    shr     eax, 16
    mov     es, ax                  ; Segment

    mov     ax, 0x4F01              ; VESA Get Mode Info
    int     0x10                    ; BIOS interrupt

    cmp     ax, 0x004F              ; Check for success
    je      .success
    mov     eax, 1                  ; Return failure
    jmp     .done

.success:
    xor     eax, eax

.done:
    pop     edx
    pop     ecx
    pop     ebx
    pop     edi
    pop     esi
    pop     ebp
    ret

;-------------------------------------------------------------------------
; VESASetMode
; In : EBP+8 = mode number (with flags)
; Out: EAX = 0 on success, 1 on failure
;-------------------------------------------------------------------------

VESASetMode:
    push    ebp
    mov     ebp, esp
    push    ebx

    mov     bx, [ebp+8]             ; Mode number
    mov     ax, 0x4F02              ; VESA Set Mode
    int     0x10                    ; BIOS interrupt

    cmp     ax, 0x004F              ; Check for success
    je      .success
    mov     eax, 1                  ; Return failure
    jmp     .done

.success:
    xor     eax, eax

.done:
    pop     ebx
    pop     ebp
    ret

;-------------------------------------------------------------------------
; SetPixel24
; In : EBP+8 = x coordinate
;      EBP+12 = y coordinate
;      EBP+16 = color (24-bit RGB)
;      EBP+20 = framebuffer base address
; Out: none
;-------------------------------------------------------------------------

SetPixel24:
    push    ebp
    mov     ebp, esp
    push    eax
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi
    push    ds

    ; Check bounds
    mov     eax, [ebp+8]            ; x
    cmp     eax, 640
    jae     .done
    mov     ebx, [ebp+12]           ; y
    cmp     ebx, 480
    jae     .done

    ; Calculate offset: (y * 640 + x) * 3
    mov     ecx, ebx                ; y
    imul    ecx, 640                ; y * 640
    add     ecx, eax                ; y * 640 + x
    imul    ecx, 3                  ; (y * 640 + x) * 3

    ; Get framebuffer address
    mov     esi, [ebp+20]           ; framebuffer base
    add     esi, ecx                ; add offset

    ; Switch to unreal mode temporarily to access >1MB
    push    eax
    push    ebx

    ; Save current DS
    mov     ax, ds
    push    ax

    ; Enter protected mode briefly
    cli
    lgdt    [TempGDT]
    mov     eax, cr0
    or      al, 1
    mov     cr0, eax

    ; Load data segment with 4GB limit
    mov     ax, 0x10
    mov     ds, ax

    ; Return to real mode but keep DS with 4GB limit
    and     al, 0xFE
    mov     cr0, eax

    ; Restore segment registers except DS (keeps unreal mode)
    pop     ax
    ; Don't restore DS - it now has 4GB limit

    pop     ebx
    pop     eax

    ; Get color
    mov     eax, [ebp+16]           ; color

    ; Write BGR (24-bit) using 32-bit addressing
    mov     byte [esi], al          ; Blue
    inc     esi
    shr     eax, 8
    mov     byte [esi], al          ; Green
    inc     esi
    shr     eax, 8
    mov     byte [esi], al          ; Red

    sti

.done:
    pop     ds
    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    pop     eax
    pop     ebp
    ret

;-------------------------------------------------------------------------
; EnableA20 - Enable A20 line for access to high memory
;-------------------------------------------------------------------------

EnableA20:
    push    eax
    push    ecx

    ; Method 1: Fast A20 (port 0x92)
    in      al, 0x92
    or      al, 2
    out     0x92, al

    ; Method 2: Keyboard controller
    call    .wait_8042
    mov     al, 0xAD        ; Disable keyboard
    out     0x64, al

    call    .wait_8042
    mov     al, 0xD0        ; Read output port
    out     0x64, al

    call    .wait_8042_data
    in      al, 0x60        ; Read current settings
    push    eax

    call    .wait_8042
    mov     al, 0xD1        ; Write output port
    out     0x64, al

    call    .wait_8042
    pop     eax
    or      al, 2           ; Set A20 bit
    out     0x60, al

    call    .wait_8042
    mov     al, 0xAE        ; Enable keyboard
    out     0x64, al

    call    .wait_8042

    pop     ecx
    pop     eax
    ret

.wait_8042:
    in      al, 0x64
    test    al, 2
    jnz     .wait_8042
    ret

.wait_8042_data:
    in      al, 0x64
    test    al, 1
    jz      .wait_8042_data
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
; and jumps into the kernel entry point
; Param 1 : GDTR
; Param 2 : PageDirectory (physical)
; Param 3 : KernelEntryLo (low 32 bits of virtual entry point)
; Param 4 : KernelEntryHi (high 32 bits of virtual entry point)
; Param 5 : MultibootInfoPtr (physical address of multiboot_info_t)
; Param 6 : MultibootMagic (0x2BADB002)
;-------------------------------------------------------------------------

StubJumpToImage:
    push        ebp
    mov         ebp, esp

    cli

    DebugPrint  Text_JumpingToPM

    mov         eax, [ebp + 8]              ; GDTR
    lgdt        [eax]

    mov         eax, [ebp + 12]             ; Paging structure (cr3)
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

%ifdef ARCH_X86_64
    ; Preserve parameters for long mode entry
    mov         eax, [ebp + 24]             ; Multiboot info pointer
    mov         [LongModeMultibootInfo], eax
    xor         edx, edx
    mov         [LongModeMultibootInfo + 4], edx

    mov         eax, [ebp + 28]             ; Multiboot magic
    mov         [LongModeMultibootMagic], eax

    mov         eax, [ebp + 16]             ; Kernel entry low
    mov         [LongModeKernelEntry], eax
    mov         eax, [ebp + 20]             ; Kernel entry high
    mov         [LongModeKernelEntry + 4], eax

    ; Enable PAE
    mov         eax, cr4
    or          eax, CR4_PAE
    mov         cr4, eax

    ; Reload CR3 with PML4 base (ensures alignment after CR4 update)
    mov         eax, [ebp + 12]
    mov         cr3, eax

    ; Enable long mode
    mov         ecx, IA32_EFER
    rdmsr
    or          eax, EFER_LME
    wrmsr

    ; Activate paging
    mov         eax, cr0
    or          eax, CR0_PAGING
    mov         cr0, eax

    ; Jump to 64-bit mode
    jmp         0x08:LongModeEntry
%else
    ; Activate paging
    mov         eax, cr0
    or          eax, CR0_PAGING
    mov         cr0, eax
    jmp         $+2                         ; Pipeline flush

    mov         eax, [ebp + 28]             ; Multiboot magic (0x2BADB002)
    mov         ebx, [ebp + 24]             ; Physical address of multiboot_info_t
    mov         edx, [ebp + 16]             ; Kernel entry virtual address
    jmp         edx
%endif

%ifdef ARCH_X86_64
[BITS 64]
LongModeEntry:
    mov         ax, 0x10
    mov         ds, ax
    mov         es, ax
    mov         ss, ax
    mov         fs, ax
    mov         gs, ax

    mov         rsp, KERNEL_LOAD_ADDRESS
    mov         rbp, rsp

    mov         eax, [LongModeMultibootMagic]
    mov         rbx, qword [LongModeMultibootInfo]
    mov         rdx, qword [LongModeKernelEntry]
    jmp         rdx
%endif

[BITS 32]
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
Text_JumpingToPM: db "[VBR C Stub] Jumping to protected mode",10,13,10,13,0
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

%ifdef ARCH_X86_64
align 16
LongModeKernelEntry:      dq 0
LongModeMultibootInfo:    dq 0
LongModeMultibootMagic:   dd 0
LongModePadding:          dd 0
%endif

; Temporary GDT for unreal mode
align 16
TempGDT:
    dw TempGDTEnd - TempGDTStart - 1    ; Limit
    dd TempGDTStart                     ; Base

TempGDTStart:
    ; Null descriptor
    dd 0x00000000
    dd 0x00000000
    ; Code descriptor (not used)
    dd 0x0000FFFF
    dd 0x00CF9A00
    ; Data descriptor (4GB, flat)
    dd 0x0000FFFF
    dd 0x00CF9200
TempGDTEnd:
