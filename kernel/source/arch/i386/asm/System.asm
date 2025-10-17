
;-------------------------------------------------------------------------
;
;   EXOS Kernel
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
;   System functions (i386)
;
;-------------------------------------------------------------------------

%include "i386.inc"
%include "System.inc"

section .data
BITS 32

    global DeadBeef

;--------------------------------------

DeadBeef      dd 0xDEADBEEF

;--------------------------------------

section .text.stub
BITS 32

global start
extern KernelMain
extern KernelLogText

stub_base:

    jmp     start

times (4 - ($ - $$)) db 0

Magic : db 'EXOS'

FUNC_HEADER
start:

    mov         [KernelStartup + KernelStartupInfo.StackTop], esp

    call        KernelMain

.hang:
    cli         ; Should not return here, hang
    hlt
    jmp .hang
    nop

;----------------------------------------------------------------------------

section .text
BITS 32

    global GetCPUID
    global DisablePaging
    global EnablePaging
    global SaveFPU
    global RestoreFPU
    global InPortByte
    global OutPortByte
    global InPortWord
    global OutPortWord
    global InPortLong
    global OutPortLong
    global InPortStringWord
    global OutPortStringWord
    global LoadGlobalDescriptorTable
    global ReadGlobalDescriptorTable
    global GetTaskRegister
    global GetPageDirectory
    global InvalidatePage
    global FlushTLB
    global SetTaskState
    global ClearTaskState
    global PeekConsoleWord
    global PokeConsoleWord
    global SaveRegisters
    global DoSystemCall
    global IdleCPU
    global DeadCPU
    global Reboot
    global TaskRunner

;--------------------------------------

FUNC_HEADER
GetCPUID :

    push ebp
    mov  ebp, esp

    push eax
    push ebx
    push ecx
    push edx
    push edi

    mov  edi, [ebp+PBN]

    mov  eax, 0
    cpuid

    mov  [edi + 0x0000], eax
    mov  [edi + 0x0004], ebx
    mov  [edi + 0x0008], ecx
    mov  [edi + 0x000C], edx

    mov  eax, 1
    cpuid

    mov  [edi + 0x0010], eax
    mov  [edi + 0x0014], ebx
    mov  [edi + 0x0018], ecx
    mov  [edi + 0x001C], edx

    ;mov  eax, 2
    ;cpuid

    ;mov  [edi + 0x0020], eax
    ;mov  [edi + 0x0024], ebx
    ;mov  [edi + 0x0028], ecx
    ;mov  [edi + 0x002C], edx

    pop  edi
    pop  edx
    pop  ecx
    pop  ebx
    pop  eax

    mov  eax, 1

    pop  ebp
    ret

;--------------------------------------

FUNC_HEADER
DisablePaging :

    mov     eax, cr0
    and     eax, 0x7FFFFFFF
    mov     cr0, eax
    ret

;--------------------------------------

FUNC_HEADER
EnablePaging :

    mov     eax, cr0
    or      eax, CR0_PAGING
    mov     cr0, eax
    ret

;--------------------------------------

FUNC_HEADER
SaveFPU :

    push    ebp
    mov     ebp, esp
    push    edi

    mov     edi, [ebp+(PBN+0)]
    fsave   [edi]

    xor     eax, eax

    pop     edi
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
RestoreFPU :

    push    ebp
    mov     ebp, esp
    push    edi

    mov     edi, [ebp+(PBN+0)]
    frstor  [edi]

    xor     eax, eax

    pop     edi
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
InPortByte :

    push    ebp
    mov     ebp, esp
    push    edx
    mov     edx, [ebp+(PBN+0)]
    xor     eax, eax
    in      al, dx
    pop     edx
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
OutPortByte :

    push    ebp
    mov     ebp, esp
    push    edx
    mov     edx, [ebp+(PBN+0)]
    mov     eax, [ebp+(PBN+4)]
    out     dx, al
    pop     edx
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
InPortWord :

    push    ebp
    mov     ebp, esp
    push    edx
    mov     edx, [ebp+(PBN+0)]
    xor     eax, eax
    in      ax, dx
    pop     edx
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
OutPortWord :

    push    ebp
    mov     ebp, esp
    push    edx
    mov     edx, [ebp+(PBN+0)]
    mov     eax, [ebp+(PBN+4)]
    out     dx, ax
    pop     edx
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
InPortLong :

    push    ebp
    mov     ebp, esp
    push    edx
    mov     edx, [ebp+(PBN+0)]
    in      eax, dx
    pop     edx
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
OutPortLong :

    push    ebp
    mov     ebp, esp
    push    edx
    mov     edx, [ebp+(PBN+0)]
    mov     eax, [ebp+(PBN+4)]
    out     dx, eax
    pop     edx
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
OutPortStringWord :

    push    ebp
    mov     ebp, esp
    push    ecx
    push    edx
    push    esi
    mov     edx, [ebp+(PBN+0)]
    mov     esi, [ebp+(PBN+4)]
    mov     ecx, [ebp+(PBN+8)]
    cld
    rep     outsw
    pop     esi
    pop     edx
    pop     ecx
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
InPortStringWord :

    push    ebp
    mov     ebp, esp
    push    ecx
    push    edx
    push    edi
    mov     edx, [ebp+(PBN+0)]
    mov     edi, [ebp+(PBN+4)]
    mov     ecx, [ebp+(PBN+8)]
    cld
    rep     insw
    pop     edi
    pop     edx
    pop     ecx
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
LoadGlobalDescriptorTable :

    push        ebp
    mov         ebp, esp

    push        ebx
    push        esi

    ;--------------------------------------
    ; Version 1

    ; Put parameters in correct order

    mov         eax, [ebp+(PBN+0)]
    mov         ebx, [ebp+(PBN+4)]

    mov         [ebp+(PBN+0)], bx
    mov         [ebp+(PBN+2)], eax

    ; Load the Global Descriptor Table

    lgdt        [ebp+PBN]

    jmp         SELECTOR_KERNEL_CODE:.flush

.flush :

    mov         ax, SELECTOR_KERNEL_DATA    ; data selector
    mov         ss, ax

    nop
    nop
    nop
    nop

    mov         ds, ax
    mov         es, ax
    mov         fs, ax
    mov         gs, ax

_LGDT_Out :

    pop  esi
    pop  ebx

    pop  ebp
    ret

;--------------------------------------

FUNC_HEADER
ReadGlobalDescriptorTable :

    ; void ReadGlobalDescriptorTable(GDTR* gdtr_ptr)
    ; Parameter: pointer to GDTR structure (6 bytes: 2 limit + 4 base)

    push    ebp
    mov     ebp, esp
    push    ebx

    mov     ebx, [ebp+(PBN+0)]  ; Get pointer to GDTR structure
    sgdt    [ebx]               ; Store GDTR at the provided address

    pop     ebx
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
GetTaskRegister :

    xor         eax, eax
    str         ax
    ret

;--------------------------------------

FUNC_HEADER
GetPageDirectory :

    mov     eax, cr3
    ret

;--------------------------------------

FUNC_HEADER
InvalidatePage :

    push        ebp
    mov         ebp, esp

    mov         eax, [ebp+(PBN+0)]
    invlpg      [eax]

    pop         ebp
    ret

;--------------------------------------

FUNC_HEADER
FlushTLB :

    mov     eax, cr3
    mov     cr3, eax
    ret

;--------------------------------------

FUNC_HEADER
SetTaskState :

    mov     eax, cr0
    or      eax, CR0_TASKSWITCH
    mov     cr0, eax
    ret

;--------------------------------------

FUNC_HEADER
ClearTaskState :

    clts
    ret

;--------------------------------------

FUNC_HEADER
PeekConsoleWord :

    push    ebp
    mov     ebp, esp
    push    esi
    mov     esi, [ebp+(PBN+0)]
    add     esi, 0xB8000
    xor     eax, eax
    mov     ax, [esi]
    pop     esi
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
PokeConsoleWord :

    push    ebp
    mov     ebp, esp
    push    eax
    push    esi
    mov     esi, [ebp+(PBN+0)]
    mov     eax, [ebp+(PBN+4)]
    add     esi, 0xB8000
    mov     [esi], ax
    pop     esi
    pop     eax
    pop     ebp
    ret

;--------------------------------------

FUNC_HEADER
SaveRegisters :

    push    ebp
    mov     ebp, esp
    push    eax
    push    ebx
    push    ecx
    push    edx
    push    esi
    push    edi
    pushfd

    cli

    mov     edi, [ebp+(PBN+0)]

    pushf                              ; Store the flags
    mov     eax, [esp]
    mov     [edi], eax
    popf
    add     edi, 4

    mov     eax, [ebp-4]               ; Store EAX
    mov     [edi], eax
    add     edi, 4
    mov     eax, [ebp-8]               ; Store EBX
    mov     [edi], eax
    add     edi, 4
    mov     eax, [ebp-12]              ; Store ECX
    mov     [edi], eax
    add     edi, 4
    mov     eax, [ebp-16]              ; Store EDX
    mov     [edi], eax
    add     edi, 4

    mov     eax, [ebp-20]              ; Store ESI
    mov     [edi], eax
    add     edi, 4
    mov     eax, [ebp-24]              ; Store EDI
    mov     [edi], eax
    add     edi, 4
    mov     eax, [ebp]                 ; Store ESP
    add     eax, 4
    mov     [edi], eax
    add     edi, 4
    mov     eax, [ebp]                 ; Store EBP
    mov     [edi], eax
    add     edi, 4
    mov     eax, [ebp+4]               ; Store EIP
    mov     [edi], eax
    add     edi, 4

    mov     ax, cs                     ; Store CS
    mov     [edi], ax
    add     edi, 2
    mov     ax, ds                     ; Store DS
    mov     [edi], ax
    add     edi, 2
    mov     ax, ss                     ; Store SS
    mov     [edi], ax
    add     edi, 2
    mov     ax, es                     ; Store ES
    mov     [edi], ax
    add     edi, 2
    mov     ax, fs                     ; Store FS
    mov     [edi], ax
    add     edi, 2
    mov     ax, gs                     ; Store GS
    mov     [edi], ax
    add     edi, 2

    mov     eax, cr0                   ; Store CR0
    mov     [edi], eax
    add     edi, 4
    mov     eax, cr2                   ; Store CR2
    mov     [edi], eax
    add     edi, 4
    mov     eax, cr3                   ; Store CR3
    mov     [edi], eax
    add     edi, 4
    mov     eax, cr4                   ; Store CR4
    mov     [edi], eax
    add     edi, 4

    mov     eax, dr0                   ; Store DR0
    mov     [edi], eax
    add     edi, 4
    mov     eax, dr1                   ; Store DR1
    mov     [edi], eax
    add     edi, 4
    mov     eax, dr2                   ; Store DR2
    mov     [edi], eax
    add     edi, 4
    mov     eax, dr3                   ; Store DR3
    mov     [edi], eax
    add     edi, 4

    mov     eax, dr4                   ; Store DR4
    mov     [edi], eax
    add     edi, 4
    mov     eax, dr5                   ; Store DR5
    mov     [edi], eax
    add     edi, 4
    mov     eax, dr6                   ; Store DR6
    mov     [edi], eax
    add     edi, 4
    mov     eax, dr7                   ; Store DR7
    mov     [edi], eax
    add     edi, 4

    popfd
    pop     edi
    pop     esi
    pop     edx
    pop     ecx
    pop     ebx
    pop     eax
    pop     ebp
    ret

FUNC_HEADER
DoSystemCall :

    push    ebp
    mov     ebp, esp
    push    ebx

    mov     eax, [ebp+(PBN+0)]
    mov     ebx, [ebp+(PBN+4)]

    int     EXOS_USER_CALL

    pop     ebx
    pop     ebp
    ret

;--------------------------------------
; DON'T call this one outside of :
; Sleep, WaitForMessage and LockMutex
; It will trigger random crashes

FUNC_HEADER
IdleCPU :

    sti
    hlt
    ret

;--------------------------------------

FUNC_HEADER
DeadCPU :

.loop:
    sti
    hlt
    jmp     .loop

;--------------------------------------

FUNC_HEADER
Reboot :

    cli

    ; Wait for keyboard controler ready
.wait_input_clear:
    in      al, 0x64
    test    al, 0x02
    jnz     .wait_input_clear

    ; Send reset command
    mov     al, 0xFE
    out     0x64, al

    ; Infinite loop
.hang:
    hlt
    jmp     .hang

;----------------------------------------------------------------------------

section .shared_text
BITS 32

;--------------------------------------
; This is the entry point of each new task
; It expects the task's main function pointer
; to reside in ebx and the argument in eax

FUNC_HEADER
TaskRunner :

    ; Clear registers
    xor         ecx, ecx
    xor         edx, edx
    xor         esi, esi
    xor         edi, edi
    xor         ebp, ebp

    ;--------------------------------------
    ; EBX contains the function
    ; EAX contains the parameter

    cmp         ebx, 0
    je          _TaskRunner_Exit

    push        eax                         ; Argument for task function
    call        ebx                         ; Call task function
    add         esp, U32_SIZE               ; Adjust stack

_TaskRunner_Exit :

    mov         ebx, eax                    ; Task exit code in ebx
    mov         eax, 0x33                   ; SYSCALL_Exit
    int         EXOS_USER_CALL

    ;--------------------------------------
    ; Do an infinite loop, task will be removed by scheduler

.sleep
    mov         eax, 0x0E
    mov         ebx, MAX_UINT
    int         EXOS_USER_CALL

    jmp         .sleep

;----------------------------------------------------------------------------

section .data
BITS 32

TaskRunnerLogMsg db '[TaskRunner] EAX=%x EBX=%x', 0

;----------------------------------------------------------------------------

section .bss

    global Stack

;----------------------------------------------------------------------------
