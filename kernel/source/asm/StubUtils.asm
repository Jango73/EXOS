; StubUtils.asm

;----------------------------------------------------------------------------
;  EXOS
;  Copyright (c) 1999-2025 Jango73
;  All rights reserved
;----------------------------------------------------------------------------

%include "./Kernel.inc"

section .text.stub
bits 16

    global Set_A20_Line
    global Clear_A20_Line
    global Gate_A20
    global Empty_8042
    global Delay
    global PrintChar
    global SetupPIC

    extern Cursor
    extern SI_IRQMask_21_RM
    extern SI_IRQMask_A1_RM

Set_A20_Line:

    mov     ah, 0xDF
    call    Gate_A20
    ret

;--------------------------------------

Clear_A20_Line:

    mov     ah, 0xDD
    call    Gate_A20
    ret

;--------------------------------------

Gate_A20:

    call    Empty_8042

    mov     al, 0xD1
    out     KEYBOARD_STATUS, al

    call    Empty_8042

    mov     al, ah
    out     KEYBOARD_CONTROL, al

    call    Empty_8042

    mov     cx, 0x14

Gate_A20_Loop:

    out     0xED, ax                   ; IO delay
    loop    Gate_A20_Loop

    ret

;--------------------------------------

Empty_8042:

    out     0x00ED, ax                 ; IO delay

    in      al, KEYBOARD_STATUS
    test    al, KEYBOARD_STATUS_OUT_FULL
    jz      Empty_8042_NoOutput

    out     0x00ED, ax                 ; IO delay

    in      al, KEYBOARD_CONTROL
    jmp     Empty_8042

Empty_8042_NoOutput:

    test    al, KEYBOARD_STATUS_IN_FULL
    jnz     Empty_8042
    ret

;--------------------------------------

Delay:

    dw      0x00EB                     ; jmp $+2
    dw      0x00EB                     ; jmp $+2
    ret

;--------------------------------------

PrintChar:

    mov     bx, 0xB800
    mov     es, bx
    mov     di, [Cursor]
    mov     [es:di], al
    add     di, 2
    mov     [Cursor], di
    ret

;--------------------------------------

SetupPIC:

; Reprogram the IRQs
; Put them after the Intel-reserved interrupts, at int 0x20-0x2F.
; The bios puts IRQs at 0x08-0x0F, which is used for the internal
; hardware interrupts as well. We just have to reprogram the 8259s.

    xor     eax, eax
    in      al, PIC1_DATA
    mov     [SI_IRQMask_21_RM], eax
    call    Delay
    xor     eax, eax
    in      al, PIC2_DATA
    mov     [SI_IRQMask_A1_RM], eax
    call    Delay

    mov     al, ICW1_INIT + ICW1_ICW4
    out     PIC1_CMD, al
    call    Delay
    mov     al, ICW1_INIT + ICW1_ICW4
    out     PIC2_CMD, al
    call    Delay

    mov     al, PIC1_VECTOR               ; Start of hardware IRQs (0x20)
    out     PIC1_DATA, al
    call    Delay
    mov     al, PIC2_VECTOR               ; Start of hardware IRQs (0x28)
    out     PIC2_DATA, al
    call    Delay

    mov     al, 0x04                      ; 8259-1 is master
    out     PIC1_DATA, al
    call    Delay
    mov     al, 0x02                      ; 8259-2 is slave
    out     PIC2_DATA, al
    call    Delay

    mov     al, ICW4_8086                 ; 8086 mode for 8259-1
    out     PIC1_DATA, al
    call    Delay
    mov     al, ICW4_8086                 ; 8086 mode for 8259-2
    out     PIC2_DATA, al
    call    Delay

    mov     al, 0xFB                   ; Mask off all IRQs for now
    out     PIC1_DATA, al
    call    Delay
    mov     al, 0xFF
    out     PIC2_DATA, al
    call    Delay

    mov     al, 0x20
    out     0x20, al
    out     0xA0, al
    ret

;--------------------------------------

bits 32

    global SetupGDT
    global SetupPaging
    global MapPages
    global CopyKernel
    global GetMemorySize

    extern GDT

SetupGDT:

    mov     esi, ebp
    add     esi, GDT
    mov     edi, PA_GDT
    mov     ecx, 8 * SEGMENT_DESCRIPTOR_SIZE
    cld
    rep     movsb
    ret

;--------------------------------------

SetupPaging:

    mov     edi, PA_PGD

    xor     ebx, ebx
    mov     dword [edi+ebx], (PA_PGL & PAGE_MASK) | PAGE_BIT_SYSTEM

    mov     ebx, (LA_KERNEL >> MUL_4MB) << MUL_4
    mov     dword [edi+ebx], (PA_PGK & PAGE_MASK) | PAGE_BIT_SYSTEM

    mov     ebx, (LA_SYSTEM >> MUL_4MB) << MUL_4
    mov     dword [edi+ebx], (PA_PGH & PAGE_MASK) | PAGE_BIT_SYSTEM

    mov     ebx, (LA_DIRECTORY >> MUL_4MB) << MUL_4
    mov     dword [edi+ebx], (PA_PGS & PAGE_MASK) | PAGE_BIT_SYSTEM

    mov     edi, PA_PGL
    mov     eax, PA_LOW
    mov     ecx, N_1MB >> MUL_4KB
    call    MapPages

    mov     edi, PA_PGH
    mov     eax, PA_SYSTEM
    mov     ecx, N_128KB >> MUL_4KB
    call    MapPages

    mov     edi, PA_PGK
    mov     eax, PA_KERNEL
    mov     ecx, KERNEL_SIZE >> MUL_4KB
    call    MapPages

    mov     edi, PA_PGS

    mov     dword [edi], (PA_PGD & PAGE_MASK) | PAGE_BIT_SYSTEM
    add     edi, 4
    mov     dword [edi], (PA_PGS & PAGE_MASK) | PAGE_BIT_SYSTEM
    add     edi, 4
    mov     dword [edi], (PA_PGL & PAGE_MASK) | PAGE_BIT_SYSTEM

    mov     edi, PA_PGS + 8 + ((LA_KERNEL >> MUL_4MB) << MUL_4)
    mov     dword [edi], (PA_PGK & PAGE_MASK) | PAGE_BIT_SYSTEM
    ret

;--------------------------------------

MapPages:

    mov     ebx, eax

MapPages_Loop:

    mov     eax, ebx
    and     eax, PAGE_MASK
    or      eax, PAGE_BIT_SYSTEM
    mov     [edi], eax

    add     edi, 4
    add     ebx, N_4KB
    dec     ecx

    cmp     ecx, 0
    jne     MapPages_Loop

    ret

;--------------------------------------

CopyKernel:

    mov     esi, ebp
    add     esi, N_4KB
    mov     edi, PA_KERNEL
    mov     ecx, KERNEL_SIZE
    sub     ecx, N_4KB
    cld
    rep     movsb
    ret

;--------------------------------------

GetMemorySize:

    mov     esi, N_4MB

GetMemorySize_Loop:

    mov     al, [esi]                  ; Save value
    mov     byte [esi], 0xAA           ; Write AA
    mov     bl, [esi]                  ; Read value to see if cell exists
    mov     [esi], al                  ; Restore value

    cmp     bl, 0xAA
    jne     GetMemorySize_Out

    add     esi, N_4KB
    jmp     GetMemorySize_Loop

GetMemorySize_Out:

    mov     eax, esi
    ret

