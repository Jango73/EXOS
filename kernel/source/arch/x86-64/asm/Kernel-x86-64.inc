%ifndef KERNEL_X86_64_INC
%define KERNEL_X86_64_INC

KERNEL_ARCH_MAX_UINT equ 0xFFFFFFFFFFFFFFFF

%include "Kernel.inc"

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

%endif ; KERNEL_X86_64_INC
