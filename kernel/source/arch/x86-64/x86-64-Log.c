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


    x86-64 logging helpers implementation

\************************************************************************/

#include "arch/x86-64/x86-64-Log.h"

#include "Log.h"
#include "Memory.h"
#include "Text.h"
#include "arch/x86-64/x86-64-Memory.h"

/***************************************************************************/

static U64 BuildLinearAddress(UINT Pml4Index, UINT PdptIndex, UINT DirectoryIndex, UINT TableIndex, U64 Offset) {
    U64 Address = ((U64)Pml4Index << 39)
        | ((U64)PdptIndex << 30)
        | ((U64)DirectoryIndex << 21)
        | ((U64)TableIndex << 12)
        | (Offset & PAGE_SIZE_MASK);
    return ArchCanonicalizeAddress(Address);
}

/***************************************************************************/

static U64 BuildRangeEnd(U64 Base, U64 Span) {
    return ArchCanonicalizeAddress(Base + (Span - 1));
}

/***************************************************************************/

/**
 * @brief Logs the complete hierarchical paging structures for x86-64.
 * @param Pml4Physical Physical address of the PML4 to inspect.
 */
void LogPageDirectory64(PHYSICAL Pml4Physical) {
    LINEAR Pml4Linear = MapTempPhysicalPage(Pml4Physical);

    if (Pml4Linear == 0) {
        ERROR(TEXT("[LogPageDirectory64] MapTempPhysicalPage failed for PML4 %p"), (LPVOID)Pml4Physical);
        return;
    }

    LPPML4 Pml4 = (LPPML4)Pml4Linear;

    DEBUG(TEXT("[LogPageDirectory64] PML4 PA=%p contents:"), (LPVOID)Pml4Physical);

    for (UINT Pml4Index = 0; Pml4Index < PML4_ENTRY_COUNT; Pml4Index++) {
        const X86_64_PML4_ENTRY *Pml4Entry = &Pml4[Pml4Index];

        if (!Pml4Entry->Present) continue;

        U64 LinearBase = BuildLinearAddress(Pml4Index, 0, 0, 0, 0);
        U64 LinearEnd = BuildRangeEnd(LinearBase, 1ull << 39);
        PHYSICAL PdptPhysical = (PHYSICAL)(Pml4Entry->Address << 12);

        DEBUG(TEXT("[LogPageDirectory64] PML4E[%u]: VA=%p-%p -> PDPT_PA=%p Present=%u RW=%u Priv=%u NX=%u"),
            Pml4Index,
            (LPVOID)LinearBase,
            (LPVOID)LinearEnd,
            (LPVOID)PdptPhysical,
            (U32)Pml4Entry->Present,
            (U32)Pml4Entry->ReadWrite,
            (U32)Pml4Entry->Privilege,
            (U32)Pml4Entry->NoExecute);

        LINEAR PdptLinear = MapTempPhysicalPage2(PdptPhysical);

        if (PdptLinear == 0) {
            ERROR(TEXT("[LogPageDirectory64] MapTempPhysicalPage2 failed for PDPT %p"),
                (LPVOID)PdptPhysical);
            continue;
        }

        LPPDPT Pdpt = (LPPDPT)PdptLinear;

        for (UINT PdptIndex = 0; PdptIndex < PDPT_ENTRY_COUNT; PdptIndex++) {
            const X86_64_PDPT_ENTRY *PdptEntry = &Pdpt[PdptIndex];

            if (!PdptEntry->Present) continue;

            U64 PdptBase = BuildLinearAddress(Pml4Index, PdptIndex, 0, 0, 0);
            U64 PdptEnd = BuildRangeEnd(PdptBase, 1ull << 30);

            if (PdptEntry->PageSize) {
                PHYSICAL HugePhysical = (PHYSICAL)(PdptEntry->Address << 12);

                DEBUG(TEXT("[LogPageDirectory64]   PDPTE[%u]: VA=%p-%p -> 1GB page PA=%p Present=%u RW=%u Priv=%u NX=%u"),
                    PdptIndex,
                    (LPVOID)PdptBase,
                    (LPVOID)PdptEnd,
                    (LPVOID)HugePhysical,
                    (U32)PdptEntry->Present,
                    (U32)PdptEntry->ReadWrite,
                    (U32)PdptEntry->Privilege,
                    (U32)PdptEntry->NoExecute);
                continue;
            }

            PHYSICAL PageDirectoryPhysical = (PHYSICAL)(PdptEntry->Address << 12);

            DEBUG(TEXT("[LogPageDirectory64]   PDPTE[%u]: VA=%p-%p -> PD_PA=%p Present=%u RW=%u Priv=%u NX=%u"),
                PdptIndex,
                (LPVOID)PdptBase,
                (LPVOID)PdptEnd,
                (LPVOID)PageDirectoryPhysical,
                (U32)PdptEntry->Present,
                (U32)PdptEntry->ReadWrite,
                (U32)PdptEntry->Privilege,
                (U32)PdptEntry->NoExecute);

            LINEAR DirectoryLinear = MapTempPhysicalPage3(PageDirectoryPhysical);

            if (DirectoryLinear == 0) {
                ERROR(TEXT("[LogPageDirectory64] MapTempPhysicalPage3 failed for directory %p"),
                    (LPVOID)PageDirectoryPhysical);
                continue;
            }

            LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)DirectoryLinear;

            for (UINT DirectoryIndex = 0; DirectoryIndex < PAGE_DIRECTORY_ENTRY_COUNT; DirectoryIndex++) {
                const X86_64_PAGE_DIRECTORY_ENTRY *DirectoryEntry = &Directory[DirectoryIndex];

                if (!DirectoryEntry->Present) continue;

                U64 DirectoryBase = BuildLinearAddress(Pml4Index, PdptIndex, DirectoryIndex, 0, 0);
                U64 DirectoryEnd = BuildRangeEnd(DirectoryBase, (U64)1 << 21);

                if (DirectoryEntry->PageSize) {
                    PHYSICAL LargePhysical = (PHYSICAL)(DirectoryEntry->Address << 12);

                    DEBUG(TEXT("[LogPageDirectory64]     PDE[%u]: VA=%p-%p -> 2MB page PA=%p Present=%u RW=%u Priv=%u Global=%u NX=%u"),
                        DirectoryIndex,
                        (LPVOID)DirectoryBase,
                        (LPVOID)DirectoryEnd,
                        (LPVOID)LargePhysical,
                        (UINT)DirectoryEntry->Present,
                        (UINT)DirectoryEntry->ReadWrite,
                        (UINT)DirectoryEntry->Privilege,
                        (UINT)DirectoryEntry->Global,
                        (UINT)DirectoryEntry->NoExecute);
                    continue;
                }

                PHYSICAL TablePhysical = (PHYSICAL)(DirectoryEntry->Address << 12);

                DEBUG(TEXT("[LogPageDirectory64]     PDE[%u]: VA=%p-%p -> PT_PA=%p Present=%u RW=%u Priv=%u Global=%u NX=%u"),
                    DirectoryIndex,
                    (LPVOID)DirectoryBase,
                    (LPVOID)DirectoryEnd,
                    (LPVOID)TablePhysical,
                    (UINT)DirectoryEntry->Present,
                    (UINT)DirectoryEntry->ReadWrite,
                    (UINT)DirectoryEntry->Privilege,
                    (UINT)DirectoryEntry->Global,
                    (UINT)DirectoryEntry->NoExecute);

                LINEAR TableLinear = MapTempPhysicalPage2(TablePhysical);

                if (TableLinear == 0) {
                    ERROR(TEXT("[LogPageDirectory64] MapTempPhysicalPage2 failed for table %p"),
                        (LPVOID)TablePhysical);

                    PdptLinear = MapTempPhysicalPage2(PdptPhysical);
                    if (PdptLinear == 0) {
                        ERROR(TEXT("[LogPageDirectory64] Failed to restore PDPT mapping %p"),
                            (LPVOID)PdptPhysical);
                        return;
                    }

                    Pdpt = (LPPDPT)PdptLinear;
                    DirectoryLinear = MapTempPhysicalPage3(PageDirectoryPhysical);
                    if (DirectoryLinear == 0) {
                        ERROR(TEXT("[LogPageDirectory64] Failed to restore directory mapping %p"),
                            (LPVOID)PageDirectoryPhysical);
                        return;
                    }

                    Directory = (LPPAGE_DIRECTORY)DirectoryLinear;
                    continue;
                }

                LPPAGE_TABLE Table = (LPPAGE_TABLE)TableLinear;
                UINT MappedCount = 0;

                for (UINT TableIndex = 0; TableIndex < PAGE_TABLE_NUM_ENTRIES; TableIndex++) {
                    const X86_64_PAGE_TABLE_ENTRY *TableEntry = &Table[TableIndex];

                    if (!TableEntry->Present) continue;

                    MappedCount++;

                    if (MappedCount <= 3u || MappedCount >= PAGE_TABLE_NUM_ENTRIES - 2u) {
                        U64 TableVirtual = BuildLinearAddress(Pml4Index, PdptIndex, DirectoryIndex, TableIndex, 0);
                        PHYSICAL PagePhysical = (PHYSICAL)(TableEntry->Address << 12);

                        DEBUG(TEXT("[LogPageDirectory64]       PTE[%u]: VA=%p -> PA=%p Present=%u RW=%u Priv=%u Dirty=%u Global=%u NX=%u"),
                            TableIndex,
                            (LPVOID)TableVirtual,
                            (LPVOID)PagePhysical,
                            (U32)TableEntry->Present,
                            (U32)TableEntry->ReadWrite,
                            (U32)TableEntry->Privilege,
                            (U32)TableEntry->Dirty,
                            (U32)TableEntry->Global,
                            (U32)TableEntry->NoExecute);
                    } else if (MappedCount == 4u) {
                        DEBUG(TEXT("[LogPageDirectory64]       ... (%u more mapped pages) ..."),
                            PAGE_TABLE_NUM_ENTRIES - 6u);
                    }
                }

                if (MappedCount > 0) {
                    DEBUG(TEXT("[LogPageDirectory64]       Total mapped pages in PT[%u]: %u/%u"),
                        DirectoryIndex,
                        MappedCount,
                        PAGE_TABLE_NUM_ENTRIES);
                }

                PdptLinear = MapTempPhysicalPage2(PdptPhysical);
                if (PdptLinear == 0) {
                    ERROR(TEXT("[LogPageDirectory64] Failed to restore PDPT mapping %p"),
                        (LPVOID)PdptPhysical);
                    return;
                }

                Pdpt = (LPPDPT)PdptLinear;

                DirectoryLinear = MapTempPhysicalPage3(PageDirectoryPhysical);
                if (DirectoryLinear == 0) {
                    ERROR(TEXT("[LogPageDirectory64] Failed to restore directory mapping %p"),
                        (LPVOID)PageDirectoryPhysical);
                    return;
                }

                Directory = (LPPAGE_DIRECTORY)DirectoryLinear;
            }
        }
    }

    DEBUG(TEXT("[LogPageDirectory64] End of page directory"));
}
