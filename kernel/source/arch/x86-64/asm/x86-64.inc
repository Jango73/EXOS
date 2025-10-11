%ifndef KERNEL_X86_64_INC
%define KERNEL_X86_64_INC

%define KERNEL_ARCH_MAX_UINT 0xFFFFFFFFFFFFFFFF

%include "../../common/Kernel.inc"

;-------------------------------------------------------------------------
;
;   EXOS Kernel
;   x86-64 specific kernel definitions shared with assembly sources
;
;   Currently only exposes the macro helpers shared by the x86-64 stubs.
;
;-------------------------------------------------------------------------

;-------------------------------------------------------------------------
; Macros

%macro FUNC_HEADER 0
    align 16
%endmacro

;-------------------------------------------------------------------------
; Kernel data layout shared with assembly

STRUC KERNELDATA_X86_64
    .IDT RESQ 1
    .GDT RESQ 1
    .TSS RESQ 1
    .PPB RESQ 1
ENDSTRUC

;-------------------------------------------------------------------------
; Descriptor and selector helpers shared with the legacy real-mode bridge

SEGMENT_DESCRIPTOR_SIZE equ 8
GATE_DESCRIPTOR_SIZE    equ 8

IDT_SIZE equ N_4KB
GDT_SIZE equ N_8KB

PRIVILEGE_KERNEL   equ 0x00
PRIVILEGE_DRIVERS  equ 0x01
PRIVILEGE_ROUTINES equ 0x02
PRIVILEGE_USER     equ 0x03

SELECTOR_GLOBAL equ 0x00
SELECTOR_LOCAL  equ 0x04

SELECTOR_NULL        equ 0x00
SELECTOR_KERNEL_CODE equ (0x08 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_KERNEL_DATA equ (0x10 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_USER_CODE   equ (0x18 | SELECTOR_GLOBAL | PRIVILEGE_USER)
SELECTOR_USER_DATA   equ (0x20 | SELECTOR_GLOBAL | PRIVILEGE_USER)
SELECTOR_REAL_CODE   equ (0x28 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_REAL_DATA   equ (0x30 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)

CR0_PROTECTED_MODE equ 0x00000001
CR0_PAGING         equ 0x80000000

%endif ; KERNEL_X86_64_INC
