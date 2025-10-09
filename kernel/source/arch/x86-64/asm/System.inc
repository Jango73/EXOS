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
;   x86-64 System helpers
;
;-------------------------------------------------------------------------

;----------------------------------------------------------------------------
; Helper values to access function parameters and local variables
; NOTE: x86-64 System V ABI passes the first arguments in registers. These
; offsets only cover stack-resident parameters and will be revisited when the
; long mode entry code is implemented.

PBN equ 0x10                           ; Param base near (first stack arg)
PBF equ 0x18                           ; Param base far
LBN equ 0x08                           ; Local base near
LBF equ 0x08                           ; Local base far

;----------------------------------------------------------------------------
; Descriptor selector helpers

PRIVILEGE_KERNEL       equ 0x00
SELECTOR_GLOBAL        equ 0x00
SELECTOR_KERNEL_CODE   equ (0x08 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_KERNEL_DATA   equ (0x10 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)

;----------------------------------------------------------------------------
; Control register and flag bits

CR0_TASKSWITCH         equ 0x0000000000000008
EFLAGS_NT              equ 0x0000000000004000

;----------------------------------------------------------------------------
; KernelStartupInfo data layout (partial)

STRUC KernelStartupInfo64
    .StubAddress        RESB 8
    .StackTop           RESB 8
    .PageDirectory      RESB 8
    .IRQMask_21_PM      RESB 4
    .IRQMask_A1_PM      RESB 4
    .IRQMask_21_RM      RESB 4
    .IRQMask_A1_RM      RESB 4
ENDSTRUC

;----------------------------------------------------------------------------
; General register snapshot layout (matches INTEL_64_GENERAL_REGISTERS)

STRUC INTEL_64_GENERAL_REGISTERS
    .RFlags             RESB 8
    .RAX                RESB 8
    .RBX                RESB 8
    .RCX                RESB 8
    .RDX                RESB 8
    .RSI                RESB 8
    .RDI                RESB 8
    .RBP                RESB 8
    .RSP                RESB 8
    .R8                 RESB 8
    .R9                 RESB 8
    .R10                RESB 8
    .R11                RESB 8
    .R12                RESB 8
    .R13                RESB 8
    .R14                RESB 8
    .R15                RESB 8
    .RIP                RESB 8
    .CS                 RESB 2
    .DS                 RESB 2
    .SS                 RESB 2
    .ES                 RESB 2
    .FS                 RESB 2
    .GS                 RESB 2
    .CR0                RESB 8
    .CR2                RESB 8
    .CR3                RESB 8
    .CR4                RESB 8
    .CR8                RESB 8
    .DR0                RESB 8
    .DR1                RESB 8
    .DR2                RESB 8
    .DR3                RESB 8
    .DR6                RESB 8
    .DR7                RESB 8
ENDSTRUC

;----------------------------------------------------------------------------
; Fixed memory locations

CONSOLE_TEXT_BUFFER    equ 0x00000000000B8000

;----------------------------------------------------------------------------
