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

static LINEAR MapTemporaryPhysicalPage(LINEAR TargetLinear, PHYSICAL Physical) {
    if (!IsInLowWindow(Physical)) {
        ERROR(TEXT("[MapTemporaryPhysicalPage] Physical %p outside low window"), (LPVOID)Physical);
        return 0;
    }

    UINT Pml4Index = (UINT)((TargetLinear >> 39) & 0x1FFu);
    UINT PdptIndex = (UINT)((TargetLinear >> 30) & 0x1FFu);
    UINT DirectoryIndex = (UINT)((TargetLinear >> 21) & 0x1FFu);
    UINT TableIndex = (UINT)((TargetLinear >> 12) & 0x1FFu);

    LPPML4 Pml4 = (LPPML4)BuildRecursiveAddress(PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT);
    LPPDPT Pdpt = (LPPDPT)BuildRecursiveAddress(PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PdptIndex);
    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)BuildRecursiveAddress(PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, DirectoryIndex, 0);
    LPPAGE_TABLE Table = (LPPAGE_TABLE)BuildRecursiveAddress(PML4_RECURSIVE_SLOT, PdptIndex, DirectoryIndex, 0);

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
