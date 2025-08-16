
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
    Hx32Out SI_Size_SYS
    call    SerialWriteNewLine

    LoadEDI     SI_Phys_PGH
    mov         edi, [edi]
    LoadEAX     SI_Phys_SYS
    mov         eax, [eax]
    LoadECX     SI_Size_SYS                 ; Number of pages
    mov         ecx, [ecx]
    shr         ecx, MUL_4KB
    call        MapPages

    ;--------------------------------------
    ; Setup kernel memory pages (SI_Size_KER + SI_Size_BSS + SI_Size_STK)

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

CopyKernel :

    DbgOut      Text_CopyKernel
    mov         esi, ebp
    LoadEAX     SI_Size_Stub
    add         esi, [eax]
    ImmHx32Out  esi
    call        SerialWriteSpace
    LoadEAX     SI_Phys_KER
    mov         eax, [eax]
    ImmHx32Out  eax
    call        SerialWriteSpace
    Hx32Out     SI_Size_KER
    call        SerialWriteNewLine

    ;--------------------------------------
    ; Copy the kernel code and data excluding the 4K stub

    mov         esi, ebp
    LoadEAX     SI_Size_Stub
    add         esi, [eax]
    LoadEDI     SI_Phys_KER
    mov         edi, [edi]
    LoadECX     SI_Size_KER
    mov         ecx, [ecx]
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
