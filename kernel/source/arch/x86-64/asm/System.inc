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
