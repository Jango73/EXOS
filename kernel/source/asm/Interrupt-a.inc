
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

; =================== INTEL386REGISTERS layout (no padding issues) ===================
; Offsets inside INTEL386REGISTERS (bytes)

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

; Layout notes for NO error code path:
; - After PUSHAD: 32 bytes saved GPRs at S0-32 .. S0-1
; - Then we PUSH DS/ES/FS/GS (each 4 bytes in 32-bit mode): GS@[S0-48], FS@[S0-44], ES@[S0-40], DS@[S0-36]
; - Hardware frame (no error code): EIP@[S0+0], CS@[S0+4], EFLAGS@[S0+8], [UserESP@[S0+12], UserSS@[S0+16] if CPL3]

%define OFF_SAVED_GPRS   (SIZE_FRAME + 16)          ; +0: EDI, +4: ESI, +8: EBP, +12: ESP(saved at ISR entry), +16: EBX, +20: EDX, +24: ECX, +28: EAX
%define OFF_HW_NOERR     (SIZE_FRAME + 16 + 32)     ; +0:EIP, +4:CS, +8:EFLAGS, (+12:UserESP, +16:UserSS if CPL3)

%define OFF_HW_ERR       (SIZE_FRAME + 16 + 32 + 4) ; base at EIP (EIP @ +0, CS @ +4, EFLAGS @ +8)
%define OFF_ERR_WORD     (OFF_HW_ERR - 4)           ; error code sits 4 bytes before EIP

%define OFF_SAVED_EAX   (OFF_SAVED_GPRS + 28)
%define OFF_SAVED_EBX   (OFF_SAVED_GPRS + 16)
%define OFF_SAVED_ECX   (OFF_SAVED_GPRS + 24)
%define OFF_SAVED_EDX   (OFF_SAVED_GPRS + 20)

%define OFF_SAVED_ESI   (OFF_SAVED_GPRS + 4)
%define OFF_SAVED_EDI   (OFF_SAVED_GPRS + 0)
%define OFF_SAVED_EBP   (OFF_SAVED_GPRS + 8)

%define OFF_SAVED_CS    (OFF_HW_NOERR + 4)
%define OFF_SAVED_ESP   (OFF_HW_NOERR + 12)         ; User ESP (only if CPL3)
%define OFF_SAVED_SS    (OFF_HW_NOERR + 16)         ; User SS (only if CPL3)

; Helpers to read/write the four pushed segment selectors (GS,FS,ES,DS) just above PUSHAD
%define OFF_PUSHED_GS   (SIZE_FRAME + 0)
%define OFF_PUSHED_FS   (SIZE_FRAME + 4)
%define OFF_PUSHED_ES   (SIZE_FRAME + 8)
%define OFF_PUSHED_DS   (SIZE_FRAME + 12)

; ------------------- Build frame (NO error code) --------------------
; Usage: ISR_BUILD_FRAME_NOERR IntNo
%macro ISR_BUILD_FRAME_NOERR 1
    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    ; Capture SS explicitly, zero-extended to 32-bit and into a callee-saved reg (ESI).
    push        ss
    pop         esi                            ; ESI = SS at ISR entry (zero-extended)

    call        EnterKernel                    ; switch to ring0 segs

    ; Reserve frame
    sub         esp, SIZE_FRAME
    mov         edi, esp                       ; EDI = &frame

    ; ---------------- Fill INTEL386REGISTERS ----------------
    ; Segment selectors from the four pushed dwords (stable across the call)
    movzx       eax, word [esp + OFF_PUSHED_DS]
    mov         word [edi + R_DS], ax
    movzx       eax, word [esp + OFF_PUSHED_ES]
    mov         word [edi + R_ES], ax
    movzx       eax, word [esp + OFF_PUSHED_FS]
    mov         word [edi + R_FS], ax
    movzx       eax, word [esp + OFF_PUSHED_GS]
    mov         word [edi + R_GS], ax

    ; HW triplet
    mov         eax, [esp + OFF_HW_NOERR + 8]      ; EFLAGS
    mov         [edi + R_EFlags], eax
    mov         eax, [esp + OFF_HW_NOERR + 0]      ; EIP
    mov         [edi + R_EIP],   eax
    movzx       eax, word [esp + OFF_HW_NOERR + 4] ; CS
    mov         word [edi + R_CS], ax

    ; Default to ring0->ring0 entry: SS from captured ESI, ESP from saved PUSHAD slot.
    mov         ax, si
    mov         word [edi + R_SS], ax
    mov         eax, [esp + OFF_SAVED_GPRS + 12]   ; ESP saved by PUSHAD = ESP at ISR entry
    mov         [edi + R_ESP], eax

    ; Saved GPRs (pushad order)
    mov         eax, [esp + OFF_SAVED_EAX]         ; EAX
    mov         [edi + R_EAX], eax
    mov         eax, [esp + OFF_SAVED_EBX]         ; EBX
    mov         [edi + R_EBX], eax
    mov         eax, [esp + OFF_SAVED_ECX]         ; ECX
    mov         [edi + R_ECX], eax
    mov         eax, [esp + OFF_SAVED_EDX]         ; EDX
    mov         [edi + R_EDX], eax
    mov         eax, [esp + OFF_SAVED_ESI]         ; ESI
    mov         [edi + R_ESI], eax
    mov         eax, [esp + OFF_SAVED_EDI]         ; EDI
    mov         [edi + R_EDI], eax
    mov         eax, [esp + OFF_SAVED_EBP]         ; EBP
    mov         [edi + R_EBP], eax

    ; If CPL3, overwrite with UserESP/SS from HW frame
    mov         eax, [esp + OFF_SAVED_CS]          ; CS
    test        eax, 3
    jz          %%no_user
    mov         eax, [esp + OFF_SAVED_ESP]         ; User ESP
    mov         [edi + R_ESP], eax
    movzx       eax, word [esp + OFF_SAVED_SS]     ; User SS
    mov         word [edi + R_SS], ax
%%no_user:

    ; Control/Debug registers: zero unknowns, fill safe subset (avoid DR4/DR5 reads).
    mov         dword [edi + R_CR0], 0
    mov         dword [edi + R_CR2], 0
    mov         dword [edi + R_CR3], 0
    mov         dword [edi + R_CR4], 0

    push        edx
    mov         edx, dr0
    mov         dword [edi + R_DR0], edx
    mov         edx, dr1
    mov         dword [edi + R_DR1], edx
    mov         edx, dr2
    mov         dword [edi + R_DR2], edx
    mov         edx, dr3
    mov         dword [edi + R_DR3], edx
    mov         edx, dr6
    mov         dword [edi + R_DR6], edx
    mov         edx, dr7
    mov         dword [edi + R_DR7], edx
    pop         edx

    mov         dword [edi + R_DR4], 0
    mov         dword [edi + R_DR5], 0

    ; ---------------- Tail: IntNo / ErrCode -----------------
    mov         dword [edi + F_IntNo],  %1
    mov         dword [edi + F_ErrCode], 0
%endmacro

%macro ISR_BUILD_FRAME_NOERR_AND_CALL 2
    ISR_BUILD_FRAME_NOERR %1

    ; Call C handler: void Handler(INTERRUPTFRAME* Frame)
    push        edi
    call        %2
    add         esp, 4
%endmacro

; ------------------- Variant: with CPU error code on stack ------------------
; For exceptions that push an error code (#TS, #NP, #SS, #GP, #PF, #AC, etc.).
; Hardware layout at entry (top->bottom): [err][EIP][CS][EFLAGS][(UserESP,UserSS if CPL3)]
%macro ISR_BUILD_FRAME_ERR_AND_CALL 2
    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    ; Capture SS explicitly into ESI
    push        ss
    pop         esi

    call        EnterKernel                ; switch to ring0 segs

    ; Reserve frame
    sub         esp, SIZE_FRAME
    mov         edi, esp                   ; EDI = &frame

    ; ---------------- Fill INTEL386REGISTERS ----------------
    movzx       eax, word [esp + OFF_PUSHED_DS]
    mov         word [edi + R_DS], ax
    movzx       eax, word [esp + OFF_PUSHED_ES]
    mov         word [edi + R_ES], ax
    movzx       eax, word [esp + OFF_PUSHED_FS]
    mov         word [edi + R_FS], ax
    movzx       eax, word [esp + OFF_PUSHED_GS]
    mov         word [edi + R_GS], ax

    ; HW triplet
    mov         eax, [esp + OFF_HW_ERR + 8]        ; EFLAGS
    mov         [edi + R_EFlags], eax
    mov         eax, [esp + OFF_HW_ERR + 0]        ; EIP
    mov         [edi + R_EIP],   eax
    movzx       eax, word [esp + OFF_HW_ERR + 4]   ; CS
    mov         word [edi + R_CS], ax

    ; Default to ring0->ring0 entry
    mov         ax, si
    mov         word [edi + R_SS], ax
    mov         eax, [esp + OFF_SAVED_GPRS + 12]   ; ESP at ISR entry
    mov         [edi + R_ESP], eax

    ; Saved GPRs (pushad order)
    mov         eax, [esp + OFF_SAVED_EAX]         ; EAX
    mov         [edi + R_EAX], eax
    mov         eax, [esp + OFF_SAVED_EBX]         ; EBX
    mov         [edi + R_EBX], eax
    mov         eax, [esp + OFF_SAVED_ECX]         ; ECX
    mov         [edi + R_ECX], eax
    mov         eax, [esp + OFF_SAVED_EDX]         ; EDX
    mov         [edi + R_EDX], eax
    mov         eax, [esp + OFF_SAVED_ESI]         ; ESI
    mov         [edi + R_ESI], eax
    mov         eax, [esp + OFF_SAVED_EDI]         ; EDI
    mov         [edi + R_EDI], eax
    mov         eax, [esp + OFF_SAVED_EBP]         ; EBP
    mov         [edi + R_EBP], eax

    ; If CPL3, overwrite with UserESP/SS from HW frame
    mov         eax, [esp + OFF_HW_ERR + 4]        ; CS
    test        eax, 3
    jz          %%no_user
    mov         eax, [esp + OFF_HW_ERR + 12]       ; User ESP
    mov         [edi + R_ESP], eax
    movzx       eax, word [esp + OFF_HW_ERR + 16]  ; User SS
    mov         word [edi + R_SS], ax
%%no_user:

    ; Control/Debug registers
    mov         dword [edi + R_CR0], 0
    mov         dword [edi + R_CR2], 0
    mov         dword [edi + R_CR3], 0
    mov         dword [edi + R_CR4], 0

    push        edx
    mov         edx, dr0
    mov         dword [edi + R_DR0], edx
    mov         edx, dr1
    mov         dword [edi + R_DR1], edx
    mov         edx, dr2
    mov         dword [edi + R_DR2], edx
    mov         edx, dr3
    mov         dword [edi + R_DR3], edx
    mov         edx, dr6
    mov         dword [edi + R_DR6], edx
    mov         edx, dr7
    mov         dword [edi + R_DR7], edx
    pop         edx

    mov         dword [edi + R_DR4], 0
    mov         dword [edi + R_DR5], 0

    ; ---------------- Tail: IntNo / ErrCode -----------------
    mov         dword [edi + F_IntNo],  %1
    mov         eax,  [esp + OFF_ERR_WORD]
    mov         dword [edi + F_ErrCode], eax

    ; Call C handler: void Handler(INTERRUPTFRAME* Frame)
    push        edi
    call        %2
    add         esp, 4
%endmacro

; ------------------- Build synthetic exit stack (NO error code) ------------------
; Recreate exactly the stack layout expected by ISR_RETURN (NO error code).
; IN: %1 = register containing pointer to INTERRUPTFRAME (NextFrame)
; ------------------- Build synthetic exit stack (NO error code) - CORRIGÉE ------------------
; Recreate exactly the stack layout expected by ISR_RETURN (NO error code).
; IN: %1 = register containing pointer to INTERRUPTFRAME (NextFrame)
%macro BUILD_EXIT_STACK_NOERR 1
    ; Save the frame pointer
    mov     ecx, %1                       ; ECX = pointeur vers NextFrame (stable)
    
    ; Target ESP before ISR_RETURN must be NextS_hw - OFF_HW_NOERR
    mov     edx, [ecx + R_ESP]            ; EDX = NextS_hw (ESP at ISR entry for next task)
    lea     ebx, [edx - OFF_HW_NOERR]     ; EBX = target ESP before ISR_RETURN

    ; [GS][FS][ES][DS] dwords - use ECX instead of %1
    movzx   eax, word [ecx + R_GS]
    mov     [ebx + OFF_PUSHED_GS], eax
    movzx   eax, word [ecx + R_FS]
    mov     [ebx + OFF_PUSHED_FS], eax
    movzx   eax, word [ecx + R_ES]
    mov     [ebx + OFF_PUSHED_ES], eax
    movzx   eax, word [ecx + R_DS]
    mov     [ebx + OFF_PUSHED_DS], eax

    ; PUSHAD block (use the symmetric OFF_SAVED_* offsets) - use ECX
    mov     eax, [ecx + R_EDI]
    mov     [ebx + OFF_SAVED_EDI], eax
    mov     eax, [ecx + R_ESI]
    mov     [ebx + OFF_SAVED_ESI], eax
    mov     eax, [ecx + R_EBP]
    mov     [ebx + OFF_SAVED_EBP], eax
    mov     eax, [ecx + R_ESP]
    mov     [ebx + OFF_SAVED_GPRS + 12], eax      ; original ESP slot
    mov     eax, [ecx + R_EBX]
    mov     [ebx + OFF_SAVED_EBX], eax
    mov     eax, [ecx + R_EDX]
    mov     [ebx + OFF_SAVED_EDX], eax
    mov     eax, [ecx + R_ECX]
    mov     [ebx + OFF_SAVED_ECX], eax
    mov     eax, [ecx + R_EAX]
    mov     [ebx + OFF_SAVED_EAX], eax

    ; HW frame - utiliser ECX
    mov     eax, [ecx + R_EIP]
    mov     [ebx + OFF_HW_NOERR + 0], eax         ; EIP
    movzx   eax, word [ecx + R_CS]
    mov     [ebx + OFF_HW_NOERR + 4], eax         ; CS as dword
    mov     eax, [ecx + R_EFlags]
    mov     [ebx + OFF_HW_NOERR + 8], eax         ; EFLAGS

    ; If CPL3, append UserESP/UserSS - utiliser ECX
    movzx   eax, word [ecx + R_CS]
    test    eax, 3
    jz      %%no_user
    mov     eax, [ecx + R_ESP]
    mov     [ebx + OFF_SAVED_ESP], eax            ; User ESP
    movzx   eax, word [ecx + R_SS]
    mov     [ebx + OFF_SAVED_SS], eax             ; User SS
%%no_user:

    ; Pivot to the synthesized stack
    mov     esp, ebx
%endmacro

%macro ISR_RETURN 0
    add         esp, SIZE_FRAME      ; free the reserved INTERRUPTFRAME

    pop         gs
    pop         fs
    pop         es
    pop         ds

    popad
    iretd
%endmacro

%macro ISR_RETURN_ERR 0
    add         esp, SIZE_FRAME      ; free the reserved INTERRUPTFRAME

    pop         gs
    pop         fs
    pop         es
    pop         ds

    popad
    add         esp, 4               ; discard error code pushed by CPU
    iretd
%endmacro

%macro ISR_PANIC_HALT 0
    cli
%%hang:
    hlt
    jmp     %%hang
%endmacro
