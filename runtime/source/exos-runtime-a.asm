; EXOSRT2.asm

BITS 32

;----------------------------------------------------------------------------

; Helper values to access function parameters and local variables

PBN equ 0x08                           ; Param base near
PBF equ 0x0A                           ; Param base far
LBN equ 0x04                           ; Local base near
LBF equ 0x04                           ; Local base far

;----------------------------------------------------------------------------

extern EXOSMain

;----------------------------------------------------------------------------

section .bss

;----------------------------------------------------------------------------

section .data

    global StartEBP
    global StartESP
    global argc
    global argv

_StartEBP          : dd 0
_StartESP          : dd 0
_argc              : dd 0
_argv              : dd 0
_TaskArgument      : dd 0

;----------------------------------------------------------------------------

section .text

    global __start__
    global __exit__
    global exoscall
    global strlen
    global strcpy
    global strcat
    global strcmp
    global strstr

;--------------------------------------
; Under EXOS, __start__ is the entry point of a task
; Upon entry, the task argument is on the stack
; The first thing to do is to save the stack
; registers in case __exit__ is called

__start__ :

    mov     [_StartEBP], ebp
    mov     [_StartESP], esp

    push    ebp
    mov     ebp, esp

    mov     eax, [ebp+(PBN+0)]
    mov     [_TaskArgument], eax

    call _SetupArguments

    mov     eax, [_argv]
    push    eax
    mov     eax, [_argc]
    push    eax
    call    EXOSMain
    add     esp, 8

    pop     ebp
    ret

;--------------------------------------
; This function is used to abort an application
; We just assign EBP and ESP with the values
; they had in __start__ and do a return
; We also store the return code in EAX

__exit__ :

    push    ebp
    mov     ebp, esp

    mov     eax, [ebp+(PBN+0)]

    mov     ebp, [_StartEBP]
    mov     esp, [_StartESP]

    ret

;--------------------------------------

_SetupArguments :

    push    ebp
    mov     ebp, esp

    xor     eax, eax
    mov     [_argc], eax
    mov     [_argv], eax

    pop     ebp
    ret

;--------------------------------------
; This is the call to EXOS base services
; We setup arguments and call interrupt 0x80
; Function number is in EAX
; Argument is in EBX

exoscall :

    push    ebp
    mov     ebp, esp

    mov     eax, [ebp+(PBN+0)]
    mov     ebx, [ebp+(PBN+4)]
    int     0x80

    pop     ebp
    ret

;--------------------------------------

strlen :

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

strcpy :

    push    ebp
    mov     ebp, esp
    mov     eax, [ebp+(PBN+4)]
    push    eax
    call    strlen
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

strcat :

    push    ebp
    mov     ebp, esp
    mov     eax, [ebp+(PBN+0)]
    push    eax
    call    strlen
    add     esp, 4
    mov     ebx, [ebp+(PBN+4)]
    push    ebx
    push    eax
    call    strcpy
    add     esp, 8
    mov     eax, [ebp+(PBN+0)]
    pop     ebp
    ret

;--------------------------------------

strcmp :

    push    ebp
    mov     ebp, esp

    mov     esi, [ebp+(PBN+0)]  ; str1
    mov     edi, [ebp+(PBN+4)]  ; str2

.loop:
    mov     al, [esi]        ; char1
    mov     bl, [edi]        ; char2
    cmp     al, bl
    jne     .diff
    test    al, al           ; end of string ?
    je      .equal
    inc     esi
    inc     edi
    jmp     .loop

.diff:
    movzx   eax, al
    movzx   ebx, bl
    sub     eax, ebx
    jmp     .end

.equal:
    xor     eax, eax         ; return 0

.end:
    pop     ebp
    ret

;----------------------------------------------------------------------------

strstr:

    push    ebp
    mov     ebp, esp

    mov     esi, [ebp+(PBN+0)] ; esi = haystack pointer
    mov     edi, [ebp+(PBN+4)] ; edi = needle pointer

    mov     al, [edi]        ; check if needle is empty
    test    al, al
    jz      .needle_empty    ; if needle == "", return haystack

.loop_haystack:
    mov     ebx, esi         ; ebx = current haystack position
    mov     ecx, edi         ; ecx = current needle position

.loop_compare:
    mov     al, [ecx]        ; load current char of needle
    test    al, al
    jz      .found           ; end of needle: match found
    mov     dl, [ebx]        ; load current char of haystack
    test    dl, dl
    jz      .not_found       ; end of haystack, no match
    cmp     al, dl
    jne     .no_match        ; mismatch, try next position
    inc     ebx
    inc     ecx
    jmp     .loop_compare    ; continue comparing next chars

.no_match:
    inc     esi
    mov     al, [esi]
    test    al, al
    jz      .not_found       ; haystack ended, not found
    jmp     .loop_haystack   ; try next haystack position

.needle_empty:
    mov     eax, esi         ; needle is empty: return haystack
    pop     ebp
    ret

.found:
    mov     eax, esi         ; return pointer to match in haystack
    pop     ebp
    ret

.not_found:
    xor     eax, eax         ; not found: return 0
    pop     ebp
    ret
