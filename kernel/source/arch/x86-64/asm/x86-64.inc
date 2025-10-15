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
; Bit layout of CR0

CR0_PROTECTED_MODE equ 0x00000001
CR0_PAGING         equ 0x80000000

;-------------------------------------------------------------------------
; Physical and virtual memory addresses

SEGMENT_DESCRIPTOR_SIZE equ 8
GATE_DESCRIPTOR_SIZE    equ 8

IDT_SIZE equ N_4KB
GDT_SIZE equ N_8KB

;-------------------------------------------------------------------------
; Privilege levels and selectors

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

;-------------------------------------------------------------------------
; KernelStartupInfo data layout (partial)

STRUC KernelStartupInfo
    .KernelPhysicalBase RESQ 1
    .KernelSize         RESQ 1
    .StackTop           RESQ 1
    .PageDirectory      RESQ 1
    .IRQMask_21_PM      RESB 4
    .IRQMask_A1_PM      RESB 4
    .IRQMask_21_RM      RESB 4
    .IRQMask_A1_RM      RESB 4
    ; Console cursor position now handled directly in InitializeConsole
    .MemorySize         RESQ 1
    .PageCount          RESQ 1
    .MultibootMemoryEntryCount RESB 4
ENDSTRUC

;-------------------------------------------------------------------------
; Kernel data layout shared with assembly

STRUC KERNELDATA_X86_64
    .IDT RESQ 1
    .GDT RESQ 1
    .TSS RESQ 1
ENDSTRUC

%endif  ; KERNEL_X86_64_INC
