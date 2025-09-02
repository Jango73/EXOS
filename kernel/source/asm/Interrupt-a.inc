
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
;   Interrupt macros
;
;-------------------------------------------------------------------------

;-------------------------------------------------------------------------
; Trap entry stack for #DB, #DF, #TS, #NP, #SS, #GP, #PF, #AC
;   
;
;   High addresses
;        |
;        v
;   
;   +------------------+ <-- ESP before exception
;   |                  |
;   |   User stack     |     (user data before exception)
;   |                  |
;   +------------------+
;   |        |   SS    | <-- 16 bits - PRESENT only if user->kernel privilege change
;   +------------------+
;   |      ESP         | <-- 32 bits - PRESENT only if user->kernel privilege change
;   +------------------+
;   |    EFLAGS        | <-- 32 bits - ALWAYS PRESENT
;   +------------------+
;   |        |   CS    | <-- 16 bits - ALWAYS PRESENT
;   +------------------+
;   |      EIP         | <-- 32 bits - ALWAYS PRESENT
;   +------------------+
;   |   ERROR_CODE     | <-- 32 bits - ALWAYS PRESENT
;   +------------------+ <-- ESP after exception (in handler)
;   |                  |
;   |  Kernel stack    |     (exception handler)
;   |                  |
;        |
;        v
;   Low addresses
;
;   ERROR_CODE = 0 for #DB, #DF, #AC; = selector for #TS, #NP, #SS, #GP; = info for #PF
;-------------------------------------------------------------------------

;-------------------------------------------------------------------------
; Trap entry stack for IRQs and #DE, #BR, #UD, #NM, #MF
; Just short of an error code
;   
;
;   High addresses
;        |
;        v
;   
;   +------------------+ <-- ESP before exception
;   |                  |
;   |   User stack     |     (user data before exception)
;   |                  |
;   +------------------+
;   |        |   SS    | <-- 16 bits - PRESENT only if user->kernel privilege change
;   +------------------+
;   |      ESP         | <-- 32 bits - PRESENT only if user->kernel privilege change
;   +------------------+
;   |    EFLAGS        | <-- 32 bits - ALWAYS PRESENT
;   +------------------+
;   |        |   CS    | <-- 16 bits - ALWAYS PRESENT
;   +------------------+
;   |      EIP         | <-- 32 bits - ALWAYS PRESENT
;   +------------------+ <-- ESP after exception (in handler)
;   |                  |
;   |  Kernel stack    |     (exception handler)
;   |                  |
;        |
;        v
;   Low addresses
;
;-------------------------------------------------------------------------

; =================== INTEL386REGISTERS layout ===================
%define R_EFlags    0
%define R_EAX       4
%define R_EBX       8
%define R_ECX       12
%define R_EDX       16
%define R_ESI       20
%define R_EDI       24
%define R_ESP       28
%define R_EBP       32
%define R_EIP       36
%define R_CS        40      ; U16
%define R_DS        42      ; U16
%define R_SS        44      ; U16
%define R_ES        46      ; U16
%define R_FS        48      ; U16
%define R_GS        50      ; U16
%define R_CR0       52
%define R_CR2       56
%define R_CR3       60
%define R_CR4       64
%define R_DR0       68
%define R_DR1       72
%define R_DR2       76
%define R_DR3       80
%define R_DR4       84
%define R_DR5       88
%define R_DR6       92
%define R_DR7       96

%define SIZE_REGS   100

; Interrupt frame = { INTEL386REGISTERS Registers; U32 IntNo; U32 ErrCode; }
%define F_IntNo     (SIZE_REGS + 0)
%define F_ErrCode   (SIZE_REGS + 4)
%define SIZE_FRAME  (SIZE_REGS + 8)

; Stack layout after our prologue (using EBP as frame pointer):
;
; [EBP + 0]     = old EBP (we push EBP after segments)
; [EBP + 4]     = return address from call EnterKernel
; [EBP + 8]     = GS (pushed)
; [EBP + 12]    = FS (pushed) 
; [EBP + 16]    = ES (pushed)
; [EBP + 20]    = DS (pushed)
; [EBP + 24]    = EDI (from pushad)
; [EBP + 28]    = ESI (from pushad)
; [EBP + 32]    = EBP_old (from pushad)
; [EBP + 36]    = ESP_at_entry (from pushad)
; [EBP + 40]    = EBX (from pushad)
; [EBP + 44]    = EDX (from pushad)
; [EBP + 48]    = ECX (from pushad)
; [EBP + 52]    = EAX (from pushad)
; [EBP + 56]    = EIP (hardware frame)
; [EBP + 60]    = CS (hardware frame)
; [EBP + 64]    = EFLAGS (hardware frame)
; [EBP + 68]    = User ESP (only if privilege change)
; [EBP + 72]    = User SS (only if privilege change)

; Frame offsets from EBP
%define FP_GS           8
%define FP_FS           12
%define FP_ES           16
%define FP_DS           20
%define FP_EDI          24
%define FP_ESI          28
%define FP_EBP_OLD      32
%define FP_ESP_ENTRY    36
%define FP_EBX          40
%define FP_EDX          44
%define FP_ECX          48
%define FP_EAX          52
%define FP_EIP          56
%define FP_CS           60
%define FP_EFLAGS       64
%define FP_USER_ESP     68
%define FP_USER_SS      72

; For error code exceptions, add 4 to all hardware frame offsets
%define FP_ERR_CODE     56
%define FP_EIP_ERR      60
%define FP_CS_ERR       64
%define FP_EFLAGS_ERR   68
%define FP_USER_ESP_ERR 72
%define FP_USER_SS_ERR  76

; ------------------- Build frame (NO error code) --------------------
%macro ISR_BUILD_FRAME_NOERR 1
    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs
    
    ; Setup frame pointer BEFORE calling EnterKernel
    push        ebp
    mov         ebp, esp                    ; EBP = stable frame pointer
    
    ; Capture SS before EnterKernel changes it
    mov         eax, ss
    push        eax                         ; Save original SS on stack
    
    call        EnterKernel                 ; Switch to ring0 segments
    
    ; Reserve space for INTERRUPTFRAME
    sub         esp, SIZE_FRAME
    mov         edi, esp                    ; EDI = pointer to frame
    
    ; ============ Fill INTEL386REGISTERS ============
    
    ; Segment registers (zero-extended to 32-bit)
    movzx       eax, word [ebp + FP_DS]
    mov         word [edi + R_DS], ax
    movzx       eax, word [ebp + FP_ES]
    mov         word [edi + R_ES], ax
    movzx       eax, word [ebp + FP_FS]
    mov         word [edi + R_FS], ax
    movzx       eax, word [ebp + FP_GS]
    mov         word [edi + R_GS], ax
    
    ; Hardware frame registers
    mov         eax, [ebp + FP_EFLAGS]
    mov         [edi + R_EFlags], eax
    mov         eax, [ebp + FP_EIP]
    mov         [edi + R_EIP], eax
    movzx       eax, word [ebp + FP_CS]
    mov         word [edi + R_CS], ax
    
    ; General purpose registers from PUSHAD
    mov         eax, [ebp + FP_EAX]
    mov         [edi + R_EAX], eax
    mov         eax, [ebp + FP_EBX]
    mov         [edi + R_EBX], eax
    mov         eax, [ebp + FP_ECX]
    mov         [edi + R_ECX], eax
    mov         eax, [ebp + FP_EDX]
    mov         [edi + R_EDX], eax
    mov         eax, [ebp + FP_ESI]
    mov         [edi + R_ESI], eax
    mov         eax, [ebp + FP_EDI]
    mov         [edi + R_EDI], eax
    mov         eax, [ebp + FP_EBP_OLD]
    mov         [edi + R_EBP], eax
    
    ; Default stack info (ring 0 -> ring 0 case)
    pop         eax                         ; Restore original SS
    mov         word [edi + R_SS], ax
    mov         eax, [ebp + FP_ESP_ENTRY]   ; ESP at ISR entry
    mov         [edi + R_ESP], eax
    
    ; Check if we came from user mode (CPL = 3)
    mov         eax, [ebp + FP_CS]
    test        eax, 3                      ; Test CPL bits
    jz          %%ring0_entry
    
    ; Ring 3 -> Ring 0: use user ESP/SS from hardware frame
    mov         eax, [ebp + FP_USER_ESP]
    mov         [edi + R_ESP], eax
    movzx       eax, word [ebp + FP_USER_SS]
    mov         word [edi + R_SS], ax
    
%%ring0_entry:
    
    ; Control registers (set to safe defaults for now)
    mov         dword [edi + R_CR0], 0
    mov         dword [edi + R_CR2], 0
    mov         dword [edi + R_CR3], 0
    mov         dword [edi + R_CR4], 0
    
    ; Debug registers
    mov         eax, dr0
    mov         [edi + R_DR0], eax
    mov         eax, dr1
    mov         [edi + R_DR1], eax
    mov         eax, dr2
    mov         [edi + R_DR2], eax
    mov         eax, dr3
    mov         [edi + R_DR3], eax
    mov         eax, dr6
    mov         [edi + R_DR6], eax
    mov         eax, dr7
    mov         [edi + R_DR7], eax
    
    ; DR4/DR5 set to safe defaults
    mov         dword [edi + R_DR4], 0
    mov         dword [edi + R_DR5], 0
    
    ; Frame metadata
    mov         dword [edi + F_IntNo], %1
    mov         dword [edi + F_ErrCode], 0
%endmacro

%macro ISR_BUILD_FRAME_NOERR_AND_CALL 2
    ISR_BUILD_FRAME_NOERR %1
    
    ; Call C handler: void Handler(INTERRUPTFRAME* Frame)
    push        edi
    call        %2
    add         esp, 4
%endmacro

; ------------------- Build frame (WITH error code) --------------------
%macro ISR_BUILD_FRAME_ERR_AND_CALL 2
    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs
    
    ; Setup frame pointer
    push        ebp
    mov         ebp, esp                    ; EBP = stable frame pointer
    
    ; Capture SS before EnterKernel changes it
    mov         eax, ss
    push        eax                         ; Save original SS on stack
    
    call        EnterKernel                 ; Switch to ring0 segments
    
    ; Reserve space for INTERRUPTFRAME
    sub         esp, SIZE_FRAME
    mov         edi, esp                    ; EDI = pointer to frame
    
    ; ============ Fill INTEL386REGISTERS ============
    
    ; Segment registers
    movzx       eax, word [ebp + FP_DS]
    mov         word [edi + R_DS], ax
    movzx       eax, word [ebp + FP_ES]
    mov         word [edi + R_ES], ax
    movzx       eax, word [ebp + FP_FS]
    mov         word [edi + R_FS], ax
    movzx       eax, word [ebp + FP_GS]
    mov         word [edi + R_GS], ax
    
    ; Hardware frame registers (with error code offset)
    mov         eax, [ebp + FP_EFLAGS_ERR]
    mov         [edi + R_EFlags], eax
    mov         eax, [ebp + FP_EIP_ERR]
    mov         [edi + R_EIP], eax
    movzx       eax, word [ebp + FP_CS_ERR]
    mov         word [edi + R_CS], ax
    
    ; General purpose registers from PUSHAD
    mov         eax, [ebp + FP_EAX]
    mov         [edi + R_EAX], eax
    mov         eax, [ebp + FP_EBX]
    mov         [edi + R_EBX], eax
    mov         eax, [ebp + FP_ECX]
    mov         [edi + R_ECX], eax
    mov         eax, [ebp + FP_EDX]
    mov         [edi + R_EDX], eax
    mov         eax, [ebp + FP_ESI]
    mov         [edi + R_ESI], eax
    mov         eax, [ebp + FP_EDI]
    mov         [edi + R_EDI], eax
    mov         eax, [ebp + FP_EBP_OLD]
    mov         [edi + R_EBP], eax
    
    ; Default stack info (ring 0 -> ring 0 case)
    pop         eax                         ; Restore original SS
    mov         word [edi + R_SS], ax
    mov         eax, [ebp + FP_ESP_ENTRY]   ; ESP at ISR entry
    mov         [edi + R_ESP], eax
    
    ; Check if we came from user mode
    mov         eax, [ebp + FP_CS_ERR]
    test        eax, 3
    jz          %%ring0_entry
    
    ; Ring 3 -> Ring 0: use user ESP/SS from hardware frame
    mov         eax, [ebp + FP_USER_ESP_ERR]
    mov         [edi + R_ESP], eax
    movzx       eax, word [ebp + FP_USER_SS_ERR]
    mov         word [edi + R_SS], ax
    
%%ring0_entry:
    
    ; Control registers
    mov         dword [edi + R_CR0], 0
    mov         dword [edi + R_CR2], 0
    mov         dword [edi + R_CR3], 0
    mov         dword [edi + R_CR4], 0
    
    ; Debug registers
    mov         eax, dr0
    mov         [edi + R_DR0], eax
    mov         eax, dr1
    mov         [edi + R_DR1], eax
    mov         eax, dr2
    mov         [edi + R_DR2], eax
    mov         eax, dr3
    mov         [edi + R_DR3], eax
    mov         eax, dr6
    mov         [edi + R_DR6], eax
    mov         eax, dr7
    mov         [edi + R_DR7], eax
    
    mov         dword [edi + R_DR4], 0
    mov         dword [edi + R_DR5], 0
    
    ; Frame metadata
    mov         dword [edi + F_IntNo], %1
    mov         eax, [ebp + FP_ERR_CODE]    ; Get error code from stack
    mov         [edi + F_ErrCode], eax
    
    ; Call C handler
    push        edi
    call        %2
    add         esp, 4
%endmacro

; ------------------- Build synthetic exit stack (NO error code) ------------------
%macro BUILD_EXIT_STACK_NOERR 1
    mov         esi, %1                     ; ESI = pointer to NextFrame (preserve across ops)
    
    ; Calculate target stack layout
    ; We need: [GS][FS][ES][DS][PUSHAD][HW_FRAME][USER_ESP,SS if CPL3]
    
    ; Determine stack size needed
    mov         eax, 4 + 4 + 4 + 4          ; 4 segment pushes
    add         eax, 32                     ; PUSHAD (8 * 4 bytes)
    add         eax, 12                     ; Basic HW frame: EIP, CS, EFLAGS
    
    ; Check if target is ring 3
    movzx       ecx, word [esi + R_CS]
    test        ecx, 3
    jz          %%calc_done
    add         eax, 8                      ; Add space for User ESP + SS
    
%%calc_done:
    ; Calculate new ESP: NextFrame.ESP - stack_size
    mov         edx, [esi + R_ESP]          ; EDX = target ESP for task
    sub         edx, eax                    ; EDX = base of new stack layout
    
    ; Build the stack from bottom to top
    
    ; Hardware frame first (at highest addresses)
    mov         eax, [esi + R_EIP]
    mov         [edx + 16 + 32 + 0], eax    ; EIP
    movzx       eax, word [esi + R_CS]
    mov         [edx + 16 + 32 + 4], eax    ; CS (zero-extended)
    mov         eax, [esi + R_EFlags]
    mov         [edx + 16 + 32 + 8], eax    ; EFLAGS
    
    ; Add user stack info if needed
    movzx       eax, word [esi + R_CS]
    test        eax, 3
    jz          %%build_pushad
    mov         eax, [esi + R_ESP]
    mov         [edx + 16 + 32 + 12], eax   ; User ESP
    movzx       eax, word [esi + R_SS]
    mov         [edx + 16 + 32 + 16], eax   ; User SS
    
%%build_pushad:
    ; PUSHAD layout: EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX
    mov         eax, [esi + R_EDI]
    mov         [edx + 16 + 0], eax         ; EDI
    mov         eax, [esi + R_ESI]
    mov         [edx + 16 + 4], eax         ; ESI
    mov         eax, [esi + R_EBP]
    mov         [edx + 16 + 8], eax         ; EBP
    mov         eax, [esi + R_ESP]
    mov         [edx + 16 + 12], eax        ; ESP (will be ignored by POPAD)
    mov         eax, [esi + R_EBX]
    mov         [edx + 16 + 16], eax        ; EBX
    mov         eax, [esi + R_EDX]
    mov         [edx + 16 + 20], eax        ; EDX
    mov         eax, [esi + R_ECX]
    mov         [edx + 16 + 24], eax        ; ECX
    mov         eax, [esi + R_EAX]
    mov         [edx + 16 + 28], eax        ; EAX
    
    ; Segment registers
    movzx       eax, word [esi + R_DS]
    mov         [edx + 0], eax              ; DS
    movzx       eax, word [esi + R_ES]
    mov         [edx + 4], eax              ; ES
    movzx       eax, word [esi + R_FS]
    mov         [edx + 8], eax              ; FS
    movzx       eax, word [esi + R_GS]
    mov         [edx + 12], eax             ; GS
    
    ; Switch to the new stack
    mov         esp, edx
%endmacro

%macro ISR_RETURN 0
    ; Clean up our local frame
    add         esp, SIZE_FRAME             ; Free INTERRUPTFRAME
    add         esp, 4                      ; Remove saved SS
    pop         ebp                         ; Restore EBP
    
    ; Now we're back to the standard ISR exit layout
    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad
    iretd
%endmacro

%macro ISR_RETURN_ERR 0
    ; Clean up our local frame
    add         esp, SIZE_FRAME             ; Free INTERRUPTFRAME
    add         esp, 4                      ; Remove saved SS
    pop         ebp                         ; Restore EBP
    
    ; Now we're back to the standard ISR exit layout
    pop         gs
    pop         fs
    pop         es
    pop         ds
    popad
    add         esp, 4                      ; Skip error code pushed by CPU
    iretd
%endmacro

%macro ISR_PANIC_HALT 0
    cli
%%hang:
    hlt
    jmp         %%hang
%endmacro
