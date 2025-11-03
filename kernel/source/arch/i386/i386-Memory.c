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


    i386-specific memory helpers

\************************************************************************/

#include "Memory.h"

#include "Console.h"
#include "CoreString.h"
#include "Kernel.h"
#include "Log.h"
#include "Stack.h"
#include "System.h"
#include "Text.h"
#include "arch/i386/i386.h"
#include "arch/i386/i386-Log.h"

/************************************************************************/

/**
 * @brief Checks whether a physical range can be safely targeted without clipping.
 *
 * The caller provides the base page frame and the number of pages to validate.
 * The function ensures that, after clipping against the allowed physical memory
 * map, the resulting range matches the requested one.
 *
 * @param Base Physical base page frame to validate.
 * @param NumPages Number of pages requested in the range.
 * @return TRUE when the range is valid or degenerate, FALSE otherwise.
 */
BOOL ValidatePhysicalTargetRange(PHYSICAL Base, UINT NumPages) {
    if (Base == 0 || NumPages == 0) return TRUE;

    UINT RequestedLength = NumPages << PAGE_SIZE_MUL;

    U64 RangeBase;
    RangeBase.LO = (U32)Base;
    RangeBase.HI = 0;

    U64 RangeLength;
    RangeLength.LO = (U32)RequestedLength;
    RangeLength.HI = 0;

    PHYSICAL ClippedBase = 0;
    UINT ClippedLength = 0;

    if (ClipPhysicalRange(RangeBase, RangeLength, &ClippedBase, &ClippedLength) == FALSE) return FALSE;

    return (ClippedBase == Base && ClippedLength == RequestedLength);
}

/************************************************************************/

/**
 * @brief Allocates and installs a page table for the linear address provided.
 *
 * The function obtains a new physical page for the table, links it in the
 * current page directory and returns the canonical virtual address of the
 * allocated table.
 *
 * @param Base Linear address whose table should be allocated.
 * @return The linear address of the mapped table, or NULL on failure.
 */
LINEAR AllocPageTable(LINEAR Base) {
    PHYSICAL PMA_Table = AllocPhysicalPage();

    if (PMA_Table == NULL) {
        ERROR(TEXT("[AllocPageTable] Out of physical pages"));
        return NULL;
    }

    Base = CanonicalizeLinearAddress(Base);

    UINT DirEntry = GetDirectoryEntry(Base);
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();

    U32 Privilege = PAGE_PRIVILEGE(Base);
    U32 DirectoryEntryValue = MakePageDirectoryEntryValue(
        PMA_Table,
        /*ReadWrite*/ 1,
        Privilege,
        /*WriteThrough*/ 0,
        /*CacheDisabled*/ 0,
        /*Global*/ 0,
        /*Fixed*/ 1);

    WritePageDirectoryEntryValue(Directory, DirEntry, DirectoryEntryValue);

    LINEAR VMA_PT = MapTemporaryPhysicalPage2(PMA_Table);
    MemorySet((LPVOID)VMA_PT, 0, PAGE_SIZE);

    FlushTLB();

    return (LINEAR)GetPageTableVAFor(Base);
}

/************************************************************************/

/**
 * @brief Retrieves a page table from an iterator when the entry is present.
 *
 * This helper validates the directory entry referenced by the iterator and
 * returns the table pointer through the provided output parameter. Large pages
 * are detected and reported through @p OutLargePage when supplied.
 *
 * @param Iterator Pointer to the iterator describing the directory entry.
 * @param OutTable Receives the page table pointer when available.
 * @param OutLargePage Optionally receives TRUE when the entry is a large page.
 * @return TRUE if a table is available, FALSE otherwise.
 */
BOOL TryGetPageTableForIterator(
    const ARCH_PAGE_ITERATOR* Iterator,
    LPPAGE_TABLE* OutTable,
    BOOL* OutLargePage) {
    if (Iterator == NULL || OutTable == NULL) return FALSE;

    if (OutLargePage != NULL) {
        *OutLargePage = FALSE;
    }

    UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(Iterator);
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    UINT DirectoryEntryValue = ReadPageDirectoryEntryValue(Directory, DirEntry);

    if ((DirectoryEntryValue & PAGE_FLAG_PRESENT) == 0) {
        return FALSE;
    }

    if ((DirectoryEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
        if (OutLargePage != NULL) {
            *OutLargePage = TRUE;
        }
        return FALSE;
    }

    *OutTable = MemoryPageIteratorGetTable(Iterator);
    return TRUE;
}

/************************************************************************/

static inline void MapOnePage(
    LINEAR Linear, PHYSICAL Physical, U32 ReadWrite, U32 Privilege, U32 WriteThrough, U32 CacheDisabled, U32 Global,
    U32 Fixed) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    UINT DirIndex = GetDirectoryEntry(Linear);

    if (!PageDirectoryEntryIsPresent(Directory, DirIndex)) {
        ConsolePanic(TEXT("[MapOnePage] PDE not present for VA %p (dir=%u)"), Linear, DirIndex);
    }

    LPPAGE_TABLE Table = GetPageTableVAFor(Linear);
    UINT TabIndex = GetTableEntry(Linear);

    WritePageTableEntryValue(
        Table,
        TabIndex,
        MakePageTableEntryValue(Physical, ReadWrite, Privilege, WriteThrough, CacheDisabled, Global, Fixed));

    InvalidatePage(Linear);
}

void ArchRemapTemporaryPage(LINEAR Linear, PHYSICAL Physical) {
    MapOnePage(Linear, Physical, /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);
}

static BOOL IsRegionFree(LINEAR Base, UINT Size) {
    LINEAR Current = CanonicalizeLinearAddress(Base);
    UINT NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT DirEntry = GetDirectoryEntry(Current);
        UINT TabEntry = GetTableEntry(Current);

        LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
        if (PageDirectoryEntryIsPresent(Directory, DirEntry)) {
            LPPAGE_TABLE Table = GetPageTableVAFor(Current);
            if (PageTableEntryIsPresent(Table, TabEntry)) {
                return FALSE;
            }
        }

        Current += PAGE_SIZE;
    }

    return TRUE;
}

static LINEAR FindFreeRegion(LINEAR StartBase, UINT Size) {
    LINEAR Base = N_4MB;

    if (StartBase >= Base) {
        Base = CanonicalizeLinearAddress(StartBase);
    }

    while (TRUE) {
        if (IsRegionFree(Base, Size)) {
            return Base;
        }

        LINEAR Next = CanonicalizeLinearAddress(Base + PAGE_SIZE);
        if (Next <= Base) {
            return NULL;
        }
        Base = Next;
    }
}

static void FreeEmptyPageTables(void) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();

    for (LINEAR Base = N_4MB; Base < VMA_KERNEL; Base += PAGE_TABLE_CAPACITY) {
        UINT DirEntry = GetDirectoryEntry(Base);

        if (!PageDirectoryEntryIsPresent(Directory, DirEntry)) {
            continue;
        }

        LPPAGE_TABLE Table = GetPageTableVAFor(Base);
        BOOL Destroy = TRUE;

        for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
            if (PageTableEntryIsPresent(Table, Index)) {
                Destroy = FALSE;
                break;
            }
        }

        if (Destroy) {
            PHYSICAL TablePhysical = PageDirectoryEntryGetPhysical(Directory, DirEntry);
            if (TablePhysical != 0) {
                SetPhysicalPageUsage((UINT)(TablePhysical >> PAGE_SIZE_MUL), FALSE);
            }

            ClearPageDirectoryEntry(Directory, DirEntry);
        }
    }
}

BOOL ArchFreeRegion(LINEAR Base, UINT Size);

static BOOL PopulateRegionPages(LINEAR Base,
                                PHYSICAL Target,
                                UINT NumPages,
                                U32 Flags,
                                LINEAR RollbackBase,
                                LPCSTR FunctionName) {
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Base);

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(&Iterator);
        UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);
        LINEAR CurrentLinear = MemoryPageIteratorGetLinear(&Iterator);

        BOOL IsLargePage = FALSE;
        LPPAGE_TABLE Table = NULL;

        if (!TryGetPageTableForIterator(&Iterator, &Table, &IsLargePage)) {
            if (IsLargePage) {
                ArchFreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                return FALSE;
            }

            if (AllocPageTable(CurrentLinear) == NULL) {
                ArchFreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                return FALSE;
            }

            if (!TryGetPageTableForIterator(&Iterator, &Table, NULL)) {
                ArchFreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                return FALSE;
            }
        }

        U32 Privilege = PAGE_PRIVILEGE(CurrentLinear);
        U32 FixedFlag = (Flags & ALLOC_PAGES_IO) ? 1u : 0u;
        U32 ReadWrite = (Flags & ALLOC_PAGES_READWRITE) ? 1u : 0u;
        U32 PteCacheDisabled = (Flags & ALLOC_PAGES_UC) ? 1u : 0u;
        U32 PteWriteThrough = (Flags & ALLOC_PAGES_WC) ? 1u : 0u;

        if (PteCacheDisabled) {
            PteWriteThrough = 0;
        }

        U32 BaseFlags = BuildPageFlags(ReadWrite, Privilege, PteWriteThrough, PteCacheDisabled, 0, FixedFlag);
        U32 ReservedFlags = BaseFlags & ~PAGE_FLAG_PRESENT;
        PHYSICAL ReservedPhysical = (PHYSICAL)(MAX_U32 & ~(PAGE_SIZE - 1));

        WritePageTableEntryValue(Table, TabEntry, MakePageEntryRaw(ReservedPhysical, ReservedFlags));

        if (Flags & ALLOC_PAGES_COMMIT) {
            PHYSICAL Physical = 0;

            if (Target != 0) {
                Physical = Target + (PHYSICAL)(Index << PAGE_SIZE_MUL);

                if (Flags & ALLOC_PAGES_IO) {
                    WritePageTableEntryValue(
                        Table,
                        TabEntry,
                        MakePageTableEntryValue(
                            Physical,
                            ReadWrite,
                            Privilege,
                            PteWriteThrough,
                            PteCacheDisabled,
                            /*Global*/ 0,
                            /*Fixed*/ 1));
                } else {
                    SetPhysicalPageUsage((UINT)(Physical >> PAGE_SIZE_MUL), TRUE);
                    WritePageTableEntryValue(
                        Table,
                        TabEntry,
                        MakePageTableEntryValue(
                            Physical,
                            ReadWrite,
                            Privilege,
                            PteWriteThrough,
                            PteCacheDisabled,
                            /*Global*/ 0,
                            /*Fixed*/ 0));
                }
            } else {
                Physical = AllocPhysicalPage();

                if (Physical == NULL) {
                    ERROR(TEXT("[%s] AllocPhysicalPage failed"), FunctionName);
                    ArchFreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                    return FALSE;
                }

                WritePageTableEntryValue(
                    Table,
                    TabEntry,
                    MakePageTableEntryValue(
                        Physical,
                        ReadWrite,
                        Privilege,
                        PteWriteThrough,
                        PteCacheDisabled,
                        /*Global*/ 0,
                        /*Fixed*/ 0));
            }
        }

        MemoryPageIteratorStepPage(&Iterator);
    }

    return TRUE;
}


LINEAR ArchAllocRegion(LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags) {
    UINT NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;
    if (NumPages == 0) NumPages = 1;

    if (Base != 0 && (Flags & ALLOC_PAGES_AT_OR_OVER) == 0) {
        if (!IsRegionFree(Base, Size)) {
            return NULL;
        }
    }

    if (Base == 0 || (Flags & ALLOC_PAGES_AT_OR_OVER)) {
        LINEAR NewBase = FindFreeRegion(Base, Size);
        if (NewBase == NULL) {
            return NULL;
        }
        Base = NewBase;
    }

    LINEAR Pointer = Base;

    if (PopulateRegionPages(Base, Target, NumPages, Flags, Pointer, TEXT("ArchAllocRegion")) == FALSE) {
        return NULL;
    }

    FlushTLB();
    return Pointer;
}

BOOL ArchResizeRegion(LINEAR Base, PHYSICAL Target, UINT Size, UINT NewSize, U32 Flags) {
    UINT CurrentPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;
    UINT RequestedPages = (NewSize + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;
    if (CurrentPages == 0) CurrentPages = 1;
    if (RequestedPages == 0) RequestedPages = 1;

    if (RequestedPages == CurrentPages) {
        return TRUE;
    }

    if (RequestedPages > CurrentPages) {
        UINT AdditionalPages = RequestedPages - CurrentPages;
        LINEAR NewBase = Base + ((LINEAR)CurrentPages << PAGE_SIZE_MUL);
        UINT AdditionalSize = AdditionalPages << PAGE_SIZE_MUL;

        if (!IsRegionFree(NewBase, AdditionalSize)) {
            return FALSE;
        }

        PHYSICAL AdditionalTarget = 0;
        if (Target != 0) {
            AdditionalTarget = Target + (PHYSICAL)(CurrentPages << PAGE_SIZE_MUL);
        }

        if (!PopulateRegionPages(NewBase,
                                 AdditionalTarget,
                                 AdditionalPages,
                                 Flags,
                                 NewBase,
                                 TEXT("ArchResizeRegion"))) {
            return FALSE;
        }

        FlushTLB();
    } else {
        UINT PagesToRelease = CurrentPages - RequestedPages;
        if (PagesToRelease != 0) {
            LINEAR ReleaseBase = Base + ((LINEAR)RequestedPages << PAGE_SIZE_MUL);
            UINT ReleaseSize = PagesToRelease << PAGE_SIZE_MUL;

            if (!ArchFreeRegion(ReleaseBase, ReleaseSize)) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

BOOL ArchFreeRegion(LINEAR Base, UINT Size) {
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Base);
    UINT NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;
    if (NumPages == 0) NumPages = 1;

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(&Iterator);
        UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);

        BOOL IsLargePage = FALSE;
        LPPAGE_TABLE Table = NULL;

        if (TryGetPageTableForIterator(&Iterator, &Table, &IsLargePage) && PageTableEntryIsPresent(Table, TabEntry)) {
            PHYSICAL EntryPhysical = PageTableEntryGetPhysical(Table, TabEntry);
            BOOL Fixed = PageTableEntryIsFixed(Table, TabEntry);

            if (!Fixed) {
                SetPhysicalPageUsage((UINT)(EntryPhysical >> PAGE_SIZE_MUL), FALSE);
            }

            ClearPageTableEntry(Table, TabEntry);
        }

        MemoryPageIteratorStepPage(&Iterator);
    }

    FreeEmptyPageTables();
    FlushTLB();

    return TRUE;
}


/**
 * @brief Builds a kernel page directory with predefined mappings.
 *
 * The directory includes low memory, kernel, task runner and recursive entries
 * and prepares associated page tables. On success, the physical address of the
 * new directory is returned.
 *
 * @return Physical address of the allocated directory, or NULL on failure.
 */
PHYSICAL AllocPageDirectory(void) {
    PHYSICAL PMA_Directory = NULL;
    PHYSICAL PMA_LowTable = NULL;
    PHYSICAL PMA_KernelTable = NULL;
    PHYSICAL PMA_TaskRunnerTable = NULL;

    LPPAGE_DIRECTORY Directory = NULL;
    LPPAGE_TABLE LowTable = NULL;
    LPPAGE_TABLE KernelTable = NULL;
    LPPAGE_TABLE TaskRunnerTable = NULL;

    DEBUG(TEXT("[AllocPageDirectory] Enter"));

    if (EnsureCurrentStackSpace(N_32KB) == FALSE) {
        ERROR(TEXT("[AllocPageDirectory] Unable to ensure stack availability"));
        return NULL;
    }

    UINT DirKernel = (VMA_KERNEL >> PAGE_TABLE_CAPACITY_MUL);
    UINT DirTaskRunner = (VMA_TASK_RUNNER >> PAGE_TABLE_CAPACITY_MUL);
    PHYSICAL PhysBaseKernel = KernelStartup.KernelPhysicalBase;
    UINT Index;

    PMA_Directory = AllocPhysicalPage();
    PMA_LowTable = AllocPhysicalPage();
    PMA_KernelTable = AllocPhysicalPage();
    PMA_TaskRunnerTable = AllocPhysicalPage();

    if (PMA_Directory == NULL || PMA_LowTable == NULL || PMA_KernelTable == NULL || PMA_TaskRunnerTable == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Out of physical pages"));
        goto Out_Error;
    }

    LINEAR VMA_PD = MapTemporaryPhysicalPage1(PMA_Directory);
    if (VMA_PD == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed on Directory"));
        goto Out_Error;
    }
    Directory = (LPPAGE_DIRECTORY)VMA_PD;
    MemorySet(Directory, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocPageDirectory] Page directory cleared"));

    WritePageDirectoryEntryValue(
        Directory,
        0,
        MakePageDirectoryEntryValue(
            PMA_LowTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Directory,
        DirKernel,
        MakePageDirectoryEntryValue(
            PMA_KernelTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Directory,
        DirTaskRunner,
        MakePageDirectoryEntryValue(
            PMA_TaskRunnerTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Directory,
        PD_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            PMA_Directory,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    LINEAR VMA_PT = MapTemporaryPhysicalPage2(PMA_LowTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed on LowTable"));
        goto Out_Error;
    }
    LowTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(LowTable, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocPageDirectory] Low memory table cleared"));

    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = (PHYSICAL)Index << PAGE_SIZE_MUL;

#ifdef PROTECT_BIOS
        BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);
#else
        BOOL Protected = FALSE;
#endif

        if (Protected) {
            ClearPageTableEntry(LowTable, Index);
        } else {
            WritePageTableEntryValue(
                LowTable,
                Index,
                MakePageTableEntryValue(
                    Physical,
                    /*ReadWrite*/ 1,
                    PAGE_PRIVILEGE_KERNEL,
                    /*WriteThrough*/ 0,
                    /*CacheDisabled*/ 0,
                    /*Global*/ 0,
                    /*Fixed*/ 1));
        }
    }

    VMA_PT = MapTemporaryPhysicalPage2(PMA_KernelTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed on KernelTable"));
        goto Out_Error;
    }
    KernelTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(KernelTable, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocPageDirectory] Kernel table cleared"));

    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = PhysBaseKernel + ((PHYSICAL)Index << PAGE_SIZE_MUL);
        WritePageTableEntryValue(
            KernelTable,
            Index,
            MakePageTableEntryValue(
                Physical,
                /*ReadWrite*/ 1,
                PAGE_PRIVILEGE_KERNEL,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                /*Global*/ 0,
                /*Fixed*/ 1));
    }

    VMA_PT = MapTemporaryPhysicalPage2(PMA_TaskRunnerTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed on TaskRunnerTable"));
        goto Out_Error;
    }
    TaskRunnerTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(TaskRunnerTable, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocPageDirectory] TaskRunner table cleared"));

    LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = KernelToPhysical(TaskRunnerLinear);

    DEBUG(TEXT("[AllocPageDirectory] TaskRunnerPhysical = %x + (%x - %x) = %x"),
        (UINT)PhysBaseKernel, (UINT)TaskRunnerLinear, (UINT)VMA_KERNEL, (UINT)TaskRunnerPhysical);

    UINT TaskRunnerTableIndex = GetTableEntry(VMA_TASK_RUNNER);

    WritePageTableEntryValue(
        TaskRunnerTable,
        TaskRunnerTableIndex,
        MakePageTableEntryValue(
            TaskRunnerPhysical,
            /*ReadWrite*/ 0,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    FlushTLB();

    DEBUG(TEXT("[AllocPageDirectory] PDE[0]=%x, PDE[768]=%x, PDE[%u]=%x, PDE[1023]=%x"),
        ReadPageDirectoryEntryValue(Directory, 0),
        ReadPageDirectoryEntryValue(Directory, 768),
        DirTaskRunner,
        ReadPageDirectoryEntryValue(Directory, DirTaskRunner),
        ReadPageDirectoryEntryValue(Directory, 1023));
    DEBUG(TEXT("[AllocPageDirectory] LowTable[0]=%x, KernelTable[0]=%x, TaskRunnerTable[%u]=%x"),
        ReadPageTableEntryValue(LowTable, 0),
        ReadPageTableEntryValue(KernelTable, 0),
        TaskRunnerTableIndex,
        ReadPageTableEntryValue(TaskRunnerTable, TaskRunnerTableIndex));
    DEBUG(TEXT("[AllocPageDirectory] TaskRunner VMA=%x -> Physical=%x"), VMA_TASK_RUNNER, TaskRunnerPhysical);

    DEBUG(TEXT("[AllocPageDirectory] Exit"));
    return PMA_Directory;

Out_Error:

    if (PMA_Directory) FreePhysicalPage(PMA_Directory);
    if (PMA_LowTable) FreePhysicalPage(PMA_LowTable);
    if (PMA_KernelTable) FreePhysicalPage(PMA_KernelTable);
    if (PMA_TaskRunnerTable) FreePhysicalPage(PMA_TaskRunnerTable);

    return NULL;
}

/************************************************************************/

/**
 * @brief Creates a user-space page directory inheriting kernel mappings.
 *
 * The new directory mirrors kernel entries from the current directory while
 * preparing its own low and kernel tables. Recursive mapping is configured
 * before returning the directory physical address.
 *
 * @return Physical address of the allocated directory, or NULL on failure.
 */
PHYSICAL AllocUserPageDirectory(void) {
    PHYSICAL PMA_Directory = NULL;
    PHYSICAL PMA_LowTable = NULL;
    PHYSICAL PMA_KernelTable = NULL;

    LPPAGE_DIRECTORY Directory = NULL;
    LPPAGE_TABLE LowTable = NULL;
    LPPAGE_TABLE KernelTable = NULL;
    LPPAGE_DIRECTORY CurrentPD = (LPPAGE_DIRECTORY)PD_VA;

    DEBUG(TEXT("[AllocUserPageDirectory] Enter"));

    if (EnsureCurrentStackSpace(N_32KB) == FALSE) {
        ERROR(TEXT("[AllocUserPageDirectory] Unable to ensure stack availability"));
        return NULL;
    }

    UINT DirKernel = (VMA_KERNEL >> PAGE_TABLE_CAPACITY_MUL);
    PHYSICAL PhysBaseKernel = KernelStartup.KernelPhysicalBase;
    UINT Index;

    PMA_Directory = AllocPhysicalPage();
    PMA_LowTable = AllocPhysicalPage();
    PMA_KernelTable = AllocPhysicalPage();

    if (PMA_Directory == NULL || PMA_LowTable == NULL || PMA_KernelTable == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] Out of physical pages"));
        goto Out_Error;
    }

    LINEAR VMA_PD = MapTemporaryPhysicalPage1(PMA_Directory);
    if (VMA_PD == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] MapTemporaryPhysicalPage1 failed on Directory"));
        goto Out_Error;
    }
    Directory = (LPPAGE_DIRECTORY)VMA_PD;
    MemorySet(Directory, 0, PAGE_SIZE);

    DEBUG(TEXT("[AllocUserPageDirectory] Page directory cleared"));

    WritePageDirectoryEntryValue(
        Directory,
        0,
        MakePageDirectoryEntryValue(
            PMA_LowTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Directory,
        DirKernel,
        MakePageDirectoryEntryValue(
            PMA_KernelTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    UINT UserStartPDE = GetDirectoryEntry(VMA_USER);
    UINT UserEndPDE = GetDirectoryEntry(VMA_LIBRARY - 1) - 1;
    for (Index = 1; Index < 1023; Index++) {
        if (PageDirectoryEntryIsPresent(CurrentPD, Index) && Index != DirKernel) {
            if (Index >= UserStartPDE && Index <= UserEndPDE) {
                DEBUG(TEXT("[AllocUserPageDirectory] Skipped user space PDE[%u]"), Index);
                continue;
            }
            WritePageDirectoryEntryValue(Directory, Index, ReadPageDirectoryEntryValue(CurrentPD, Index));
            DEBUG(TEXT("[AllocUserPageDirectory] Copied PDE[%u]"), Index);
        }
    }

    WritePageDirectoryEntryValue(
        Directory,
        PD_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            PMA_Directory,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    LINEAR VMA_PT = MapTemporaryPhysicalPage2(PMA_LowTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] MapTemporaryPhysicalPage2 failed on LowTable"));
        goto Out_Error;
    }
    LowTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(LowTable, 0, PAGE_SIZE);

    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = (PHYSICAL)Index << PAGE_SIZE_MUL;

#ifdef PROTECT_BIOS
        BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);
#else
        BOOL Protected = FALSE;
#endif

        if (Protected) {
            ClearPageTableEntry(LowTable, Index);
        } else {
            WritePageTableEntryValue(
                LowTable,
                Index,
                MakePageTableEntryValue(
                    Physical,
                    /*ReadWrite*/ 1,
                    PAGE_PRIVILEGE_KERNEL,
                    /*WriteThrough*/ 0,
                    /*CacheDisabled*/ 0,
                    /*Global*/ 0,
                    /*Fixed*/ 1));
        }
    }

    VMA_PT = MapTemporaryPhysicalPage2(PMA_KernelTable);
    if (VMA_PT == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] MapTemporaryPhysicalPage2 failed on KernelTable"));
        goto Out_Error;
    }
    KernelTable = (LPPAGE_TABLE)VMA_PT;
    MemorySet(KernelTable, 0, PAGE_SIZE);

    for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = PhysBaseKernel + ((PHYSICAL)Index << PAGE_SIZE_MUL);
        WritePageTableEntryValue(
            KernelTable,
            Index,
            MakePageTableEntryValue(
                Physical,
                /*ReadWrite*/ 1,
                PAGE_PRIVILEGE_KERNEL,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                /*Global*/ 0,
                /*Fixed*/ 1));
    }

    FlushTLB();

    DEBUG(TEXT("[AllocUserPageDirectory] PDE[0]=%x, PDE[768]=%x, PDE[1023]=%x"),
        ReadPageDirectoryEntryValue(Directory, 0),
        ReadPageDirectoryEntryValue(Directory, 768),
        ReadPageDirectoryEntryValue(Directory, 1023));
    DEBUG(TEXT("[AllocUserPageDirectory] LowTable[0]=%x, KernelTable[0]=%x"),
        ReadPageTableEntryValue(LowTable, 0),
        ReadPageTableEntryValue(KernelTable, 0));

    DEBUG(TEXT("[AllocUserPageDirectory] Exit"));
    return PMA_Directory;

Out_Error:

    if (PMA_Directory) FreePhysicalPage(PMA_Directory);
    if (PMA_LowTable) FreePhysicalPage(PMA_LowTable);
    if (PMA_KernelTable) FreePhysicalPage(PMA_KernelTable);

    return NULL;
}

/************************************************************************/

/**
 * @brief Initializes the i386 memory manager structures.
 *
 * This routine prepares the physical page bitmap, builds and loads the initial
 * page directory, and initializes segmentation through the GDT. It must be
 * called during early kernel initialization.
 */
void InitializeMemoryManager(void) {
    DEBUG(TEXT("[InitializeMemoryManager] Enter"));

    UpdateKernelMemoryMetricsFromMultibootMap();

    if (KernelStartup.PageCount == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    UINT BitmapBytes = (KernelStartup.PageCount + 7u) >> MUL_8;
    UINT BitmapBytesAligned = (UINT)PAGE_ALIGN(BitmapBytes);

    PHYSICAL KernelSpan = (PHYSICAL)KernelStartup.KernelSize + (PHYSICAL)N_512KB;
    PHYSICAL MapSize = (PHYSICAL)PAGE_ALIGN(KernelSpan);
    PHYSICAL LoaderReservedEnd = KernelStartup.KernelPhysicalBase + MapSize;
    PHYSICAL PpbPhysical = PAGE_ALIGN(LoaderReservedEnd);

    Kernel.PPB = (LPPAGEBITMAP)(UINT)PpbPhysical;
    Kernel.PPBSize = BitmapBytesAligned;

    DEBUG(TEXT("[InitializeMemoryManager] Kernel.PPB physical base: %p"), (LPVOID)PpbPhysical);
    DEBUG(TEXT("[InitializeMemoryManager] Kernel.PPB bytes (aligned): %lX"), BitmapBytesAligned);

    MemorySet(Kernel.PPB, 0, Kernel.PPBSize);

    MarkUsedPhysicalMemory();

    if (KernelStartup.MemorySize == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    DEBUG(TEXT("[InitializeMemoryManager] Temp pages reserved: %p, %p, %p"),
        I386_TEMP_LINEAR_PAGE_1,
        I386_TEMP_LINEAR_PAGE_2,
        I386_TEMP_LINEAR_PAGE_3);

    PHYSICAL NewPageDirectory = AllocPageDirectory();

    LogPageDirectory(NewPageDirectory);

    DEBUG(TEXT("[InitializeMemoryManager] Page directory ready"));

    if (NewPageDirectory == NULL) {
        ERROR(TEXT("[InitializeMemoryManager] AllocPageDirectory failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    DEBUG(TEXT("[InitializeMemoryManager] New page directory: %p"), NewPageDirectory);

    LoadPageDirectory(NewPageDirectory);

    DEBUG(TEXT("[InitializeMemoryManager] Page directory set: %p"), NewPageDirectory);

    FlushTLB();

    DEBUG(TEXT("[InitializeMemoryManager] TLB flushed"));

    Kernel_i386.GDT = (LPSEGMENT_DESCRIPTOR)AllocKernelRegion(0, GDT_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Kernel_i386.GDT == NULL) {
        ERROR(TEXT("[InitializeMemoryManager] AllocRegion for GDT failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    InitializeGlobalDescriptorTable(Kernel_i386.GDT);

    DEBUG(TEXT("[InitializeMemoryManager] Loading GDT"));

    LoadGlobalDescriptorTable((PHYSICAL)Kernel_i386.GDT, GDT_SIZE - 1);

    LogGlobalDescriptorTable(Kernel_i386.GDT, 10);

    DEBUG(TEXT("[InitializeMemoryManager] Exit"));
}

/************************************************************************/

/**
 * @brief Resolves a linear address into a physical address when mapped.
 *
 * The current page directory and table are inspected to fetch the page frame
 * and combine it with the page offset derived from the linear address.
 *
 * @param Address Linear address to resolve.
 * @return Physical address matching the mapping, or 0 if not mapped.
 */
PHYSICAL MapLinearToPhysical(LINEAR Address) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Address);
    UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(&Iterator);
    UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);

    if (!PageDirectoryEntryIsPresent(Directory, DirEntry)) return 0;

    LPPAGE_TABLE Table = MemoryPageIteratorGetTable(&Iterator);
    if (!PageTableEntryIsPresent(Table, TabEntry)) return 0;

    PHYSICAL PagePhysical = PageTableEntryGetPhysical(Table, TabEntry);
    if (PagePhysical == 0) return 0;

    return (PHYSICAL)(PagePhysical | (Address & (PAGE_SIZE - 1)));
}

/************************************************************************/

/**
 * @brief Checks whether a linear address refers to a valid mapped page.
 *
 * This helper validates both directory and table entries for the supplied
 * address and confirms their presence.
 *
 * @param Address Linear address to validate.
 * @return TRUE when the address is mapped, FALSE otherwise.
 */
BOOL IsValidMemory(LINEAR Address) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    UINT DirectoryIndex = GetDirectoryEntry(Address);
    UINT TableIndex = GetTableEntry(Address);

    if (Directory == NULL) return FALSE;
    if (DirectoryIndex >= PAGE_TABLE_NUM_ENTRIES) return FALSE;
    if (TableIndex >= PAGE_TABLE_NUM_ENTRIES) return FALSE;

    if (PageDirectoryEntryIsPresent(Directory, DirectoryIndex) == FALSE) return FALSE;

    LPPAGE_TABLE Table = GetPageTableVAFor(Address);
    if (Table == NULL) return FALSE;
    if (PageTableEntryIsPresent(Table, TableIndex) == FALSE) return FALSE;

    return TRUE;
}

/************************************************************************/
