
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    Common Intel x86 definitions shared between 32-bit and 64-bit builds

\************************************************************************/

#ifndef X86_COMMON_H_INCLUDED
#define X86_COMMON_H_INCLUDED

#include "Base.h"

/*************************************************************************/
// Defines

#define INTEL_CPU_MASK_STEPPING 0x0000000F
#define INTEL_CPU_MASK_MODEL 0x000000F0
#define INTEL_CPU_MASK_FAMILY 0x00000F00
#define INTEL_CPU_MASK_TYPE 0x00003000

#define INTEL_CPU_SHFT_STEPPING 0x00
#define INTEL_CPU_SHFT_MODEL 0x04
#define INTEL_CPU_SHFT_FAMILY 0x08
#define INTEL_CPU_SHFT_TYPE 0x0C

#define INTEL_CPU_TYPE_OEM 0x00
#define INTEL_CPU_TYPE_OVERDRIVE 0x01
#define INTEL_CPU_TYPE_DUAL 0x02
#define INTEL_CPU_TYPE_RESERVED 0x03

#define INTEL_CPU_FEAT_FPU 0x00000001
#define INTEL_CPU_FEAT_VME 0x00000002
#define INTEL_CPU_FEAT_DE 0x00000004
#define INTEL_CPU_FEAT_PSE 0x00000008
#define INTEL_CPU_FEAT_TSC 0x00000010
#define INTEL_CPU_FEAT_MSR 0x00000020
#define INTEL_CPU_FEAT_PAE 0x00000040
#define INTEL_CPU_FEAT_MCE 0x00000080
#define INTEL_CPU_FEAT_CX8 0x00000100
#define INTEL_CPU_FEAT_APIC 0x00000200
#define INTEL_CPU_FEAT_RES1 0x00000400
#define INTEL_CPU_FEAT_RES2 0x00000800
#define INTEL_CPU_FEAT_MTRR 0x00001000
#define INTEL_CPU_FEAT_PGE 0x00002000
#define INTEL_CPU_FEAT_MCA 0x00004000
#define INTEL_CPU_FEAT_CMOV 0x00008000
#define INTEL_CPU_FEAT_RES3 0x00010000
#define INTEL_CPU_FEAT_RES4 0x00020000
#define INTEL_CPU_FEAT_RES5 0x00040000
#define INTEL_CPU_FEAT_RES6 0x00080000
#define INTEL_CPU_FEAT_RES7 0x00100000
#define INTEL_CPU_FEAT_RES8 0x00200000
#define INTEL_CPU_FEAT_RESA 0x00400000
#define INTEL_CPU_FEAT_MMX 0x00800000
#define INTEL_CPU_FEAT_RESB 0x01000000
#define INTEL_CPU_FEAT_RESC 0x02000000
#define INTEL_CPU_FEAT_RESD 0x04000000
#define INTEL_CPU_FEAT_RESE 0x08000000
#define INTEL_CPU_FEAT_RESF 0x10000000
#define INTEL_CPU_FEAT_RESG 0x20000000
#define INTEL_CPU_FEAT_RESH 0x40000000
#define INTEL_CPU_FEAT_RESI 0x80000000

/*************************************************************************/
// Structures

typedef struct tag_INTEL_FPU_REGISTERS {
    U16 Control;
    U16 Status;
    U16 Tag;
    U48 IP;
    U48 DP;
    U80 ST0, ST1, ST2, ST3;
    U80 ST4, ST5, ST6, ST7;
} INTEL_FPU_REGISTERS, *LPINTEL_FPU_REGISTERS;

typedef union tag_INTEL_X86_REGISTERS {
    struct {
        U16 DS;
        U16 ES;
        U16 FS;
        U16 GS;
        U8 AL;
        U8 AH;
        U16 F1;
        U8 BL;
        U8 BH;
        U16 F2;
        U8 CL;
        U8 CH;
        U16 F3;
        U8 DL;
        U8 DH;
        U16 F4;
    } H;
    struct {
        U16 DS;
        U16 ES;
        U16 FS;
        U16 GS;
        U16 AX;
        U16 F1;
        U16 BX;
        U16 F2;
        U16 CX;
        U16 F3;
        U16 DX;
        U16 F4;
        U16 SI;
        U16 F5;
        U16 DI;
        U16 F6;
        U16 FL;
        U16 F9;
    } X;
    struct {
        U16 DS;
        U16 ES;
        U16 FS;
        U16 GS;
        U32 EAX;
        U32 EBX;
        U32 ECX;
        U32 EDX;
        U32 ESI;
        U32 EDI;
        U32 EFL;
    } E;
} INTEL_X86_REGISTERS, *LPINTEL_X86_REGISTERS;

#endif  // X86_COMMON_H_INCLUDED
