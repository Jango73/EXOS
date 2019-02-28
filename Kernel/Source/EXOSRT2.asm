
; EXOSRT2.asm

;----------------------------------------------------------------------------

; Helper values to access function parameters and local variables

PBN equ 0x08                           ; Param base near
PBF equ 0x0A                           ; Param base far
LBN equ 0x04                           ; Local base near
LBF equ 0x04                           ; Local base far

;----------------------------------------------------------------------------

extern _EXOSMain

;----------------------------------------------------------------------------

segment .bss use32

;----------------------------------------------------------------------------

segment .data use32

    global _StartEBP
    global _StartESP
    global _argc
    global _argv

_StartEBP          : dd 0
_StartESP          : dd 0
_argc              : dd 0
_argv              : dd 0
_TaskArgument      : dd 0

;----------------------------------------------------------------------------

segment .text use32

    global __start__
    global __exit__
    global __exoscall
    global _strlen
    global _strcpy
    global _strcat

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

    call    _SetupArguments

    mov     eax, [_argv]
    push    eax
    mov     eax, [_argc]
    push    eax
    call    _EXOSMain
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

    mov     eax, 0
	mov     [_argc], eax
	mov     [_argv], eax

    pop     ebp
    ret

;--------------------------------------
; This is the call to EXOS base services
; We setup arguments and call interrupt 0x80
; Function number is in EAX
; Argument is in EBX

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

;----------------------------------------------------------------------------
