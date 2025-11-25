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


    x86-64 memory-specific definitions

\************************************************************************/

#ifndef X86_64_MEMORY_H_INCLUDED
#define X86_64_MEMORY_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/
// #defines

#define PAGE_SIZE N_4KB
#define PAGE_SIZE_MUL MUL_4KB
#define PAGE_SIZE_MASK (PAGE_SIZE - 1u)

#define PAGE_TABLE_ENTRY_SIZE (sizeof(U64))
#define PAGE_TABLE_NUM_ENTRIES 512u
#define PAGE_TABLE_SIZE (PAGE_TABLE_NUM_ENTRIES * PAGE_TABLE_ENTRY_SIZE)
#define PAGE_TABLE_SIZE_MUL MUL_4KB
#define PAGE_TABLE_SIZE_MASK (PAGE_TABLE_SIZE - 1u)

#define PAGE_TABLE_CAPACITY (PAGE_TABLE_NUM_ENTRIES * PAGE_SIZE)
#define PAGE_TABLE_CAPACITY_MUL MUL_2MB
#define PAGE_TABLE_CAPACITY_MASK (PAGE_TABLE_CAPACITY - 1u)

#define PAGE_MASK (~(PAGE_SIZE - 1u))

#define PAGE_2M_SIZE N_2MB
#define PAGE_2M_MASK (PAGE_2M_SIZE - 1u)

#define PAGE_PRIVILEGE_KERNEL 0
#define PAGE_PRIVILEGE_USER 1

#define PAGE_ALIGN(a) (((a) + PAGE_SIZE - 1u) & PAGE_MASK)

#define X86_64_TEMP_LINEAR_PAGE_1 (VMA_KERNEL + (LINEAR)0x0000000000100000)
#define X86_64_TEMP_LINEAR_PAGE_2 (X86_64_TEMP_LINEAR_PAGE_1 + (LINEAR)PAGE_SIZE)
#define X86_64_TEMP_LINEAR_PAGE_3 (X86_64_TEMP_LINEAR_PAGE_2 + (LINEAR)PAGE_SIZE)

#define TEMP_LINEAR_PAGE_1 X86_64_TEMP_LINEAR_PAGE_1
#define TEMP_LINEAR_PAGE_2 X86_64_TEMP_LINEAR_PAGE_2
#define TEMP_LINEAR_PAGE_3 X86_64_TEMP_LINEAR_PAGE_3

#ifndef CONFIG_VMA_KERNEL
#error "CONFIG_VMA_KERNEL is not defined"
#endif

#define VMA_USER ((LINEAR)0x0000000000400000)
#define VMA_KERNEL (CONFIG_VMA_KERNEL)

#define PAGE_PRIVILEGE(adr) ((adr >= VMA_USER && adr < VMA_KERNEL) ? PAGE_PRIVILEGE_USER : PAGE_PRIVILEGE_KERNEL)

#define PML4_ENTRY_COUNT 512u
#define PDPT_ENTRY_COUNT 512u
#define PAGE_DIRECTORY_ENTRY_COUNT 512u

#define PML4_RECURSIVE_SLOT 510u

#define PAGE_FLAG_PRESENT ((U64)1u << 0)
#define PAGE_FLAG_READ_WRITE ((U64)1u << 1)
#define PAGE_FLAG_USER ((U64)1u << 2)
#define PAGE_FLAG_WRITE_THROUGH ((U64)1u << 3)
#define PAGE_FLAG_CACHE_DISABLED ((U64)1u << 4)
#define PAGE_FLAG_ACCESSED ((U64)1u << 5)
#define PAGE_FLAG_DIRTY ((U64)1u << 6)
#define PAGE_FLAG_PAGE_SIZE ((U64)1u << 7)
#define PAGE_FLAG_GLOBAL ((U64)1u << 8)
#define PAGE_FLAG_NO_EXECUTE ((U64)1ull << 63)

/************************************************************************/
// typedefs

typedef struct PACKED tag_X86_64_PAGING_ENTRY {
    U64 Present : 1;
    U64 ReadWrite : 1;
    U64 Privilege : 1;
    U64 WriteThrough : 1;
    U64 CacheDisabled : 1;
    U64 Accessed : 1;
    U64 Dirty : 1;
    U64 PageSize : 1;
    U64 Global : 1;
    U64 Available : 3;
    U64 Address : 40;
    U64 AvailableHigh : 11;
    U64 NoExecute : 1;
} X86_64_PAGING_ENTRY, *LPX86_64_PAGING_ENTRY;

typedef X86_64_PAGING_ENTRY X86_64_PML4_ENTRY;
typedef X86_64_PAGING_ENTRY X86_64_PDPT_ENTRY;
typedef X86_64_PAGING_ENTRY X86_64_PAGE_DIRECTORY_ENTRY;
typedef X86_64_PAGING_ENTRY X86_64_PAGE_TABLE_ENTRY;

typedef X86_64_PML4_ENTRY* LPPML4;
typedef X86_64_PDPT_ENTRY* LPPDPT;
typedef X86_64_PAGE_DIRECTORY_ENTRY* LPPAGE_DIRECTORY;
typedef X86_64_PAGE_TABLE_ENTRY* LPPAGE_TABLE;

typedef struct tag_ARCH_PAGE_ITERATOR {
    LINEAR Linear;
    UINT Pml4Index;
    UINT PdptIndex;
    UINT DirectoryIndex;
    UINT TableIndex;
} ARCH_PAGE_ITERATOR;

/************************************************************************/
// inlines

static inline U64 CanonicalizeLinearAddress(U64 Address) {
    const U64 SignBit = ((U64)1u << 47);
    const U64 LowMask = ((U64)1u << 48) - 1u;

    if ((Address & SignBit) != 0) {
        return Address | ~LowMask;
    }

    return Address & LowMask;
}

/************************************************************************/

static inline U64 GetMaxLinearAddressPlusOne(void) {
    return (U64)0x0001000000000000ull;
}

/************************************************************************/

static inline U64 GetMaxPhysicalAddressPlusOne(void) {
    return MAX_U64;
}

/************************************************************************/

static inline BOOL ClipPhysicalRange(U64 Base, U64 Length, PHYSICAL* OutBase, UINT* OutLength) {
    const U64 Limit = GetMaxPhysicalAddressPlusOne();

    if ((Length == 0) || OutBase == NULL || OutLength == NULL) return FALSE;
    if (Base >= Limit) return FALSE;

    U64 MaxLength = Limit - Base;
    U64 ClippedLength = (Length > MaxLength) ? MaxLength : Length;

    *OutBase = (PHYSICAL)Base;
    *OutLength = (UINT)ClippedLength;

    return (*OutLength != 0);
}

/************************************************************************/
// external symbols

PHYSICAL ComputeLowMemoryWindowLimit(UINT TotalMemoryBytes);
PHYSICAL GetLowMemoryWindowLimit(void);

static inline LINEAR BuildRecursiveAddress(UINT Pml4, UINT Pdpt, UINT Directory, UINT Table) {
    return (LINEAR)(((U64)Pml4 << 39)
        | ((U64)Pdpt << 30)
        | ((U64)Directory << 21)
        | ((U64)Table << 12));
}

#endif  // X86_64_MEMORY_H_INCLUDED
