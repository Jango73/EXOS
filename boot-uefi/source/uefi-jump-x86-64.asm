BITS 64
default rel

section .text

global StubJumpToImage
global LongModeEntry

%define KERNEL_LOAD_ADDRESS 0x200000
%define TRANSITION_STACK_TOP 0x00200000

extern VbrLongModeCodeSelector
extern VbrLongModeDataSelector
extern UefiStubMultibootInfoPtr
extern UefiStubMultibootMagic
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
    mov         ax, [rel VbrLongModeDataSelector]
    mov         ds, ax
    mov         es, ax
    mov         ss, ax
    mov         fs, ax
    mov         gs, ax

    mov         rsp, TRANSITION_STACK_TOP
    mov         rbp, rsp

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
