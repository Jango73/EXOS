
; System.asm

;----------------------------------------------------------------------------

%include "./Kernel.inc"

;----------------------------------------------------------------------------

; Helper values to access function parameters and local variables

PBN equ 0x08                           ; Param base near
PBF equ 0x0A                           ; Param base far
LBN equ 0x04                           ; Local base near
LBF equ 0x04                           ; Local base far

;----------------------------------------------------------------------------

TEMP_GDT_BASE equ 0x0000EF00
RMC_BASE      equ 0x0000F000
RMC_SEGMENT   equ 0x00000F00
RMC_STACK     equ 0x00000400

;----------------------------------------------------------------------------

section .data
bits 32

    global DeadBeef

;--------------------------------------

DeadBeef      dd 0xDEADBEEF

;----------------------------------------------------------------------------

section .text
bits 32

    global GetGDTR
    global GetLDTR
    global GetESP
    global GetEBP
    global GetDR6
    global GetDR7
    global SetDR6
    global SetDR7
    global GetCPUID
    global DisablePaging
    global EnablePaging
    global DisableInterrupts
    global EnableInterrupts
    global SaveFlags
    global RestoreFlags
    global InPortByte
    global OutPortByte
    global InPortWord
    global OutPortWord
    global InPortLong
    global OutPortLong
    global InPortStringWord
    global MaskIRQ
    global UnmaskIRQ
    global DisableIRQ
    global EnableIRQ
    global LoadGlobalDescriptorTable
    global LoadLocalDescriptorTable
    global LoadInterruptDescriptorTable
    global LoadPageDirectory
    global LoadInitialTaskRegister
    global GetTaskRegister
    global GetPageDirectory
    global SetPageDirectory
    global InvalidatePage
    global FlushTLB
    global SwitchToTask
    global TaskRunner
    global SetTaskState
    global ClearTaskState
    global PeekConsoleWord
    global PokeConsoleWord
    global SaveRegisters
    global MemorySet
    global MemoryCopy
    global DoSystemCall
    global IdleCPU
    global Reboot

;--------------------------------------

GetGDTR:
    push        ebp
    mov         ebp, esp
    sub         esp, 6            ; need 6 bytes for sgdt

    sgdt        [ebp-6]           ; stores limit(2) + base(4)
    mov         eax, [ebp-4]      ; eax = base

    add         esp, 6
    pop         ebp
    ret

;--------------------------------------

GetLDTR:
    push        ebp
    mov         ebp, esp
    sub         esp, 6            ; need 6 bytes for sgdt

    sldt        [ebp-6]           ; stores limit(2) + base(4)
    mov         eax, [ebp-4]      ; eax = base

    add         esp, 6
    pop         ebp
    ret

;--------------------------------------

GetESP :

    mov         eax, esp
    ret

;--------------------------------------

GetEBP :

    mov         eax, ebp
    ret

;--------------------------------------

GetDR6 :

    mov         eax, dr6
    ret

;--------------------------------------

GetDR7 :

    mov         eax, dr7
    ret

;--------------------------------------

SetDR6 :

    push        ebp
    mov         ebp, esp

    mov         eax, [ebp+PBN]
    mov         dr6, eax

    pop         ebp
    ret

;--------------------------------------

SetDR7 :

    push        ebp
    mov         ebp, esp

    mov         eax, [ebp+PBN]
    mov         dr7, eax

    pop         ebp
    ret

;--------------------------------------

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

    ;mov  eax, 1
    ;cpuid

    ;mov  [edi + 0x0010], eax
    ;mov  [edi + 0x0014], ebx
    ;mov  [edi + 0x0018], ecx
    ;mov  [edi + 0x001C], edx

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

DisablePaging :

    mov     eax, cr0
    and     eax, 0x7FFFFFFF
    mov     cr0, eax
    ret

;--------------------------------------

EnablePaging :

    mov     eax, cr0
    or      eax, CR0_PAGING
    mov     cr0, eax
    ret

;--------------------------------------

DisableInterrupts :

   cli
   ret

;--------------------------------------

EnableInterrupts :

   sti
   ret

;--------------------------------------

SaveFlags :

    push    ebp
    mov     ebp, esp
    push    edi

    mov     edi, [ebp+(PBN+0)]
    pushfd
    pop     eax
    mov     [edi], eax

    xor     eax, eax

    pop     edi
    pop     ebp
    ret

;--------------------------------------

RestoreFlags :

    push    ebp
    mov     ebp, esp
    push    edi

    mov     edi, [ebp+(PBN+0)]
    mov     eax, [edi]
    push    eax
    popfd

    xor     eax, eax

    pop     edi
    pop     ebp
    ret

;--------------------------------------

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

MaskIRQ :

    push ebp
    mov  ebp, esp
    push ebx
    push ecx
    push edx

    mov  ecx, [ebp+PBN]
    and  ecx, 0x07
    mov  eax, 1
    shl  eax, cl
    mov  ebx, [ebp+PBN]
    cmp  ebx, 8
    jge  _MaskIRQ_High

    mov  edx, [KernelStartup + KernelStartupInfo.IRQMask_21_PM]
    or   edx, eax
    mov  [KernelStartup + KernelStartupInfo.IRQMask_21_PM], edx
    mov  eax, edx
    out  PIC1_DATA, al
    jmp  _MaskIRQ_Out

_MaskIRQ_High :

    mov  edx, [KernelStartup + KernelStartupInfo.IRQMask_A1_PM]
    or   edx, eax
    mov  [KernelStartup + KernelStartupInfo.IRQMask_A1_PM], edx
    mov  eax, edx
    out  PIC2_DATA, al

_MaskIRQ_Out :

    pop  edx
    pop  ecx
    pop  ebx
    pop  ebp
    ret

;--------------------------------------

UnmaskIRQ :

    push ebp
    mov  ebp, esp
    push ebx
    push ecx
    push edx

    mov  ecx, [ebp+PBN]
    and  ecx, 0x07
    mov  eax, 1
    shl  eax, cl
    not  eax
    mov  ebx, [ebp+PBN]
    cmp  ebx, 8
    jge  _UnmaskIRQ_High

    mov  edx, [KernelStartup + KernelStartupInfo.IRQMask_21_PM]
    and  edx, eax
    mov  [KernelStartup + KernelStartupInfo.IRQMask_21_PM], edx
    mov  eax, edx
    out  PIC1_DATA, al
    jmp  _UnmaskIRQ_Out

_UnmaskIRQ_High :

    mov  edx, [KernelStartup + KernelStartupInfo.IRQMask_A1_PM]
    and  edx, eax
    mov  [KernelStartup + KernelStartupInfo.IRQMask_A1_PM], edx
    mov  eax, edx
    out  PIC2_DATA, al

_UnmaskIRQ_Out :

    pop  edx
    pop  ecx
    pop  ebx
    pop  ebp
    ret

;--------------------------------------

DisableIRQ :

    push    ebp
    mov     ebp, esp
    pushfd
    cli
    mov     eax, [ebp+PBN]
    push    eax
    call MaskIRQ
    add     esp, 4
    popfd
    pop     ebp
    ret

;--------------------------------------

EnableIRQ :

    push    ebp
    mov     ebp, esp
    pushfd
    cli
    mov     eax, [ebp+PBN]
    push    eax
    call UnmaskIRQ
    add     esp, 4
    popfd
    pop  ebp
    ret

;--------------------------------------

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

    jmp         0x08:.flush

.flush :

    mov ax, 0x10    ; data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

_LGDT_Out :

    pop  esi
    pop  ebx

    pop  ebp
    ret

;--------------------------------------

LoadLocalDescriptorTable :

    cli
    push ebp
    mov  ebp, esp

    ; Put parameters in correct order

    mov  eax, [ebp+(PBN+0)]
    mov  ebx, [ebp+(PBN+4)]

    mov  [ebp+(PBN+0)], bx
    mov  [ebp+(PBN+2)], eax

    ; Load the Local Descriptor Table

    lldt [ebp+PBN]

    pop  ebp
    sti
    ret

;--------------------------------------

LoadInterruptDescriptorTable :

    push    ebp
    mov     ebp, esp
    pushfd

    cli

    ;--------------------------------------
    ; Put parameters in correct order

    mov     eax, [ebp+(PBN+0)]
    mov     ebx, [ebp+(PBN+4)]

    mov     [ebp+(PBN+0)], bx
    mov     [ebp+(PBN+2)], eax

    ;--------------------------------------
    ; Load the Interrupt Descriptor Table

    lidt    [ebp+(PBN+0)]

    popfd
    pop     ebp
    ret

;--------------------------------------

LoadPageDirectory :

    push    ebp
    mov     ebp, esp
    mov     eax, [ebp+PBN]
    mov     cr3, eax
    pop     ebp
    ret

;--------------------------------------

LoadInitialTaskRegister :

    push        ebp
    mov         ebp, esp
    push        ebx

    mov         eax, [ebp+PBN]
    ltr         ax

    ;--------------------------------------
    ; Clear the nested task bit in eflags

    pushfd
    pop         eax
    mov         ebx, EFLAGS_NT
    not         ebx
    and         eax, ebx
    push        eax
    popfd

    ;--------------------------------------
    ; Set the task switch flag in CR0

;    mov     eax, cr0
;    or      eax, CR0_TASKSWITCH
;    mov     cr0, eax

    ;--------------------------------------
    ; Clear the task switch flag in CR0

    clts

    pop         ebx
    pop         ebp
    ret

;--------------------------------------

GetTaskRegister :

    xor         eax, eax
    str         ax
    ret

;--------------------------------------

GetPageDirectory :

    mov     eax, cr3
    ret

;--------------------------------------

SetPageDirectory :

    push        ebp
    mov         ebp, esp
    push        ebx

    mov         eax, [ebp+PBN+0]
    mov         cr3, eax

    pop         ebx
    pop         ebp
    ret

;--------------------------------------

InvalidatePage :

    push        ebp
    mov         ebp, esp

    mov         eax, [ebp+(PBN+0)]
    invlpg      [eax]

    pop         ebp
    ret

;--------------------------------------

FlushTLB :

    mov     eax, cr3
    mov     cr3, eax
    ret

;--------------------------------------

SwitchToTask :

    push        ebp
    mov         ebp, esp
    sub         esp, 6                      ; reserve 6 bytes for far pointer: [offset(4)][selector(2)]

    mov         eax, [ebp+(PBN+0)]
    mov         dword [ebp-(LBN+6)], 0
    mov         word [ebp-(LBN+2)], ax
    jmp         far dword [ebp-(LBN+6)]

    add         esp, 6
    pop         ebp
    ret

;--------------------------------------
; This is the entry point of each new task
; It expects the task's main function pointer
; to reside in ebx and the argument in eax
; They should be set in the TSS by the kernel

TaskRunner :

    ;--------------------------------------
    ; EBX in the TSS contains the function
    ; EAX in the TSS contains the parameter

    cmp         ebx, 0
    je          _TaskRunner_KillTask

    PRE_CALL_C
    push        eax                        ; Argument for task function
    call        ValidateEIPOrDie
    add         esp, 4                     ; Adjust stack
    POST_CALL_C

    PRE_CALL_C
    push        eax                        ; Argument for task function
    call        ebx                        ; Call task function
    add         esp, 4                     ; Adjust stack
    POST_CALL_C

    ;--------------------------------------
    ; When we come back from the function,
    ; we kill the task so that the kernel
    ; frees resources allocated and the
    ; scheduler does not jump to it

_TaskRunner_KillTask :

    ;--------------------------------------
    ; Kill the task

    PRE_CALL_C
    call        GetCurrentTask
    POST_CALL_C

    push        eax
    call        KillTask
    add         esp, 4

    ;--------------------------------------
    ; Do an infinite loop, task will be removed by scheduler

_TaskRunner_L1 :

    nop
    nop
    nop
    nop
    jmp         _TaskRunner_L1

;--------------------------------------

SetTaskState :

    mov     eax, cr0
    or      eax, CR0_TASKSWITCH
    mov     cr0, eax
    ret

;--------------------------------------

ClearTaskState :

    clts
    ret

;--------------------------------------

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

;--------------------------------------

MemorySet :

    push    ebp
    mov     ebp, esp

    push    ecx
    push    edi
    push    es

    push    ds
    pop     es

    mov     edi, [ebp+(PBN+0)]
    mov     eax, [ebp+(PBN+4)]
    mov     ecx, [ebp+(PBN+8)]
    cld
    rep     stosb

    pop     es
    pop     edi
    pop     ecx

    pop     ebp
    ret

;--------------------------------------

MemoryCopy :

    push    ebp
    mov     ebp, esp

    push    ecx
    push    esi
    push    edi
    push    es

    push    ds
    pop     es

    mov     edi, [ebp+(PBN+0)]
    mov     esi, [ebp+(PBN+4)]
    mov     ecx, [ebp+(PBN+8)]
    cld
    rep     movsb

    pop     es
    pop     edi
    pop     esi
    pop     ecx

    pop     ebp
    ret

;--------------------------------------

DoSystemCall :

    push    ebp
    mov     ebp, esp
    push    ebx

    mov     eax, [ebp+(PBN+0)]
    mov     ebx, [ebp+(PBN+4)]

    int     0x80

    pop     ebx
    pop     ebp
    ret

;--------------------------------------

IdleCPU :

    cli
    sti
    hlt
    ret

;--------------------------------------

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

section .bss

    global Stack

;----------------------------------------------------------------------------
