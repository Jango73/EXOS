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


    x86-64 memory helpers

\************************************************************************/

#include "arch/x86-64/x86-64-Memory.h"

#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "System.h"

/************************************************************************/

/**
 * @brief Compute the size of the 4 KiB page window reserved in low memory.
 * @param TotalMemoryBytes Total detected physical memory size in bytes.
 * @return Window size rounded up to 2 MiB and clamped to total memory.
 */
PHYSICAL ComputeLowMemoryWindowLimit(UINT TotalMemoryBytes) {
    if (TotalMemoryBytes == 0) {
        return 0;
    }

    UINT OnePercent = TotalMemoryBytes / 100u;
    if ((TotalMemoryBytes % 100u) != 0u) {
        OnePercent++;
    }

    UINT Window = (OnePercent + (PAGE_2M_SIZE - 1u)) & ~(PAGE_2M_MASK);

    if (Window > TotalMemoryBytes) {
        Window = TotalMemoryBytes;
    }

    return (PHYSICAL)Window;
}

/************************************************************************/

/**
 * @brief Convenience wrapper to compute the low window limit from startup info.
 * @return Window size rounded up to 2 MiB and clamped to total memory.
 */
PHYSICAL GetLowMemoryWindowLimit(void) {
    return ComputeLowMemoryWindowLimit(KernelStartup.MemorySize);
}

/************************************************************************/

static LINEAR G_TempLinear1 = TEMP_LINEAR_PAGE_1;
static LINEAR G_TempLinear2 = TEMP_LINEAR_PAGE_2;
static LINEAR G_TempLinear3 = TEMP_LINEAR_PAGE_3;

/************************************************************************/

static inline void SetPageFlags(LPX86_64_PAGE_TABLE_ENTRY Entry, U64 Flags, PHYSICAL Physical) {
    Entry->Present = (Flags & PAGE_FLAG_PRESENT) ? 1u : 0u;
    Entry->ReadWrite = (Flags & PAGE_FLAG_READ_WRITE) ? 1u : 0u;
    Entry->Privilege = (Flags & PAGE_FLAG_USER) ? 1u : 0u;
    Entry->WriteThrough = (Flags & PAGE_FLAG_WRITE_THROUGH) ? 1u : 0u;
    Entry->CacheDisabled = (Flags & PAGE_FLAG_CACHE_DISABLED) ? 1u : 0u;
    Entry->Accessed = 0u;
    Entry->Dirty = 0u;
    Entry->PageSize = 0u;
    Entry->Global = (Flags & PAGE_FLAG_GLOBAL) ? 1u : 0u;
    Entry->Address = (U64)Physical >> PAGE_SIZE_MUL;
    Entry->Available = 0u;
    Entry->AvailableHigh = 0u;
    Entry->NoExecute = (Flags & PAGE_FLAG_NO_EXECUTE) ? 1u : 0u;
}

/************************************************************************/

static inline void InvalidatePage(LINEAR Linear) {
    __asm__ volatile("invlpg (%0)" ::"r"(Linear) : "memory");
}

/************************************************************************/

static inline BOOL IsInLowWindow(PHYSICAL Physical) {
    return (Physical < GetCachedLowMemoryWindowLimit());
}

/************************************************************************/
// Paging walkers

static inline UINT GetPml4Index(LINEAR Linear) { return (UINT)((Linear >> 39) & 0x1FFu); }
static inline UINT GetPdptIndex(LINEAR Linear) { return (UINT)((Linear >> 30) & 0x1FFu); }
static inline UINT GetDirectoryIndex(LINEAR Linear) { return (UINT)((Linear >> 21) & 0x1FFu); }
static inline UINT GetTableIndex(LINEAR Linear) { return (UINT)((Linear >> 12) & 0x1FFu); }

static inline LPPML4 GetCurrentPml4VA(void) {
    return (LPPML4)BuildRecursiveAddress(PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT);
}

static inline LPPDPT GetPageDirectoryPointerVAFor(UINT Pml4Index, UINT PdptIndex) {
    return (LPPDPT)BuildRecursiveAddress(PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PdptIndex);
}

static inline LPPAGE_DIRECTORY GetPageDirectoryVAFor(UINT PdptIndex, UINT DirectoryIndex) {
    return (LPPAGE_DIRECTORY)BuildRecursiveAddress(PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, DirectoryIndex, 0);
}

static inline LPPAGE_TABLE GetPageTableVAFor(UINT PdptIndex, UINT DirectoryIndex) {
    return (LPPAGE_TABLE)BuildRecursiveAddress(PML4_RECURSIVE_SLOT, PdptIndex, DirectoryIndex, 0);
}

/************************************************************************/

static LINEAR MapTemporaryPhysicalPage(LINEAR TargetLinear, PHYSICAL Physical) {
    if (!IsInLowWindow(Physical)) {
        ERROR(TEXT("[MapTemporaryPhysicalPage] Physical %p outside low window"), (LPVOID)Physical);
        return 0;
    }

    UINT Pml4Index = GetPml4Index(TargetLinear);
    UINT PdptIndex = GetPdptIndex(TargetLinear);
    UINT DirectoryIndex = GetDirectoryIndex(TargetLinear);
    UINT TableIndex = GetTableIndex(TargetLinear);

    LPPML4 Pml4 = GetCurrentPml4VA();
    LPPDPT Pdpt = GetPageDirectoryPointerVAFor(Pml4Index, PdptIndex);
    LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(PdptIndex, DirectoryIndex);
    LPPAGE_TABLE Table = GetPageTableVAFor(PdptIndex, DirectoryIndex);

    X86_64_PML4_ENTRY* Pml4Entry = &Pml4[Pml4Index];
    X86_64_PDPT_ENTRY* PdptEntry = &Pdpt[PdptIndex];
    X86_64_PAGE_DIRECTORY_ENTRY* DirEntry = &Directory[DirectoryIndex];
    X86_64_PAGE_TABLE_ENTRY* TabEntry = &Table[TableIndex];

    if (Pml4Entry->Present == 0 || PdptEntry->Present == 0 || DirEntry->Present == 0) {
        ERROR(TEXT("[MapTemporaryPhysicalPage] Missing paging structure for VA %p"), (LPVOID)TargetLinear);
        return 0;
    }

    U64 Flags = PAGE_FLAG_PRESENT | PAGE_FLAG_READ_WRITE | PAGE_FLAG_GLOBAL;
    SetPageFlags(TabEntry, Flags, Physical);
    InvalidatePage(TargetLinear);

    return TargetLinear;
}

/************************************************************************/

LINEAR MapTemporaryPhysicalPage1(PHYSICAL Physical) {
    return MapTemporaryPhysicalPage(G_TempLinear1, Physical);
}

/************************************************************************/

LINEAR MapTemporaryPhysicalPage2(PHYSICAL Physical) {
    return MapTemporaryPhysicalPage(G_TempLinear2, Physical);
}

/************************************************************************/

LINEAR MapTemporaryPhysicalPage3(PHYSICAL Physical) {
    return MapTemporaryPhysicalPage(G_TempLinear3, Physical);
}

/************************************************************************/

/**
 * @brief Map a linear address to a physical address in the current CR3.
 * @param Linear Linear address to resolve.
 * @return Physical address or 0 on failure.
 */
PHYSICAL MapLinearToPhysical(LINEAR Linear) {
    UINT Pml4Index = GetPml4Index(Linear);
    UINT PdptIndex = GetPdptIndex(Linear);
    UINT DirectoryIndex = GetDirectoryIndex(Linear);
    UINT TableIndex = GetTableIndex(Linear);

    LPPML4 Pml4 = GetCurrentPml4VA();
    const X86_64_PML4_ENTRY* Pml4Entry = &Pml4[Pml4Index];
    if (!Pml4Entry->Present) return 0;

    LPPDPT Pdpt = GetPageDirectoryPointerVAFor(Pml4Index, PdptIndex);
    const X86_64_PDPT_ENTRY* PdptEntry = &Pdpt[PdptIndex];
    if (!PdptEntry->Present) return 0;

    if (PdptEntry->PageSize) {
        PHYSICAL Base = (PHYSICAL)(PdptEntry->Address << 12);
        PHYSICAL Offset = (PHYSICAL)(Linear & (((PHYSICAL)1u << 30) - 1u));
        return Base + Offset;
    }

    LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(PdptIndex, DirectoryIndex);
    const X86_64_PAGE_DIRECTORY_ENTRY* DirEntry = &Directory[DirectoryIndex];
    if (!DirEntry->Present) return 0;

    if (DirEntry->PageSize) {
        PHYSICAL Base = (PHYSICAL)(DirEntry->Address << 12);
        PHYSICAL Offset = (PHYSICAL)(Linear & PAGE_2M_MASK);
        return Base + Offset;
    }

    LPPAGE_TABLE Table = GetPageTableVAFor(PdptIndex, DirectoryIndex);
    const X86_64_PAGE_TABLE_ENTRY* TabEntry = &Table[TableIndex];
    if (!TabEntry->Present) return 0;

    PHYSICAL Base = (PHYSICAL)(TabEntry->Address << PAGE_SIZE_MUL);
    PHYSICAL Offset = (PHYSICAL)(Linear & PAGE_SIZE_MASK);
    return Base + Offset;
}

/************************************************************************/

/**
 * @brief Check if a linear address is mapped and accessible.
 * @param Pointer Linear address to test.
 * @return TRUE if address is valid.
 */
BOOL IsValidMemory(LINEAR Pointer) {
    return MapLinearToPhysical(Pointer) != 0;
}

/************************************************************************/

/**
 * @brief Map or remap a single page (4K) in the low window.
 * @param Linear Target linear address.
 * @param Physical Physical address to map.
 * @param Flags Page flags.
 * @return TRUE on success.
 */
static BOOL MapOnePage(LINEAR Linear, PHYSICAL Physical, U64 Flags) {
    UINT Pml4Index = GetPml4Index(Linear);
    UINT PdptIndex = GetPdptIndex(Linear);
    UINT DirectoryIndex = GetDirectoryIndex(Linear);
    UINT TableIndex = GetTableIndex(Linear);

    LPPML4 Pml4 = GetCurrentPml4VA();
    LPPDPT Pdpt = GetPageDirectoryPointerVAFor(Pml4Index, PdptIndex);
    LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(PdptIndex, DirectoryIndex);
    LPPAGE_TABLE Table = GetPageTableVAFor(PdptIndex, DirectoryIndex);

    if (!Pml4[Pml4Index].Present || !Pdpt[PdptIndex].Present || !Directory[DirectoryIndex].Present) {
        ERROR(TEXT("[MapOnePage] Missing paging structures for VA %p"), (LPVOID)Linear);
        return FALSE;
    }

    SetPageFlags(&Table[TableIndex], Flags, Physical);
    InvalidatePage(Linear);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Unmap a 4K page.
 * @param Linear Linear address.
 */
static void UnmapOnePage(LINEAR Linear) {
    UINT Pml4Index = GetPml4Index(Linear);
    UINT PdptIndex = GetPdptIndex(Linear);
    UINT DirectoryIndex = GetDirectoryIndex(Linear);
    UINT TableIndex = GetTableIndex(Linear);

    LPPAGE_TABLE Table = GetPageTableVAFor(PdptIndex, DirectoryIndex);
    Table[TableIndex].Present = 0;
    InvalidatePage(Linear);
}

/************************************************************************/
