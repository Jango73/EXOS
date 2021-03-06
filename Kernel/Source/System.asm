
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

segment .data use32

    global _IRQMask_21
    global _IRQMask_A1
    global _IRQMask_21_RM
    global _IRQMask_A1_RM

;--------------------------------------

_IRQMask_21    dd 0x000000FB
_IRQMask_A1    dd 0x000000FF
_IRQMask_21_RM dd 0x00000000
_IRQMask_A1_RM dd 0x00000000

;----------------------------------------------------------------------------

segment .text use32

    global _ProtectedModeEntry
    global _GetCPUID
    global _DisablePaging
    global _EnablePaging
    global _DisableInterrupts
    global _EnableInterrupts
    global _SaveFlags
    global _RestoreFlags
    global _InPortByte
    global _OutPortByte
    global _InPortWord
    global _OutPortWord
    global _InPortStringWord
    global _MaskIRQ
    global _UnmaskIRQ
    global _DisableIRQ
    global _EnableIRQ
    global _LoadGlobalDescriptorTable
    global _LoadLocalDescriptorTable
    global _LoadInterruptDescriptorTable
    global _LoadPageDirectory
    global _LoadInitialTaskRegister
    global _GetTaskRegister
    global _GetPageDirectory
    global _FlushTLB
    global _SwitchToTask
    global _TaskRunner
    global _SetTaskState
    global _ClearTaskState
    global _PeekConsoleWord
    global _PokeConsoleWord
    global _SaveRegisters
    global _MemorySet
    global _MemoryCopy
    global _DoSystemCall
    global _Reboot

;--------------------------------------

_ProtectedModeEntry :

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

    mov     [_StubAddress], ebp

    ;--------------------------------------

    mov     eax, 0xB8000
    mov     byte [eax], 'J'

    ;--------------------------------------
    ; Setup the kernel's stack

    mov     ax,  SELECTOR_KERNEL_DATA
    mov     ss,  ax

    mov     esp, _Stack                ; Start of kernel heap
    add     esp, (N_32KB - 3072)       ; Minimum stack size
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
    ; Jump to main kernel routine

    jmp     _KernelMain

    ;--------------------------------------

_ProtectedModeEntry_Hang :

    jmp     _ProtectedModeEntry_Hang

;--------------------------------------

_GetCPUID :

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

_DisablePaging :

    mov     eax, cr0
    and     eax, 0x7FFFFFFF
    mov     cr0, eax
   ret

;--------------------------------------

_EnablePaging :

    mov     eax, cr0
    or      eax, CR0_PAGING
    mov     cr0, eax
   ret

;--------------------------------------

_DisableInterrupts :

   cli
   ret

;--------------------------------------

_EnableInterrupts :

   sti
   ret

;--------------------------------------

_SaveFlags :

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

_RestoreFlags :

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

_InPortByte :

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

_OutPortByte :

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

_InPortWord :

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

_OutPortWord :

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

_InPortStringWord :

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

_MaskIRQ :

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

    mov  edx, [_IRQMask_21]
    or   edx, eax
    mov  [_IRQMask_21], edx
    mov  eax, edx
    out  PIC1_DATA, al
    jmp  _MaskIRQ_Out

_MaskIRQ_High :

    mov  edx, [_IRQMask_A1]
    or   edx, eax
    mov  [_IRQMask_A1], edx
    mov  eax, edx
    out  PIC2_DATA, al

_MaskIRQ_Out :

    pop  edx
    pop  ecx
    pop  ebx
    pop  ebp
    ret

;--------------------------------------

_UnmaskIRQ :

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

    mov  edx, [_IRQMask_21]
    and  edx, eax
    mov  [_IRQMask_21], edx
    mov  eax, edx
    out  PIC1_DATA, al
    jmp  _UnmaskIRQ_Out

_UnmaskIRQ_High :

    mov  edx, [_IRQMask_A1]
    and  edx, eax
    mov  [_IRQMask_A1], edx
    mov  eax, edx
    out  PIC2_DATA, al

_UnmaskIRQ_Out :

    pop  edx
    pop  ecx
    pop  ebx
    pop  ebp
    ret

;--------------------------------------

_DisableIRQ :

    push    ebp
    mov     ebp, esp
    pushfd
    cli
    mov     eax, [ebp+PBN]
    push    eax
    call    _MaskIRQ
    add     esp, 4
    popfd
    pop     ebp
    ret

;--------------------------------------

_EnableIRQ :

    push    ebp
    mov     ebp, esp
    pushfd
    cli
    mov     eax, [ebp+PBN]
    push    eax
    call    _UnmaskIRQ
    add     esp, 4
    popfd
    pop  ebp
    ret

;--------------------------------------

_LoadGlobalDescriptorTable :

   push ebp
   mov  ebp, esp

    push ebx
    push esi

    ;--------------------------------------
    ; Version 1

    ; Put parameters in correct order

    mov  eax, [ebp+(PBN+0)]
    mov  ebx, [ebp+(PBN+4)]

    mov  [ebp+(PBN+0)], bx
    mov  [ebp+(PBN+2)], eax

    ; Load the Global Descriptor Table

    lgdt [ebp+PBN]

_LGDT_Out :

    pop  esi
    pop  ebx

   pop  ebp
    ret

;--------------------------------------

_LoadLocalDescriptorTable :

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
    ret

;--------------------------------------

_LoadInterruptDescriptorTable :

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

_LoadPageDirectory :

    push    ebp
    mov     ebp, esp
    mov     eax, [ebp+PBN]
    mov     cr3, eax
    pop     ebp
    ret

;--------------------------------------

_LoadInitialTaskRegister :

    push    ebp
    mov     ebp, esp
    push ebx

    mov     eax, [ebp+PBN]
    ltr     ax

    ;--------------------------------------
    ; Clear the nested task bit in eflags

    pushfd
    pop     eax
    mov     ebx, EFLAGS_NT
    not     ebx
    and     eax, ebx
    push eax
    popfd

    ;--------------------------------------
    ; Set the task switch flag in CR0

;    mov     eax, cr0
;    or      eax, CR0_TASKSWITCH
;    mov     cr0, eax

    ;--------------------------------------
    ; Clear the task switch flag in CR0

    clts

   pop      ebx
    pop     ebp
    ret

;--------------------------------------

_GetTaskRegister :

    xor     eax, eax
    str     ax
    ret

;--------------------------------------

_GetPageDirectory :

    mov     eax, cr3
    ret

;--------------------------------------

_FlushTLB :

    mov     eax, cr3
    mov     cr3, eax
    ret

;--------------------------------------

_SwitchToTask :

    push    ebp
    mov     ebp, esp
    sub     esp, 6

    mov     eax, [ebp+(PBN+0)]
    mov     dword [ebp-(LBN+6)], 0
    mov     word [ebp-(LBN+2)], ax
    jmp     far dword [ebp-(LBN+6)]

    add     esp, 6
    pop     ebp
    ret

;--------------------------------------
; This is the entry point of each new task
; It expects the task's main function pointer
; to reside in ebx and the argument in eax
; They should be set in the TSS by the kernel

_TaskRunner :

    ;--------------------------------------
    ; EBX in the TSS contains the function
    ; EAX in the TSS contains the parameter

    cmp     ebx, 0
    je      _TaskRunner_KillTask

    push    eax                        ; Argument for task function
    call    ebx                        ; Call task function
    add     esp, 4                     ; Adjust stack

    ;--------------------------------------
    ; When we come back from the function,
    ; we kill the task so that the kernel
    ; frees resources allocated and the
    ; scheduler does not jump to it

_TaskRunner_KillTask :

    ;--------------------------------------
    ; Kill the task

    call    _GetCurrentTask

    push    eax
    call    _KillTask
    add     esp, 4

    ;--------------------------------------
    ; Do an infinite loop, task will be removed by scheduler

_TaskRunner_L1 :

    nop
    nop
    nop
    nop
    jmp     _TaskRunner_L1

;--------------------------------------

_SetTaskState :

    mov     eax, cr0
    or      eax, CR0_TASKSWITCH
    mov     cr0, eax
    ret

;--------------------------------------

_ClearTaskState :

    clts
    ret

;--------------------------------------

_PeekConsoleWord :

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

_PokeConsoleWord :

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

_SaveRegisters :

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
    mov     eax, 0                     ; Store EIP
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

_MemorySet :

    push    ebp
    mov     ebp, esp
    push    ecx
    push    edi
    mov     edi, [ebp+(PBN+0)]
    mov     eax, [ebp+(PBN+4)]
    mov     ecx, [ebp+(PBN+8)]
    cld
    rep     stosb
    pop     edi
    pop     ecx
    pop     ebp
    ret

;--------------------------------------

_MemoryCopy :

    push    ebp
    mov     ebp, esp
    push    ecx
    push    esi
    push    edi
    mov     edi, [ebp+(PBN+0)]
    mov     esi, [ebp+(PBN+4)]
    mov     ecx, [ebp+(PBN+8)]
    cld
    rep     movsb
    pop     edi
    pop     esi
    pop     ecx
    pop     ebp
    ret

;--------------------------------------

_DoSystemCall :

    push    ebp
    mov     ebp, esp
    push    ebx

    mov     eax, [ebp+(PBN+0)]
    mov     ebx, [ebp+(PBN+4)]

    int     0x80

    pop     ebx
    pop     ebp
    ret

;----------------------------------------------------------------------------

segment .stack use32

    global _Stack

;--------------------------------------

_Stack : dd 0, 0, 0, 0, 0, 0, 0, 0

;----------------------------------------------------------------------------
