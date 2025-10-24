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

PRIVILEGE_KERNEL        equ 0x00
SELECTOR_GLOBAL         equ 0x00
SELECTOR_KERNEL_CODE    equ (0x08 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
SELECTOR_KERNEL_DATA    equ (0x10 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)

CR0_PROTECTED_MODE 	    equ 0x00000001  ; Protected mode on/off
CR0_COPROCESSOR         equ 0x00000002  ; Math present
CR0_MONITOR_COPROCESSOR equ 0x00000004  ; Emulate co-processor
CR0_TASKSWITCH          equ 0x00000008  ; Set on task switch
CR0_80387               equ 0x00000010  ; Type of co-processor
CR0_PAGING              equ 0x80000000  ; Paging on/off

;----------------------------------------------------------------------------

CONSOLE_TEXT_BUFFER_BASE    equ 0x00000000000B8000

REG_OFF_RFLAGS equ 0
REG_OFF_RAX    equ 8
REG_OFF_RBX    equ 16
REG_OFF_RCX    equ 24
REG_OFF_RDX    equ 32
REG_OFF_RSI    equ 40
REG_OFF_RDI    equ 48
REG_OFF_RBP    equ 56
REG_OFF_RSP    equ 64
REG_OFF_R8     equ 72
REG_OFF_R9     equ 80
REG_OFF_R10    equ 88
REG_OFF_R11    equ 96
REG_OFF_R12    equ 104
REG_OFF_R13    equ 112
REG_OFF_R14    equ 120
REG_OFF_R15    equ 128
REG_OFF_RIP    equ 136
REG_OFF_CS     equ 144
REG_OFF_DS     equ 146
REG_OFF_SS     equ 148
REG_OFF_ES     equ 150
REG_OFF_FS     equ 152
REG_OFF_GS     equ 154
REG_OFF_CR0    equ 156
REG_OFF_CR2    equ 164
REG_OFF_CR3    equ 172
REG_OFF_CR4    equ 180
REG_OFF_CR8    equ 188
REG_OFF_DR0    equ 196
REG_OFF_DR1    equ 204
REG_OFF_DR2    equ 212
REG_OFF_DR3    equ 220
REG_OFF_DR6    equ 228
REG_OFF_DR7    equ 236

;----------------------------------------------------------------------------

KERNELSTARTUP_IRQMASK_21_PM equ 24
KERNELSTARTUP_IRQMASK_A1_PM equ 28

;----------------------------------------------------------------------------

extern KernelMain
extern KernelLogText
extern KernelStartup

;----------------------------------------------------------------------------
; Stack helpers
;----------------------------------------------------------------------------

; Align the current stack pointer on a 16-byte boundary. The macro stores the
; adjustment that was applied on the stack and uses r11 as a temporary
; register. r11 is caller-saved in the System V AMD64 ABI and is therefore free
; to clobber in these low level helpers.
%macro STACK_ALIGN_16_ENTER 0
    mov     r11, rsp
    and     r11, 0x0F
    sub     rsp, r11
    sub     rsp, 8
    push    r11
%endmacro

; Restore the stack pointer after STACK_ALIGN_16_ENTER.
%macro STACK_ALIGN_16_LEAVE 0
    pop     r11
    add     rsp, 8
    add     rsp, r11
%endmacro
