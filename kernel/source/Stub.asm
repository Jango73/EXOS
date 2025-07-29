
; Stub.asm

;----------------------------------------------------------------------------
;  EXOS
;  Copyright (c) 1999 Exelsius
;  All rights reserved
;----------------------------------------------------------------------------

%include "./Kernel.inc"

;----------------------------------------------------------------------------

extern ProtectedModeEntry

;----------------------------------------------------------------------------

DOS_CALL      equ 0x21
DOS_PRINT     equ 0x09

;----------------------------------------------------------------------------

; Segment virtual base   : 0
; Segment virtual offset : 0

section .text.stub
bits 16

    global StartAbsolute

;--------------------------------------

StartAbsolute :

    mov     al, 'a'
    call    PrintChar

    jmp     Start

;--------------------------------------

; Here we store some startup info for the kernel
; It must start at stub + 16

times (16 - ($ - StartAbsolute)) db 0

SI_Loader_SS      dd 0
SI_Loader_SP      dd 0
SI_IRQMask_21_RM  dd 0
SI_IRQMask_A1_RM  dd 0
SI_Console_Width  dd 0
SI_Console_Height dd 0
SI_Console_CX     dd 0
SI_Console_CY     dd 0
SI_Memory         dd 0

;--------------------------------------

GDT :

GDT_Null :

    dd 0x00000000
    dd 0x00000000

GDT_Unused :

    dd 0x00000000
    dd 0x00000000

GDT_Kernel_Code :

    dd 0x0000FFFF
    dd 0x00CF9A00

GDT_Kernel_Data :

    dd 0x0000FFFF
    dd 0x00CF9200

GDT_User_Code :

    dd 0x0000FFFF
    dd 0x00CFFA00

GDT_User_Data :

    dd 0x0000FFFF
    dd 0x00CFF200

GDT_Real_Code :

    dd 0x0000FFFF
    dd 0x000F9A00

GDT_Real_Data :

    dd 0x0000FFFF
    dd 0x000F9200

;--------------------------------------

IDT_Label :

    dw  0
    dd  0

GDT_Label :

    dw  (N_8KB - 1)
    dd  GDT

Start32_Entry :

    dd Start32
    dw SELECTOR_KERNEL_CODE

Cursor dw 0

;--------------------------------------

Start :

    ;--------------------------------------
    ; Save loader's stack

    xor     esi, esi
    xor     edi, edi

    mov     si, ss
    mov     di, sp

    ;--------------------------------------
    ; Adjust segments

    cli
    mov     ax, cs
    mov     ds, ax
    mov     ss, ax
    mov     es, ax
    sti

    mov     ax, N_4KB - 16
    mov     sp, ax

    ;--------------------------------------
    ; Store loader's stack

    mov     [SI_Loader_SS - StartAbsolute], esi
    mov     [SI_Loader_SP - StartAbsolute], edi

    ;--------------------------------------
    ; Get 32-bit address of code

    xor     eax, eax
    mov     ax, cs
    shl     eax, MUL_16
    mov     ebp, eax

    ;--------------------------------------
    ; Get some information from the BIOS

    mov     ah, 0x03
    mov     bh, 0x00
    int     0x10

    xor     eax, eax
    mov     al, dl
    mov     [SI_Console_CX - StartAbsolute], eax

    xor     eax, eax
    mov     al, dh
    mov     [SI_Console_CY - StartAbsolute], eax

    ;--------------------------------------
    ; Disable interrupts

    cli

    ;--------------------------------------
    ; Load Interrupt Descriptor Table
    ; It has a limit of 0 to force shutdown if a
    ; non-maskable interrupt occurs

    mov     al, 'b'
    call    PrintChar

    lidt    [IDT_Label - StartAbsolute]

    ;--------------------------------------
    ; Load Global Descriptor Table

    mov     al, 'c'
    call    PrintChar

    mov     edi, GDT_Label - StartAbsolute
    add     edi, 2
    add     [edi], ebp

    lgdt    [GDT_Label - StartAbsolute]

    ;--------------------------------------
    ; Set A-20 line

    mov     al, 'd'
    call    PrintChar

    call    Set_A20_Line

    ;--------------------------------------

    mov     al, 'e'
    call    PrintChar

    xor     ax, ax
    out     0xF0, al
    call    Delay
    out     0xF1, al
    call    Delay

    ;--------------------------------------
    ; Set the PIC to protected mode

    mov     al, 'f'
    call    PrintChar

    call    SetupPIC

    ;--------------------------------------
    ; Save code address for 32 bit code

    mov     al, 'g'
    call    PrintChar

    add     [Start32_Entry - StartAbsolute], ebp

    ;--------------------------------------
    ; Switch to protected mode

    mov     al, 'h'
    call    PrintChar

    mov     eax, CR0_PROTECTEDMODE
    mov     cr0, eax

    ;--------------------------------------
    ; Clear the prefetch queue

    jmp     Next

Next :

    jmp     far dword [Start32_Entry - StartAbsolute]

;--------------------------------------

Set_A20_Line :

    mov     ah, 0xDF
    call    Gate_A20
    ret

;--------------------------------------

Clear_A20_Line :

    mov     ah, 0xDD
    call    Gate_A20
    ret

;--------------------------------------

Gate_A20 :

    call    Empty_8042

    mov     al, 0xD1
    out     KEYBOARD_STATUS, al

    call    Empty_8042

    mov     al, ah
    out     KEYBOARD_CONTROL, al

    call    Empty_8042

    mov     cx, 0x14

Gate_A20_Loop :

    out     0xED, ax                   ; IO delay
    loop    Gate_A20_Loop

    ret

;--------------------------------------

Empty_8042 :

    out     0x00ED, ax                 ; IO delay

    in      al, KEYBOARD_STATUS
    test    al, KEYBOARD_STATUS_OUT_FULL
    jz      Empty_8042_NoOutput

    out     0x00ED, ax                 ; IO delay

    in      al, KEYBOARD_CONTROL
    jmp     Empty_8042

Empty_8042_NoOutput :

    test    al, KEYBOARD_STATUS_IN_FULL
    jnz     Empty_8042
    ret

;--------------------------------------

Delay :

    dw      0x00EB                     ; jmp $+2
    dw      0x00EB                     ; jmp $+2
   ret

;--------------------------------------

PrintChar :

    push    bx
    push    di
    push    es
    mov     bx, 0xB800
    mov     es, bx
    mov     di, [Cursor - StartAbsolute]
    mov     [es:di], al
    inc     di
    inc     di
    mov     [Cursor - StartAbsolute], di
    pop     es
    pop     di
    pop     bx
    ret

;--------------------------------------

SetupPIC :

; Reprogram the IRQs
; Put them after the Intel-reserved interrupts, at int 0x20-0x2F.
; The bios puts IRQs at 0x08-0x0F, which is used for the internal
; hardware interrupts as well. We just have to reprogram the 8259s.

    push ax
    push bx
    push cx

    xor     eax, eax
    in      al, PIC1_DATA
    mov     [SI_IRQMask_21_RM - StartAbsolute], eax
    call Delay
    xor     eax, eax
    in      al, PIC2_DATA
    mov     [SI_IRQMask_A1_RM - StartAbsolute], eax
    call Delay

    mov     al, ICW1_INIT + ICW1_ICW4
    out     PIC1_CMD, al
    call Delay
    mov     al, ICW1_INIT + ICW1_ICW4
    out     PIC2_CMD, al
    call Delay

    mov     al, PIC1_VECTOR               ; Start of hardware IRQs (0x20)
    out     PIC1_DATA, al
    call Delay
    mov     al, PIC2_VECTOR               ; Start of hardware IRQs (0x28)
    out     PIC2_DATA, al
    call Delay

    mov     al, 0x04                      ; 8259-1 is master
    out     PIC1_DATA, al
    call Delay
    mov     al, 0x02                      ; 8259-2 is slave
    out     PIC2_DATA, al
    call Delay

    mov     al, ICW4_8086                 ; 8086 mode for 8259-1
    out     PIC1_DATA, al
    call Delay
    mov     al, ICW4_8086                 ; 8086 mode for 8259-2
    out     PIC2_DATA, al
    call Delay

    mov     al, 0xFB                   ; Mask off all IRQs for now
    out     PIC1_DATA, al
    call Delay
    mov     al, 0xFF
    out     PIC2_DATA, al
    call Delay

    mov     al, 0x20
    out     0x20, al
    out     0xA0, al

    pop  cx
    pop  bx
    pop  ax
    ret

;----------------------------------------------------------------------------

; This is the start of the PM code
; EBP contains the physical address of the stub

bits 32

Final_GDT :

    dw  (N_8KB - 1)
    dd LA_GDT

Start32 :

    ;--------------------------------------
    ; Adjust segment registers

    mov     ax, SELECTOR_KERNEL_DATA
    mov     ds, ax
    mov     es, ax

    ;--------------------------------------
    ; Get physical memory size

    call    GetMemorySize
    mov     esi, ebp
    add     esi, SI_Memory - StartAbsolute
    mov     [esi], eax

    ;--------------------------------------
    ; Clear system area

    mov     eax, 0xB8000
    mov     byte [eax], 'A'

    mov     edi, PA_SYSTEM
    mov     ecx, N_128KB
    xor     eax, eax
    cld
    rep     stosb

    ;--------------------------------------
    ; Copy GDT to RAM location of GDT

    mov     eax, 0xB8000
    mov     byte [eax], 'B'

    call    SetupGDT

    ;--------------------------------------
    ; Setup page directories and tables

    mov     eax, 0xB8000
    mov     byte [eax], 'C'

    call    SetupPaging

    ;--------------------------------------
    ; Load the page directory
    ; Register cr3 holds the physical address of
    ; the page directory

    mov     eax, 0xB8000
    mov     byte [eax], 'D'

    mov     eax, PA_PGD
    mov     cr3, eax

    ;--------------------------------------
    ; Copy kernel to high memory

    mov     eax, 0xB8000
    mov     byte [eax], 'E'

    call    CopyKernel

    ;--------------------------------------
    ; Enable paging

    mov     eax, 0xB8000
    mov     byte [eax], 'F'

    mov     eax, cr0
    or      eax, CR0_PAGING
    mov     cr0, eax

    ;--------------------------------------
    ; Load the final Global Descriptor Table

    mov     eax, 0xB8000
    mov     byte [eax], 'G'

    mov     eax, ebp
    add     eax, Final_GDT - StartAbsolute
    lgdt    [eax]

    ;--------------------------------------
    ; Jump to kernel

    mov     eax, 0xB8000
    mov     byte [eax], 'H'

    mov     eax, ProtectedModeEntry
    jmp     eax

;--------------------------------------

Hang_Loop :

    jmp     Hang_Loop

;--------------------------------------

SetupGDT :

    mov     esi, ebp
    add     esi, GDT
    mov     edi, PA_GDT
    mov     ecx, 8 * SEGMENT_DESCRIPTOR_SIZE
    cld
    rep     movsb
    ret

;--------------------------------------

SetupPaging :

    ;--------------------------------------
    ; Setup page directory

    mov     edi, PA_PGD

    xor     ebx, ebx
    mov     dword [edi+ebx], (PA_PGL & PAGE_MASK) | PAGE_BIT_SYSTEM

    mov     ebx, (LA_KERNEL >> MUL_4MB) << MUL_4
    mov     dword [edi+ebx], (PA_PGK & PAGE_MASK) | PAGE_BIT_SYSTEM

    mov     ebx, (LA_SYSTEM >> MUL_4MB) << MUL_4
    mov     dword [edi+ebx], (PA_PGH & PAGE_MASK) | PAGE_BIT_SYSTEM

    mov     ebx, (LA_DIRECTORY >> MUL_4MB) << MUL_4
    mov     dword [edi+ebx], (PA_PGS & PAGE_MASK) | PAGE_BIT_SYSTEM

    ;--------------------------------------
    ; Setup identity mapped low memory pages (1 MB)

    mov     edi, PA_PGL
    mov     eax, PA_LOW
    mov     ecx, N_1MB >> MUL_4KB
    call    MapPages

    ;--------------------------------------
    ; Setup identity mapped system memory pages (128 KB)

    mov     edi, PA_PGH
    mov     eax, PA_SYSTEM
    mov     ecx, N_128KB >> MUL_4KB
    call    MapPages

    ;--------------------------------------
    ; Setup kernel memory pages (2 MB)

    mov     edi, PA_PGK
    mov     eax, PA_KERNEL
    mov     ecx, N_2MB >> MUL_4KB
    call    MapPages

    ;--------------------------------------
    ; Setup system page table
    ; Linear   0xFF801000
    ; 1st entry points to page directory
    ; 2nd entry points to system page table
    ; 3rd entry points to page table 0
    ; 4th entry points to page table 1

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

; EDI = Page table base
; EAX = Physical address
; ECX = Count

MapPages :

    mov     ebx, eax

MapPages_Loop :

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

CopyKernel :

    ;--------------------------------------
    ; Copy the kernel code and data without the stub

    mov     esi, ebp
    add     esi, N_4KB
    mov     edi, PA_KERNEL
    mov     ecx, KERNEL_SIZE
    cld
    rep     movsb
    ret

;--------------------------------------

GetMemorySize :

    ;--------------------------------------
    ; Start counting at 4 mb

    mov     esi, N_4MB

GetMemorySize_Loop :

    mov     al, [esi]                  ; Save value
    mov     byte [esi], 0xAA           ; Write AA
    mov     bl, [esi]                  ; Read value to see if cell exists
    mov     [esi], al                  ; Restore value

    cmp     bl, 0xAA
    jne     GetMemorySize_Out

    add     esi, N_4KB
    jmp     GetMemorySize_Loop

GetMemorySize_Out :

    mov     eax, esi

    ret

_GetMemorySizeEnd :

;----------------------------------------------------------------------------

section .data align=16

Text_JmpStart :
    db 'jmp     Start', '$'

;----------------------------------------------------------------------------
