
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

; After pushad, saved GPRs sit 32 bytes below ESP; HW frame is above them.
%define OFF_SAVED_GPRS   (SIZE_FRAME + 16)          ; +0: EDI, +4: ESI, +8: EBP, +12: ESP(sv), +16: EBX, +20: EDX, +24: ECX, +28: EAX
%define OFF_HW_NOERR     (SIZE_FRAME + 16 + 32)     ; +0:EIP, +4:CS, +8:EFLAGS, (+12:UserESP, +16:UserSS if CPL3)

; ------------------- Build frame (NO error code) and call C --------------------
; Usage: ISR_BUILD_FRAME_NOERR_AND_CALL IntNo, C_HandlerLabel
%macro ISR_BUILD_FRAME_NOERR_AND_CALL 2
    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    ; Capture original segment selectors BEFORE EnterKernel (no stack net effect)
    ; DS/ES via MOV, FS/GS via PUSH/POP (works in 32-bit mode)
    mov         ax, ds
    mov         bx, es
    push        fs
    pop         ecx                    ; ECX low16 = FS
    push        gs
    pop         edx                    ; EDX low16 = GS

    call        EnterKernel            ; switch to ring0 segs, preserve regs we just captured

    ; Reserve frame
    sub         esp, SIZE_FRAME
    mov         edi, esp               ; EDI = &frame

    ; ---------------- Fill INTEL386REGISTERS ----------------
    ; HW triplet
    mov         eax, [esp + OFF_HW_NOERR + 8]      ; EFLAGS
    mov         [edi + R_EFlags], eax
    mov         eax, [esp + OFF_HW_NOERR + 0]      ; EIP
    mov         [edi + R_EIP],   eax
    movzx       eax, word [esp + OFF_HW_NOERR + 4] ; CS
    mov         word [edi + R_CS], ax

    mov         dword [edi + R_ESP], 0
    mov         word [edi + R_SS], 0

    ; Saved GPRs (pushad order)
    mov         eax, [esp + OFF_SAVED_GPRS + 28]   ; EAX
    mov         [edi + R_EAX], eax
    mov         eax, [esp + OFF_SAVED_GPRS + 16]   ; EBX
    mov         [edi + R_EBX], eax
    mov         eax, [esp + OFF_SAVED_GPRS + 24]   ; ECX
    mov         [edi + R_ECX], eax
    mov         eax, [esp + OFF_SAVED_GPRS + 20]   ; EDX
    mov         [edi + R_EDX], eax
    mov         eax, [esp + OFF_SAVED_GPRS + 4]    ; ESI
    mov         [edi + R_ESI], eax
    mov         eax, [esp + OFF_SAVED_GPRS + 0]    ; EDI
    mov         [edi + R_EDI], eax
    mov         eax, [esp + OFF_SAVED_GPRS + 8]    ; EBP
    mov         [edi + R_EBP], eax

    ; ESP/SS meaningful only if CPL3 -> copy UserESP/SS, else keep 0
    mov         eax, [esp + OFF_HW_NOERR + 4]      ; CS
    test        eax, 3
    jz          %%no_user

    mov         eax, [esp + OFF_HW_NOERR + 12]     ; User ESP
    mov         [edi + R_ESP], eax
    movzx       eax, word [esp + OFF_HW_NOERR + 16]; User SS
    mov         word [edi + R_SS], ax

%%no_user:

    ; Original segment regs captured before EnterKernel
    mov         word [edi + R_DS], ax              ; DS
    mov         word [edi + R_ES], bx              ; ES
    mov         word [edi + R_FS], cx              ; FS (low16 of ECX)
    mov         word [edi + R_GS], dx              ; GS (low16 of EDX)

    ; Control/Debug registers not captured here -> set to 0
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
    mov         edx, dr4
    mov         dword [edi + R_DR4], edx
    mov         edx, dr5
    mov         dword [edi + R_DR5], edx
    mov         edx, dr6
    mov         dword [edi + R_DR6], edx
    mov         edx, dr7
    mov         dword [edi + R_DR7], edx
    pop         edx

    ; ---------------- Tail: IntNo / ErrCode -----------------
    mov     dword [edi + F_IntNo],  %1
    mov     dword [edi + F_ErrCode], 0

    ; Call C handler: void Handler(INTERRUPTFRAME* Frame)
    push        edi
    call        %2
    add         esp, 4
%endmacro

; ------------------- Variant: with CPU error code on stack ------------------
; For exceptions that push an error code (#TS, #NP, #SS, #GP, #PF, #AC, etc.).
; Hardware layout at entry (top->bottom): [err][EIP][CS][EFLAGS][(UserESP,UserSS if CPL3)]
%define OFF_HW_ERR       (SIZE_FRAME + 16 + 32)     ; base of EIP; error code at +12
%define OFF_ERR_WORD     (OFF_HW_ERR + 12)          ; location of error code dword

%macro ISR_BUILD_FRAME_ERR_AND_CALL 2
    cli
    pushad
    push        ds
    push        es
    push        fs
    push        gs

    ; Capture original segment selectors BEFORE EnterKernel
    mov         ax, ds
    mov         bx, es
    push        fs
    pop         ecx                    ; ECX low16 = FS
    push        gs
    pop         edx                    ; EDX low16 = GS

    call        EnterKernel            ; switch to ring0 segs

    ; Reserve frame
    sub         esp, SIZE_FRAME
    mov         edi, esp               ; EDI = &frame

    ; ---------------- Fill INTEL386REGISTERS ----------------
    ; HW triplet
    mov         eax, [esp + OFF_HW_ERR + 8]      ; EFLAGS
    mov         [edi + R_EFlags], eax
    mov         eax, [esp + OFF_HW_ERR + 0]      ; EIP
    mov         [edi + R_EIP],   eax
    movzx       eax, word [esp + OFF_HW_ERR + 4] ; CS
    mov         word [edi + R_CS], ax

    mov         dword [edi + R_ESP], 0
    mov         word  [edi + R_SS],  0

    ; Saved GPRs (pushad order)
    mov         eax, [esp + OFF_SAVED_GPRS + 28]   ; EAX
    mov         [edi + R_EAX], eax
    mov         eax, [esp + OFF_SAVED_GPRS + 16]   ; EBX
    mov         [edi + R_EBX], eax
    mov         eax, [esp + OFF_SAVED_GPRS + 24]   ; ECX
    mov         [edi + R_ECX], eax
    mov         eax, [esp + OFF_SAVED_GPRS + 20]   ; EDX
    mov         [edi + R_EDX], eax
    mov         eax, [esp + OFF_SAVED_GPRS + 4]    ; ESI
    mov         [edi + R_ESI], eax
    mov         eax, [esp + OFF_SAVED_GPRS + 0]    ; EDI
    mov         [edi + R_EDI], eax
    mov         eax, [esp + OFF_SAVED_GPRS + 8]    ; EBP
    mov         [edi + R_EBP], eax

    ; ESP/SS meaningful only if CPL3 -> copy UserESP/SS
    mov         eax, [esp + OFF_HW_ERR + 4]        ; CS
    test        eax, 3
    jz          %%no_user

    mov         eax, [esp + OFF_HW_ERR + 16]       ; User ESP (shifted by error code)
    mov         [edi + R_ESP], eax
    movzx       eax, word [esp + OFF_HW_ERR + 20]  ; User SS (shifted by error code)
    mov         word [edi + R_SS], ax

%%no_user:

    ; Original segment regs captured before EnterKernel
    mov         word [edi + R_DS], ax              ; DS
    mov         word [edi + R_ES], bx              ; ES
    mov         word [edi + R_FS], cx              ; FS (low16 of ECX)
    mov         word [edi + R_GS], dx              ; GS (low16 of EDX)

    ; Control/Debug registers not captured here -> set to 0
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
    mov         edx, dr4
    mov         dword [edi + R_DR4], edx
    mov         edx, dr5
    mov         dword [edi + R_DR5], edx
    mov         edx, dr6
    mov         dword [edi + R_DR6], edx
    mov         edx, dr7
    mov         dword [edi + R_DR7], edx
    pop         edx

    ; ---------------- Tail: IntNo / ErrCode -----------------
    mov     dword [edi + F_IntNo],  %1
    mov     eax,  [esp + OFF_ERR_WORD]
    mov     dword [edi + F_ErrCode], eax

    ; Call C handler: void Handler(INTERRUPTFRAME* Frame)
    push        edi
    call        %2
    add         esp, 4
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
    add         esp, 4              ; discard error code pushed by CPU
    iretd
%endmacro

%macro ISR_PANIC_HALT 0
    cli
%%hang:
    hlt
    jmp     %%hang
%endmacro
