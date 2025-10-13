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
#ifndef ARCH_X86_64_X86_64_MEMORY_H_INCLUDED
#define ARCH_X86_64_X86_64_MEMORY_H_INCLUDED

#include "Base.h"

/*************************************************************************/
// #defines
/*************************************************************************/
#define PAGE_SIZE N_4KB
#define PAGE_SIZE_MUL MUL_4KB
#define PAGE_SIZE_MASK ((U64)PAGE_SIZE - 1ull)

#define PAGE_TABLE_ENTRY_SIZE ((UINT)sizeof(U64))
#define PAGE_TABLE_NUM_ENTRIES 512u
#define PAGE_TABLE_SIZE (PAGE_TABLE_NUM_ENTRIES * PAGE_TABLE_ENTRY_SIZE)
#define PAGE_TABLE_SIZE_MUL MUL_4KB
#define PAGE_TABLE_CAPACITY (PAGE_TABLE_NUM_ENTRIES * PAGE_SIZE)
#define PAGE_TABLE_CAPACITY_MUL MUL_2MB
#define PAGE_TABLE_CAPACITY_MASK ((U64)PAGE_TABLE_CAPACITY - 1ull)

#define PAGE_MASK (~((U64)PAGE_SIZE - 1ull))

#define PAGE_PRIVILEGE_KERNEL 0u
#define PAGE_PRIVILEGE_USER 1u

#define PAGE_FLAG_PRESENT (1ull << 0)
#define PAGE_FLAG_READ_WRITE (1ull << 1)
#define PAGE_FLAG_USER (1ull << 2)
#define PAGE_FLAG_WRITE_THROUGH (1ull << 3)
#define PAGE_FLAG_CACHE_DISABLED (1ull << 4)
#define PAGE_FLAG_ACCESSED (1ull << 5)
#define PAGE_FLAG_DIRTY (1ull << 6)
#define PAGE_FLAG_PAGE_SIZE (1ull << 7)
#define PAGE_FLAG_GLOBAL (1ull << 8)
#define PAGE_FLAG_FIXED (1ull << 9)
#define PAGE_FLAG_NO_EXECUTE (1ull << 63)

#define PML4_ENTRY_COUNT 512u
#define PDPT_ENTRY_COUNT 512u
#define PAGE_DIRECTORY_ENTRY_COUNT 512u
#define PML4_RECURSIVE_SLOT 510u
#define PD_RECURSIVE_SLOT PML4_RECURSIVE_SLOT

#define VMA_RAM 0x0000000000000000ull
#define VMA_VIDEO 0x00000000000A0000ull
#define VMA_CONSOLE 0x00000000000B8000ull
#define VMA_USER 0x0000000000400000ull
#define VMA_LIBRARY 0x00007F0000000000ull
#define VMA_TASK_RUNNER (VMA_LIBRARY - PAGE_SIZE)
#define VMA_KERNEL 0xFFFFFFFF80000000ull

#define X86_64_TEMP_LINEAR_PAGE_1 0xFFFFFFFF80100000ull
#define X86_64_TEMP_LINEAR_PAGE_2 0xFFFFFFFF80101000ull
#define X86_64_TEMP_LINEAR_PAGE_3 0xFFFFFFFF80102000ull

#define PAGE_PRIVILEGE(Address) \
    (((U64)(Address) >= VMA_USER && (U64)(Address) < VMA_KERNEL) ? PAGE_PRIVILEGE_USER : PAGE_PRIVILEGE_KERNEL)

#define PAGE_ALIGN(Address) (((U64)(Address) + PAGE_SIZE - 1ull) & PAGE_MASK)
/*************************************************************************/
// typedefs
/*************************************************************************/
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

typedef struct tag_ARCH_PAGE_ITERATOR {
    U64 Linear;
    UINT Pml4Index;
    UINT PdptIndex;
    UINT DirectoryIndex;
    UINT TableIndex;
} ARCH_PAGE_ITERATOR;
/*************************************************************************/
// inlines
/*************************************************************************/
static inline U64 ArchCanonicalizeAddress(U64 Address) {
    const U64 SignBit = 1ull << 47;
    const U64 Mask = (1ull << 48) - 1ull;

    Address &= Mask;
    if ((Address & SignBit) != 0) {
        Address |= 0xFFFF000000000000ull;
    }

    return Address;
}

static inline U64 ArchBuildRecursiveAddress(UINT Pml4, UINT Pdpt, UINT Directory, UINT Table, U64 Offset) {
    U64 Address = ((U64)Pml4 << 39) | ((U64)Pdpt << 30) | ((U64)Directory << 21) | ((U64)Table << 12) |
        (Offset & PAGE_SIZE_MASK);
    return ArchCanonicalizeAddress(Address);
}

static inline UINT GetPml4Entry(U64 Address) {
    return (UINT)((Address >> 39) & 0x1FFu);
}

static inline UINT GetPdptEntry(U64 Address) {
    return (UINT)((Address >> 30) & 0x1FFu);
}

static inline UINT GetDirectoryEntry(U64 Address) {
    return (UINT)((Address >> 21) & 0x1FFu);
}

static inline UINT GetTableEntry(U64 Address) {
    return (UINT)((Address >> 12) & 0x1FFu);
}

static inline U64 BuildPageFlags(
    U32 ReadWrite,
    U32 Privilege,
    U32 WriteThrough,
    U32 CacheDisabled,
    U32 Global,
    U32 Fixed) {
    U64 Flags = PAGE_FLAG_PRESENT;

    if (ReadWrite) Flags |= PAGE_FLAG_READ_WRITE;
    if (Privilege == PAGE_PRIVILEGE_USER) Flags |= PAGE_FLAG_USER;
    if (WriteThrough) Flags |= PAGE_FLAG_WRITE_THROUGH;
    if (CacheDisabled) Flags |= PAGE_FLAG_CACHE_DISABLED;
    if (Global) Flags |= PAGE_FLAG_GLOBAL;
    if (Fixed) Flags |= PAGE_FLAG_FIXED;

    return Flags;
}

static inline U64 MakePageDirectoryEntryValue(
    PHYSICAL Physical,
    U32 ReadWrite,
    U32 Privilege,
    U32 WriteThrough,
    U32 CacheDisabled,
    U32 Global,
    U32 Fixed) {
    U64 Flags = BuildPageFlags(ReadWrite, Privilege, WriteThrough, CacheDisabled, Global, Fixed);
    Flags &= ~PAGE_FLAG_PAGE_SIZE;
    return ((U64)Physical & PAGE_MASK) | Flags;
}

static inline U64 MakePageTableEntryValue(
    PHYSICAL Physical,
    U32 ReadWrite,
    U32 Privilege,
    U32 WriteThrough,
    U32 CacheDisabled,
    U32 Global,
    U32 Fixed) {
    U64 Flags = BuildPageFlags(ReadWrite, Privilege, WriteThrough, CacheDisabled, Global, Fixed);
    return ((U64)Physical & PAGE_MASK) | Flags;
}

static inline U64 MakePageEntryRaw(PHYSICAL Physical, U64 Flags) {
    return ((U64)Physical & PAGE_MASK) | (Flags & 0xFFFu);
}

static inline void WritePageDirectoryEntryValue(LPPAGE_DIRECTORY Directory, UINT Index, U64 Value) {
    ((volatile U64*)Directory)[Index] = Value;
}

static inline void WritePageTableEntryValue(LPPAGE_TABLE Table, UINT Index, U64 Value) {
    ((volatile U64*)Table)[Index] = Value;
}

static inline U64 ReadPageDirectoryEntryValue(const LPPAGE_DIRECTORY Directory, UINT Index) {
    return ((volatile const U64*)Directory)[Index];
}

static inline U64 ReadPageTableEntryValue(const LPPAGE_TABLE Table, UINT Index) {
    return ((volatile const U64*)Table)[Index];
}

static inline BOOL PageDirectoryEntryIsPresent(const LPPAGE_DIRECTORY Directory, UINT Index) {
    return (ReadPageDirectoryEntryValue(Directory, Index) & PAGE_FLAG_PRESENT) != 0;
}

static inline BOOL PageTableEntryIsPresent(const LPPAGE_TABLE Table, UINT Index) {
    return (ReadPageTableEntryValue(Table, Index) & PAGE_FLAG_PRESENT) != 0;
}

static inline PHYSICAL PageDirectoryEntryGetPhysical(const LPPAGE_DIRECTORY Directory, UINT Index) {
    return (PHYSICAL)(ReadPageDirectoryEntryValue(Directory, Index) & PAGE_MASK);
}

static inline PHYSICAL PageTableEntryGetPhysical(const LPPAGE_TABLE Table, UINT Index) {
    return (PHYSICAL)(ReadPageTableEntryValue(Table, Index) & PAGE_MASK);
}

static inline BOOL PageTableEntryIsFixed(const LPPAGE_TABLE Table, UINT Index) {
    return (ReadPageTableEntryValue(Table, Index) & PAGE_FLAG_FIXED) != 0;
}

static inline void ClearPageDirectoryEntry(LPPAGE_DIRECTORY Directory, UINT Index) {
    WritePageDirectoryEntryValue(Directory, Index, 0ull);
}

static inline void ClearPageTableEntry(LPPAGE_TABLE Table, UINT Index) {
    WritePageTableEntryValue(Table, Index, 0ull);
}

static inline U64 ArchGetMaxLinearAddressPlusOne(void) {
    return 1ull << 48;
}

static inline U64 ArchGetMaxPhysicalAddressPlusOne(void) {
    return 1ull << 52;
}

static inline BOOL ArchClipPhysicalRange(U64 Base, U64 Length, PHYSICAL* OutBase, UINT* OutLength) {
    U64 Limit = ArchGetMaxPhysicalAddressPlusOne();

    if (Length == 0 || OutBase == NULL || OutLength == NULL) return FALSE;
    if (Base >= Limit) return FALSE;

    U64 End = Base + Length;
    if (End > Limit) End = Limit;

    U64 NewLength = End - Base;
    if (NewLength == 0) return FALSE;

    *OutBase = (PHYSICAL)Base;
    *OutLength = (UINT)NewLength;
    return TRUE;
}

static inline LPPML4 GetCurrentPml4VA(void) {
    return (LPPML4)ArchBuildRecursiveAddress(PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, 0);
}

static inline LPPAGE_DIRECTORY GetCurrentPageDirectoryVA(void) {
    return (LPPAGE_DIRECTORY)ArchBuildRecursiveAddress(PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, 0u, 0u, 0);
}

static inline LPPDPT GetPageDirectoryPointerTableVAFor(U64 Address) {
    return (LPPDPT)ArchBuildRecursiveAddress(PML4_RECURSIVE_SLOT, GetPml4Entry(Address), 0u, 0u, 0);
}

static inline LPPAGE_DIRECTORY GetPageDirectoryVAFor(U64 Address) {
    return (LPPAGE_DIRECTORY)ArchBuildRecursiveAddress(
        PML4_RECURSIVE_SLOT, GetPml4Entry(Address), GetPdptEntry(Address), 0u, 0);
}

static inline LPPAGE_TABLE GetPageTableVAFor(U64 Address) {
    return (LPPAGE_TABLE)ArchBuildRecursiveAddress(
        PML4_RECURSIVE_SLOT, GetPml4Entry(Address), GetPdptEntry(Address), GetDirectoryEntry(Address), 0);
}

static inline volatile U64* GetPageTableEntryRawPointer(U64 Address) {
    return &((volatile U64*)GetPageTableVAFor(Address))[GetTableEntry(Address)];
}

static inline ARCH_PAGE_ITERATOR MemoryPageIteratorFromLinear(U64 Linear) {
    ARCH_PAGE_ITERATOR Iterator;
    Iterator.Linear = Linear;
    Iterator.Pml4Index = GetPml4Entry(Linear);
    Iterator.PdptIndex = GetPdptEntry(Linear);
    Iterator.DirectoryIndex = GetDirectoryEntry(Linear);
    Iterator.TableIndex = GetTableEntry(Linear);
    return Iterator;
}

static inline U64 MemoryPageIteratorGetLinear(const ARCH_PAGE_ITERATOR* Iterator) {
    return Iterator->Linear;
}

static inline UINT MemoryPageIteratorGetPml4Index(const ARCH_PAGE_ITERATOR* Iterator) {
    return Iterator->Pml4Index;
}

static inline UINT MemoryPageIteratorGetPdptIndex(const ARCH_PAGE_ITERATOR* Iterator) {
    return Iterator->PdptIndex;
}

static inline UINT MemoryPageIteratorGetDirectoryIndex(const ARCH_PAGE_ITERATOR* Iterator) {
    return Iterator->DirectoryIndex;
}

static inline UINT MemoryPageIteratorGetTableIndex(const ARCH_PAGE_ITERATOR* Iterator) {
    return Iterator->TableIndex;
}

static inline void MemoryPageIteratorStepPage(ARCH_PAGE_ITERATOR* Iterator) {
    Iterator->Linear += PAGE_SIZE;
    Iterator->Pml4Index = GetPml4Entry(Iterator->Linear);
    Iterator->PdptIndex = GetPdptEntry(Iterator->Linear);
    Iterator->DirectoryIndex = GetDirectoryEntry(Iterator->Linear);
    Iterator->TableIndex = GetTableEntry(Iterator->Linear);
}

static inline U64 ArchAlignLinearToTableBoundary(U64 Linear) {
    return Linear & ~PAGE_TABLE_CAPACITY_MASK;
}

static inline void MemoryPageIteratorAlignToTableStart(ARCH_PAGE_ITERATOR* Iterator) {
    Iterator->Linear = ArchAlignLinearToTableBoundary(Iterator->Linear);
    Iterator->Pml4Index = GetPml4Entry(Iterator->Linear);
    Iterator->PdptIndex = GetPdptEntry(Iterator->Linear);
    Iterator->DirectoryIndex = GetDirectoryEntry(Iterator->Linear);
    Iterator->TableIndex = 0u;
}

static inline void MemoryPageIteratorNextTable(ARCH_PAGE_ITERATOR* Iterator) {
    Iterator->Linear = ArchAlignLinearToTableBoundary(Iterator->Linear) + PAGE_TABLE_CAPACITY;
    Iterator->Pml4Index = GetPml4Entry(Iterator->Linear);
    Iterator->PdptIndex = GetPdptEntry(Iterator->Linear);
    Iterator->DirectoryIndex = GetDirectoryEntry(Iterator->Linear);
    Iterator->TableIndex = 0u;
}

static inline BOOL MemoryPageIteratorIsAtTableStart(const ARCH_PAGE_ITERATOR* Iterator) {
    return Iterator->TableIndex == 0u;
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
/*************************************************************************/
// external symbols
/*************************************************************************/
// None.

#endif  // ARCH_X86_64_X86_64_MEMORY_H_INCLUDED
