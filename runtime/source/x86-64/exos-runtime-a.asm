;-------------------------------------------------------------------------
;
;   EXOS Runtime (x86-64)
;
;   64-bit entry points and basic memory/string routines used by the
;   kernel and user-space runtime.  These routines follow the System V
;   AMD64 calling convention to remain compatible with the rest of the
;   kernel which is compiled with GCC in freestanding mode.
;
;-------------------------------------------------------------------------

BITS 64

;----------------------------------------------------------------------------
; System call numbers used by the user-space runtime.
;
; These values match the kernel's SYSCALL_* definitions (see User.h) and are
; kept local to the assembly to avoid pulling additional headers.

%define SYSCALL_EXIT   0x33
%define SYSCALL_SLEEP  0x0F

;----------------------------------------------------------------------------
; Runtime symbols (only required outside of the kernel build)

%ifndef __KERNEL__
extern  exosmain
extern  _argc
extern  _argv
extern  _SetupArguments
%endif

;----------------------------------------------------------------------------

section .data

%ifndef __KERNEL__
    global  StartEBP
    global  StartESP

StartEBP        : dq 0
StartESP        : dq 0
_TaskArgument   : dq 0
%endif

;----------------------------------------------------------------------------

section .text

%ifndef __KERNEL__
    global  __start__
    global  __exit__
    global  exoscall
%endif
    global  memset
    global  memcpy
    global  memcmp
    global  memmove
    global  strlen
    global  strcpy
    global  strncpy
    global  strcat
    global  strcmp
    global  strncmp
    global  strstr
    global  strchr

;-------------------------------------------------------------------------
; __start__ : entry point for executables

%ifndef __KERNEL__
__start__:
    mov     [StartEBP], rbp
    mov     [StartESP], rsp

    push    rbp
    mov     rbp, rsp

    mov     [_TaskArgument], rdi

    call    _SetupArguments

    mov     edi, [_argc]
    mov     rsi, [_argv]
    call    exosmain

    mov     edi, eax
    call    __exit__

    ud2

;-------------------------------------------------------------------------
; __exit__ : request task termination and yield until scheduler cleans up

__exit__:
    mov     esi, edi
    mov     edi, SYSCALL_EXIT
    syscall

.wait_for_scheduler:
    mov     edi, SYSCALL_SLEEP
    mov     esi, 0xFFFFFFFF
    syscall
    jmp     .wait_for_scheduler

;-------------------------------------------------------------------------
; exoscall : bridge to EXOS system services

exoscall:
    syscall
    ret
%endif ; __KERNEL__

;-------------------------------------------------------------------------
; memset

memset:
    mov     r8, rdi
    mov     rcx, rdx
    test    rcx, rcx
    je      .done
    mov     al, sil
    rep     stosb
.done:
    mov     rax, r8
    ret

;-------------------------------------------------------------------------
; memcpy

memcpy:
    mov     rax, rdi
    mov     rcx, rdx
    rep     movsb
    ret

;-------------------------------------------------------------------------
; memcmp

memcmp:
    mov     rcx, rdx
    test    rcx, rcx
    je      .equal
.loop:
    mov     al, [rdi]
    mov     dl, [rsi]
    cmp     al, dl
    jne     .diff
    inc     rdi
    inc     rsi
    dec     rcx
    jne     .loop
.equal:
    xor     eax, eax
    ret
.diff:
    movzx   eax, byte [rdi]
    movzx   edx, byte [rsi]
    sub     eax, edx
    ret

;-------------------------------------------------------------------------
; memmove

memmove:
    mov     r8, rdi
    mov     rcx, rdx
    test    rcx, rcx
    je      .done
    cmp     rdi, rsi
    je      .done
    jb      .forward
    lea     r9, [rsi + rcx]
    cmp     rdi, r9
    jae     .forward
    lea     rsi, [rsi + rcx - 1]
    lea     rdi, [rdi + rcx - 1]
    std
    rep     movsb
    cld
    jmp     .finish
.forward:
    cld
    rep     movsb
.finish:
    cld
.done:
    mov     rax, r8
    ret

;-------------------------------------------------------------------------
; strlen

strlen:
    mov     r8, rdi
.loop:
    mov     al, [rdi]
    test    al, al
    je      .found
    inc     rdi
    jmp     .loop
.found:
    sub     rdi, r8
    mov     rax, rdi
    ret

;-------------------------------------------------------------------------
; strcpy

strcpy:
    mov     rax, rdi
.copy:
    mov     dl, [rsi]
    mov     [rdi], dl
    inc     rdi
    inc     rsi
    test    dl, dl
    jne     .copy
    ret

;-------------------------------------------------------------------------
; strncpy

strncpy:
    mov     rax, rdi
    mov     rcx, rdx
    test    rcx, rcx
    je      .done
.loop:
    mov     dl, [rsi]
    mov     [rdi], dl
    inc     rdi
    dec     rcx
    test    dl, dl
    jne     .next
    jmp     .pad
.next:
    inc     rsi
    test    rcx, rcx
    jne     .loop
    ret
.pad:
    test    rcx, rcx
    je      .done
    mov     byte [rdi], 0
    inc     rdi
    dec     rcx
    jne     .pad
.done:
    ret

;-------------------------------------------------------------------------
; strcat

strcat:
    mov     rax, rdi
.seek_end:
    mov     dl, [rdi]
    test    dl, dl
    je      .copy
    inc     rdi
    jmp     .seek_end
.copy:
    mov     dl, [rsi]
    mov     [rdi], dl
    inc     rdi
    inc     rsi
    test    dl, dl
    jne     .copy
    ret

;-------------------------------------------------------------------------
; strcmp

strcmp:
.loop:
    mov     al, [rdi]
    mov     dl, [rsi]
    cmp     al, dl
    jne     .diff
    test    al, al
    je      .equal
    inc     rdi
    inc     rsi
    jmp     .loop
.diff:
    movzx   eax, byte [rdi]
    movzx   edx, byte [rsi]
    sub     eax, edx
    ret
.equal:
    xor     eax, eax
    ret

;-------------------------------------------------------------------------
; strncmp

strncmp:
    mov     rcx, rdx
    test    rcx, rcx
    je      .equal
.loop:
    mov     al, [rdi]
    mov     dl, [rsi]
    cmp     al, dl
    jne     .diff
    test    al, al
    je      .equal
    inc     rdi
    inc     rsi
    dec     rcx
    jne     .loop
.equal:
    xor     eax, eax
    ret
.diff:
    movzx   eax, byte [rdi]
    movzx   edx, byte [rsi]
    sub     eax, edx
    ret

;-------------------------------------------------------------------------
; strstr

strstr:
    mov     r8, rdi
    mov     al, [rsi]
    test    al, al
    je      .needle_empty
.outer:
    mov     r9, rdi
    mov     r10, rsi
.inner:
    mov     al, [r10]
    test    al, al
    je      .found
    mov     dl, [r9]
    test    dl, dl
    je      .not_found
    cmp     dl, al
    jne     .advance
    inc     r9
    inc     r10
    jmp     .inner
.advance:
    inc     rdi
    mov     dl, [rdi]
    test    dl, dl
    jne     .outer
    jmp     .not_found
.needle_empty:
    mov     rax, r8
    ret
.found:
    mov     rax, rdi
    ret
.not_found:
    xor     eax, eax
    ret

;-------------------------------------------------------------------------
; strchr

strchr:
    movzx   edx, sil
.loop:
    movzx   ecx, byte [rdi]
    cmp     ecx, edx
    je      .found
    test    ecx, ecx
    je      .not_found
    inc     rdi
    jmp     .loop
.found:
    mov     rax, rdi
    ret
.not_found:
    xor     eax, eax
    ret

;----------------------------------------------------------------------------
; Mark the stack as non-executable

section .note.GNU-stack noalloc noexec nowrite
