; EXOS UEFI jump stub (x86-32)

BITS 32

section .text

global StubJumpToImage
global _StubJumpToImage
global ProtectedEntryPoint

%define KERNEL_LOAD_ADDRESS 0x200000

StubJumpToImage:
_StubJumpToImage:
    push        ebp
    mov         ebp, esp

    cli

    mov         eax, [ebp + 8]              ; GDTR
    lgdt        [eax]

    mov         eax, [ebp + 12]             ; Page directory
    mov         cr3, eax

    mov         eax, cr0
    or          eax, 0x80000000
    mov         cr0, eax
    jmp         0x08:ProtectedEntryPoint

ProtectedEntryPoint:
    mov         ax, 0x10
    mov         ds, ax
    mov         es, ax
    mov         ss, ax
    mov         esp, KERNEL_LOAD_ADDRESS

    mov         eax, [ebp + 28]             ; Multiboot magic
    mov         ebx, [ebp + 24]             ; Multiboot info pointer
    mov         edx, [ebp + 16]             ; Kernel entry
    jmp         edx

section .data

global __fltused
__fltused dd 0
