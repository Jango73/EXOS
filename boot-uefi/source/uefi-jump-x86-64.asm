BITS 64
default rel

section .text

global StubJumpToImage
global LongModeEntry

%define KERNEL_LOAD_ADDRESS 0x200000
%define TRANSITION_STACK_TOP 0x00200000
%define BOOT_MARKER_BASE_X 2
%define BOOT_MARKER_Y_TRANSITION 2
%define BOOT_MARKER_SIZE 8
%define BOOT_MARKER_SPACING 2
%define BOOT_MARKER_STRIDE (BOOT_MARKER_SIZE + BOOT_MARKER_SPACING)
%define BOOT_MARKER_GROUP_SIZE 10
%define BOOT_MARKER_LINE_STRIDE (BOOT_MARKER_SIZE + BOOT_MARKER_SPACING)
%define BOOT_STAGE_STUB_ENTRY 22
%define BOOT_STAGE_STUB_AFTER_CR3 23
%define BOOT_STAGE_LONG_MODE_ENTRY 25
%define BOOT_STAGE_JUMP_KERNEL 26

%ifndef BOOT_STAGE_MARKERS
%define BOOT_STAGE_MARKERS 0
%endif

extern VbrLongModeCodeSelector
extern VbrLongModeDataSelector
extern UefiStubMultibootInfoPtr
extern UefiStubMultibootMagic
extern UefiStubTestOnly
extern UefiStubFramebufferLow
extern UefiStubFramebufferHigh
extern UefiStubFramebufferPitch
extern UefiStubFramebufferBytesPerPixel

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
%if BOOT_STAGE_MARKERS = 1
    ; Stage BOOT_STAGE_STUB_ENTRY: stub entry reached.
    mov         eax, dword [rel UefiStubFramebufferBytesPerPixel]
    cmp         eax, 4
    jne         .skip_marker_20
    mov         eax, dword [rel UefiStubFramebufferHigh]
    shl         rax, 32
    mov         edx, dword [rel UefiStubFramebufferLow]
    or          rax, rdx
    mov         rdi, rax
    mov         ebx, dword [rel UefiStubFramebufferPitch]
    test        ebx, ebx
    jz          .skip_marker_20
    mov         edx, ebx
    imul        edx, (BOOT_MARKER_Y_TRANSITION + ((BOOT_STAGE_STUB_ENTRY / BOOT_MARKER_GROUP_SIZE) * BOOT_MARKER_LINE_STRIDE))
    lea         rsi, [rdi + rdx + (BOOT_MARKER_BASE_X + ((BOOT_STAGE_STUB_ENTRY % BOOT_MARKER_GROUP_SIZE) * BOOT_MARKER_STRIDE)) * 4]
    mov         ecx, 8
.marker20_row:
    mov         dword [rsi + 0],  0x00AA00FF
    mov         dword [rsi + 4],  0x00AA00FF
    mov         dword [rsi + 8],  0x00AA00FF
    mov         dword [rsi + 12], 0x00AA00FF
    mov         dword [rsi + 16], 0x00AA00FF
    mov         dword [rsi + 20], 0x00AA00FF
    mov         dword [rsi + 24], 0x00AA00FF
    mov         dword [rsi + 28], 0x00AA00FF
    add         rsi, rbx
    dec         ecx
    jnz         .marker20_row
.skip_marker_20:
%endif

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

    mov         eax, r13d
    mov         cr3, rax
%if BOOT_STAGE_MARKERS = 1
    ; Stage BOOT_STAGE_STUB_AFTER_CR3: CR3 switched.
    mov         eax, dword [rel UefiStubFramebufferBytesPerPixel]
    cmp         eax, 4
    jne         .skip_marker_21
    mov         eax, dword [rel UefiStubFramebufferHigh]
    shl         rax, 32
    mov         edx, dword [rel UefiStubFramebufferLow]
    or          rax, rdx
    mov         rdi, rax
    mov         ebx, dword [rel UefiStubFramebufferPitch]
    test        ebx, ebx
    jz          .skip_marker_21
    mov         edx, ebx
    imul        edx, (BOOT_MARKER_Y_TRANSITION + ((BOOT_STAGE_STUB_AFTER_CR3 / BOOT_MARKER_GROUP_SIZE) * BOOT_MARKER_LINE_STRIDE))
    lea         rsi, [rdi + rdx + (BOOT_MARKER_BASE_X + ((BOOT_STAGE_STUB_AFTER_CR3 % BOOT_MARKER_GROUP_SIZE) * BOOT_MARKER_STRIDE)) * 4]
    mov         ecx, 8
.marker21_row:
    mov         dword [rsi + 0],  0x0000AAFF
    mov         dword [rsi + 4],  0x0000AAFF
    mov         dword [rsi + 8],  0x0000AAFF
    mov         dword [rsi + 12], 0x0000AAFF
    mov         dword [rsi + 16], 0x0000AAFF
    mov         dword [rsi + 20], 0x0000AAFF
    mov         dword [rsi + 24], 0x0000AAFF
    mov         dword [rsi + 28], 0x0000AAFF
    add         rsi, rbx
    dec         ecx
    jnz         .marker21_row
.skip_marker_21:
%endif
    mov         rsp, TRANSITION_STACK_TOP

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
%if BOOT_STAGE_MARKERS = 1
    ; Stage BOOT_STAGE_LONG_MODE_ENTRY: long mode entry reached.
    mov         eax, dword [rel UefiStubFramebufferBytesPerPixel]
    cmp         eax, 4
    jne         .skip_marker_22
    mov         eax, dword [rel UefiStubFramebufferHigh]
    shl         rax, 32
    mov         edx, dword [rel UefiStubFramebufferLow]
    or          rax, rdx
    mov         rdi, rax
    mov         ebx, dword [rel UefiStubFramebufferPitch]
    test        ebx, ebx
    jz          .skip_marker_22
    mov         edx, ebx
    imul        edx, (BOOT_MARKER_Y_TRANSITION + ((BOOT_STAGE_LONG_MODE_ENTRY / BOOT_MARKER_GROUP_SIZE) * BOOT_MARKER_LINE_STRIDE))
    lea         rsi, [rdi + rdx + (BOOT_MARKER_BASE_X + ((BOOT_STAGE_LONG_MODE_ENTRY % BOOT_MARKER_GROUP_SIZE) * BOOT_MARKER_STRIDE)) * 4]
    mov         ecx, 8
.marker22_row:
    mov         dword [rsi + 0],  0x0000FF00
    mov         dword [rsi + 4],  0x0000FF00
    mov         dword [rsi + 8],  0x0000FF00
    mov         dword [rsi + 12], 0x0000FF00
    mov         dword [rsi + 16], 0x0000FF00
    mov         dword [rsi + 20], 0x0000FF00
    mov         dword [rsi + 24], 0x0000FF00
    mov         dword [rsi + 28], 0x0000FF00
    add         rsi, rbx
    dec         ecx
    jnz         .marker22_row
.skip_marker_22:
%endif
    mov         ax, [rel VbrLongModeDataSelector]
    mov         ds, ax
    mov         es, ax
    mov         ss, ax
    mov         fs, ax
    mov         gs, ax

    mov         rsp, TRANSITION_STACK_TOP
    mov         rbp, rsp

%if BOOT_STAGE_MARKERS = 1
    ; Stage BOOT_STAGE_JUMP_KERNEL: jumping from loader stub to kernel entry.
    mov         eax, dword [rel UefiStubFramebufferBytesPerPixel]
    cmp         eax, 4
    jne         .skip_marker_23
    mov         eax, dword [rel UefiStubFramebufferHigh]
    shl         rax, 32
    mov         edx, dword [rel UefiStubFramebufferLow]
    or          rax, rdx
    mov         rdi, rax
    mov         ebx, dword [rel UefiStubFramebufferPitch]
    test        ebx, ebx
    jz          .skip_marker_23
    mov         edx, ebx
    imul        edx, (BOOT_MARKER_Y_TRANSITION + ((BOOT_STAGE_JUMP_KERNEL / BOOT_MARKER_GROUP_SIZE) * BOOT_MARKER_LINE_STRIDE))
    lea         rsi, [rdi + rdx + (BOOT_MARKER_BASE_X + ((BOOT_STAGE_JUMP_KERNEL % BOOT_MARKER_GROUP_SIZE) * BOOT_MARKER_STRIDE)) * 4]
    mov         ecx, 8
.marker23_row:
    mov         dword [rsi + 0],  0x00FFFFFF
    mov         dword [rsi + 4],  0x00FFFFFF
    mov         dword [rsi + 8],  0x00FFFFFF
    mov         dword [rsi + 12], 0x00FFFFFF
    mov         dword [rsi + 16], 0x00FFFFFF
    mov         dword [rsi + 20], 0x00FFFFFF
    mov         dword [rsi + 24], 0x00FFFFFF
    mov         dword [rsi + 28], 0x00FFFFFF
    add         rsi, rbx
    dec         ecx
    jnz         .marker23_row
.skip_marker_23:
%endif

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
