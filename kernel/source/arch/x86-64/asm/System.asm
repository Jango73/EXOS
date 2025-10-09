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

STUB_FUNC GetCR4
STUB_FUNC GetESP
STUB_FUNC GetEBP
STUB_FUNC GetDR6
STUB_FUNC GetDR7
STUB_FUNC SetDR6
STUB_FUNC SetDR7
STUB_FUNC GetCPUID
STUB_FUNC DisablePaging
STUB_FUNC EnablePaging
STUB_FUNC DisableInterrupts
STUB_FUNC EnableInterrupts
STUB_FUNC SaveFlags
STUB_FUNC RestoreFlags
STUB_FUNC SaveFPU
STUB_FUNC RestoreFPU
STUB_FUNC InPortByte
STUB_FUNC OutPortByte
STUB_FUNC InPortWord
STUB_FUNC OutPortWord
STUB_FUNC InPortLong
STUB_FUNC OutPortLong
STUB_FUNC InPortStringWord
STUB_FUNC OutPortStringWord
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

section .bss
align 16

    global Stack
Stack:
    resq 1

;----------------------------------------------------------------------------
