;-------------------------------------------------------------------------
;  EXOS
;  Copyright (c) 1999-2025 Jango73
;  All rights reserved
;-------------------------------------------------------------------------

%include "./Kernel.inc"
%include "./Serial.inc"

;-------------------------------------------------------------------------
; Macros

%idefine Offset(X) ((X) - StartAbsolute)

%macro LoadEAX 1
    mov     eax, ebp
    add     eax, %1 - StartAbsolute
%endmacro

%macro LoadEBX 1
    mov     ebx, ebp
    add     ebx, %1 - StartAbsolute
%endmacro

%macro LoadECX 1
    mov     ecx, ebp
    add     ecx, %1 - StartAbsolute
%endmacro

%macro LoadEDX 1
    mov     edx, ebp
    add     edx, %1 - StartAbsolute
%endmacro

%macro LoadESI 1
    mov     esi, ebp
    add     esi, %1 - StartAbsolute
%endmacro

%macro LoadEDI 1
    mov     edi, ebp
    add     edi, %1 - StartAbsolute
%endmacro

;-------------------------------------------------------------------------

extern EXOS_Start
extern EXOS_End

;-------------------------------------------------------------------------

; Segment virtual base   : 0
; Segment virtual offset : 0

section .text.stub
bits 16

    global StartAbsolute
    global Start32

;--------------------------------------

StartAbsolute :

    jmp     Start

times (4 - ($ - StartAbsolute)) db 0

Magic :

    db 'EXOS'

;--------------------------------------

; Here we store some startup info for the kernel
; It must start at stub + KERNEL_STARTUP_INFO_OFFSET

times (KERNEL_STARTUP_INFO_OFFSET - ($ - StartAbsolute)) db 0

SI_Loader_SS        dd 0
SI_Loader_SP        dd 0
SI_IRQMask_21_RM    dd 0
SI_IRQMask_A1_RM    dd 0

SI_Console_Width    dd 0
SI_Console_Height   dd 0
SI_Console_CX       dd 0
SI_Console_CY       dd 0

SI_Memory           dd 0 ; Total memory size
SI_PageCount        dd 0 ; Total number of pages

SI_Size_LOW         dd N_1MB ; Low Memory Area Size
SI_Size_HMA         dd N_64KB ; High Memory Area Size
SI_Size_IDT         dd IDT_SIZE ; Interrupt Descriptor Table Size
SI_Size_GDT         dd GDT_SIZE ; Kernel Global Descriptor Table Size
SI_Size_PGD         dd PAGE_TABLE_SIZE ; Kernel Page Directory Size
SI_Size_PGS         dd PAGE_TABLE_SIZE ; System Page Table Size
SI_Size_PGK         dd PAGE_TABLE_SIZE ; Kernel Page Table Size
SI_Size_PGL         dd PAGE_TABLE_SIZE ; Low Memory Page Table size
SI_Size_PGH         dd PAGE_TABLE_SIZE ; High Memory Page Table size
SI_Size_TSS         dd N_32KB ; Task State Segment Size
SI_Size_PPB         dd N_128KB ; Physical Page Bitmap Size
SI_Size_KER         dd 0 ; Kernel image size aligned 4K
SI_Size_BSS         dd N_4KB ; Kernel BSS Size
SI_Size_STK         dd N_32KB ; Kernel Stack Size
SI_Size_SYS         dd 0; Sum of IDT to STK

SI_Phys_LOW         dd 0
SI_Phys_HMA         dd 0 ; Physical address of High Memory Area
SI_Phys_IDT         dd 0 ; Physical address of Interrupt Descriptor Table
SI_Phys_GDT         dd 0 ; Physical address of Kernel Global Descriptor Table
SI_Phys_PGD         dd 0 ; Physical address of Kernel Page Directory
SI_Phys_PGS         dd 0 ; Physical address of System Page Table
SI_Phys_PGK         dd 0 ; Physical address of Kernel Page Table
SI_Phys_PGL         dd 0 ; Physical address of Low Memory Page Table
SI_Phys_PGH         dd 0 ; Physical address of High Memory Page Table
SI_Phys_TSS         dd 0 ; Physical address of Task State Segment
SI_Phys_PPB         dd 0 ; Physical address of Physical Page Bitmap
SI_Phys_KER         dd 0 ; Physical address of Kernel
SI_Phys_BSS         dd 0 ; Physical address of BSS
SI_Phys_STK         dd 0 ; Physical address of Kernel Stack
SI_Phys_SYS         dd 0 ; -> IDT

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

Text_StubStart : db 'EXOS 16 bit stub starting...', 10, 13, 0

;--------------------------------------

Start :

    mov         si, Text_StubStart
    call        PrintString16

    ;--------------------------------------
    ; Get loader's stack

    xor         esi, esi
    xor         edi, edi

    mov         si, ss
    mov         di, sp

    ;--------------------------------------
    ; Adjust segments

    cli
    mov         ax, cs
    mov         ds, ax
    mov         ss, ax
    mov         es, ax
    sti

    mov         ax, N_4KB - 16
    mov         sp, ax

    ;--------------------------------------
    ; Store loader's stack

    mov         [Offset(SI_Loader_SS)], esi
    mov         [Offset(SI_Loader_SP)], edi

    call        ComputeAddresses

    ;--------------------------------------
    ; Get 32-bit address of code

    xor         eax, eax
    mov         ax, cs
    shl         eax, MUL_16
    mov         ebp, eax

    ;--------------------------------------
    ; Get some information from the BIOS

    mov         ah, 0x03
    mov         bh, 0x00
    int         0x10

    xor         eax, eax
    mov         al, dl
    mov         [Offset(SI_Console_CX)], eax

    xor         eax, eax
    mov         al, dh
    mov         [Offset(SI_Console_CY)], eax

    ;--------------------------------------
    ; Disable interrupts

    cli

    ;--------------------------------------
    ; Load Interrupt Descriptor Table
    ; It has a limit of 0 to force shutdown if a
    ; non-maskable interrupt occurs

    mov     al, 'b'
    call    PrintChar

    lidt    [Offset(IDT_Label)]

    ;--------------------------------------
    ; Load Global Descriptor Table

    mov     al, 'c'
    call    PrintChar

    mov     edi, Offset(GDT_Label)
    add     edi, 2
    add     [edi], ebp

    lgdt    [Offset(GDT_Label)]

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

    add     [Offset(Start32_Entry)], ebp

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

    jmp     far dword [Offset(Start32_Entry)]

;--------------------------------------

ComputeAddresses:

    mov         eax, EXOS_End
    sub         eax, EXOS_Start
    add         eax, N_4KB                      ; Add stub size
    add         eax, 0xFFF                      ; Align to 4K
    and         eax, 0xFFFFF000
    mov         [Offset(SI_Size_KER)], eax

    mov         eax, [Offset(SI_Phys_LOW)]      ; Start at physical address 0

    add         eax, [Offset(SI_Size_LOW)]      ; HMA
    mov         [Offset(SI_Phys_HMA)], eax

    add         eax, [Offset(SI_Size_HMA)]      ; Interrupt Descriptor Table
    mov         [Offset(SI_Phys_IDT)], eax
    mov         [Offset(SI_Phys_SYS)], eax      ; System starts here

    add         eax, [Offset(SI_Size_IDT)]      ; Kernel Global Descriptor Table
    mov         [Offset(SI_Phys_GDT)], eax

    add         eax, [Offset(SI_Size_GDT)]      ; Kernel Page Directory
    mov         [Offset(SI_Phys_PGD)], eax

    add         eax, [Offset(SI_Size_PGD)]      ; System Page Table
    mov         [Offset(SI_Phys_PGS)], eax

    add         eax, [Offset(SI_Size_PGS)]      ; Kernel Page Table
    mov         [Offset(SI_Phys_PGK)], eax

    add         eax, [Offset(SI_Size_PGK)]      ; "Conventional" Memory Page Table
    mov         [Offset(SI_Phys_PGL)], eax

    add         eax, [Offset(SI_Size_PGL)]      ; HMA Table
    mov         [Offset(SI_Phys_PGH)], eax

    add         eax, [Offset(SI_Size_PGH)]      ; Task State Segment
    mov         [Offset(SI_Phys_TSS)], eax

    add         eax, [Offset(SI_Size_TSS)]      ; Physical Page Bitmap (track allocated pages)
    mov         [Offset(SI_Phys_PPB)], eax

    add         eax, [Offset(SI_Size_PPB)]      ; Kernel code and data
    mov         [Offset(SI_Phys_KER)], eax

    add         eax, [Offset(SI_Size_KER)]      ; Kernel bss
    mov         [Offset(SI_Phys_BSS)], eax

    add         eax, [Offset(SI_Size_BSS)]      ; Kernel stack
    mov         [Offset(SI_Phys_STK)], eax

    sub         eax, [Offset(SI_Size_LOW)]
    sub         eax, [Offset(SI_Size_HMA)]
    mov         [Offset(SI_Size_SYS)], eax

    ret

;--------------------------------------

PrintString16:
    lodsb
    or          al, al
    jz          .done
    mov         ah, 0x0E
    int         0x10
    jmp         PrintString16
.done:
    ret

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

;-------------------------------------------------------------------------

Delay :

    dw      0x00EB                     ; jmp $+2
    dw      0x00EB                     ; jmp $+2
    ret

;-------------------------------------------------------------------------

PrintChar :

    push    bx
    push    di
    push    es
    mov     bx, 0xB800
    mov     es, bx
    mov     di, [Offset(Cursor)]
    mov     [es:di], al
    inc     di
    inc     di
    mov     [Offset(Cursor)], di
    pop     es
    pop     di
    pop     bx
    ret

;-------------------------------------------------------------------------

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
    mov     [Offset(SI_IRQMask_21_RM)], eax
    call Delay
    xor     eax, eax
    in      al, PIC2_DATA
    mov     [Offset(SI_IRQMask_A1_RM)], eax
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

;-------------------------------------------------------------------------
;-------------------------------------------------------------------------
;-------------------------------------------------------------------------

; This is the start of the protected mode code
; EBP contains the physical address of the stub
; To address data in the stub, we now have to use ebp + (<address of var> - StartAbsolute)
; (<address of var> - StartAbsolute) yields a relative address

bits 32

Final_GDT :

    dw  (N_8KB - 1)
    dd LA_GDT

Text_StubStart32 :
    db '[STUB] Stub 32 bits starting up', 10, 13, 0

Text_GetMemorySize :
    db '[STUB] Checking memory size', 10, 13, 0

Text_EBP :
    db '[STUB] EBP : ', 0

Text_Memory :
    db '[STUB] Memory size : ', 0

Text_FinalGDT :
    db '[STUB] Final GDT : ', 0

Text_PA_IDT :
    db '[STUB] PA (Physical address) of Interrupt Descriptor Table : ', 0

Text_PA_GDT :
    db '[STUB] PA of Kernel Global Descriptor : ', 0

Text_PA_PGD :
    db '[STUB] PA of Kernel Page Directory : ', 0

Text_PA_PGS :
    db '[STUB] PA of System Page Table : ', 0

Text_PA_PGK :
    db '[STUB] PA of Kernel Page Table : ', 0

Text_PA_PGL :
    db '[STUB] PA of Low Memory Page Table : ', 0

Text_PA_PGH :
    db '[STUB] PA of High Memory Page Table : ', 0

Text_PA_TSS :
    db '[STUB] PA of Task State Segment : ', 0

Text_PA_PPB :
    db '[STUB] PA of Physical Page Bitmap : ', 0

Text_PA_KER :
    db '[STUB] PA of Kernel code and data : ', 0

Text_PA_BSS :
    db '[STUB] PA of Kernel bss : ', 0

Text_PA_STK :
    db '[STUB] PA of Kernel stack : ', 0

Text_Size_KER :
    db '[STUB] Kernel size : ', 0

Text_Size_SYS :
    db '[STUB] System size : ', 0

Text_SetupGDT :
    db '[STUB] Setting up GDT', 10, 13, 0

Text_MappingLowMemory :
    db '[STUB] Mapping low memory', 0

Text_MappingKernel :
    db '[STUB] Mapping kernel', 0

Text_MappingSystem :
    db '[STUB] Mapping system', 0

Text_MappingDirectory :
    db '[STUB] Mapping directory', 0

Text_PageDirectoryEntry :
    db ', writing Page Directory Entry (Address, Data) : ', 0

Text_StartOfStack :
    db '[STUB] Kernel stack (Address, Data) : ', 0

Text_SetupPaging :
    db '[STUB] Setting up paging', 10, 13, 0

Text_MapPages :
    db '[STUB] Mapping pages (Page address, Physical address, Size) : ', 0

Text_CopyKernel :
    db '[STUB] Copying kernel (without 4Kb stub) to high memory (From, To, Size) ', 0

Text_EnablePaging :
    db '[STUB] Enabling paging', 10, 13, 0

Text_LoadFinalGDT :
    db '[STUB] Loading final GDT', 10, 13, 0

Text_JumpToKernel :
    db '[STUB] Jumping to C kernel', 10, 13, 0

Start32 :

    call    SerialInit
    mov     esi, Text_StubStart32
    call    SerialWriteString

    ;--------------------------------------
    ; Adjust segment registers

    mov     ax, SELECTOR_KERNEL_DATA
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax

    ;--------------------------------------
    ; Get physical memory size and store in

    call    GetMemorySize

    LoadESI SI_Memory
    mov     [esi], eax

    shr     eax, PAGE_SIZE_MUL

    LoadESI SI_PageCount
    mov     [esi], eax

    DbgOut  Text_EBP
    ImmHx32Out ebp
    call    SerialWriteNewLine

    DbgOut  Text_Memory
    Hx32Out SI_Memory
    call    SerialWriteNewLine

    DbgOut  Text_FinalGDT
    mov     esi, Final_GDT
    Hx32Out (Final_GDT + 2)
    call    SerialWriteNewLine

    DbgOut  Text_PA_IDT
    Hx32Out SI_Phys_IDT
    call    SerialWriteNewLine

    DbgOut  Text_PA_GDT
    Hx32Out SI_Phys_GDT
    call    SerialWriteNewLine

    DbgOut  Text_PA_PGD
    Hx32Out SI_Phys_PGD
    call    SerialWriteNewLine

    DbgOut  Text_PA_PGS
    Hx32Out SI_Phys_PGS
    call    SerialWriteNewLine

    DbgOut  Text_PA_PGK
    Hx32Out SI_Phys_PGK
    call    SerialWriteNewLine

    DbgOut  Text_PA_PGL
    Hx32Out SI_Phys_PGL
    call    SerialWriteNewLine

    DbgOut  Text_PA_PGH
    Hx32Out SI_Phys_PGH
    call    SerialWriteNewLine

    DbgOut  Text_PA_TSS
    Hx32Out SI_Phys_TSS
    call    SerialWriteNewLine

    DbgOut  Text_PA_PPB
    Hx32Out SI_Phys_PPB
    call    SerialWriteNewLine

    DbgOut  Text_PA_KER
    Hx32Out SI_Phys_KER
    call    SerialWriteNewLine

    DbgOut  Text_PA_BSS
    Hx32Out SI_Phys_BSS
    call    SerialWriteNewLine

    DbgOut  Text_PA_STK
    Hx32Out SI_Phys_STK
    call    SerialWriteNewLine

    DbgOut  Text_Size_KER
    Hx32Out SI_Size_KER
    call    SerialWriteNewLine

    DbgOut  Text_Size_SYS
    Hx32Out SI_Size_SYS
    call    SerialWriteNewLine

    ;--------------------------------------
    ; Clear system area where reside :
    ;   Kernel Global Descriptor
    ;   Kernel Page Directory
    ;   System Page Table
    ;   Kernel Page Table
    ;   Low Memory Page
    ;   High Memory Page
    ;   Task State Segment
    ;   Physical Page Bitmap
    ;   Kernel code and data
    ;   Kernel bss and stack

    LoadEDI     SI_Phys_IDT
    mov         edi, [edi]
    LoadECX     SI_Size_SYS
    mov         ecx, [ecx]
    xor         eax, eax
    cld
    rep         stosb

    ;--------------------------------------
    ; Copy GDT to physical address of GDT

    call        SetupGDT

    ;--------------------------------------
    ; Setup page directories and tables

    call        SetupPaging

    ;--------------------------------------
    ; Load the page directory
    ; Register cr3 holds the physical address of
    ; the page directory

    LoadEAX     SI_Phys_PGD
    mov         eax, [eax]
    mov         cr3, eax

    ;--------------------------------------
    ; Copy kernel to high memory

    call        CopyKernel

    ;--------------------------------------
    ; Enable paging

    DbgOut  Text_EnablePaging

    mov     eax, cr0
    or      eax, CR0_PAGING
    mov     cr0, eax

    ;--------------------------------------
    ; Load the final Global Descriptor Table

    DbgOut  Text_LoadFinalGDT

    mov     eax, ebp
    add     eax, Offset(Final_GDT)
    lgdt    [eax]

    ;--------------------------------------
    ; Jump to paged protected mode code

    mov     eax, ProtectedModeEntry
    jmp     eax

;-------------------------------------------------------------------------

Hang_Loop :

    cli
    hlt
    jmp     Hang_Loop

;-------------------------------------------------------------------------

SetupGDT :

    DbgOut  Text_SetupGDT

    mov         esi, ebp
    add         esi, GDT
    LoadEDI     SI_Phys_GDT
    mov         edi, [edi]
    mov         ecx, 8 * SEGMENT_DESCRIPTOR_SIZE
    cld
    rep         movsb
    ret

;-------------------------------------------------------------------------

SetupPaging :

    DbgOut  Text_SetupPaging

    ;--------------------------------------
    ; Setup page directory

    LoadEDI     SI_Phys_PGD
    mov         edi, [edi]

    xor         ebx, ebx
    LoadEAX     SI_Phys_PGL
    mov         eax, [eax]
    and         eax, PAGE_MASK
    or          eax, PAGE_BIT_SYSTEM
    mov         dword [edi+ebx], eax

    mov         ebx, (LA_KERNEL >> MUL_4MB) << MUL_4
    LoadEAX     SI_Phys_PGK
    mov         eax, [eax]
    and         eax, PAGE_MASK
    or          eax, PAGE_BIT_SYSTEM
    mov         dword [edi+ebx], eax

    mov         ebx, (LA_SYSTEM >> MUL_4MB) << MUL_4
    LoadEAX     SI_Phys_PGH
    mov         eax, [eax]
    and         eax, PAGE_MASK
    or          eax, PAGE_BIT_SYSTEM
    mov         dword [edi+ebx], eax

    mov         ebx, (LA_DIRECTORY >> MUL_4MB) << MUL_4
    LoadEAX     SI_Phys_PGS
    mov         eax, [eax]
    and         eax, PAGE_MASK
    or          eax, PAGE_BIT_SYSTEM
    mov         dword [edi+ebx], eax

    ;--------------------------------------
    ; Setup identity mapped low memory pages (1 MB)

    DbgOut  Text_MapPages
    Hx32Out SI_Phys_PGL
    call    SerialWriteSpace
    Hx32Out SI_Phys_LOW
    call    SerialWriteSpace
    ImmHx32Out N_1MB
    call    SerialWriteNewLine

    LoadEDI     SI_Phys_PGL
    mov         edi, [edi]
    LoadEAX     SI_Phys_LOW
    mov         eax, [eax]
    mov         ecx, N_1MB >> MUL_4KB
    call        MapPages

    ;--------------------------------------
    ; Setup identity mapped system memory pages (128 KB)

    DbgOut  Text_MapPages
    Hx32Out SI_Phys_PGH
    call    SerialWriteSpace
    Hx32Out SI_Phys_SYS
    call    SerialWriteSpace
    ImmHx32Out N_128KB
    call    SerialWriteNewLine

    LoadEDI     SI_Phys_PGH
    mov         edi, [edi]
    LoadEAX     SI_Phys_SYS
    mov         eax, [eax]
    mov         ecx, N_128KB >> MUL_4KB     ; Number of pages
    call        MapPages

    ;--------------------------------------
    ; Setup kernel memory pages (SI_Size_KER + SI_Size_BSS + SI_Size_STK + N_4KB bytes)

    DbgOut      Text_MapPages
    Hx32Out     SI_Phys_PGK
    call        SerialWriteSpace
    Hx32Out     SI_Phys_KER
    call        SerialWriteSpace

    LoadECX     SI_Size_KER
    mov         ecx, [ecx]
    LoadEDX     SI_Size_BSS
    add         ecx, [edx]
    LoadEDX     SI_Size_STK
    add         ecx, [edx]
    ImmHx32Out  ecx
    call        SerialWriteNewLine

    LoadEDI     SI_Phys_PGK
    mov         edi, [edi]
    LoadEAX     SI_Phys_KER
    mov         eax, [eax]
    LoadECX     SI_Size_KER
    mov         ecx, [ecx]
    LoadEDX     SI_Size_BSS
    mov         edx, [edx]
    add         ecx, edx
    LoadEDX     SI_Size_STK
    mov         edx, [edx]
    add         ecx, edx

    shr         ecx, MUL_4KB                ; Number of pages

    call        MapPages

    ;--------------------------------------
    ; Setup system page table
    ; Linear   0xFF801000
    ; 1st entry points to page directory
    ; 2nd entry points to system page table
    ; 3rd entry points to page table 0
    ; 4th entry points to page table 1

    LoadEDI     SI_Phys_PGS
    mov         edi, [edi]

    LoadEAX     SI_Phys_PGD
    mov         eax, [eax]
    and         eax, PAGE_MASK
    or          eax, PAGE_BIT_SYSTEM
    mov         dword [edi], eax

    add         edi, 4

    LoadEAX     SI_Phys_PGS
    mov         eax, [eax]
    and         eax, PAGE_MASK
    or          eax, PAGE_BIT_SYSTEM
    mov         dword [edi], eax

    add         edi, 4

    LoadEAX     SI_Phys_PGL
    mov         eax, [eax]
    and         eax, PAGE_MASK
    or          eax, PAGE_BIT_SYSTEM
    mov         dword [edi], eax

    LoadEDI     SI_Phys_PGS
    mov         edi, [edi]
    add         edi, 8 + ((LA_KERNEL >> MUL_4MB) << MUL_4)
    LoadEAX     SI_Phys_PGK
    mov         eax, [eax]
    and         eax, PAGE_MASK
    or          eax, PAGE_BIT_SYSTEM
    mov         dword [edi], eax

    ret

;-------------------------------------------------------------------------
; Maps a virtual address range to a physical address range
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

;-------------------------------------------------------------------------

CopyKernel :

    DbgOut      Text_CopyKernel
    mov         esi, ebp
    add         esi, N_4KB
    ImmHx32Out  esi
    call        SerialWriteSpace
    LoadEAX     SI_Phys_KER
    mov         eax, [eax]
    ImmHx32Out  eax
    call        SerialWriteSpace
    Hx32Out     SI_Size_KER
    call        SerialWriteNewLine

    ;--------------------------------------
    ; Copy the kernel code and data without the 4K stub

    mov         esi, ebp
    add         esi, N_4KB
    LoadEDI     SI_Phys_KER
    mov         edi, [edi]
    LoadECX     SI_Size_KER
    mov         ecx, [ecx]
    sub         ecx, N_4KB
    cld
    rep         movsb
    ret

;-------------------------------------------------------------------------

GetMemorySize :

    DbgOut      Text_GetMemorySize

    ;--------------------------------------
    ; Start checking at 4 mb

    mov         esi, N_4MB

GetMemorySize_Loop :

    mov         cl, 0xAA
    mov         al, [esi]                  ; Save value
    mov         byte [esi], cl             ; Write AA
    mov         bl, [esi]                  ; Read value to see if cell exists
    mov         [esi], al                  ; Restore value

    cmp         bl, cl
    jne         GetMemorySize_Out

    add         esi, N_4KB
    jmp         GetMemorySize_Loop

GetMemorySize_Out :

    mov         eax, esi

    ret

_GetMemorySizeEnd :

;--------------------------------------

section .text
bits 32

    global ProtectedModeEntry

ProtectedModeEntry :

    cli

    ;--------------------------------------
    ; DS, ES and GS are used to access the kernel's data

    mov         ax,  SELECTOR_KERNEL_DATA
    mov         ds,  ax
    mov         es,  ax
    mov         fs,  ax
    mov         gs,  ax

    ;--------------------------------------
    ; Save the base address of the stub

    mov         [StubAddress], ebp

    ;--------------------------------------
    ; Setup the kernel's stack

    mov         ax,  SELECTOR_KERNEL_DATA
    mov         ss,  ax

    ; Compute virtual stack address = LA_KERNEL + (KER_SIZE + BSS_SIZE)
    mov         eax, LA_KERNEL
    LoadEDX     SI_Size_KER
    add         eax, [edx]
    LoadEDX     SI_Size_BSS
    add         eax, [edx]

    mov         esp, eax                    ; Start of kernel stack
    add         esp, STK_SIZE               ; Minimum stack size
    mov         ebp, esp

    ;--------------------------------------
    ; Setup local descriptor register

    xor         ax, ax
    lldt        ax

    ;--------------------------------------
    ; Clear registers

    xor         eax, eax
    xor         ebx, ebx
    xor         ecx, ecx
    xor         edx, edx
    xor         esi, esi
    xor         edi, edi

    ;--------------------------------------
    ; Jump to main kernel routine in C

    jmp         KernelMain

    ;--------------------------------------

_ProtectedModeEntry_Hang :

    cli
    hlt
    jmp         $

;----------------------------------------------------------------------------
