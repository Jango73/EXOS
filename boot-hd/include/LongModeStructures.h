/************************************************************************\

    EXOS Bootloader
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


    Long mode paging structures for the VBR payload

\************************************************************************/
#ifndef LONG_MODE_STRUCTURES_H_INCLUDED
#define LONG_MODE_STRUCTURES_H_INCLUDED

#include "../../kernel/include/Base.h"

/************************************************************************/
// Constants describing the long mode paging layout
/************************************************************************/
#define PAGE_SIZE N_4KB
#define PAGE_TABLE_NUM_ENTRIES 512u
#define PAGE_DIRECTORY_ENTRY_COUNT 512u
#define PML4_RECURSIVE_SLOT 510u

/************************************************************************/
// Size helpers derived from the raw entry layout
/************************************************************************/
#define PAGE_TABLE_ENTRY_SIZE ((UINT)sizeof(VBR_X86_64_PAGING_ENTRY))
#define PAGE_TABLE_SIZE (PAGE_TABLE_NUM_ENTRIES * PAGE_TABLE_ENTRY_SIZE)

typedef struct tag_SEGMENT_DESCRIPTOR {
    U32 Limit_00_15 : 16;
    U32 Base_00_15 : 16;
    U32 Base_16_23 : 8;
    U32 Accessed : 1;
    U32 CanWrite : 1;
    U32 ConformExpand : 1;
    U32 Type : 1;
    U32 Segment : 1;
    U32 Privilege : 2;
    U32 Present : 1;
    U32 Limit_16_19 : 4;
    U32 Available : 1;
    U32 Unused : 1;
    U32 OperandSize : 1;
    U32 Granularity : 1;
    U32 Base_24_31 : 8;
} SEGMENT_DESCRIPTOR, *LPSEGMENT_DESCRIPTOR;

typedef struct {
    U16 Limit;
    U32 Base;
} GDT_REGISTER;

/************************************************************************/
// Raw paging entries usable from 32-bit code
/************************************************************************/
typedef struct tag_VBR_X86_64_PAGING_ENTRY {
    U32 Low;
    U32 High;
} VBR_X86_64_PAGING_ENTRY;

typedef VBR_X86_64_PAGING_ENTRY* LPVBR_X86_64_PAGING_ENTRY;

typedef VBR_X86_64_PAGING_ENTRY X86_64_PAGING_ENTRY;
typedef X86_64_PAGING_ENTRY* LPX86_64_PAGING_ENTRY;

typedef X86_64_PAGING_ENTRY X86_64_PML4_ENTRY;
typedef X86_64_PAGING_ENTRY X86_64_PDPT_ENTRY;
typedef X86_64_PAGING_ENTRY X86_64_PAGE_DIRECTORY_ENTRY;
typedef X86_64_PAGING_ENTRY X86_64_PAGE_TABLE_ENTRY;

typedef X86_64_PML4_ENTRY* LPPML4;
typedef X86_64_PDPT_ENTRY* LPPDPT;
typedef X86_64_PAGE_DIRECTORY_ENTRY* LPPAGE_DIRECTORY;
typedef X86_64_PAGE_TABLE_ENTRY* LPPAGE_TABLE;

#endif // LONG_MODE_STRUCTURES_H_INCLUDED
