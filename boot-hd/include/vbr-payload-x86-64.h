
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

#ifndef VBR_PAYLOAD_X86_64_H_INCLUDED
#define VBR_PAYLOAD_X86_64_H_INCLUDED

#include "Base.h"

/************************************************************************/
// Constants describing the long mode paging layout

#define PAGE_SIZE N_4KB
#define PAGE_TABLE_NUM_ENTRIES 512
#define PAGE_DIRECTORY_ENTRY_COUNT 512
#define PML4_RECURSIVE_SLOT 510

/************************************************************************/
// Size helpers derived from the raw entry layout

#define PAGE_TABLE_ENTRY_SIZE ((UINT)sizeof(VBR_X86_64_PAGING_ENTRY))
#define PAGE_TABLE_SIZE (PAGE_TABLE_NUM_ENTRIES * PAGE_TABLE_ENTRY_SIZE)

/************************************************************************/

typedef struct PACKED tag_SEGMENT_DESCRIPTOR {
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

typedef struct PACKED tag_GDT_REGISTER {
    U16 Limit;
    U32 Base;
} GDT_REGISTER;

/************************************************************************/
// Raw paging entries usable from 32-bit code

typedef struct PACKED tag_VBR_X86_64_PAGING_ENTRY {
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

/************************************************************************/
// Segment selector helpers shared between C and assembly

#define VBR_GDT_ENTRY_LIST(OP) \
    OP(VBR_GDT_ENTRY_NULL, 0u) \
    OP(VBR_GDT_ENTRY_PROTECTED_CODE, 1u) \
    OP(VBR_GDT_ENTRY_PROTECTED_DATA, 2u) \
    OP(VBR_GDT_ENTRY_LONG_MODE_CODE, 3u)

enum {
#define VBR_GDT_ENTRY(Name, Value) Name = Value,
    VBR_GDT_ENTRY_LIST(VBR_GDT_ENTRY)
#undef VBR_GDT_ENTRY
};

#define VBR_GDT_SELECTOR_FROM_INDEX(Index) ((U16)((Index) * (U16)sizeof(SEGMENT_DESCRIPTOR)))

enum {
    VBR_PROTECTED_MODE_CODE_SELECTOR = VBR_GDT_SELECTOR_FROM_INDEX(VBR_GDT_ENTRY_PROTECTED_CODE),
    VBR_PROTECTED_MODE_DATA_SELECTOR = VBR_GDT_SELECTOR_FROM_INDEX(VBR_GDT_ENTRY_PROTECTED_DATA),
    VBR_LONG_MODE_CODE_SELECTOR = VBR_GDT_SELECTOR_FROM_INDEX(VBR_GDT_ENTRY_LONG_MODE_CODE),
    VBR_LONG_MODE_DATA_SELECTOR = VBR_PROTECTED_MODE_DATA_SELECTOR
};

extern const U16 VbrProtectedModeCodeSelector;
extern const U16 VbrProtectedModeDataSelector;
extern const U16 VbrLongModeCodeSelector;
extern const U16 VbrLongModeDataSelector;

#endif // VBR_PAYLOAD_X86_64_H_INCLUDED
