/************************************************************************\\

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


    x86-64 IsValidMemory implementation

\************************************************************************/

#include "Memory.h"

/************************************************************************/
/**
 * @brief Check if a linear address is mapped and accessible.
 * @param Pointer Linear address to test.
 * @return TRUE if address is valid.
 */
BOOL IsValidMemory(LINEAR Pointer) {
    U64 Address = (U64)Pointer;

    if (ArchCanonicalizeAddress(Address) != Address) return FALSE;

    LPPML4 Pml4 = GetCurrentPml4VA();
    UINT Pml4Index = GetPml4Entry(Address);
    if (Pml4Index >= PML4_ENTRY_COUNT) return FALSE;
    if (!PageDirectoryEntryIsPresent((LPPAGE_DIRECTORY)Pml4, Pml4Index)) return FALSE;

    LPPDPT Pdpt = GetPageDirectoryPointerTableVAFor(Address);
    UINT PdptIndex = GetPdptEntry(Address);
    if (PdptIndex >= PDPT_ENTRY_COUNT) return FALSE;
    if (!PageDirectoryEntryIsPresent((LPPAGE_DIRECTORY)Pdpt, PdptIndex)) return FALSE;

    LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(Address);
    UINT DirectoryIndex = GetDirectoryEntry(Address);
    if (DirectoryIndex >= PAGE_DIRECTORY_ENTRY_COUNT) return FALSE;
    if (!PageDirectoryEntryIsPresent(Directory, DirectoryIndex)) return FALSE;

    LPPAGE_TABLE Table = GetPageTableVAFor(Address);
    UINT TableIndex = GetTableEntry(Address);
    if (TableIndex >= PAGE_TABLE_NUM_ENTRIES) return FALSE;
    if (!PageTableEntryIsPresent(Table, TableIndex)) return FALSE;

    return TRUE;
}

/************************************************************************/
