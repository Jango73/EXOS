
;-------------------------------------------------------------------------
;
;   EXOS Kernel
;   x86-64 specific kernel definitions shared with assembly sources
;
;   Currently only exposes the macro helpers shared by the x86-64 stubs.
;
;-------------------------------------------------------------------------

%ifndef KERNEL_X86_64_INC
%define KERNEL_X86_64_INC

%define KERNEL_ARCH_MAX_UINT 0xFFFFFFFFFFFFFFFF

%include "../../common/Kernel.inc"
%include "../../common/Cpu.inc"

;-------------------------------------------------------------------------
; Physical and virtual memory addresses

SEGMENT_DESCRIPTOR_SIZE equ 8
GATE_DESCRIPTOR_SIZE    equ 8

IDT_SIZE equ N_4KB
GDT_SIZE equ N_8KB

;-------------------------------------------------------------------------
; Privilege levels and selectors

CPU_PRIVILEGE_KERNEL   equ 0x00
CPU_PRIVILEGE_DRIVERS  equ 0x01
CPU_PRIVILEGE_ROUTINES equ 0x02
CPU_PRIVILEGE_USER     equ 0x03

SELECTOR_GLOBAL equ 0x00
SELECTOR_LOCAL  equ 0x04

SELECTOR_NULL        equ 0x00
SELECTOR_KERNEL_CODE equ (0x08 | SELECTOR_GLOBAL | CPU_PRIVILEGE_KERNEL)
SELECTOR_KERNEL_DATA equ (0x10 | SELECTOR_GLOBAL | CPU_PRIVILEGE_KERNEL)
SELECTOR_USER_CODE   equ (0x18 | SELECTOR_GLOBAL | CPU_PRIVILEGE_USER)
SELECTOR_USER_DATA   equ (0x20 | SELECTOR_GLOBAL | CPU_PRIVILEGE_USER)
SELECTOR_REAL_CODE   equ (0x28 | SELECTOR_GLOBAL | CPU_PRIVILEGE_KERNEL)
SELECTOR_REAL_DATA   equ (0x30 | SELECTOR_GLOBAL | CPU_PRIVILEGE_KERNEL)

;-------------------------------------------------------------------------
; KernelStartupInfo data layout (partial)
; WARNING: MUST stay in sync with C struct tag_KERNELSTARTUPINFO
; in kernel/include/KernelData.h.

STRUC KernelStartupInfo
    .KernelPhysicalBase RESQ 1
    .KernelSize         RESQ 1
    .KernelReservedBytes RESQ 1
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
; Interrupt frame
; /!\ Structure MUST BE IDENTICAL to struct INTERRUPT_FRAME in arch/x86-64/x86-64.h /!\

STRUC INTERRUPT_FRAME
; CPU registers
    .EFlags             RESQ 1
    .RAX                RESQ 1
    .RBX                RESQ 1
    .RCX                RESQ 1
    .RDX                RESQ 1
    .RSI                RESQ 1
    .RDI                RESQ 1
    .RSP                RESQ 1
    .RBP                RESQ 1
    .R8                 RESQ 1
    .R9                 RESQ 1
    .R10                RESQ 1
    .R11                RESQ 1
    .R12                RESQ 1
    .R13                RESQ 1
    .R14                RESQ 1
    .R15                RESQ 1
    .RIP                RESQ 1
    .CS                 RESQ 1
    .DS                 RESQ 1
    .SS                 RESQ 1
    .ES                 RESQ 1
    .FS                 RESQ 1
    .GS                 RESQ 1
    .CR0                RESQ 1
    .CR2                RESQ 1
    .CR3                RESQ 1
    .CR4                RESQ 1
    .CR8                RESQ 1
    .DR0                RESQ 1
    .DR1                RESQ 1
    .DR2                RESQ 1
    .DR3                RESQ 1
    .DR6                RESQ 1
    .DR7                RESQ 1
; FPU registers
    .FPU_Control        RESB 2      ; FPU Control register
    .FPU_Status         RESB 2      ; FPU Status register
    .FPU_Tag            RESB 2      ; FPU Tag register
    .FPU_IP_LO          RESB 2      ; FPU Instruction Pointer (low 16 bits)
    .FPU_IP_HI          RESB 4      ; FPU Instruction Pointer (high 32 bits)
    .FPU_DP_LO          RESB 2      ; FPU Data Pointer (low 16 bits)
    .FPU_DP_HI          RESB 4      ; FPU Data Pointer (high 32 bits)
    .FPU_ST0_LO         RESB 2      ; ST0 register (low 16 bits)
    .FPU_ST0_HI         RESB 8      ; ST0 register (high 64 bits)
    .FPU_ST1_LO         RESB 2      ; ST1 register (low 16 bits)
    .FPU_ST1_HI         RESB 8      ; ST1 register (high 64 bits)
    .FPU_ST2_LO         RESB 2      ; ST2 register (low 16 bits)
    .FPU_ST2_HI         RESB 8      ; ST2 register (high 64 bits)
    .FPU_ST3_LO         RESB 2      ; ST3 register (low 16 bits)
    .FPU_ST3_HI         RESB 8      ; ST3 register (high 64 bits)
    .FPU_ST4_LO         RESB 2      ; ST4 register (low 16 bits)
    .FPU_ST4_HI         RESB 8      ; ST4 register (high 64 bits)
    .FPU_ST5_LO         RESB 2      ; ST5 register (low 16 bits)
    .FPU_ST5_HI         RESB 8      ; ST5 register (high 64 bits)
    .FPU_ST6_LO         RESB 2      ; ST6 register (low 16 bits)
    .FPU_ST6_HI         RESB 8      ; ST6 register (high 64 bits)
    .FPU_ST7_LO         RESB 2      ; ST7 register (low 16 bits)
    .FPU_ST7_HI         RESB 8      ; ST7 register (high 64 bits)
; Fault data
    .SS0                RESQ 1      ; SS in ring 0
    .RSP0               RESQ 1      ; RSP in ring 0
    .IntNo              RESB 4      ; Interrupt / exception vector
    .ErrCode            RESB 4      ; CPU error code (0 for #UD)
ENDSTRUC

;-------------------------------------------------------------------------
; Kernel data layout shared with assembly

STRUC KERNELDATA_X86_64
    .IDT RESQ 1
    .GDT RESQ 1
    .TSS RESQ 1
ENDSTRUC

STRUC X86_64_TASK_STATE_SEGMENT
    .Reserved0 RESD 1
    .RSP0 RESQ 1
    .RSP1 RESQ 1
    .RSP2 RESQ 1
    .Reserved1 RESQ 1
    .IST1 RESQ 1
    .IST2 RESQ 1
    .IST3 RESQ 1
    .IST4 RESQ 1
    .IST5 RESQ 1
    .IST6 RESQ 1
    .IST7 RESQ 1
    .Reserved2 RESQ 1
    .Reserved3 RESW 1
    .IOMapBase RESW 1
ENDSTRUC

;-------------------------------------------------------------------------
; Macros

%macro FUNC_HEADER 0
    align 16
%endmacro

%endif  ; KERNEL_X86_64_INC
