BITS 64
default rel

section .text

global StubJumpToImage
global LongModeEntry

%define KERNEL_LOAD_ADDRESS 0x200000
%define TRANSITION_STACK_TOP 0x0000A000
%define MARK_BASE_X 8
%define MARK_BASE_Y 8
%define MARK_SIZE 8
%define MARK_GAP 4
%define MARK_STRIDE (MARK_SIZE + MARK_GAP)
%define MARK_INDEX_PRE_CR3 5
%define MARK_INDEX_CR3_PRETEST 6
%define MARK_INDEX_POST_CR3 7
%define MARK_INDEX_LONGMODE 8

extern VbrLongModeCodeSelector
extern VbrLongModeDataSelector
extern UefiStubMultibootInfoPtr
extern UefiStubMultibootMagic
extern UefiStubFramebufferBase
extern UefiStubFramebufferPitch
extern UefiStubFramebufferBytesPerPixel
extern UefiStubTestOnly

; Parameters (Microsoft x64 ABI)
; RCX: GDTR
; RDX: Paging structure physical address (CR3)
; R8 : Kernel entry low
; R9 : Kernel entry high
; [RSP+0x20]: Multiboot info pointer
; [RSP+0x28]: Multiboot magic

StubJumpToImage:
    cli
    ; Read parameters from globals instead of stack (bare metal stack offsets are unreliable).
    mov     r10d, dword [rel UefiStubMultibootInfoPtr]
    mov     r11d, dword [rel UefiStubMultibootMagic]

    mov     r12, rcx
    mov     r13, rdx
    mov     r14d, r8d
    mov     r15d, r9d

    ; Framebuffer marker before CR3 switch.
    mov         r9, qword [rel UefiStubFramebufferBase]
    test        r9, r9
    jz          .skip_fb_mark_pre_cr3
    mov         edx, dword [rel UefiStubFramebufferPitch]
    test        edx, edx
    jz          .skip_fb_mark_pre_cr3
    mov         ecx, dword [rel UefiStubFramebufferBytesPerPixel]
    test        ecx, ecx
    jnz         .fb_bpp_ok_pre_cr3
    mov         ecx, 4
.fb_bpp_ok_pre_cr3:
    mov         eax, MARK_BASE_Y
    imul        eax, edx
    add         r9, rax
    mov         eax, MARK_BASE_X + (MARK_STRIDE * MARK_INDEX_PRE_CR3)
    imul        eax, ecx
    add         r9, rax

    xor         rsi, rsi
.fb_row_pre_cr3:
    mov         rax, rsi
    imul        rax, rdx
    add         rax, r9
    xor         rdi, rdi
.fb_col_pre_cr3:
    mov         rbx, rdi
    imul        rbx, rcx
    add         rbx, rax
    mov         dword [rbx], 0x00FF8000
    inc         rdi
    cmp         rdi, MARK_SIZE
    jl          .fb_col_pre_cr3
    inc         rsi
    cmp         rsi, MARK_SIZE
    jl          .fb_row_pre_cr3
.skip_fb_mark_pre_cr3:
    mov         eax, dword [rel UefiStubTestOnly]
    test        eax, eax
    jz          .continue_boot
    ; Stub test mode: halt here to confirm stub execution on bare metal.
.stub_test_loop:
    hlt
    jmp         .stub_test_loop
.continue_boot:

    lgdt        [r12]
    mov         ax, [rel VbrLongModeDataSelector]
    mov         ds, ax
    mov         es, ax
    mov         ss, ax
    mov         fs, ax
    mov         gs, ax

    ; CR3 pre-test marker before loading new CR3.
    mov         r9, qword [rel UefiStubFramebufferBase]
    test        r9, r9
    jz          .skip_fb_mark_cr3_pret
    mov         edx, dword [rel UefiStubFramebufferPitch]
    test        edx, edx
    jz          .skip_fb_mark_cr3_pret
    mov         ecx, dword [rel UefiStubFramebufferBytesPerPixel]
    test        ecx, ecx
    jnz         .fb_bpp_ok_cr3_pret
    mov         ecx, 4
.fb_bpp_ok_cr3_pret:
    mov         eax, MARK_BASE_Y
    imul        eax, edx
    add         r9, rax
    mov         eax, MARK_BASE_X + (MARK_STRIDE * MARK_INDEX_CR3_PRETEST)
    imul        eax, ecx
    add         r9, rax

    xor         rsi, rsi
.fb_row_cr3_pret:
    mov         rax, rsi
    imul        rax, rdx
    add         rax, r9
    xor         rdi, rdi
.fb_col_cr3_pret:
    mov         rbx, rdi
    imul        rbx, rcx
    add         rbx, rax
    mov         dword [rbx], 0x0000FFFF
    inc         rdi
    cmp         rdi, MARK_SIZE
    jl          .fb_col_cr3_pret
    inc         rsi
    cmp         rsi, MARK_SIZE
    jl          .fb_row_cr3_pret
.skip_fb_mark_cr3_pret:

    mov         eax, r13d
    mov         cr3, rax
    mov         rsp, TRANSITION_STACK_TOP

    ; Framebuffer marker after CR3 switch.
    mov         rax, r10
    test        rax, rax
    jz          .skip_fb_mark_cr3
    mov         eax, dword [r10 + 88]       ; framebuffer_addr_low
    mov         r8d, dword [r10 + 92]       ; framebuffer_addr_high
    mov         r9, r8
    shl         r9, 32
    or          r9, rax
    test        r9, r9
    jz          .skip_fb_mark_cr3
    mov         edx, dword [r10 + 96]       ; framebuffer_pitch
    test        edx, edx
    jz          .skip_fb_mark_cr3
    movzx       ecx, byte [r10 + 108]       ; framebuffer_bpp
    shr         ecx, 3
    test        ecx, ecx
    jnz         .fb_bpp_ok_cr3
    mov         ecx, 4
.fb_bpp_ok_cr3:
    mov         eax, MARK_BASE_Y
    imul        eax, edx
    add         r9, rax
    mov         eax, MARK_BASE_X + (MARK_STRIDE * MARK_INDEX_POST_CR3)
    imul        eax, ecx
    add         r9, rax

    xor         rsi, rsi
.fb_row_cr3:
    mov         rax, rsi
    imul        rax, rdx
    add         rax, r9
    xor         rdi, rdi
.fb_col_cr3:
    mov         rbx, rdi
    imul        rbx, rcx
    add         rbx, rax
    mov         dword [rbx], 0x00FFFF00
    inc         rdi
    cmp         rdi, MARK_SIZE
    jl          .fb_col_cr3
    inc         rsi
    cmp         rsi, MARK_SIZE
    jl          .fb_row_cr3
.skip_fb_mark_cr3:

    mov         rax, r15
    shl         rax, 32
    or          rax, r14
    mov         r14, rax

    mov         r12d, r10d
    mov         r13d, r11d

    mov         ax, [rel VbrLongModeCodeSelector]
    push        rax
    lea         rax, [LongModeEntry]
    push        rax
    retfq

LongModeEntry:
    mov         ax, [rel VbrLongModeDataSelector]
    mov         ds, ax
    mov         es, ax
    mov         ss, ax
    mov         fs, ax
    mov         gs, ax

    mov         rsp, KERNEL_LOAD_ADDRESS
    mov         rbp, rsp

    ; Framebuffer marker to confirm LongModeEntry reached.
    mov         rax, r12
    test        rax, rax
    jz          .skip_fb_mark
    mov         eax, dword [r12 + 88]       ; framebuffer_addr_low
    mov         r8d, dword [r12 + 92]       ; framebuffer_addr_high
    mov         r9, r8
    shl         r9, 32
    or          r9, rax
    test        r9, r9
    jz          .skip_fb_mark
    mov         edx, dword [r12 + 96]       ; framebuffer_pitch
    test        edx, edx
    jz          .skip_fb_mark
    movzx       ecx, byte [r12 + 108]       ; framebuffer_bpp
    shr         ecx, 3
    test        ecx, ecx
    jnz         .fb_bpp_ok
    mov         ecx, 4
.fb_bpp_ok:
    mov         eax, MARK_BASE_Y
    imul        eax, edx
    add         r9, rax
    mov         eax, MARK_BASE_X + (MARK_STRIDE * MARK_INDEX_LONGMODE)
    imul        eax, ecx
    add         r9, rax

    xor         rsi, rsi
.fb_row:
    mov         rax, rsi
    imul        rax, rdx
    add         rax, r9
    xor         rdi, rdi
.fb_col:
    mov         rbx, rdi
    imul        rbx, rcx
    add         rbx, rax
    mov         dword [rbx], 0x00FF00FF
    inc         rdi
    cmp         rdi, MARK_SIZE
    jl          .fb_col
    inc         rsi
    cmp         rsi, MARK_SIZE
    jl          .fb_row
.skip_fb_mark:

    mov         eax, r13d
    mov         rbx, r12
    mov         rdx, r14
    jmp         rdx

section .data

global _fltused
global __fltused
_fltused:
__fltused:
    dq 0
