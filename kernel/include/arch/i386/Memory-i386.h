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


    i386 memory-specific definitions

\************************************************************************/

#ifndef ARCH_I386_MEMORY_I386_H_INCLUDED
#define ARCH_I386_MEMORY_I386_H_INCLUDED

#include "Base.h"

/***************************************************************************/
// Page directory entry structure (4 bytes)
/***************************************************************************/

typedef struct tag_PAGE_DIRECTORY {
    U32 Present : 1;    // Is page present in RAM ?
    U32 ReadWrite : 1;  // Read-write access rights
    U32 Privilege : 1;  // Privilege level
    U32 WriteThrough : 1;
    U32 CacheDisabled : 1;
    U32 Accessed : 1;  // Has page been accessed ?
    U32 Reserved : 1;
    U32 PageSize : 1;  // 0 = 4KB
    U32 Global : 1;    // Ignored
    U32 User : 2;      // Available to OS
    U32 Fixed : 1;     // EXOS : Can page be swapped ?
    U32 Address : 20;  // Physical address
} PAGE_DIRECTORY, *LPPAGE_DIRECTORY;

/***************************************************************************/
// Page table entry structure (4 bytes)
/***************************************************************************/

typedef struct tag_PAGE_TABLE {
    U32 Present : 1;    // Is page present in RAM ?
    U32 ReadWrite : 1;  // Read-write access rights
    U32 Privilege : 1;  // Privilege level
    U32 WriteThrough : 1;
    U32 CacheDisabled : 1;
    U32 Accessed : 1;  // Has page been accessed ?
    U32 Dirty : 1;     // Has been written to ?
    U32 Reserved : 1;  // Reserved by Intel
    U32 Global : 1;
    U32 User : 2;      // Available to OS
    U32 Fixed : 1;     // EXOS : Can page be swapped ?
    U32 Address : 20;  // Physical address
} PAGE_TABLE, *LPPAGE_TABLE;

/***************************************************************************/
// Page sizing and address space definitions
/***************************************************************************/

#define PAGE_SIZE N_4KB
#define PAGE_SIZE_MUL MUL_4KB
#define PAGE_SIZE_MASK (PAGE_SIZE - 1)

#define PAGE_TABLE_SIZE N_4KB
#define PAGE_TABLE_SIZE_MUL MUL_4KB
#define PAGE_TABLE_SIZE_MASK (PAGE_TABLE_SIZE - 1)

#define PAGE_TABLE_ENTRY_SIZE (sizeof(U32))
#define PAGE_TABLE_NUM_ENTRIES (PAGE_TABLE_SIZE / PAGE_TABLE_ENTRY_SIZE)

#define PAGE_TABLE_CAPACITY (PAGE_TABLE_NUM_ENTRIES * PAGE_SIZE)
#define PAGE_TABLE_CAPACITY_MUL MUL_4MB
#define PAGE_TABLE_CAPACITY_MASK (PAGE_TABLE_CAPACITY - 1)

#define PAGE_MASK (~(PAGE_SIZE - 1))

#define PAGE_PRIVILEGE_KERNEL 0
#define PAGE_PRIVILEGE_USER 1

#define PAGE_ALIGN(a) (((a) + PAGE_SIZE - 1) & PAGE_MASK)

/***************************************************************************/
// Virtual memory layout
/***************************************************************************/

#define VMA_RAM 0x00000000                         // Reserved for kernel
#define VMA_VIDEO 0x000A0000                       // Reserved for kernel
#define VMA_CONSOLE 0x000B8000                     // Reserved for kernel
#define VMA_USER 0x00400000                        // Start of user address space
#define VMA_LIBRARY 0xA0000000                     // Dynamic Libraries
#define VMA_TASK_RUNNER (VMA_LIBRARY - PAGE_SIZE)  // User alias for TaskRunner
#define VMA_KERNEL 0xC0000000                      // Kernel

#define PAGE_PRIVILEGE(adr) ((adr >= VMA_USER && adr < VMA_KERNEL) ? PAGE_PRIVILEGE_USER : PAGE_PRIVILEGE_KERNEL)

/***************************************************************************/
// Recursive mapping constants
/***************************************************************************/

#define PD_RECURSIVE_SLOT 1023u         // PDE index used for self-map
#define PD_VA ((LINEAR)0xFFFFF000)      // Page Directory linear alias
#define PT_BASE_VA ((LINEAR)0xFFC00000) // Page Tables linear window

/***************************************************************************/
// Helper functions for page table navigation
/***************************************************************************/

static inline UINT GetDirectoryEntry(LINEAR Address) {
    return Address >> PAGE_TABLE_CAPACITY_MUL;
}

static inline UINT GetTableEntry(LINEAR Address) {
    return (Address & PAGE_TABLE_CAPACITY_MASK) >> PAGE_SIZE_MUL;
}

static inline LPPAGE_DIRECTORY GetCurrentPageDirectoryVA(void) {
    return (LPPAGE_DIRECTORY)PD_VA;
}

static inline LPPAGE_TABLE GetPageTableVAFor(LINEAR Address) {
    UINT dir = GetDirectoryEntry(Address);
    return (LPPAGE_TABLE)(PT_BASE_VA + (dir << PAGE_SIZE_MUL));
}

static inline volatile U32* GetPageTableEntryRawPointer(LINEAR Address) {
    UINT tab = GetTableEntry(Address);
    return (volatile U32*)&GetPageTableVAFor(Address)[tab];
}

static inline U32 MakePageTableEntryValue(
    PHYSICAL Physical,
    U32 ReadWrite,
    U32 Privilege,
    U32 WriteThrough,
    U32 CacheDisabled,
    U32 Global,
    U32 Fixed) {
    U32 val = 0;
    val |= 1u;  // Present

    if (ReadWrite) val |= (1u << 1);
    if (Privilege) val |= (1u << 2);  // 1=user, 0=kernel
    if (WriteThrough) val |= (1u << 3);
    if (CacheDisabled) val |= (1u << 4);

    // Accessed (bit 5) / Dirty (bit 6) left to CPU
    if (Global) val |= (1u << 8);
    if (Fixed) val |= (1u << 9);  // EXOS specific

    val |= (U32)(Physical & ~(PAGE_SIZE - 1));

    return val;
}

#endif  // ARCH_I386_MEMORY_I386_H_INCLUDED
