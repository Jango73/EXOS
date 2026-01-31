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
;   Common CPU register bit layouts
;
;-------------------------------------------------------------------------

%ifndef CPU_COMMON_INC
%define CPU_COMMON_INC

;-------------------------------------------------------------------------
; Bit layout of CR0

CR0_PROTECTED_MODE          equ 0x00000001  ; Protected mode on or off
CR0_COPROCESSOR             equ 0x00000002  ; Math present
CR0_MONITOR_COPROCESSOR     equ 0x00000004  ; Emulate coprocessor
CR0_EMULATION               equ 0x00000004  ; Emulate coprocessor
CR0_TASKSWITCH              equ 0x00000008  ; Set on task switch
CR0_80387                   equ 0x00000010  ; Type of coprocessor
CR0_NUMERIC_ERROR           equ 0x00000020  ; Native x87 error reporting
CR0_PAGING                  equ 0x80000000  ; Paging on or off

;-------------------------------------------------------------------------
; Bit layout of CR4

CR4_PAE                     equ 0x00000020  ; Physical Address Extension
CR4_OSFXSR                  equ 0x00000200  ; OS supports FXSAVE/FXRSTOR
CR4_OSXMMEXCPT              equ 0x00000400  ; OS supports SIMD exceptions

%endif  ; CPU_COMMON_INC
