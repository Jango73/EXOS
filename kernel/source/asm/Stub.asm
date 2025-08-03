
; Stub.asm

;----------------------------------------------------------------------------
;  EXOS
;  Copyright (c) 1999-2025 Jango73
;  All rights reserved
;----------------------------------------------------------------------------

%include "./Kernel.inc"

;----------------------------------------------------------------------------

DOS_CALL      equ 0x21
DOS_PRINT     equ 0x09

;----------------------------------------------------------------------------

; Segment virtual base   : 0
; Segment virtual offset : 0

section .text.stub
bits 16

    global StartAbsolute
    global Cursor
    global SI_IRQMask_21_RM
    global SI_IRQMask_A1_RM
    global GDT
    extern Set_A20_Line
    extern PrintChar
    extern SetupPIC
    extern Delay
    extern SetupGDT
    extern SetupPaging
    extern CopyKernel
    extern GetMemorySize

;--------------------------------------

StartAbsolute :

    jmp     Start

;--------------------------------------

; Here we store some startup info for the kernel
; It must start at stub + KERNEL_STARTUP_INFO_OFFSET

times (KERNEL_STARTUP_INFO_OFFSET - ($ - StartAbsolute)) db 0

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

;----------------------------------------------------------------------------
;----------------------------------------------------------------------------
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
    mov     ecx, SYSTEM_SIZE
    xor     eax, eax
    cld
    rep     stosb

    ;--------------------------------------
    ; Copy GDT to physical address of GDT

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
section .text
bits 32

    global ProtectedModeEntry

ProtectedModeEntry :

    cli

    ;--------------------------------------

    mov     eax, 0xB8000
    mov     byte [eax], 'I'

    ;--------------------------------------
    ; DS, ES and GS are used to access the kernel's data

    mov     ax,  SELECTOR_KERNEL_DATA
    mov     ds,  ax
    mov     es,  ax
    mov     gs,  ax

    ;--------------------------------------
    ; FS is used to communicate with user

    mov     ax,  SELECTOR_KERNEL_DATA
    mov     fs,  ax

    ;--------------------------------------
    ; Save the base address of the stub

    mov     [StubAddress], ebp

    ;--------------------------------------

    mov     eax, 0xB8000
    mov     byte [eax], 'J'

    ;--------------------------------------
    ; Setup the kernel's stack

    mov     ax,  SELECTOR_KERNEL_DATA
    mov     ss,  ax

    mov     esp, KernelStack           ; Start of kernel stack
    add     esp, STK_SIZE              ; Minimum stack size
    mov     ebp, esp

    ;--------------------------------------
    ; Setup local descriptor register

    xor     ax, ax
    lldt    ax

    ;--------------------------------------
    ; Clear registers

    xor     eax, eax
    xor     ebx, ebx
    xor     ecx, ecx
    xor     edx, edx
    xor     esi, esi
    xor     edi, edi

    ;--------------------------------------

    mov     eax, 0xB8000
    mov     byte [eax], 'K'

    ;--------------------------------------
    ; Jump to main kernel routine in C

    jmp     KernelMain

    ;--------------------------------------

_ProtectedModeEntry_Hang :

    jmp     $

;----------------------------------------------------------------------------

section .stack

    global KernelStack

KernelStack:

    times STK_SIZE db 0
