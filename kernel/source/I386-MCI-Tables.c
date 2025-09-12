
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

/*************************************************************************************************\

  Documentation extracted from :

           Intel Architecture
         Software Developer�s
                       Manual

                   Volume 2 :
    Instruction Set Reference

  -----------------------------------------------------------------------------------------------
  MISSING INSTRUCTIONS (not implemented in this table):
  
  - FPU instructions (x87 floating point): FADD, FSUB, FMUL, FDIV, FLD, FST, etc.
  - MMX instructions: MOVQ, PADDB, PSUBB, PCMPEQB, etc.
  - SSE instructions: MOVAPS, ADDPS, MULPS, etc.
  - SSE2 instructions: MOVAPD, ADDPD, etc.
  - SYSENTER/SYSEXIT (Pentium II+ fast system calls)
  - CMPXCHG8B (i486+ 8-byte compare-exchange)
  - Some privileged/debug instructions: MOV to/from debug registers
  - RDPMC, MONITOR, MWAIT (newer CPU instructions)
  
  This table covers the complete i386 integer instruction set plus most i486+ extensions.

  -----------------------------------------------------------------------------------------------
  Intel Architecture Instruction Format

  [Instruction Prefixes] [ Opcode ] [  ModR/M  ] [   SIB   ] [ Displacement ] [  Immediate  ]
   Up to four prefixes    1 or 2     1 byte       1 byte      Address disp.    Imm. data of
   of 1-byte each         byte       (optional)   (optional)  of 1, 2, or 4    1, 2, or 4
   (optional)             opcode                              bytes or none    bytes or none

  -----------------------------------------------------------------------------------------------
  Abbreviations used in the opcode table :

  A Direct address. The instruction has no ModR/M byte; the address of the operand is en-coded
    in the instruction; and no base register, index register, or scaling factor can be
    applied, for example, far JMP (EA).

  C The reg field of the ModR/M byte selects a control register, for example, MOV (0F20, 0F22).

  D The reg field of the ModR/M byte selects a debug register, for example, MOV (0F21,0F23).

  E A ModR/M byte follows the opcode and specifies the operand. The operand is either a
    general-purpose register or a memory address. If it is a memory address, the address is
    computed from a segment register and any of the following values: a base register, an
    index register, a scaling factor, a displacement.

  F EFLAGS Register.

  G The reg field of the ModR/M byte selects a general register, for example, AX (000).

  I Immediate data. The operand value is encoded in subsequent bytes of the instruction.

  J The instruction contains a relative offset to be added to the instruction pointer register,
    for example, JMP short, LOOP.

  M The ModR/M byte may refer only to memory, for example, BOUND, LES, LDS, LSS, LFS, LGS, CMPXCHG8B.

  O The instruction has no ModR/M byte; the offset of the operand is coded as a word or
    double word (depending on address size attribute) in the instruction. No base register,
    index register, or scaling factor can be applied, for example, MOV (A0�A3).

  P The reg field of the ModR/M byte selects a packed quadword MMX register.

  Q An ModR/M byte follows the opcode and specifies the operand. The operand is either
    an MMX register or a memory address. If it is a memory address, the address is com-puted
    from a segment register and any of the following values: a base register, an index
    register, a scaling factor, and a displacement.

  R The mod field of the ModR/M byte may refer only to a general register, for example, MOV (0F20-0F24, 0F26).

  S The reg field of the ModR/M byte selects a segment register, for example, MOV (8C,8E).

  T The reg field of the ModR/M byte selects a test register, for example, MOV (0F24,0F26).

  X Memory addressed by the DS:SI register pair (for example, MOVS, OUTS, or LODS).

  Y Memory addressed by the ES:DI register pair (for example, MOVS, INS, or STOS).

  a Two one-word operands in memory or two double-word operands in memory, depending
    on operand size attribute (used only by the BOUND instruction).

  b Byte, regardless of operand-size attribute.

  c Byte or word, depending on operand-size attribute.

  d Doubleword, regardless of operand-size attribute.

  p 32-bit or 48-bit pointer, depending on operand size attribute.

  q Quadword, regardless of operand-size attribute.

  s 6-byte pseudo-descriptor.

  v Word or doubleword, depending on operand-size attribute.

  w Word, regardless of operand-size attribute.

\*************************************************************************************************/

#include "../include/I386-MCI.h"

// Empty string pointer

CSTR NS        [] = "";

// Intel machine code mnemonics

CSTR AAA       [] = "AAA";
CSTR AAD       [] = "AAD";
CSTR AAM       [] = "AAM";
CSTR AAS       [] = "AAS";
CSTR ADC       [] = "ADC";
CSTR ADD       [] = "ADD";
CSTR AND       [] = "AND";
CSTR ARPL      [] = "ARPL";
CSTR CALL      [] = "CALL";
CSTR CBW       [] = "CBW";
CSTR CLC       [] = "CLC";
CSTR CLD       [] = "CLD";
CSTR CLI       [] = "CLI";
CSTR CLTS      [] = "CLTS";
CSTR CMC       [] = "CMC";
CSTR CMP       [] = "CMP";
CSTR CMPSB     [] = "CMPSB";
CSTR CMPSW     [] = "CMPSW";
CSTR CS_       [] = "CS:";
CSTR CWD       [] = "CWD";
CSTR CMOVO     [] = "CMOVO";
CSTR CMOVNO    [] = "CMOVNO";
CSTR CMOVB     [] = "CMOVB";
CSTR CMOVNB    [] = "CMOVNB";
CSTR CMOVE     [] = "CMOVE";
CSTR CMOVNE    [] = "CMOVNE";
CSTR CMOVBE    [] = "CMOVBE";
CSTR CMOVVA    [] = "CMOVVA";
CSTR CMOVS     [] = "CMOVS";
CSTR CMOVNS    [] = "CMOVNS";
CSTR CMOVP     [] = "CMOVP";
CSTR CMOVNP    [] = "CMOVNP";
CSTR CMOVL     [] = "CMOVL";
CSTR CMOVGE    [] = "CMOVGE";
CSTR CMOVLE    [] = "CMOVLE";
CSTR CMOVG     [] = "CMOVG";
CSTR CMPXCH8B  [] = "CMPXCH8B";
CSTR CPUID     [] = "CPUID";

CSTR PUSH      [] = "PUSH";
CSTR POP       [] = "POP";
CSTR OR        [] = "OR";
CSTR SBB       [] = "SBB";
CSTR ES_       [] = "ES:";
CSTR DAA       [] = "DAA";
CSTR SUB       [] = "SUB";
CSTR DAS       [] = "DAS";
CSTR XOR       [] = "XOR";
CSTR SS_       [] = "SS:";
CSTR DS_       [] = "DS:";
CSTR INC       [] = "INC";
CSTR DEC       [] = "DEC";
CSTR PUSHA     [] = "PUSHA";
CSTR POPA      [] = "POPA";
CSTR BOUND     [] = "BOUND";
CSTR FS_       [] = "FS:";
CSTR GS_       [] = "GS:";
CSTR IMUL      [] = "IMUL";
CSTR INSB      [] = "INSB";
CSTR INSW      [] = "INSW";
CSTR OUTSB     [] = "OUTSB";
CSTR OUTSW     [] = "OUTSW";
CSTR JO        [] = "JO";
CSTR JNO       [] = "JNO";
CSTR JJB       [] = "JB";
CSTR JNB       [] = "JNB";
CSTR JZ        [] = "JZ";
CSTR JNZ       [] = "JNZ";
CSTR JBE       [] = "JBE";
CSTR JNBE      [] = "JNBE";
CSTR JS        [] = "JS";
CSTR JNS       [] = "JNS";
CSTR JP        [] = "JP";
CSTR JNP       [] = "JNP";
CSTR JL        [] = "JL";
CSTR JNL       [] = "JNL";
CSTR JLE       [] = "JLE";
CSTR JNLE      [] = "JNLE";
CSTR TEST      [] = "TEST";
CSTR XCHG      [] = "XCHG";
CSTR MOV       [] = "MOV";
CSTR LEA       [] = "LEA";
CSTR NOP       [] = "NOP";
CSTR WAIT      [] = "WAIT";
CSTR PUSHF     [] = "PUSHF";
CSTR SAHF      [] = "SAHF";
CSTR LAHF      [] = "LAHF";
CSTR MOVSB     [] = "MOVSB";
CSTR MOVSW     [] = "MOVSW";
CSTR STOSB     [] = "STOSB";
CSTR STOSW     [] = "STOSW";
CSTR LODSB     [] = "LODSB";
CSTR LODSW     [] = "LODSW";
CSTR SCASB     [] = "SCASB";
CSTR SCASW     [] = "SCASW";
CSTR RET       [] = "RET";
CSTR LES       [] = "LES";
CSTR LDS       [] = "LDS";
CSTR ENTER     [] = "ENTER";
CSTR LEAVE     [] = "LEAVE";
CSTR RETF      [] = "RETF";
CSTR _INT      [] = "INT";
CSTR INTO      [] = "INTO";
CSTR IRET      [] = "IRET";
CSTR XLAT      [] = "XLAT";
CSTR LOOPN     [] = "LOOPN";
CSTR LOOPE     [] = "LOOPE";
CSTR LOOP      [] = "LOOP";
CSTR JCXZ      [] = "JCXZ";
CSTR IN        [] = "IN";
CSTR OUT       [] = "OUT";
CSTR JMP       [] = "JMP";
CSTR LOCK      [] = "LOCK";
CSTR REPNE     [] = "REPNE";
CSTR REP       [] = "REP";
CSTR HLT       [] = "HLT";
CSTR STC       [] = "STC";
CSTR STI       [] = "STI";
CSTR STD       [] = "STD";
CSTR LAR       [] = "LAR";
CSTR LSL       [] = "LSL";
CSTR INVD      [] = "INVD";
CSTR WBINVD    [] = "WBINVD";
CSTR UD2       [] = "UD2";
CSTR WRMSR     [] = "WRMSR";
CSTR RDTSC     [] = "RDTSC";
CSTR RDMSR     [] = "RDMSR";
CSTR RDPMC     [] = "RDPMC";
CSTR PUNPCKLBW [] = "PUNPCKLBW";
CSTR PUNPCKLWD [] = "PUNPCKLWD";
CSTR PUNPCKLDQ [] = "PUNPCKLDQ";
CSTR PACKUSDW  [] = "PACKUSDW";
CSTR PCMPGTB   [] = "PCMPGTB";
CSTR PCMPGTW   [] = "PCMPGTW";
CSTR PCMPGTD   [] = "PCMPGTD";
CSTR PACKSSWB  [] = "PACKSSWB";
CSTR PUNPCKHBW [] = "PUNPCKHBW";
CSTR PUNPCKHWD [] = "PUNPCKHWD";
CSTR PUNPCKHDQ [] = "PUNPCKHDQ";
CSTR PACKSSDW  [] = "PACKSSDW";
CSTR MOVD      [] = "MOVD";
CSTR MOVQ      [] = "MOVQ";
CSTR PCMPEQB   [] = "PCMPEQB";
CSTR PCMPEQW   [] = "PCMPEQW";
CSTR PCMPEQD   [] = "PCMPEQD";
CSTR EMMS      [] = "EMMS";
CSTR SETO      [] = "SETO";
CSTR SETNO     [] = "SETNO";
CSTR SETB      [] = "SETB";
CSTR SETNB     [] = "SETNB";
CSTR SETZ      [] = "SETZ";
CSTR SETNZ     [] = "SETNZ";
CSTR SETBE     [] = "SETBE";
CSTR SETNBE    [] = "SETNBE";
CSTR SETS      [] = "SETS";
CSTR SETNS     [] = "SETNS";
CSTR SETP      [] = "SETP";
CSTR SETNP     [] = "SETNP";
CSTR SETL      [] = "SETL";
CSTR SETNL     [] = "SETNL";
CSTR SETLE     [] = "SETLE";
CSTR SETNLE    [] = "SETNLE";
CSTR BT        [] = "BT";
CSTR SHLD      [] = "SHLD";
CSTR RSM       [] = "RSM";
CSTR BTS       [] = "BTS";
CSTR SHRD      [] = "SHRD";
CSTR CMPXCHG   [] = "CMPXCHG";
CSTR LSS       [] = "LSS";
CSTR BTR       [] = "BTR";
CSTR LFS       [] = "LFS";
CSTR LGS       [] = "LGS";
CSTR MOVZX     [] = "MOVZX";
CSTR BTC       [] = "BTC";
CSTR BSF       [] = "BSF";
CSTR BSR       [] = "BSR";
CSTR MOVSX     [] = "MOVSX";
CSTR XADD      [] = "XADD";
CSTR BSWAP     [] = "BSWAP";
CSTR PSRLW     [] = "PSRLW";
CSTR PSRLD     [] = "PSRLD";
CSTR PSRLQ     [] = "PSRLQ";
CSTR PMULLW    [] = "PMULLW";
CSTR PSUBUSB   [] = "PSUBUSB";
CSTR PSUBUSW   [] = "PSUBUSW";
CSTR PAND      [] = "PAND";
CSTR PADDUSB   [] = "PADDUSB";
CSTR PADDUSW   [] = "PADDUSW";
CSTR PANDN     [] = "PANDN";
CSTR PSRAW     [] = "PSRAW";
CSTR PSRAD     [] = "PSRAD";
CSTR PMULHW    [] = "PMULHW";
CSTR PSUBSB    [] = "PSUBSB";
CSTR PSUBSW    [] = "PSUBSW";
CSTR POR       [] = "POR";
CSTR PADDSB    [] = "PADDSB";
CSTR PADDSW    [] = "PADDSW";
CSTR PXOR      [] = "PXOR";
CSTR PSLLW     [] = "PSLLW";
CSTR PSLLD     [] = "PSLLD";
CSTR PSLLQ     [] = "PSLLQ";
CSTR PMADDWD   [] = "PMADDWD";
CSTR PSUBB     [] = "PSUBB";
CSTR PSUBW     [] = "PSUBW";
CSTR PSUBD     [] = "PSUBD";
CSTR PADDB     [] = "PADDB";
CSTR PADDW     [] = "PADDW";
CSTR PADDD     [] = "PADDD";
CSTR ROL       [] = "ROL";
CSTR ROR       [] = "ROR";
CSTR RCL       [] = "RCL";
CSTR RCR       [] = "RCR";
CSTR SHL       [] = "SHL";
CSTR SHR       [] = "SHR";
CSTR SAR       [] = "SAR";
CSTR NOT       [] = "NOT";
CSTR NEG       [] = "NEG";
CSTR MUL       [] = "MUL";
CSTR DIV       [] = "DIV";
CSTR IDIV      [] = "IDIV";
CSTR SLDT      [] = "SLDT";
CSTR _STR      [] = "STR";
CSTR LLDT      [] = "LLDT";
CSTR LTR       [] = "LTR";
CSTR VERR      [] = "VERR";
CSTR VERW      [] = "VERW";
CSTR SGDT      [] = "SGDT";
CSTR SIDT      [] = "SIDT";
CSTR LGDT      [] = "LGDT";
CSTR LIDT      [] = "LIDT";
CSTR SMSW      [] = "SMSW";
CSTR LMSW      [] = "LMSW";
CSTR INVLPG    [] = "INVLPG";
CSTR PSRL      [] = "PSRL";
CSTR PSRA      [] = "PSRA";
CSTR PSLL      [] = "PSLL";

// Opcode extension groups

CSTR XG1       [] = "XG1";
CSTR XG2       [] = "XG2";
CSTR XG3       [] = "XG3";
CSTR XG4       [] = "XG4";
CSTR XG5       [] = "XG5";
CSTR XG6       [] = "XG6";
CSTR XG7       [] = "XG7";
CSTR XG8       [] = "XG8";
CSTR XG9       [] = "XG9";
CSTR XG10      [] = "XG10";

// Immediate registers

CSTR _AL       [] = "_AL";
CSTR _CL       [] = "_CL";
CSTR _DL       [] = "_DL";
CSTR _BL       [] = "_BL";
CSTR _AH       [] = "_AH";
CSTR _CH       [] = "_CH";
CSTR _DH       [] = "_DH";
CSTR _BH       [] = "_BH";
CSTR _AX       [] = "_AX";
CSTR _CX       [] = "_CX";
CSTR _DX       [] = "_DX";
CSTR _BX       [] = "_BX";
CSTR _SP       [] = "_SP";
CSTR _BP       [] = "_BP";
CSTR _SI       [] = "_SI";
CSTR _DI       [] = "_DI";
CSTR _EAX      [] = "_EAX";
CSTR _ECX      [] = "_ECX";
CSTR _EDX      [] = "_EDX";
CSTR _EBX      [] = "_EBX";
CSTR _ESP      [] = "_ESP";
CSTR _EBP      [] = "_EBP";
CSTR _ESI      [] = "_ESI";
CSTR _EDI      [] = "_EDI";
CSTR _ES       [] = "_ES";
CSTR _CS       [] = "_CS";
CSTR _SS       [] = "_SS";
CSTR _DS       [] = "_DS";
CSTR _FS       [] = "_FS";
CSTR _GS       [] = "_GS";

// Immediate numbers (SHL AX, 1) (INT 3) (etc...)

CSTR _01h      [] = "_01h";
CSTR _03h      [] = "_03h";

// Miscellaneous names

CSTR BYTEPTR   [] = "BYTE PTR";
CSTR WORDPTR   [] = "WORD PTR";
CSTR DWORDPTR  [] = "DWORD PTR";
CSTR QWORDPTR  [] = "QWORD PTR";
CSTR FPU       [] = "FPU";
CSTR INVALID   [] = "????";

// Operand addressing modes and types
// See notes at start of file

CSTR Ap        [] = "Ap";
CSTR Cd        [] = "Cd";
CSTR Dd        [] = "Dd";
CSTR Eb        [] = "Eb";
CSTR Ed        [] = "Ed";
CSTR Ep        [] = "Ep";
CSTR Ew        [] = "Ew";
CSTR Ev        [] = "Ev";
CSTR Fv        [] = "Fv";
CSTR Gb        [] = "Gb";
CSTR Gw        [] = "Gw";
CSTR Gv        [] = "Gv";
CSTR Ib        [] = "Ib";
CSTR Iw        [] = "Iw";
CSTR Iv        [] = "Iv";
CSTR Jb        [] = "Jb";
CSTR Jv        [] = "Jv";
CSTR M         [] = "M";
CSTR Ma        [] = "Ma";
CSTR Mp        [] = "Mp";
CSTR Mq        [] = "Mq";
CSTR Ms        [] = "Ms";
CSTR Ob        [] = "Ob";
CSTR Ov        [] = "Ov";
CSTR Pd        [] = "Pd";
CSTR Pq        [] = "Pq";
CSTR Qd        [] = "Qd";
CSTR Qq        [] = "Qq";
CSTR Rd        [] = "Rd";
CSTR Sw        [] = "Sw";
CSTR Xb        [] = "Xb";
CSTR Xv        [] = "Xv";
CSTR Yb        [] = "Yb";
CSTR Yv        [] = "Yv";

/*************************************************************************************************/

INTEL_OPCODE_PROTOTYPE Opcode_Table [512] =
{
	// One byte opcodes

	// 0x00 - 0x0F
	{ .Name = ADD     , .Operand = { Eb  , Gb  , NS   } },
	{ .Name = ADD     , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = ADD     , .Operand = { Gb  , Eb  , NS   } },
	{ .Name = ADD     , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = ADD     , .Operand = { _AL , Ib  , NS   } },
	{ .Name = ADD     , .Operand = { _AX , Iv  , NS   } },
	{ .Name = PUSH    , .Operand = { _ES , NS  , NS   } },
	{ .Name = POP     , .Operand = { _ES , NS  , NS   } },
	{ .Name = OR      , .Operand = { Eb  , Gb  , NS   } },
	{ .Name = OR      , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = OR      , .Operand = { Gb  , Eb  , NS   } },
	{ .Name = OR      , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = OR      , .Operand = { _AL , Ib  , NS   } },
	{ .Name = OR      , .Operand = { _AX , Iv  , NS   } },
	{ .Name = PUSH    , .Operand = { _CS , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },

	// 0x10 - 0x1F
	{ .Name = ADC     , .Operand = { Eb  , Gb  , NS   } },
	{ .Name = ADC     , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = ADC     , .Operand = { Gb  , Eb  , NS   } },
	{ .Name = ADC     , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = ADC     , .Operand = { _AL , Ib  , NS   } },
	{ .Name = ADC     , .Operand = { _AX , Iv  , NS   } },
	{ .Name = PUSH    , .Operand = { _SS , NS  , NS   } },
	{ .Name = POP     , .Operand = { _SS , NS  , NS   } },
	{ .Name = SBB     , .Operand = { Eb  , Gb  , NS   } },
	{ .Name = SBB     , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = SBB     , .Operand = { Gb  , Eb  , NS   } },
	{ .Name = SBB     , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = SBB     , .Operand = { _AL , Ib  , NS   } },
	{ .Name = SBB     , .Operand = { _AX , Iv  , NS   } },
	{ .Name = PUSH    , .Operand = { _DS , NS  , NS   } },
	{ .Name = POP     , .Operand = { _DS , NS  , NS   } },

	// 0x20 - 0x2F
	{ .Name = AND     , .Operand = { Eb  , Gb  , NS   } },
	{ .Name = AND     , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = AND     , .Operand = { Gb  , Eb  , NS   } },
	{ .Name = AND     , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = AND     , .Operand = { _AL , Ib  , NS   } },
	{ .Name = AND     , .Operand = { _AX , Iv  , NS   } },
	{ .Name = ES_     , .Operand = { NS  , NS  , NS   } },
	{ .Name = DAA     , .Operand = { NS  , NS  , NS   } },
	{ .Name = SUB     , .Operand = { Eb  , Gb  , NS   } },
	{ .Name = SUB     , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = SUB     , .Operand = { Gb  , Eb  , NS   } },
	{ .Name = SUB     , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = SUB     , .Operand = { _AL , Ib  , NS   } },
	{ .Name = SUB     , .Operand = { _AX , Iv  , NS   } },
	{ .Name = CS_     , .Operand = { NS  , NS  , NS   } },
	{ .Name = DAS     , .Operand = { NS  , NS  , NS   } },

	// 0x30 - 0x3F
	{ .Name = XOR     , .Operand = { Eb  , Gb  , NS   } },
	{ .Name = XOR     , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = XOR     , .Operand = { Gb  , Eb  , NS   } },
	{ .Name = XOR     , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = XOR     , .Operand = { _AL , Ib  , NS   } },
	{ .Name = XOR     , .Operand = { _AX , Iv  , NS   } },
	{ .Name = SS_     , .Operand = { NS  , NS  , NS   } },
	{ .Name = AAA     , .Operand = { NS  , NS  , NS   } },
	{ .Name = CMP     , .Operand = { Eb  , Gb  , NS   } },
	{ .Name = CMP     , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = CMP     , .Operand = { Gb  , Eb  , NS   } },
	{ .Name = CMP     , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMP     , .Operand = { _AL , Ib  , NS   } },
	{ .Name = CMP     , .Operand = { _AX , Iv  , NS   } },
	{ .Name = DS_     , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },

	// 0x40 - 0x4F
	{ .Name = INC     , .Operand = { _AX , NS  , NS   } },
	{ .Name = INC     , .Operand = { _CX , NS  , NS   } },
	{ .Name = INC     , .Operand = { _DX , NS  , NS   } },
	{ .Name = INC     , .Operand = { _BX , NS  , NS   } },
	{ .Name = INC     , .Operand = { _SP , NS  , NS   } },
	{ .Name = INC     , .Operand = { _BP , NS  , NS   } },
	{ .Name = INC     , .Operand = { _SI , NS  , NS   } },
	{ .Name = INC     , .Operand = { _DI , NS  , NS   } },
	{ .Name = DEC     , .Operand = { _AX , NS  , NS   } },
	{ .Name = DEC     , .Operand = { _CX , NS  , NS   } },
	{ .Name = DEC     , .Operand = { _DX , NS  , NS   } },
	{ .Name = DEC     , .Operand = { _BX , NS  , NS   } },
	{ .Name = DEC     , .Operand = { _SP , NS  , NS   } },
	{ .Name = DEC     , .Operand = { _BP , NS  , NS   } },
	{ .Name = DEC     , .Operand = { _SI , NS  , NS   } },
	{ .Name = DEC     , .Operand = { _DI , NS  , NS   } },

	// 0x50 - 0x5F
	{ .Name = PUSH    , .Operand = { _AX , NS  , NS   } },
	{ .Name = PUSH    , .Operand = { _CX , NS  , NS   } },
	{ .Name = PUSH    , .Operand = { _DX , NS  , NS   } },
	{ .Name = PUSH    , .Operand = { _BX , NS  , NS   } },
	{ .Name = PUSH    , .Operand = { _SP , NS  , NS   } },
	{ .Name = PUSH    , .Operand = { _BP , NS  , NS   } },
	{ .Name = PUSH    , .Operand = { _SI , NS  , NS   } },
	{ .Name = PUSH    , .Operand = { _DI , NS  , NS   } },
	{ .Name = POP     , .Operand = { _AX , NS  , NS   } },
	{ .Name = POP     , .Operand = { _CX , NS  , NS   } },
	{ .Name = POP     , .Operand = { _DX , NS  , NS   } },
	{ .Name = POP     , .Operand = { _BX , NS  , NS   } },
	{ .Name = POP     , .Operand = { _SP , NS  , NS   } },
	{ .Name = POP     , .Operand = { _BP , NS  , NS   } },
	{ .Name = POP     , .Operand = { _SI , NS  , NS   } },
	{ .Name = POP     , .Operand = { _DI , NS  , NS   } },

	// 0x60 - 0x6F
	{ .Name = PUSHA   , .Operand = { NS  , NS  , NS   } },
	{ .Name = POPA    , .Operand = { NS  , NS  , NS   } },
	{ .Name = BOUND   , .Operand = { Gv  , Ma  , NS   } },
	{ .Name = ARPL    , .Operand = { Ew  , Gw  , NS   } },
	{ .Name = FS_     , .Operand = { NS  , NS  , NS   } },
	{ .Name = GS_     , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PUSH    , .Operand = { Iv  , NS  , NS   } },
	{ .Name = IMUL    , .Operand = { Gv  , Ev  , Iv   } },
	{ .Name = PUSH    , .Operand = { Ib  , NS  , NS   } },
	{ .Name = IMUL    , .Operand = { Gv  , Ev  , Ib   } },
	{ .Name = INSB    , .Operand = { Yb  , _DX , NS   } },
	{ .Name = INSW    , .Operand = { Yv  , _DX , NS   } },
	{ .Name = OUTSB   , .Operand = { _DX , Xb  , NS   } },
	{ .Name = OUTSW   , .Operand = { _DX , Xv  , NS   } },

	// 0x70 - 0x7F
	{ .Name = JO      , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JNO     , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JJB     , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JNB     , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JZ      , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JNZ     , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JBE     , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JNBE    , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JS      , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JNS     , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JP      , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JNP     , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JL      , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JNL     , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JLE     , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JNLE    , .Operand = { Jb  , NS  , NS   } },

	// 0x80 - 0x8F
	{ .Name = XG1     , .Operand = { Eb  , Ib  , NS   } },
	{ .Name = XG1     , .Operand = { Ev  , Iv  , NS   } },
	{ .Name = XG1     , .Operand = { Eb  , Ib  , NS   } },
	{ .Name = XG1     , .Operand = { Ev  , Ib  , NS   } },
	{ .Name = TEST    , .Operand = { Eb  , Gb  , NS   } },
	{ .Name = TEST    , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = XCHG    , .Operand = { Eb  , Gb  , NS   } },
	{ .Name = XCHG    , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = MOV     , .Operand = { Eb  , Gb  , NS   } },
	{ .Name = MOV     , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = MOV     , .Operand = { Gb  , Eb  , NS   } },
	{ .Name = MOV     , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = MOV     , .Operand = { Ew  , Sw  , NS   } },
	{ .Name = LEA     , .Operand = { Gv  , M   , NS   } },
	{ .Name = MOV     , .Operand = { Sw  , Ew  , NS   } },
	{ .Name = POP     , .Operand = { Ev  , NS  , NS   } },

	// 0x90 - 0x9F
	{ .Name = NOP     , .Operand = { NS  , NS  , NS   } },
	{ .Name = XCHG    , .Operand = { _AX , _CX , NS   } },
	{ .Name = XCHG    , .Operand = { _AX , _DX , NS   } },
	{ .Name = XCHG    , .Operand = { _AX , _BX , NS   } },
	{ .Name = XCHG    , .Operand = { _AX , _SP , NS   } },
	{ .Name = XCHG    , .Operand = { _AX , _BP , NS   } },
	{ .Name = XCHG    , .Operand = { _AX , _SI , NS   } },
	{ .Name = XCHG    , .Operand = { _AX , _DI , NS   } },
	{ .Name = CBW     , .Operand = { NS  , NS  , NS   } },
	{ .Name = CWD     , .Operand = { NS  , NS  , NS   } },
	{ .Name = CALL    , .Operand = { Ap  , NS  , NS   } },
	{ .Name = WAIT    , .Operand = { NS  , NS  , NS   } },
	{ .Name = PUSHF   , .Operand = { Fv  , NS  , NS   } },
	{ .Name = POP     , .Operand = { Fv  , NS  , NS   } },
	{ .Name = SAHF    , .Operand = { NS  , NS  , NS   } },
	{ .Name = LAHF    , .Operand = { NS  , NS  , NS   } },

	// 0xA0 - 0xAF
	{ .Name = MOV     , .Operand = { _AL , Ob  , NS   } },
	{ .Name = MOV     , .Operand = { _AX , Ov  , NS   } },
	{ .Name = MOV     , .Operand = { Ob  , _AL , NS   } },
	{ .Name = MOV     , .Operand = { Ov  , _AX , NS   } },
	{ .Name = MOVSB   , .Operand = { Xb  , Yb  , NS   } },
	{ .Name = MOVSW   , .Operand = { Xv  , Yv  , NS   } },
	{ .Name = CMPSB   , .Operand = { Xb  , Yb  , NS   } },
	{ .Name = CMPSW   , .Operand = { Xv  , Yv  , NS   } },
	{ .Name = TEST    , .Operand = { _AL , Ib  , NS   } },
	{ .Name = TEST    , .Operand = { _AX , Iv  , NS   } },
	{ .Name = STOSB   , .Operand = { Yb  , _AL , NS   } },
	{ .Name = STOSW   , .Operand = { Yv  , _AX , NS   } },
	{ .Name = LODSB   , .Operand = { _AL , Xb  , NS   } },
	{ .Name = LODSW   , .Operand = { _AX , Xv  , NS   } },
	{ .Name = SCASB   , .Operand = { _AL , Yb  , NS   } },
	{ .Name = SCASW   , .Operand = { _AX , Yv  , NS   } },

	// 0xB0 - 0xBF
	{ .Name = MOV     , .Operand = { _AL , Ib  , NS   } },
	{ .Name = MOV     , .Operand = { _CL , Ib  , NS   } },
	{ .Name = MOV     , .Operand = { _DL , Ib  , NS   } },
	{ .Name = MOV     , .Operand = { _BL , Ib  , NS   } },
	{ .Name = MOV     , .Operand = { _AH , Ib  , NS   } },
	{ .Name = MOV     , .Operand = { _CH , Ib  , NS   } },
	{ .Name = MOV     , .Operand = { _DH , Ib  , NS   } },
	{ .Name = MOV     , .Operand = { _BH , Ib  , NS   } },
	{ .Name = MOV     , .Operand = { _AX , Iv  , NS   } },
	{ .Name = MOV     , .Operand = { _CX , Iv  , NS   } },
	{ .Name = MOV     , .Operand = { _DX , Iv  , NS   } },
	{ .Name = MOV     , .Operand = { _BX , Iv  , NS   } },
	{ .Name = MOV     , .Operand = { _SP , Iv  , NS   } },
	{ .Name = MOV     , .Operand = { _BP , Iv  , NS   } },
	{ .Name = MOV     , .Operand = { _SI , Iv  , NS   } },
	{ .Name = MOV     , .Operand = { _DI , Iv  , NS   } },

	// 0xC0 - 0xCF
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = RET     , .Operand = { Iw  , NS  , NS   } },
	{ .Name = RET     , .Operand = { NS  , NS  , NS   } },
	{ .Name = LES     , .Operand = { Gv  , Mp  , NS   } },
	{ .Name = LDS     , .Operand = { Gv  , Mp  , NS   } },
	{ .Name = MOV     , .Operand = { Eb  , Ib  , NS   } },
	{ .Name = MOV     , .Operand = { Ev  , Iv  , NS   } },
	{ .Name = ENTER   , .Operand = { Iw  , Ib  , NS   } },
	{ .Name = LEAVE   , .Operand = { NS  , NS  , NS   } },
	{ .Name = RETF    , .Operand = { Iw  , NS  , NS   } },
	{ .Name = RETF    , .Operand = { NS  , NS  , NS   } },
	{ .Name = _INT    , .Operand = { _03h, NS  , NS   } },
	{ .Name = _INT    , .Operand = { Ib  , NS  , NS   } },
	{ .Name = INTO    , .Operand = { NS  , NS  , NS   } },
	{ .Name = IRET    , .Operand = { NS  , NS  , NS   } },

	// 0xD0 - 0xDF
	{ .Name = XG2     , .Operand = { Eb  , _01h, NS   } },
	{ .Name = XG2     , .Operand = { Ev  , _01h, NS   } },
	{ .Name = XG2     , .Operand = { Eb  , _CL , NS   } },
	{ .Name = XG2     , .Operand = { Ev  , _CL , NS   } },
	{ .Name = AAM     , .Operand = { NS  , NS  , NS   } },
	{ .Name = AAD     , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = XLAT    , .Operand = { NS  , NS  , NS   } },
	{ .Name = FPU     , .Operand = { NS  , NS  , NS   } },
	{ .Name = FPU     , .Operand = { NS  , NS  , NS   } },
	{ .Name = FPU     , .Operand = { NS  , NS  , NS   } },
	{ .Name = FPU     , .Operand = { NS  , NS  , NS   } },
	{ .Name = FPU     , .Operand = { NS  , NS  , NS   } },
	{ .Name = FPU     , .Operand = { NS  , NS  , NS   } },
	{ .Name = FPU     , .Operand = { NS  , NS  , NS   } },
	{ .Name = FPU     , .Operand = { NS  , NS  , NS   } },

	// 0xE0 - 0xEF
	{ .Name = LOOPN   , .Operand = { Jb  , NS  , NS   } },
	{ .Name = LOOPE   , .Operand = { Jb  , NS  , NS   } },
	{ .Name = LOOP    , .Operand = { Jb  , NS  , NS   } },
	{ .Name = JCXZ    , .Operand = { Jb  , NS  , NS   } },
	{ .Name = IN      , .Operand = { _AL , Ib  , NS   } },
	{ .Name = IN      , .Operand = { _AX , Ib  , NS   } },
	{ .Name = OUT     , .Operand = { Ib  , _AL , NS   } },
	{ .Name = OUT     , .Operand = { Ib  , _AX , NS   } },
	{ .Name = CALL    , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JMP     , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JMP     , .Operand = { Ap  , NS  , NS   } },
	{ .Name = JMP     , .Operand = { Jb  , NS  , NS   } },
	{ .Name = IN      , .Operand = { _AL , _DX , NS   } },
	{ .Name = IN      , .Operand = { _AX , _DX , NS   } },
	{ .Name = OUT     , .Operand = { _DX , _AL , NS   } },
	{ .Name = OUT     , .Operand = { _DX , _AX , NS   } },

	// 0xF0 - 0xFF
	{ .Name = LOCK    , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = REPNE   , .Operand = { NS  , NS  , NS   } },
	{ .Name = REP     , .Operand = { NS  , NS  , NS   } },
	{ .Name = HLT     , .Operand = { NS  , NS  , NS   } },
	{ .Name = CMC     , .Operand = { NS  , NS  , NS   } },
	{ .Name = XG3     , .Operand = { Eb  , NS  , NS   } },
	{ .Name = XG3     , .Operand = { Ev  , NS  , NS   } },
	{ .Name = CLC     , .Operand = { NS  , NS  , NS   } },
	{ .Name = STC     , .Operand = { NS  , NS  , NS   } },
	{ .Name = CLI     , .Operand = { NS  , NS  , NS   } },
	{ .Name = STI     , .Operand = { NS  , NS  , NS   } },
	{ .Name = CLD     , .Operand = { NS  , NS  , NS   } },
	{ .Name = STD     , .Operand = { NS  , NS  , NS   } },
	{ .Name = XG4     , .Operand = { NS  , NS  , NS   } },
	{ .Name = XG5     , .Operand = { NS  , NS  , NS   } },

	// Two byte opcodes

	// 0x00 - 0x0F
	{ .Name = XG6     , .Operand = { NS  , NS  , NS   } },
	{ .Name = XG7     , .Operand = { NS  , NS  , NS   } },
	{ .Name = LAR     , .Operand = { Gv  , Ew  , NS   } },
	{ .Name = LSL     , .Operand = { Gv  , Ew  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = CLTS    , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = INVD    , .Operand = { NS  , NS  , NS   } },
	{ .Name = WBINVD  , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = UD2     , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },

	// 0x10 - 0x1F
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },

	// 0x20 - 0x2F
	{ .Name = MOV     , .Operand = { Rd  , Cd  , NS   } },
	{ .Name = MOV     , .Operand = { Rd  , Dd  , NS   } },
	{ .Name = MOV     , .Operand = { Cd  , Rd  , NS   } },
	{ .Name = MOV     , .Operand = { Dd  , Rd  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },

	// 0x30 - 0x3F
	{ .Name = WRMSR   , .Operand = { NS  , NS  , NS   } },
	{ .Name = RDTSC   , .Operand = { NS  , NS  , NS   } },
	{ .Name = RDMSR   , .Operand = { NS  , NS  , NS   } },
	{ .Name = RDPMC   , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },

	// 0x40 - 0x4F
	{ .Name = CMOVO   , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVNO  , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVB   , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVNB  , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVE   , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVNE  , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVBE  , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVVA  , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVS   , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVNS  , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVP   , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVNP  , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVL   , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVGE  , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVLE  , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = CMOVG   , .Operand = { Gv  , Ev  , NS   } },

	// 0x50 - 0x5F
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },

	// 0x60 - 0x6F
	{ .Name = PUNPCKLBW, .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PUNPCKLWD, .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PUNPCKLDQ, .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PACKUSDW, .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PCMPGTB , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PCMPGTW , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PCMPGTD , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PACKSSWB, .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PUNPCKHBW, .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PUNPCKHWD, .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PUNPCKHDQ, .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PACKSSDW, .Operand = { Pq  , Qd  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = MOVD    , .Operand = { Pd  , Ed  , NS   } },
	{ .Name = MOVQ    , .Operand = { Pq  , Qq  , NS   } },

	// 0x70 - 0x7F
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = XG10    , .Operand = { NS  , NS  , NS   } },
	{ .Name = XG10    , .Operand = { NS  , NS  , NS   } },
	{ .Name = XG10    , .Operand = { NS  , NS  , NS   } },
	{ .Name = PCMPEQB , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PCMPEQW , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PCMPEQD , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = EMMS    , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = MOVD    , .Operand = { Ed  , Pd  , NS   } },
	{ .Name = MOVQ    , .Operand = { Qq  , Pq  , NS   } },

	// 0x80 - 0x8F
	{ .Name = JO      , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JNO     , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JJB     , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JNB     , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JZ      , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JNZ     , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JBE     , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JNBE    , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JS      , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JNS     , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JP      , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JNP     , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JL      , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JNL     , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JLE     , .Operand = { Jv  , NS  , NS   } },
	{ .Name = JNLE    , .Operand = { Jv  , NS  , NS   } },

	// 0x90 - 0x9F
	{ .Name = SETO    , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETNO   , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETB    , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETNB   , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETZ    , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETNZ   , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETBE   , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETNBE  , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETS    , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETNS   , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETP    , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETNP   , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETL    , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETNL   , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETLE   , .Operand = { Eb  , NS  , NS   } },
	{ .Name = SETNLE  , .Operand = { Eb  , NS  , NS   } },

	// 0xA0 - 0xAF
	{ .Name = PUSH    , .Operand = { _FS , NS  , NS   } },
	{ .Name = POP     , .Operand = { _FS , NS  , NS   } },
	{ .Name = CPUID   , .Operand = { NS  , NS  , NS   } },
	{ .Name = BT      , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = SHLD    , .Operand = { Ev  , Gv  , Ib   } },
	{ .Name = SHLD    , .Operand = { Ev  , Gv  , _CL  } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PUSH    , .Operand = { _GS , NS  , NS   } },
	{ .Name = POP     , .Operand = { _GS , NS  , NS   } },
	{ .Name = RSM     , .Operand = { NS  , NS  , NS   } },
	{ .Name = BTS     , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = SHRD    , .Operand = { Ev  , Gv  , Ib   } },
	{ .Name = SHRD    , .Operand = { Ev  , Gv  , _CL  } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = IMUL    , .Operand = { Gv  , Ev  , NS   } },

	// 0xB0 - 0xBF
	{ .Name = CMPXCHG , .Operand = { Eb  , Gb  , NS   } },
	{ .Name = CMPXCHG , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = LSS     , .Operand = { Mp  , NS  , NS   } },
	{ .Name = BTR     , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = LFS     , .Operand = { Mp  , NS  , NS   } },
	{ .Name = LGS     , .Operand = { Mp  , NS  , NS   } },
	{ .Name = MOVZX   , .Operand = { Gv  , Eb  , NS   } },
	{ .Name = MOVZX   , .Operand = { Gv  , Ew  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = INVALID , .Operand = { NS  , NS  , NS   } },
	{ .Name = XG8     , .Operand = { Ev  , Ib  , NS   } },
	{ .Name = BTC     , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = BSF     , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = BSR     , .Operand = { Gv  , Ev  , NS   } },
	{ .Name = MOVSX   , .Operand = { Gv  , Eb  , NS   } },
	{ .Name = MOVSX   , .Operand = { Gv  , Ew  , NS   } },

	// 0xC0 - 0xCF
	{ .Name = XADD    , .Operand = { Eb  , Gb  , NS   } },
	{ .Name = XADD    , .Operand = { Ev  , Gv  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = XG9     , .Operand = { NS  , NS  , NS   } },
	{ .Name = BSWAP   , .Operand = { _EAX, NS  , NS   } },
	{ .Name = BSWAP   , .Operand = { _ECX, NS  , NS   } },
	{ .Name = BSWAP   , .Operand = { _EDX, NS  , NS   } },
	{ .Name = BSWAP   , .Operand = { _EBX, NS  , NS   } },
	{ .Name = BSWAP   , .Operand = { _ESP, NS  , NS   } },
	{ .Name = BSWAP   , .Operand = { _EBP, NS  , NS   } },
	{ .Name = BSWAP   , .Operand = { _ESI, NS  , NS   } },
	{ .Name = BSWAP   , .Operand = { _EDI, NS  , NS   } },

	// 0xD0 - 0xDF
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PSRLW   , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PSRLD   , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PSRLQ   , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PMULLW  , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PSUBUSB , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = PSUBUSW , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PAND    , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = PADDUSB , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = PADDUSW , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PANDN   , .Operand = { Pq  , Qq  , NS   } },

	// 0xE0 - 0xEF
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PSRAW   , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PSRAD   , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PMULHW  , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PSUBSB  , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = PSUBSW  , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = POR     , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = PADDSB  , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = PADDSW  , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PXOR    , .Operand = { Pq  , Qq  , NS   } },

	// 0xF0 - 0xFF
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PSLLW   , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PSLLD   , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = PSLLQ   , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PMADDWD , .Operand = { Pq  , Qd  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PSUBB   , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = PSUBW   , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = PSUBD   , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PADDB   , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = PADDW   , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = PADDD   , .Operand = { Pq  , Qq  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
};

/*************************************************************************************************/

INTEL_OPCODE_PROTOTYPE Extension_Table [80] =
{
	// Group 1
	{ .Name = ADD     , .Operand = { NS  , NS  , NS   } },
	{ .Name = OR      , .Operand = { NS  , NS  , NS   } },
	{ .Name = ADC     , .Operand = { NS  , NS  , NS   } },
	{ .Name = SBB     , .Operand = { NS  , NS  , NS   } },
	{ .Name = AND     , .Operand = { NS  , NS  , NS   } },
	{ .Name = SUB     , .Operand = { NS  , NS  , NS   } },
	{ .Name = XOR     , .Operand = { NS  , NS  , NS   } },
	{ .Name = CMP     , .Operand = { NS  , NS  , NS   } },

	// Group 2
	{ .Name = ROL     , .Operand = { NS  , NS  , NS   } },
	{ .Name = ROR     , .Operand = { NS  , NS  , NS   } },
	{ .Name = RCL     , .Operand = { NS  , NS  , NS   } },
	{ .Name = RCR     , .Operand = { NS  , NS  , NS   } },
	{ .Name = SHL     , .Operand = { NS  , NS  , NS   } },
	{ .Name = SHR     , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = SAR     , .Operand = { NS  , NS  , NS   } },

	// Group 3
	{ .Name = TEST    , .Operand = { Ib  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NOT     , .Operand = { NS  , NS  , NS   } },
	{ .Name = NEG     , .Operand = { NS  , NS  , NS   } },
	{ .Name = MUL     , .Operand = { _AL , NS  , NS   } },
	{ .Name = IMUL    , .Operand = { _AL , NS  , NS   } },
	{ .Name = DIV     , .Operand = { _AL , NS  , NS   } },
	{ .Name = IDIV    , .Operand = { _AL , NS  , NS   } },

	// Group 4
	{ .Name = INC     , .Operand = { Eb  , NS  , NS   } },
	{ .Name = DEC     , .Operand = { Eb  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },

	// Group 5
	{ .Name = INC     , .Operand = { Ev  , NS  , NS   } },
	{ .Name = DEC     , .Operand = { Ev  , NS  , NS   } },
	{ .Name = CALL    , .Operand = { Ev  , NS  , NS   } },
	{ .Name = CALL    , .Operand = { Ep  , NS  , NS   } },
	{ .Name = JMP     , .Operand = { Ev  , NS  , NS   } },
	{ .Name = JMP     , .Operand = { Ep  , NS  , NS   } },
	{ .Name = PUSH    , .Operand = { Ev  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },

	// Group 6
	{ .Name = SLDT    , .Operand = { Ew  , NS  , NS   } },
	{ .Name = _STR    , .Operand = { Ew  , NS  , NS   } },
	{ .Name = LLDT    , .Operand = { Ew  , NS  , NS   } },
	{ .Name = LTR     , .Operand = { Ew  , NS  , NS   } },
	{ .Name = VERR    , .Operand = { Ew  , NS  , NS   } },
	{ .Name = VERW    , .Operand = { Ew  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },

	// Group 7
	{ .Name = SGDT    , .Operand = { Ms  , NS  , NS   } },
	{ .Name = SIDT    , .Operand = { Ms  , NS  , NS   } },
	{ .Name = LGDT    , .Operand = { Ms  , NS  , NS   } },
	{ .Name = LIDT    , .Operand = { Ms  , NS  , NS   } },
	{ .Name = SMSW    , .Operand = { Ew  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = LMSW    , .Operand = { Ew  , NS  , NS   } },
	{ .Name = INVLPG  , .Operand = { NS  , NS  , NS   } },

	// Group 8
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = BT      , .Operand = { NS  , NS  , NS   } },
	{ .Name = BTS     , .Operand = { NS  , NS  , NS   } },
	{ .Name = BTR     , .Operand = { NS  , NS  , NS   } },
	{ .Name = BTC     , .Operand = { NS  , NS  , NS   } },

	// Group 9
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = CMPXCH8B, .Operand = { Mq  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },

	// Group A
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PSRL    , .Operand = { Pq  , Ib  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PSRA    , .Operand = { Pq  , Ib  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
	{ .Name = PSLL    , .Operand = { Pq  , Ib  , NS   } },
	{ .Name = NS      , .Operand = { NS  , NS  , NS   } },
};
