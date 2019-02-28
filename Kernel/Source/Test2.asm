
; Test2.asm

;----------------------------------------------------------------------------

; Helper values to access function parameters and local variables

PBN equ 0x08                           ; Param base near
PBF equ 0x0A                           ; Param base far
LBN equ 0x04                           ; Local base near
LBF equ 0x04                           ; Local base far

;----------------------------------------------------------------------------

segment .text use32

    global _Main
    global __exoscall
    global _strlen
    global _strcpy
    global _strcat
    global _HelloWorld

;--------------------------------------

_Main :

    push    ebp
    mov     ebp, esp
    push    dword 0x24
    push    dword _HelloWorld
    call    __exoscall
    add     esp, 8
L1: jmp     L1
    pop     ebp
    ret

;--------------------------------------

__exoscall :

    push    ebp
    mov     ebp, esp
    mov     eax, [ebp+(PBN+0)]
    mov     ebx, [ebp+(PBN+4)]
    int     0x80
    pop     ebp
    ret

;--------------------------------------

_strlen :

    push    ebp
    mov     ebp, esp
    mov     esi, [ebp+(PBN+0)]
    mov     edi, esi
_strlen_loop :
    lodsb
    cmp     al, 0
    je      _strlen_out
    inc     esi
    jmp     _strlen_loop
_strlen_out :
    sub     esi, edi
    mov     eax, esi
    pop     ebp
    ret

;--------------------------------------

_strcpy :

    push    ebp
    mov     ebp, esp
    mov     eax, [ebp+(PBN+4)]
    push    eax
    call    _strlen
    add     esp, 4
    inc     eax
    mov     ecx, eax
    mov     edi, [ebp+(PBN+0)]
    mov     esi, [ebp+(PBN+4)]
    cld
    rep     movsb
    mov     eax, [ebp+(PBN+0)]
    pop     ebp
    ret

;--------------------------------------

_strcat :

    push    ebp
    mov     ebp, esp
    mov     eax, [ebp+(PBN+0)]
    push    eax
    call    _strlen
    add     esp, 4
    mov     ebx, [ebp+(PBN+4)]
    push    ebx
    push    eax
    call    _strcpy
    add     esp, 8
    mov     eax, [ebp+(PBN+0)]
    pop     ebp
    ret

;--------------------------------------

_strcmp :

    push    ebp
    mov     ebp, esp
    xor     eax, eax
    pop     ebp
    ret

_HelloWorld :

    db      'Hello world !', 10, 0

;----------------------------------------------------------------------------
