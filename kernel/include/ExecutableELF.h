
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


    Executable ELF

\************************************************************************/
#ifndef EXECUTABLEELF_H_INCLUDED
#define EXECUTABLEELF_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "File.h"
#include "Executable.h"

/************************************************************************/
// ELF signature and basic constants

#define ELF_SIGNATURE          0x464C457F  // 0x7F 'E' 'L' 'F'

/* e_ident indices */
#define EI_MAG0                0
#define EI_MAG1                1
#define EI_MAG2                2
#define EI_MAG3                3
#define EI_CLASS               4
#define EI_DATA                5
#define EI_VERSION             6
#define EI_OSABI               7
#define EI_ABIVERSION          8
#define EI_PAD                 9
#define EI_NIDENT              16

/* EI_CLASS */
#define ELFCLASS32             1
/* EI_DATA */
#define ELFDATA2LSB            1
/* e_version */
#define EV_CURRENT             1

/* e_type */
#define ET_NONE                0
#define ET_REL                 1
#define ET_EXEC                2
#define ET_DYN                 3

/* e_machine */
#define EM_386                 3

/* Program header types */
#define PT_NULL                0
#define PT_LOAD                1
#define PT_DYNAMIC             2
#define PT_INTERP              3
#define PT_NOTE                4
#define PT_PHDR                6
#define PT_TLS                 7
#define PT_GNU_STACK           0x6474e551

/* Program header flags */
#define PF_X                   0x1
#define PF_W                   0x2
#define PF_R                   0x4

/************************************************************************/
// Minimal 32-bit ELF structures (packed)

typedef struct __attribute__((packed)) tag_Elf32_Ehdr {
    U8  e_ident[EI_NIDENT];
    U16 e_type;
    U16 e_machine;
    U32 e_version;
    U32 e_entry;
    U32 e_phoff;
    U32 e_shoff;
    U32 e_flags;
    U16 e_ehsize;
    U16 e_phentsize;
    U16 e_phnum;
    U16 e_shentsize;
    U16 e_shnum;
    U16 e_shstrndx;
} Elf32_Ehdr;

typedef struct __attribute__((packed)) tag_Elf32_Phdr {
    U32 p_type;
    U32 p_offset;
    U32 p_vaddr;
    U32 p_paddr;
    U32 p_filesz;
    U32 p_memsz;
    U32 p_flags;
    U32 p_align;
} Elf32_Phdr;

/************************************************************************/
// ELF-specific entry points (mirror the generic ones, with explicit bases)

BOOL GetExecutableInfo_ELF(LPFILE File, LPEXECUTABLEINFO Info);
BOOL LoadExecutable_ELF(LPFILE File, LPEXECUTABLEINFO Info, LINEAR CodeBase, LINEAR DataBase, LINEAR BssBase);

/************************************************************************/

#endif
