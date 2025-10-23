
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

#ifndef I386_MEMORY_H_INCLUDED
#define I386_MEMORY_H_INCLUDED

#include "Base.h"

/************************************************************************/
// #defines

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

#define VMA_RAM 0x00000000                         // Reserved for kernel
#define VMA_VIDEO 0x000A0000                       // Reserved for kernel
#define VMA_CONSOLE 0x000B8000                     // Reserved for kernel
#define VMA_USER 0x00400000                        // Start of user address space
#define VMA_LIBRARY 0xA0000000                     // Dynamic Libraries
#define VMA_TASK_RUNNER (VMA_LIBRARY - PAGE_SIZE)  // User alias for TaskRunner

#ifndef CONFIG_VMA_KERNEL
#error "CONFIG_VMA_KERNEL is not defined"
#endif

#if defined(__KERNEL__) && (CONFIG_VMA_KERNEL) > 0xFFFFFFFFu
#error "CONFIG_VMA_KERNEL does not fit in 32 bits"
#endif

#define VMA_KERNEL ((LINEAR)(CONFIG_VMA_KERNEL))   // Kernel

#define I386_TEMP_LINEAR_PAGE_1 (VMA_KERNEL + (LINEAR)0x00100000)
#define I386_TEMP_LINEAR_PAGE_2 (I386_TEMP_LINEAR_PAGE_1 + (LINEAR)0x00001000)
#define I386_TEMP_LINEAR_PAGE_3 (I386_TEMP_LINEAR_PAGE_2 + (LINEAR)0x00001000)

#define PAGE_PRIVILEGE(adr) ((adr >= VMA_USER && adr < VMA_KERNEL) ? PAGE_PRIVILEGE_USER : PAGE_PRIVILEGE_KERNEL)

#define PD_RECURSIVE_SLOT 1023u         // PDE index used for self-map
#define PD_VA ((LINEAR)0xFFFFF000)      // Page Directory linear alias
#define PT_BASE_VA ((LINEAR)0xFFC00000) // Page Tables linear window

#define PAGE_FLAG_PRESENT (1u << 0)
#define PAGE_FLAG_READ_WRITE (1u << 1)
#define PAGE_FLAG_USER (1u << 2)
#define PAGE_FLAG_WRITE_THROUGH (1u << 3)
#define PAGE_FLAG_CACHE_DISABLED (1u << 4)
#define PAGE_FLAG_ACCESSED (1u << 5)
#define PAGE_FLAG_DIRTY (1u << 6)
#define PAGE_FLAG_PAGE_SIZE (1u << 7)
#define PAGE_FLAG_GLOBAL (1u << 8)
#define PAGE_FLAG_FIXED (1u << 9)

/************************************************************************/
// typedefs

typedef struct tag_PAGE_DIRECTORY {
    U32 Present : 1;    // Is page present in RAM?
    U32 ReadWrite : 1;  // Read-write access rights
    U32 Privilege : 1;  // Privilege level
    U32 WriteThrough : 1;
    U32 CacheDisabled : 1;
    U32 Accessed : 1;  // Has page been accessed?
    U32 Reserved : 1;
    U32 PageSize : 1;  // 0 = 4KB
    U32 Global : 1;    // Ignored
    U32 User : 2;      // Available to OS
    U32 Fixed : 1;     // EXOS: Can page be swapped?
    U32 Address : 20;  // Physical address
} PAGE_DIRECTORY, *LPPAGE_DIRECTORY;

typedef struct tag_PAGE_TABLE {
    U32 Present : 1;    // Is page present in RAM?
    U32 ReadWrite : 1;  // Read-write access rights
    U32 Privilege : 1;  // Privilege level
    U32 WriteThrough : 1;
    U32 CacheDisabled : 1;
    U32 Accessed : 1;  // Has page been accessed?
    U32 Dirty : 1;     // Has been written to?
    U32 Reserved : 1;  // Reserved by Intel
    U32 Global : 1;
    U32 User : 2;      // Available to OS
    U32 Fixed : 1;     // EXOS: Can page be swapped?
    U32 Address : 20;  // Physical address
} PAGE_TABLE, *LPPAGE_TABLE;

typedef struct tag_ARCH_PAGE_ITERATOR {
    LINEAR Linear;
    UINT DirectoryIndex;
    UINT TableIndex;
} ARCH_PAGE_ITERATOR;

/************************************************************************/
// inlines

static inline LINEAR CanonicalizeLinearAddress(LINEAR Address) {
    return Address;
}

static inline UINT GetDirectoryEntry(LINEAR Address) {
    return Address >> PAGE_TABLE_CAPACITY_MUL;
}

static inline UINT GetTableEntry(LINEAR Address) {
    return (Address & PAGE_TABLE_CAPACITY_MASK) >> PAGE_SIZE_MUL;
}

static inline U64 ArchGetMaxLinearAddressPlusOne(void) {
    return U64_Make(1, 0x00000000u);
}

static inline U64 ArchGetMaxPhysicalAddressPlusOne(void) {
    return U64_Make(1, 0x00000000u);
}

static inline BOOL ArchClipPhysicalRange(U64 Base, U64 Length, PHYSICAL* OutBase, UINT* OutLength) {
    U64 Limit = ArchGetMaxPhysicalAddressPlusOne();

    if ((Length.HI == 0 && Length.LO == 0) || OutBase == NULL || OutLength == NULL) return FALSE;
    if (U64_Cmp(Base, Limit) >= 0) return FALSE;

    U64 End = U64_Add(Base, Length);
    if (U64_Cmp(End, Limit) > 0) End = Limit;

    U64 NewLength = U64_Sub(End, Base);

    *OutBase = Base.LO;
    if (NewLength.HI != 0) {
        *OutLength = MAX_U32 - Base.LO;
    } else {
        *OutLength = NewLength.LO;
    }

    return (*OutLength != 0);
}

static inline LPPAGE_DIRECTORY GetCurrentPageDirectoryVA(void) {
    return (LPPAGE_DIRECTORY)PD_VA;
}

static inline LPPAGE_TABLE GetPageTableVAFor(LINEAR Address) {
    UINT Directory = GetDirectoryEntry(Address);
    return (LPPAGE_TABLE)(PT_BASE_VA + (Directory << PAGE_SIZE_MUL));
}

static inline volatile U32* GetPageTableEntryRawPointer(LINEAR Address) {
    UINT Table = GetTableEntry(Address);
    return (volatile U32*)&GetPageTableVAFor(Address)[Table];
}

static inline U32 BuildPageFlags(
    U32 ReadWrite,
    U32 Privilege,
    U32 WriteThrough,
    U32 CacheDisabled,
    U32 Global,
    U32 Fixed) {
    U32 Flags = PAGE_FLAG_PRESENT;

    if (ReadWrite) Flags |= PAGE_FLAG_READ_WRITE;
    if (Privilege == PAGE_PRIVILEGE_USER) Flags |= PAGE_FLAG_USER;
    if (WriteThrough) Flags |= PAGE_FLAG_WRITE_THROUGH;
    if (CacheDisabled) Flags |= PAGE_FLAG_CACHE_DISABLED;
    if (Global) Flags |= PAGE_FLAG_GLOBAL;
    if (Fixed) Flags |= PAGE_FLAG_FIXED;

    return Flags;
}

static inline U32 MakePageDirectoryEntryValue(
    PHYSICAL Physical,
    U32 ReadWrite,
    U32 Privilege,
    U32 WriteThrough,
    U32 CacheDisabled,
    U32 Global,
    U32 Fixed) {
    U32 Flags = BuildPageFlags(ReadWrite, Privilege, WriteThrough, CacheDisabled, Global, Fixed);
    Flags &= ~PAGE_FLAG_PAGE_SIZE;  // PDE uses 4KB pages in EXOS
    return (U32)(Physical & ~(PAGE_SIZE - 1)) | Flags;
}

static inline U32 MakePageTableEntryValue(
    PHYSICAL Physical,
    U32 ReadWrite,
    U32 Privilege,
    U32 WriteThrough,
    U32 CacheDisabled,
    U32 Global,
    U32 Fixed) {
    U32 Flags = BuildPageFlags(ReadWrite, Privilege, WriteThrough, CacheDisabled, Global, Fixed);
    return (U32)(Physical & ~(PAGE_SIZE - 1)) | Flags;
}

static inline U32 MakePageEntryRaw(PHYSICAL Physical, U32 Flags) {
    return (U32)(Physical & ~(PAGE_SIZE - 1)) | (Flags & 0xFFFu);
}

static inline void WritePageDirectoryEntryValue(LPPAGE_DIRECTORY Directory, UINT Index, U32 Value) {
    ((volatile U32*)Directory)[Index] = Value;
}

static inline void WritePageTableEntryValue(LPPAGE_TABLE Table, UINT Index, U32 Value) {
    ((volatile U32*)Table)[Index] = Value;
}

static inline U32 ReadPageDirectoryEntryValue(const LPPAGE_DIRECTORY Directory, UINT Index) {
    return ((volatile const U32*)Directory)[Index];
}

static inline U32 ReadPageTableEntryValue(const LPPAGE_TABLE Table, UINT Index) {
    return ((volatile const U32*)Table)[Index];
}

static inline BOOL PageDirectoryEntryIsPresent(const LPPAGE_DIRECTORY Directory, UINT Index) {
    return (ReadPageDirectoryEntryValue(Directory, Index) & PAGE_FLAG_PRESENT) != 0;
}

static inline BOOL PageTableEntryIsPresent(const LPPAGE_TABLE Table, UINT Index) {
    return (ReadPageTableEntryValue(Table, Index) & PAGE_FLAG_PRESENT) != 0;
}

static inline PHYSICAL PageDirectoryEntryGetPhysical(const LPPAGE_DIRECTORY Directory, UINT Index) {
    return (PHYSICAL)(ReadPageDirectoryEntryValue(Directory, Index) & ~(PAGE_SIZE - 1));
}

static inline PHYSICAL PageTableEntryGetPhysical(const LPPAGE_TABLE Table, UINT Index) {
    return (PHYSICAL)(ReadPageTableEntryValue(Table, Index) & ~(PAGE_SIZE - 1));
}

static inline BOOL PageTableEntryIsFixed(const LPPAGE_TABLE Table, UINT Index) {
    return (ReadPageTableEntryValue(Table, Index) & PAGE_FLAG_FIXED) != 0;
}

static inline void ClearPageDirectoryEntry(LPPAGE_DIRECTORY Directory, UINT Index) {
    WritePageDirectoryEntryValue(Directory, Index, 0u);
}

static inline void ClearPageTableEntry(LPPAGE_TABLE Table, UINT Index) {
    WritePageTableEntryValue(Table, Index, 0u);
}

static inline ARCH_PAGE_ITERATOR MemoryPageIteratorFromLinear(LINEAR Linear) {
    ARCH_PAGE_ITERATOR Iterator;
    Iterator.Linear = Linear;
    Iterator.DirectoryIndex = GetDirectoryEntry(Linear);
    Iterator.TableIndex = GetTableEntry(Linear);
    return Iterator;
}

static inline LINEAR MemoryPageIteratorGetLinear(const ARCH_PAGE_ITERATOR* Iterator) {
    return Iterator->Linear;
}

static inline UINT MemoryPageIteratorGetDirectoryIndex(const ARCH_PAGE_ITERATOR* Iterator) {
    return Iterator->DirectoryIndex;
}

static inline UINT MemoryPageIteratorGetTableIndex(const ARCH_PAGE_ITERATOR* Iterator) {
    return Iterator->TableIndex;
}

static inline void MemoryPageIteratorStepPage(ARCH_PAGE_ITERATOR* Iterator) {
    Iterator->Linear += PAGE_SIZE;
    Iterator->TableIndex++;
    if (Iterator->TableIndex >= PAGE_TABLE_NUM_ENTRIES) {
        Iterator->TableIndex = 0;
        Iterator->DirectoryIndex = GetDirectoryEntry(Iterator->Linear);
    }
}

static inline LINEAR ArchAlignLinearToTableBoundary(LINEAR Linear) {
    return Linear & ~PAGE_TABLE_CAPACITY_MASK;
}

static inline void MemoryPageIteratorAlignToTableStart(ARCH_PAGE_ITERATOR* Iterator) {
    Iterator->Linear = ArchAlignLinearToTableBoundary(Iterator->Linear);
    Iterator->DirectoryIndex = GetDirectoryEntry(Iterator->Linear);
    Iterator->TableIndex = 0;
}

static inline void MemoryPageIteratorNextTable(ARCH_PAGE_ITERATOR* Iterator) {
    Iterator->Linear = ArchAlignLinearToTableBoundary(Iterator->Linear) + PAGE_TABLE_CAPACITY;
    Iterator->DirectoryIndex = GetDirectoryEntry(Iterator->Linear);
    Iterator->TableIndex = 0;
}

static inline BOOL MemoryPageIteratorIsAtTableStart(const ARCH_PAGE_ITERATOR* Iterator) {
    return Iterator->TableIndex == 0;
}

static inline LPPAGE_TABLE MemoryPageIteratorGetTable(const ARCH_PAGE_ITERATOR* Iterator) {
    return GetPageTableVAFor(Iterator->Linear);
}

static inline BOOL ArchPageTableIsEmpty(const LPPAGE_TABLE Table) {
    for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        if (PageTableEntryIsPresent(Table, Index)) return FALSE;
    }
    return TRUE;
}

#endif  // I386_MEMORY_H_INCLUDED
