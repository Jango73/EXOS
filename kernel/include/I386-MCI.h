
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


    i386 Machine code instructions

\************************************************************************/

#ifndef I386_MCI_H_INCLUDED
#define I386_MCI_H_INCLUDED

/*************************************************************************************************/

#include "Base.h"

/*************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************************************************/

// The ModR/M structure
// Used to select a register or memory addressing mode

typedef union tag_Intel_ModR_M
{
	U8 Byte;
	struct Intel_ModR_M_Bits
	{
		U8 R_M : 3; // The R/M field may select a register or memory
		U8 Reg : 3; // The Reg field may select a register or an opcode extension
		U8 Mod : 2; // The Mod field selects memory addressing mode
	} Bits;
} Intel_ModR_M, *lpIntel_ModR_M;

/*************************************************************************************************/

// The SIB structure
// Used for 32-bit instructions like MOV +40[EAX+EBX*8], 400

typedef union tag_Intel_SIB
{
	U8 Byte;
	struct Intel_SIB_Bits
	{
		U8 Base  : 3;
		U8 Index : 3;
		U8 Scale : 2;
	} Bits;
} Intel_SIB, *lpIntel_SIB;

/*************************************************************************************************/

#define INTEL_MAX_OPERANDS 3

#define INTEL_OPERAND_R    0
#define INTEL_OPERAND_I8   1
#define INTEL_OPERAND_I16  2
#define INTEL_OPERAND_I32  3
#define INTEL_OPERAND_I64  4
#define INTEL_OPERAND_DSP  5
#define INTEL_OPERAND_II   6
#define INTEL_OPERAND_BI   7
#define INTEL_OPERAND_BISD 8
#define INTEL_OPERAND_SO16 9
#define INTEL_OPERAND_SO32 10
#define INTEL_OPERAND_STR  11

/*************************************************************************************************/

// This is used to address the type and size without knowing the type of operand
typedef struct tag_Intel_Operand_Any
{
	U32 Type;
	U32 Size;
} Intel_Operand_Any, *lpIntel_Operand_Any;

// This is any register
typedef struct tag_Intel_Operand_R
{
	U32 Type;
	U32 Size;
	U32 Register;
} Intel_Operand_R, *lpIntel_Operand_R;

// This is an immediate byte value (MOV AL, 10)
typedef struct tag_Intel_Operand_I8
{
	U32 Type;
	U32 Size;
	U8  Value;
} Intel_Operand_I8, *lpIntel_Operand_I8;

// This is an immediate word value (MOV AX, 10)
typedef struct tag_Intel_Operand_I16
{
	U32 Type;
	U32 Size;
	U16 Value;
} Intel_Operand_I16, *lpIntel_Operand_I16;

// This is an immediate dword value (MOV EAX, 10)
typedef struct tag_Intel_Operand_I32
{
	U32 Type;
	U32 Size;
	U32 Value;
} Intel_Operand_I32, *lpIntel_Operand_I32;

// This is an immediate qword value (MOV MM0, 10)
typedef struct tag_Intel_Operand_I64
{
	U32 Type;
	U32 Size;
	U64 Value;
} Intel_Operand_I64, *lpIntel_Operand_I64;

// This is a displacement value
typedef struct tag_Intel_Operand_DSP
{
	U32 Type;
	U32 Size;
	I32 Value;
} Intel_Operand_DSP, *lpIntel_Operand_DSP;

// This is an indirect immediate addressing (MOV [200], AX)
typedef struct tag_Intel_Operand_II
{
	U32 Type;
	U32 Size;
	U32 Value;
} Intel_Operand_II, *lpIntel_Operand_II;

// This is a 16 bit [base+index] format instruction
typedef struct tag_Intel_Operand_BI
{
	U32 Type;
	U32 Size;
	U32 Base;
	U32 Index;
} Intel_Operand_BI, *lpIntel_Operand_BI;

// This is a 32 bit disp[base+index*scale] format instruction
typedef struct tag_Intel_Operand_BISD
{
	U32 Type;
	U32 Size;
	U32 Base;
	U32 Index;
	U32 Scale;
	U32 Displace;
} Intel_Operand_BISD, *lpIntel_Operand_BISD;

// This is a segment:offset16 format instruction
typedef struct tag_Intel_Operand_SO16
{
	U32 Type;
	U32 Size;
	U16 Segment;
	U16 Offset;
} Intel_Operand_SO16, *lpIntel_Operand_SO16;

// This is a segment:offset32 format instruction
typedef struct tag_Intel_Operand_SO32
{
	U32 Type;
	U32 Size;
	U16 Segment;
	U32 Offset;
} Intel_Operand_SO32, *lpIntel_Operand_SO32;

typedef struct tag_Intel_Operand_STR
{
	U32 Type;
	U32 Size;
	STR String [8];
} Intel_Operand_STR, *lpIntel_Operand_STR;

/*************************************************************************************************/

// Intel i386 instruction operand

typedef union tag_Intel_Operand
{
	Intel_Operand_Any  Any;
	Intel_Operand_R    R;
	Intel_Operand_I8   I8;
	Intel_Operand_I16  I16;
	Intel_Operand_I32  I32;
	Intel_Operand_I64  I64;
	Intel_Operand_DSP  DSP;
	Intel_Operand_II   II;
	Intel_Operand_BI   BI;
	Intel_Operand_BISD BISD;
	Intel_Operand_SO16 SO16;
	Intel_Operand_SO32 SO32;
	Intel_Operand_STR  STR;
} Intel_Operand, *lpIntel_Operand;

/*************************************************************************************************/

// The structure used for decoding and encoding Intel i386 instructions

typedef struct TAG_INTEL_INSTRUCTION
{
	STR           Name [16];
	U32           Opcode;
	Intel_ModR_M  ModR_M;
	Intel_SIB     SIB;
	U32           NumOperands;
	Intel_Operand Operand [INTEL_MAX_OPERANDS];
	U8*           Base;
	LINEAR        Address;
	U32           Length;
	U32           OperandSize;
	U32           AddressSize;
} INTEL_INSTRUCTION, *LP_INTEL_INSTRUCTION;

/*************************************************************************************************/

typedef struct TAG_INTEL_MACHINE_CODE
{
	U32 Size;
	U32 Offset_ModR_M;
	U32 Offset_SIB;
	U32 Offset_Imm;
	U32 Offset_P32;
	U32 Offset_P48;
	U8  Code [32];
} INTEL_MACHINE_CODE, *LP_INTEL_MACHINE_CODE;

/*************************************************************************************************/

// This structure is used to store an opcode scheme in the opcode table

typedef struct TAG_INTEL_OPCODE_PROTOTYPE
{
	LPCSTR Name;
	LPCSTR Operand [INTEL_MAX_OPERANDS];
} INTEL_OPCODE_PROTOTYPE, *LP_INTEL_OPCODE_PROTOTYPE;

/*************************************************************************************************/

// Bit sizes

#define I8BIT  8
#define I16BIT 16
#define I32BIT 32
#define I48BIT 48
#define I64BIT 64

/*************************************************************************************************/

// Intel i386 registers

#define INTEL_REG_NONE 0

#define INTEL_REG_AL   1
#define INTEL_REG_CL   2
#define INTEL_REG_DL   3
#define INTEL_REG_BL   4
#define INTEL_REG_AH   5
#define INTEL_REG_CH   6
#define INTEL_REG_DH   7
#define INTEL_REG_BH   8

#define INTEL_REG_AX   9
#define INTEL_REG_CX   10
#define INTEL_REG_DX   11
#define INTEL_REG_BX   12
#define INTEL_REG_SP   13
#define INTEL_REG_BP   14
#define INTEL_REG_SI   15
#define INTEL_REG_DI   16

#define INTEL_REG_EAX  17
#define INTEL_REG_ECX  18
#define INTEL_REG_EDX  19
#define INTEL_REG_EBX  20
#define INTEL_REG_ESP  21
#define INTEL_REG_EBP  22
#define INTEL_REG_ESI  23
#define INTEL_REG_EDI  24

#define INTEL_REG_MM0  25
#define INTEL_REG_MM1  26
#define INTEL_REG_MM2  27
#define INTEL_REG_MM3  28
#define INTEL_REG_MM4  29
#define INTEL_REG_MM5  30
#define INTEL_REG_MM6  31
#define INTEL_REG_MM7  32

#define INTEL_REG_ES   33
#define INTEL_REG_CS   34
#define INTEL_REG_SS   35
#define INTEL_REG_DS   36
#define INTEL_REG_FS   37
#define INTEL_REG_GS   38

#define INTEL_REG_CR0  39
#define INTEL_REG_CR2  40
#define INTEL_REG_CR3  41
#define INTEL_REG_CR4  42

#define INTEL_REG_8    INTEL_REG_AL
#define INTEL_REG_16   INTEL_REG_AX
#define INTEL_REG_32   INTEL_REG_EAX
#define INTEL_REG_64   INTEL_REG_MM0
#define INTEL_REG_SEG  INTEL_REG_ES
#define INTEL_REG_CRT  INTEL_REG_CR0
#define INTEL_REG_LAST (INTEL_REG_CR4 + 1)

/*************************************************************************************************/

extern INTEL_OPCODE_PROTOTYPE Opcode_Table [512];
extern INTEL_OPCODE_PROTOTYPE Extension_Table [80];
extern LPCSTR Intel_RegNames [];

extern CSTR BYTEPTR   [];
extern CSTR WORDPTR   [];
extern CSTR DWORDPTR  [];
extern CSTR QWORDPTR  [];
extern CSTR FPU       [];
extern CSTR INVALID   [];

/*************************************************************************************************/

U32 Intel_GetRegisterSize (U32);

/*************************************************************************************************/

int  SetIntelAttributes (long OperandSize, long AddressSize);

U32 Intel_MachineCodeToStructure (LPCSTR, LPCSTR, LP_INTEL_INSTRUCTION);
int Intel_StructureToString      (LP_INTEL_INSTRUCTION, LPSTR);
U32 Intel_MachineCodeToString    (LPCSTR, LPCSTR, LPSTR);
U32 Intel_StructureToMachineCode (LP_INTEL_INSTRUCTION, LP_INTEL_MACHINE_CODE);

/*************************************************************************************************/

LP_INTEL_INSTRUCTION  New_Intel_Instruction  (void);
lpIntel_Operand_R    New_Intel_Operand_R    (void);
lpIntel_Operand_I8   New_Intel_Operand_I8   (void);
lpIntel_Operand_I16  New_Intel_Operand_I16  (void);
lpIntel_Operand_I32  New_Intel_Operand_I32  (void);
lpIntel_Operand_I64  New_Intel_Operand_I64  (void);
lpIntel_Operand_DSP  New_Intel_Operand_DSP  (void);
lpIntel_Operand_II   New_Intel_Operand_II   (void);
lpIntel_Operand_BI   New_Intel_Operand_BI   (void);
lpIntel_Operand_BISD New_Intel_Operand_BISD (void);
lpIntel_Operand_SO16 New_Intel_Operand_SO16 (void);
lpIntel_Operand_SO32 New_Intel_Operand_SO32 (void);

int Delete_Intel_Instruction  (LP_INTEL_INSTRUCTION);
int Delete_Intel_Operand_R    (lpIntel_Operand_R);
int Delete_Intel_Operand_I8   (lpIntel_Operand_I8);
int Delete_Intel_Operand_I16  (lpIntel_Operand_I16);
int Delete_Intel_Operand_I32  (lpIntel_Operand_I32);
int Delete_Intel_Operand_I64  (lpIntel_Operand_I64);
int Delete_Intel_Operand_DSP  (lpIntel_Operand_DSP);
int Delete_Intel_Operand_II   (lpIntel_Operand_II);
int Delete_Intel_Operand_BI   (lpIntel_Operand_BI);
int Delete_Intel_Operand_BISD (lpIntel_Operand_BISD);
int Delete_Intel_Operand_SO16 (lpIntel_Operand_SO16);
int Delete_Intel_Operand_SO32 (lpIntel_Operand_SO32);

/*************************************************************************************************/

#ifdef __cplusplus
};
#endif

/*************************************************************************************************/

#endif
