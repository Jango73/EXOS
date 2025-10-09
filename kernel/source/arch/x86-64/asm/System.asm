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
;   System functions (x86-64)
;
;-------------------------------------------------------------------------

%include "x86-64.inc"
%include "System.inc"

;----------------------------------------------------------------------------

section .data
BITS 64

    global DeadBeef

;--------------------------------------

DeadBeef      dq 0x00000000DEADBEEF

;--------------------------------------

section .text.stub
BITS 64

global start
extern KernelMain
extern KernelLogText

stub_base:

    jmp     start

    times (4 - ($ - $$)) db 0

Magic : db 'EXOS'

FUNC_HEADER
start:
    ud2
    hlt
    jmp     start

;----------------------------------------------------------------------------

section .text
BITS 64

; Placeholder implementations until the long mode system layer is implemented.

%macro STUB_FUNC 1
    global %1
%1:
    ud2
    ret
%endmacro

%macro SYS_FUNC_BEGIN 1
    FUNC_HEADER
    global %1
%1:
%endmacro

%macro SYS_FUNC_END 0
    ret
%endmacro

CR0_PAGING  equ 0x0000000080000000

STUB_FUNC MaskIRQ
STUB_FUNC UnmaskIRQ
STUB_FUNC DisableIRQ
STUB_FUNC EnableIRQ
STUB_FUNC LoadGlobalDescriptorTable
STUB_FUNC ReadGlobalDescriptorTable
STUB_FUNC LoadLocalDescriptorTable
STUB_FUNC LoadInterruptDescriptorTable
STUB_FUNC LoadPageDirectory
STUB_FUNC LoadInitialTaskRegister
STUB_FUNC GetTaskRegister
STUB_FUNC GetPageDirectory
STUB_FUNC InvalidatePage
STUB_FUNC FlushTLB
STUB_FUNC SwitchToTask
STUB_FUNC SetTaskState
STUB_FUNC ClearTaskState
STUB_FUNC PeekConsoleWord
STUB_FUNC PokeConsoleWord
STUB_FUNC SaveRegisters
STUB_FUNC MemorySet
STUB_FUNC MemoryCopy
STUB_FUNC MemoryCompare
STUB_FUNC MemoryMove
STUB_FUNC DoSystemCall
STUB_FUNC IdleCPU
STUB_FUNC DeadCPU
STUB_FUNC Reboot
STUB_FUNC TaskRunner

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN GetCPUID
    push    rbx

    mov     eax, 0
    cpuid

    mov     dword [rdi + 0x00], eax
    mov     dword [rdi + 0x04], ebx
    mov     dword [rdi + 0x08], ecx
    mov     dword [rdi + 0x0C], edx

    mov     eax, 1
    cpuid

    mov     dword [rdi + 0x10], eax
    mov     dword [rdi + 0x14], ebx
    mov     dword [rdi + 0x18], ecx
    mov     dword [rdi + 0x1C], edx

    mov     eax, 1

    pop     rbx
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN DisablePaging
    mov     rax, cr0
    btr     rax, 31
    mov     cr0, rax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN EnablePaging
    mov     rax, cr0
    or      rax, CR0_PAGING
    mov     cr0, rax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN DisableInterrupts
    cli
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN EnableInterrupts
    sti
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN SaveFlags
    pushfq
    pop     rax
    mov     dword [rdi], eax
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN RestoreFlags
    mov     eax, dword [rdi]
    push    rax
    popfq
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN SaveFPU
    fsave   [rdi]
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN RestoreFPU
    frstor  [rdi]
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN InPortByte
    mov     dx, di
    xor     eax, eax
    in      al, dx
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN OutPortByte
    mov     dx, di
    mov     eax, esi
    out     dx, al
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN InPortWord
    mov     dx, di
    xor     eax, eax
    in      ax, dx
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN OutPortWord
    mov     dx, di
    mov     eax, esi
    out     dx, ax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN InPortLong
    mov     dx, di
    in      eax, dx
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN OutPortLong
    mov     dx, di
    mov     eax, esi
    out     dx, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN InPortStringWord
    mov     dx, di
    mov     rdi, rsi
    mov     rcx, rdx
    cld
    rep     insw
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

SYS_FUNC_BEGIN OutPortStringWord
    mov     dx, di
    mov     rcx, rdx
    cld
    rep     outsw
    xor     eax, eax
SYS_FUNC_END

;----------------------------------------------------------------------------

section .bss
align 16

    global Stack
Stack:
    resq 1

;----------------------------------------------------------------------------
