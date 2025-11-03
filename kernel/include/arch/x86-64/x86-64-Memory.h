
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

#include "Base.h"

/************************************************************************/
// #defines

#define PAGE_SIZE N_4KB
#define PAGE_SIZE_MUL MUL_4KB
#define PAGE_SIZE_MASK ((U64)PAGE_SIZE - (U64)1)

#define PAGE_TABLE_ENTRY_SIZE ((UINT)sizeof(U64))
#define PAGE_TABLE_NUM_ENTRIES 512u
#define PAGE_TABLE_SIZE (PAGE_TABLE_NUM_ENTRIES * PAGE_TABLE_ENTRY_SIZE)
#define PAGE_TABLE_SIZE_MUL MUL_4KB
#define PAGE_TABLE_CAPACITY (PAGE_TABLE_NUM_ENTRIES * PAGE_SIZE)
#define PAGE_TABLE_CAPACITY_MUL MUL_2MB
#define PAGE_TABLE_CAPACITY_MASK ((U64)PAGE_TABLE_CAPACITY - (U64)1)

#define PAGE_MASK (~((U64)PAGE_SIZE - (U64)1))

#define PAGE_PRIVILEGE_KERNEL 0u
#define PAGE_PRIVILEGE_USER 1u

#define PAGE_FLAG_PRESENT ((U64)1 << 0)
#define PAGE_FLAG_READ_WRITE ((U64)1 << 1)
#define PAGE_FLAG_USER ((U64)1 << 2)
#define PAGE_FLAG_WRITE_THROUGH ((U64)1 << 3)
#define PAGE_FLAG_CACHE_DISABLED ((U64)1 << 4)
#define PAGE_FLAG_ACCESSED ((U64)1 << 5)
#define PAGE_FLAG_DIRTY ((U64)1 << 6)
#define PAGE_FLAG_PAGE_SIZE ((U64)1 << 7)
#define PAGE_FLAG_GLOBAL ((U64)1 << 8)
#define PAGE_FLAG_FIXED ((U64)1 << 9)
#define PAGE_FLAG_NO_EXECUTE ((U64)1 << 63)

#define PML4_ENTRY_COUNT 512u
#define PDPT_ENTRY_COUNT 512u
#define PAGE_DIRECTORY_ENTRY_COUNT 512u
#define PML4_RECURSIVE_SLOT 510u
#define PD_RECURSIVE_SLOT PML4_RECURSIVE_SLOT

#define VMA_RAM ((U64)0x0000000000000000)
#define VMA_VIDEO ((U64)0x00000000000A0000)
#define VMA_CONSOLE ((U64)0x00000000000B8000)
#define VMA_USER ((U64)0x0000000000400000)
#define VMA_LIBRARY ((U64)0x00007F0000000000)
#define VMA_TASK_RUNNER (VMA_LIBRARY - PAGE_SIZE)
#ifndef CONFIG_VMA_KERNEL
#error "CONFIG_VMA_KERNEL is not defined"
#endif

#define VMA_KERNEL (CONFIG_VMA_KERNEL)

#define X86_64_TEMP_LINEAR_PAGE_1 (VMA_KERNEL + (LINEAR)0x00100000)
#define X86_64_TEMP_LINEAR_PAGE_2 (X86_64_TEMP_LINEAR_PAGE_1 + (U64)0x00001000)
#define X86_64_TEMP_LINEAR_PAGE_3 (X86_64_TEMP_LINEAR_PAGE_2 + (U64)0x00001000)

#define TEMP_LINEAR_PAGE_1 X86_64_TEMP_LINEAR_PAGE_1
#define TEMP_LINEAR_PAGE_2 X86_64_TEMP_LINEAR_PAGE_2
#define TEMP_LINEAR_PAGE_3 X86_64_TEMP_LINEAR_PAGE_3

#define PAGE_PRIVILEGE(Address) \
    (((U64)(Address) >= VMA_USER && (U64)(Address) < VMA_KERNEL) ? PAGE_PRIVILEGE_USER : PAGE_PRIVILEGE_KERNEL)

#define PAGE_ALIGN(Address) (((U64)(Address) + PAGE_SIZE - (U64)1) & PAGE_MASK)

/************************************************************************/
// typedefs

typedef struct tag_X86_64_PAGING_ENTRY {
    U64 Present : 1;
    U64 ReadWrite : 1;
    U64 Privilege : 1;
    U64 WriteThrough : 1;
    U64 CacheDisabled : 1;
    U64 Accessed : 1;
    U64 Dirty : 1;
    U64 PageSize : 1;
    U64 Global : 1;
    U64 Available_9_11 : 3;
    U64 Address : 40;
    U64 Available_52_58 : 7;
    U64 Reserved_59_62 : 4;
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

/************************************************************************/
// inlines

static inline U64 CanonicalizeLinearAddress(U64 Address) {
    const U64 SignBit = (U64)1 << 47;
    const U64 Mask = ((U64)1 << 48) - (U64)1;

    Address &= Mask;
    if ((Address & SignBit) != 0) {
        Address |= (U64)0xFFFF000000000000;
    }

    return Address;
}

#endif  // X86_64_MEMORY_H_INCLUDED
