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


    x86-64-specific memory helpers

\************************************************************************/

#include "Memory.h"

#include "Console.h"
#include "CoreString.h"
#include "Kernel.h"
#include "Log.h"
#include "Stack.h"
#include "System.h"
#include "Text.h"
#include "arch/x86-64/x86-64.h"
#include "arch/x86-64/x86-64-Log.h"

/************************************************************************/

/**
 * @brief Validate that a physical range remains intact after clipping.
 *
 * The routine checks whether the provided base and length in pages survive
 * the ClipPhysicalRange() constraints without alteration.
 *
 * @param Base Physical base page frame to validate.
 * @param NumPages Number of pages requested in the range.
 * @return TRUE when the range is valid or degenerate, FALSE otherwise.
 */
BOOL ValidatePhysicalTargetRange(PHYSICAL Base, UINT NumPages) {
    if (Base == 0 || NumPages == 0) return TRUE;

    UINT RequestedLength = NumPages << PAGE_SIZE_MUL;

    PHYSICAL ClippedBase = 0;
    UINT ClippedLength = 0;

    if (ClipPhysicalRange((U64)Base, (U64)RequestedLength, &ClippedBase, &ClippedLength) == FALSE) return FALSE;

    return (ClippedBase == Base && ClippedLength == RequestedLength);
}

/************************************************************************/

/**
 * @brief Allocate and link a page table for the provided linear address.
 *
 * The helper walks the paging hierarchy, checks that upper levels are present,
 * allocates a new table and installs it in the page directory.
 *
 * @param Base Linear address whose table should be allocated.
 * @return Canonical virtual address of the mapped table, or NULL on failure.
 */
LINEAR AllocPageTable(LINEAR Base) {
    PHYSICAL PMA_Table = AllocPhysicalPage();

    if (PMA_Table == NULL) {
        ERROR(TEXT("[AllocPageTable] Out of physical pages"));
        return NULL;
    }

    Base = CanonicalizeLinearAddress(Base);

    UINT DirEntry = GetDirectoryEntry(Base);
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Base);
    UINT Pml4Index = MemoryPageIteratorGetPml4Index(&Iterator);
    UINT PdptIndex = MemoryPageIteratorGetPdptIndex(&Iterator);

    LPPML4 Pml4 = GetCurrentPml4VA();
    U64 Pml4EntryValue = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)Pml4, Pml4Index);

    if ((Pml4EntryValue & PAGE_FLAG_PRESENT) == 0) {
        return NULL;
    }

    PHYSICAL PdptPhysical = (PHYSICAL)(Pml4EntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY PdptLinear = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(PdptPhysical);
    U64 PdptEntryValue = ReadPageDirectoryEntryValue(PdptLinear, PdptIndex);

    if ((PdptEntryValue & PAGE_FLAG_PRESENT) == 0) {
        return NULL;
    }

    if ((PdptEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
        return NULL;
    }

    PHYSICAL DirectoryPhysical = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(DirectoryPhysical);

    U32 Privilege = PAGE_PRIVILEGE(Base);
    U64 DirectoryEntryValue = MakePageDirectoryEntryValue(
        PMA_Table,
        /*ReadWrite*/ 1,
        Privilege,
        /*WriteThrough*/ 0,
        /*CacheDisabled*/ 0,
        /*Global*/ 0,
        /*Fixed*/ 1);

    WritePageDirectoryEntryValue(Directory, DirEntry, DirectoryEntryValue);

    LINEAR VMA_PT = MapTemporaryPhysicalPage3(PMA_Table);
    MemorySet((LPVOID)VMA_PT, 0, PAGE_SIZE);

    FlushTLB();

    return (LINEAR)GetPageTableVAFor(Base);
}

/************************************************************************/

/**
 * @brief Retrieve the page table referenced by an iterator when present.
 *
 * The iterator supplies the paging indexes and the function verifies the
 * presence of intermediate levels. Large pages are reported through
 * @p OutLargePage when requested.
 *
 * @param Iterator Pointer describing the directory entry to inspect.
 * @param OutTable Receives the resulting page table pointer when available.
 * @param OutLargePage Optionally receives TRUE when the entry maps a large page.
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

    UINT Pml4Index = MemoryPageIteratorGetPml4Index(Iterator);
    UINT PdptIndex = MemoryPageIteratorGetPdptIndex(Iterator);
    UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(Iterator);

    LPPML4 Pml4 = GetCurrentPml4VA();
    U64 Pml4EntryValue = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)Pml4, Pml4Index);

    if ((Pml4EntryValue & PAGE_FLAG_PRESENT) == 0) {
        return FALSE;
    }

    PHYSICAL PdptPhysical = (PHYSICAL)(Pml4EntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY PdptLinear = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(PdptPhysical);
    U64 PdptEntryValue = ReadPageDirectoryEntryValue(PdptLinear, PdptIndex);

    if ((PdptEntryValue & PAGE_FLAG_PRESENT) == 0) {
        return FALSE;
    }

    if ((PdptEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
        if (OutLargePage != NULL) {
            *OutLargePage = TRUE;
        }
        return FALSE;
    }

    PHYSICAL DirectoryPhysical = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(DirectoryPhysical);
    U64 DirectoryEntryValue = ReadPageDirectoryEntryValue(Directory, DirEntry);

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

typedef enum _PAGE_TABLE_POPULATE_MODE {
    PAGE_TABLE_POPULATE_IDENTITY,
    PAGE_TABLE_POPULATE_SINGLE_ENTRY,
    PAGE_TABLE_POPULATE_EMPTY
} PAGE_TABLE_POPULATE_MODE;

#define USERLAND_SEEDED_TABLES 1u

typedef struct _PAGE_TABLE_SETUP {
    UINT DirectoryIndex;
    U32 ReadWrite;
    U32 Privilege;
    U32 Global;
    PAGE_TABLE_POPULATE_MODE Mode;
    PHYSICAL Physical;
    union {
        struct {
            PHYSICAL PhysicalBase;
            BOOL ProtectBios;
        } Identity;
        struct {
            UINT TableIndex;
            PHYSICAL Physical;
            U32 ReadWrite;
            U32 Privilege;
            U32 Global;
        } Single;
    } Data;
} PAGE_TABLE_SETUP;

typedef struct _REGION_SETUP {
    LPCSTR Label;
    UINT PdptIndex;
    U32 ReadWrite;
    U32 Privilege;
    U32 Global;
    PHYSICAL PdptPhysical;
    PHYSICAL DirectoryPhysical;
    PAGE_TABLE_SETUP Tables[64];
    UINT TableCount;
} REGION_SETUP;

/************************************************************************/

typedef struct _LOW_REGION_SHARED_TABLES {
    PHYSICAL BiosTablePhysical;
    PHYSICAL IdentityTablePhysical;
} LOW_REGION_SHARED_TABLES;

static LOW_REGION_SHARED_TABLES LowRegionSharedTables = {
    .BiosTablePhysical = NULL,
    .IdentityTablePhysical = NULL,
};

/************************************************************************/

/**
 * @brief Obtain or create a shared identity table used by the low region.
 *
 * The function lazily allocates the table, initializes its entries according
 * to the requested physical base and BIOS protection flag, and records the
 * physical address for future reuse.
 *
 * @param TablePhysical Receives the physical address of the shared table.
 * @param PhysicalBase Physical base used to populate identity mappings.
 * @param ProtectBios TRUE when BIOS ranges must be cleared from the table.
 * @param Label Debug label describing the shared table.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL EnsureSharedLowTable(
    PHYSICAL* TablePhysical,
    PHYSICAL PhysicalBase,
    BOOL ProtectBios,
    LPCSTR Label) {

    if (TablePhysical == NULL || Label == NULL) {
        ERROR(TEXT("[SetupLowRegion] Invalid shared table parameters"));
        return FALSE;
    }

    if (*TablePhysical != NULL) {
        DEBUG(TEXT("[SetupLowRegion] Reusing shared %s table at %p"), Label, *TablePhysical);
        return TRUE;
    }

    PHYSICAL Physical = AllocPhysicalPage();

    if (Physical == NULL) {
        ERROR(TEXT("[SetupLowRegion] Out of physical pages for shared %s table"), Label);
        return FALSE;
    }

    LINEAR Linear = MapTemporaryPhysicalPage3(Physical);

    if (Linear == NULL) {
        ERROR(TEXT("[SetupLowRegion] MapTemporaryPhysicalPage3 failed for shared %s table"), Label);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    LPPAGE_TABLE Table = (LPPAGE_TABLE)Linear;
    MemorySet(Table, 0, PAGE_SIZE);

#if !defined(PROTECT_BIOS)
    UNUSED(ProtectBios);
#endif

    for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL EntryPhysical = PhysicalBase + ((PHYSICAL)Index << PAGE_SIZE_MUL);

#ifdef PROTECT_BIOS
        if (ProtectBios) {
            BOOL Protected =
                (EntryPhysical == 0) || (EntryPhysical > PROTECTED_ZONE_START && EntryPhysical <= PROTECTED_ZONE_END);

            if (Protected) {
                ClearPageTableEntry(Table, Index);
                continue;
            }
        }
#endif

        WritePageTableEntryValue(
            Table,
            Index,
            MakePageTableEntryValue(
                EntryPhysical,
                /*ReadWrite*/ 1,
                PAGE_PRIVILEGE_KERNEL,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                /*Global*/ 0,
                /*Fixed*/ 1));
    }

    *TablePhysical = Physical;

    DEBUG(TEXT("[SetupLowRegion] Shared %s table prepared at %p (base %p)"), Label, Physical, PhysicalBase);

    return TRUE;
}

/************************************************************************/
/**
 * @brief Clear a REGION_SETUP structure to its default state.
 * @param Region Structure to reset.
 */
static void ResetRegionSetup(REGION_SETUP* Region) {
    MemorySet(Region, 0, sizeof(REGION_SETUP));
}

/************************************************************************/

/**
 * @brief Release the physical resources owned by a REGION_SETUP.
 * @param Region Structure that tracks the allocated tables.
 */
static void ReleaseRegionSetup(REGION_SETUP* Region) {
    if (Region->PdptPhysical != NULL) {
        FreePhysicalPage(Region->PdptPhysical);
        Region->PdptPhysical = NULL;
    }

    if (Region->DirectoryPhysical != NULL) {
        FreePhysicalPage(Region->DirectoryPhysical);
        Region->DirectoryPhysical = NULL;
    }

    for (UINT Index = 0; Index < Region->TableCount; Index++) {
        if (Region->Tables[Index].Physical != NULL) {
            FreePhysicalPage(Region->Tables[Index].Physical);
            Region->Tables[Index].Physical = NULL;
        }
    }

    Region->TableCount = 0;
}

/************************************************************************/

/**
 * @brief Allocate a page table and populate it according to the setup entry.
 * @param Region Parent region that will own the table.
 * @param Table Table description containing allocation parameters.
 * @param Directory Page-directory view used to link the table.
 * @return TRUE on success, FALSE when allocation or mapping fails.
 */
static BOOL AllocateTableAndPopulate(
    REGION_SETUP* Region,
    PAGE_TABLE_SETUP* Table,
    LPPAGE_DIRECTORY Directory) {

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] begin"), Region->Label, Table->DirectoryIndex);

    Table->Physical = AllocPhysicalPage();

    if (Table->Physical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] %s region out of physical pages"), Region->Label);
        return FALSE;
    }

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] physical %p mode %u"),
        Region->Label,
        Table->DirectoryIndex,
        Table->Physical,
        (UINT)Table->Mode);

    LINEAR TableLinear = MapTemporaryPhysicalPage3(Table->Physical);

    if (TableLinear == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage3 failed for %s table"), Region->Label);
        FreePhysicalPage(Table->Physical);
        Table->Physical = NULL;
        return FALSE;
    }

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] mapped at %p"),
        Region->Label,
        Table->DirectoryIndex,
        TableLinear);

    LPPAGE_TABLE TableVA = (LPPAGE_TABLE)TableLinear;
    MemorySet(TableVA, 0, PAGE_SIZE);

    switch (Table->Mode) {
    case PAGE_TABLE_POPULATE_IDENTITY:
        for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
            PHYSICAL Physical = Table->Data.Identity.PhysicalBase + ((PHYSICAL)Index << PAGE_SIZE_MUL);

#ifdef PROTECT_BIOS
            if (Table->Data.Identity.ProtectBios) {
                BOOL Protected =
                    (Physical == 0) || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);

                if (Protected) {
                    ClearPageTableEntry(TableVA, Index);
                    continue;
                }
            }
#endif

            WritePageTableEntryValue(
                TableVA,
                Index,
                MakePageTableEntryValue(
                    Physical,
                    Table->ReadWrite,
                    Table->Privilege,
                    /*WriteThrough*/ 0,
                    /*CacheDisabled*/ 0,
                    Table->Global,
                    /*Fixed*/ 1));
        }
        break;

    case PAGE_TABLE_POPULATE_SINGLE_ENTRY:
        WritePageTableEntryValue(
            TableVA,
            Table->Data.Single.TableIndex,
            MakePageTableEntryValue(
                Table->Data.Single.Physical,
                Table->Data.Single.ReadWrite,
                Table->Data.Single.Privilege,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                Table->Data.Single.Global,
                /*Fixed*/ 1));
        break;

    case PAGE_TABLE_POPULATE_EMPTY:
    default:
        break;
    }

    WritePageDirectoryEntryValue(
        Directory,
        Table->DirectoryIndex,
        MakePageDirectoryEntryValue(
            Table->Physical,
            Table->ReadWrite,
            Table->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Table->Global,
            /*Fixed*/ 1));

    U64 DirectoryEntryValue = ReadPageDirectoryEntryValue(Directory, Table->DirectoryIndex);
    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] entry value=%p"),
        Region->Label,
        Table->DirectoryIndex,
        (LINEAR)DirectoryEntryValue);

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] table ready at %p"),
        Region->Label,
        Table->DirectoryIndex,
        Table->Physical);

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] complete"), Region->Label, Table->DirectoryIndex);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Build identity-mapped tables for the low virtual address space.
 * @param Region Region descriptor to populate.
 * @param UserSeedTables Number of empty user tables to pre-allocate.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL SetupLowRegion(REGION_SETUP* Region, UINT UserSeedTables) {
    ResetRegionSetup(Region);

    Region->Label = TEXT("Low");
    Region->PdptIndex = GetPdptEntry(0);
    Region->ReadWrite = 1;
    Region->Privilege = (UserSeedTables != 0u) ? PAGE_PRIVILEGE_USER : PAGE_PRIVILEGE_KERNEL;
    Region->Global = 0;

    DEBUG(TEXT("[SetupLowRegion] Config PdptIndex=%u Privilege=%u UserSeedTables=%u"),
        Region->PdptIndex,
        Region->Privilege,
        UserSeedTables);

    if (EnsureSharedLowTable(&LowRegionSharedTables.BiosTablePhysical, 0, TRUE, TEXT("BIOS")) == FALSE) {
        return FALSE;
    }

    if (EnsureSharedLowTable(
            &LowRegionSharedTables.IdentityTablePhysical,
            ((PHYSICAL)PAGE_TABLE_NUM_ENTRIES << PAGE_SIZE_MUL),
            FALSE,
            TEXT("low identity")) == FALSE) {
        return FALSE;
    }

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();

    DEBUG(TEXT("[SetupLowRegion] PDPT %p, directory %p"), Region->PdptPhysical, Region->DirectoryPhysical);

    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Low region out of physical pages"));
        if (Region->PdptPhysical != NULL) {
            FreePhysicalPage(Region->PdptPhysical);
            Region->PdptPhysical = NULL;
        }
        if (Region->DirectoryPhysical != NULL) {
            FreePhysicalPage(Region->DirectoryPhysical);
            Region->DirectoryPhysical = NULL;
        }
        return FALSE;
    }

    LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(Region->PdptPhysical);

    if (Pdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed for low PDPT"));
        return FALSE;
    }

    DEBUG(TEXT("[SetupLowRegion] PDPT mapped at %p"), Pdpt);

    MemorySet(Pdpt, 0, PAGE_SIZE);

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(Region->DirectoryPhysical);

    if (Directory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed for low directory"));
        return FALSE;
    }

    DEBUG(TEXT("[SetupLowRegion] Directory mapped at %p"), Directory);

    MemorySet(Directory, 0, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        Pdpt,
        Region->PdptIndex,
        MakePageDirectoryEntryValue(
            Region->DirectoryPhysical,
            Region->ReadWrite,
            Region->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Region->Global,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupLowRegion] PDPT[%u] -> %p"), Region->PdptIndex, Region->DirectoryPhysical);

    UINT LowDirectoryIndex = GetDirectoryEntry(0);

    WritePageDirectoryEntryValue(
        Directory,
        LowDirectoryIndex,
        MakePageDirectoryEntryValue(
            LowRegionSharedTables.BiosTablePhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupLowRegion] Directory[%u] -> shared BIOS table %p"),
        LowDirectoryIndex,
        LowRegionSharedTables.BiosTablePhysical);

    WritePageDirectoryEntryValue(
        Directory,
        LowDirectoryIndex + 1u,
        MakePageDirectoryEntryValue(
            LowRegionSharedTables.IdentityTablePhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupLowRegion] Directory[%u] -> shared identity table %p"),
        LowDirectoryIndex + 1u,
        LowRegionSharedTables.IdentityTablePhysical);

    if (UserSeedTables != 0u) {
        UINT TableCapacity = (UINT)(sizeof(Region->Tables) / sizeof(Region->Tables[0]));
        DEBUG(TEXT("[SetupLowRegion] User seed request=%u current=%u capacity=%u region=%p tables=%p"),
            UserSeedTables,
            Region->TableCount,
            TableCapacity,
            Region,
            Region->Tables);

        UINT BaseDirectory = GetDirectoryEntry((U64)VMA_USER);

        for (UINT Index = 0; Index < UserSeedTables; Index++) {
            if (Region->TableCount >= TableCapacity) {
                ERROR(TEXT("[SetupLowRegion] User seed table overflow index=%u count=%u capacity=%u"),
                    Index,
                    Region->TableCount,
                    TableCapacity);
                return FALSE;
            }

            PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];
            DEBUG(TEXT("[SetupLowRegion] Seeding idx=%u count=%u table=%p base=%u"),
                Index,
                Region->TableCount,
                Table,
                BaseDirectory);

            Table->DirectoryIndex = BaseDirectory + Index;
            Table->ReadWrite = 1;
            Table->Privilege = PAGE_PRIVILEGE_USER;
            Table->Global = 0;
            Table->Mode = PAGE_TABLE_POPULATE_EMPTY;
            DEBUG(TEXT("[SetupLowRegion] Preparing user seed table slot=%u"), Table->DirectoryIndex);
            if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) return FALSE;
            DEBUG(TEXT("[SetupLowRegion] Seed slot=%u populated physical=%p"),
                Table->DirectoryIndex,
                Table->Physical);
            Region->TableCount++;
            DEBUG(TEXT("[SetupLowRegion] Table count advanced to %u"), Region->TableCount);
        }
    }

    DEBUG(TEXT("[SetupLowRegion] Completed table count %u (shared bios=%p identity=%p)"),
        Region->TableCount,
        LowRegionSharedTables.BiosTablePhysical,
        LowRegionSharedTables.IdentityTablePhysical);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Compute the number of bytes of kernel memory that must be mapped.
 * @return Size in bytes covered by kernel tables.
 */
static UINT ComputeKernelCoverageBytes(void) {
    PHYSICAL PhysBaseKernel = KernelStartup.KernelPhysicalBase;
    PHYSICAL CoverageEnd = PhysBaseKernel + (PHYSICAL)KernelStartup.KernelSize;

    if (KernelStartup.StackTop > CoverageEnd) {
        CoverageEnd = KernelStartup.StackTop;
    }

    if (CoverageEnd <= PhysBaseKernel) {
        return PAGE_TABLE_CAPACITY;
    }

    PHYSICAL Coverage = CoverageEnd - PhysBaseKernel;
    UINT CoverageBytes = (UINT)PAGE_ALIGN((UINT)Coverage);

    if (CoverageBytes < PAGE_TABLE_CAPACITY) {
        CoverageBytes = PAGE_TABLE_CAPACITY;
    }

    return CoverageBytes;
}

/************************************************************************/

/**
 * @brief Create identity mappings for the kernel virtual address space.
 * @param Region Region descriptor to populate.
 * @param TableCountRequired Number of tables that must be allocated.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL SetupKernelRegion(REGION_SETUP* Region, UINT TableCountRequired) {
    ResetRegionSetup(Region);

    Region->Label = TEXT("Kernel");
    Region->PdptIndex = GetPdptEntry((U64)VMA_KERNEL);
    Region->ReadWrite = 1;
    Region->Privilege = PAGE_PRIVILEGE_KERNEL;
    Region->Global = 0;

    if (TableCountRequired > ARRAY_COUNT(Region->Tables)) {
        ERROR(TEXT("[AllocPageDirectory] Kernel region requires too many tables"));
        return FALSE;
    }

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();

    DEBUG(TEXT("[SetupKernelRegion] PDPT %p, directory %p"), Region->PdptPhysical, Region->DirectoryPhysical);

    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Kernel region out of physical pages"));
        return FALSE;
    }

    LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(Region->PdptPhysical);

    if (Pdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed for kernel PDPT"));
        return FALSE;
    }

    MemorySet(Pdpt, 0, PAGE_SIZE);

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(Region->DirectoryPhysical);

    if (Directory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed for kernel directory"));
        return FALSE;
    }

    MemorySet(Directory, 0, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        Pdpt,
        Region->PdptIndex,
        MakePageDirectoryEntryValue(
            Region->DirectoryPhysical,
            Region->ReadWrite,
            Region->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Region->Global,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupKernelRegion] PDPT[%u] -> %p"), Region->PdptIndex, Region->DirectoryPhysical);

    UINT DirectoryIndex = GetDirectoryEntry((U64)VMA_KERNEL);
    PHYSICAL PhysBaseKernel = KernelStartup.KernelPhysicalBase;

    for (UINT TableIndex = 0; TableIndex < TableCountRequired; TableIndex++) {
        PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];
        Table->DirectoryIndex = DirectoryIndex + TableIndex;
        Table->ReadWrite = 1;
        Table->Privilege = PAGE_PRIVILEGE_KERNEL;
        Table->Global = 0;
        Table->Mode = PAGE_TABLE_POPULATE_IDENTITY;
        Table->Data.Identity.PhysicalBase = PhysBaseKernel + ((PHYSICAL)TableIndex << PAGE_TABLE_CAPACITY_MUL);
        Table->Data.Identity.ProtectBios = FALSE;

        if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) {
            return FALSE;
        }
        Region->TableCount++;
    }

    DEBUG(TEXT("[SetupKernelRegion] Completed table count %u"), Region->TableCount);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Map the user-mode task runner trampoline into the new address space.
 * @param Region Region descriptor to populate.
 * @param TaskRunnerPhysical Physical address of the task runner code.
 * @param TaskRunnerTableIndex Page table index that contains the trampoline.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL SetupTaskRunnerRegion(
    REGION_SETUP* Region,
    PHYSICAL TaskRunnerPhysical,
    UINT TaskRunnerTableIndex) {
    ResetRegionSetup(Region);

    Region->Label = TEXT("TaskRunner");
    Region->PdptIndex = GetPdptEntry((U64)VMA_TASK_RUNNER);
    Region->ReadWrite = 1;
    Region->Privilege = PAGE_PRIVILEGE_USER;
    Region->Global = 0;

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();

    DEBUG(TEXT("[SetupTaskRunnerRegion] PDPT %p, directory %p"), Region->PdptPhysical, Region->DirectoryPhysical);

    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] TaskRunner region out of physical pages"));
        return FALSE;
    }

    LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(Region->PdptPhysical);

    if (Pdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed for TaskRunner PDPT"));
        return FALSE;
    }

    MemorySet(Pdpt, 0, PAGE_SIZE);

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(Region->DirectoryPhysical);

    if (Directory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed for TaskRunner directory"));
        return FALSE;
    }

    MemorySet(Directory, 0, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        Pdpt,
        Region->PdptIndex,
        MakePageDirectoryEntryValue(
            Region->DirectoryPhysical,
            Region->ReadWrite,
            Region->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Region->Global,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupTaskRunnerRegion] PDPT[%u] -> %p"), Region->PdptIndex, Region->DirectoryPhysical);

    PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];
    Table->DirectoryIndex = GetDirectoryEntry((U64)VMA_TASK_RUNNER);
    Table->ReadWrite = 1;
    Table->Privilege = PAGE_PRIVILEGE_USER;
    Table->Global = 0;
    Table->Mode = PAGE_TABLE_POPULATE_SINGLE_ENTRY;
    Table->Data.Single.TableIndex = TaskRunnerTableIndex;
    Table->Data.Single.Physical = TaskRunnerPhysical;
    Table->Data.Single.ReadWrite = 0;
    Table->Data.Single.Privilege = PAGE_PRIVILEGE_USER;
    Table->Data.Single.Global = 0;

    if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) {
        return FALSE;
    }

    Region->TableCount++;
    DEBUG(TEXT("[SetupTaskRunnerRegion] Completed table count %u"), Region->TableCount);
    return TRUE;
}

/************************************************************************/

/*
static U64 ReadTableEntrySnapshot(PHYSICAL TablePhysical, UINT Index) {
    if (TablePhysical == NULL) {
        return 0;
    }

    LINEAR Linear = MapTemporaryPhysicalPage3(TablePhysical);

    if (Linear == NULL) {
        return 0;
    }

    return ReadPageTableEntryValue((LPPAGE_TABLE)Linear, Index);
}
*/

/**
 * @brief Build the kernel-mode long mode paging hierarchy.
 *
 * Low, kernel and task runner regions are prepared, connected to a newly
 * allocated PML4 and the recursive slot is configured before returning the
 * physical address.
 *
 * @return Physical address of the allocated PML4, or NULL on failure.
 */
PHYSICAL AllocPageDirectory(void) {
    REGION_SETUP LowRegion;
    REGION_SETUP KernelRegion;
    REGION_SETUP TaskRunnerRegion;
    PHYSICAL Pml4Physical = NULL;
    BOOL Success = FALSE;

    DEBUG(TEXT("[AllocPageDirectory] Enter"));

    if (EnsureCurrentStackSpace(N_32KB) == FALSE) {
        ERROR(TEXT("[AllocPageDirectory] Unable to ensure stack availability"));
        return NULL;
    }

    ResetRegionSetup(&LowRegion);
    ResetRegionSetup(&KernelRegion);
    ResetRegionSetup(&TaskRunnerRegion);

    UINT LowPml4Index = GetPml4Entry(0);
    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);
    UINT TaskRunnerPml4Index = GetPml4Entry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerTableIndex = GetTableEntry((U64)VMA_TASK_RUNNER);

    UINT KernelCoverageBytes = ComputeKernelCoverageBytes();
    UINT KernelTableCount = KernelCoverageBytes >> PAGE_TABLE_CAPACITY_MUL;
    if (KernelTableCount == 0u) KernelTableCount = 1u;

    if (SetupLowRegion(&LowRegion, 0u) == FALSE) goto Out;
    DEBUG(TEXT("[AllocPageDirectory] Low region tables=%u"), LowRegion.TableCount);

    if (SetupKernelRegion(&KernelRegion, KernelTableCount) == FALSE) goto Out;
    DEBUG(TEXT("[AllocPageDirectory] Kernel region tables=%u"), KernelRegion.TableCount);

    LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = KernelToPhysical(TaskRunnerLinear);

    DEBUG(TEXT("[AllocPageDirectory] TaskRunnerPhysical = %p + (%p - %p) = %p"),
        KernelStartup.KernelPhysicalBase,
        TaskRunnerLinear,
        VMA_KERNEL,
        TaskRunnerPhysical);

    if (SetupTaskRunnerRegion(&TaskRunnerRegion, TaskRunnerPhysical, TaskRunnerTableIndex) == FALSE) goto Out;
    DEBUG(TEXT("[AllocPageDirectory] TaskRunner tables=%u"), TaskRunnerRegion.TableCount);

    Pml4Physical = AllocPhysicalPage();

    if (Pml4Physical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Out of physical pages"));
        goto Out;
    }

    LINEAR Pml4Linear = MapTemporaryPhysicalPage1(Pml4Physical);

    if (Pml4Linear == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed on PML4"));
        goto Out;
    }

    LPPAGE_DIRECTORY Pml4 = (LPPAGE_DIRECTORY)Pml4Linear;
    MemorySet(Pml4, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] PML4 mapped at %p"), Pml4);

    WritePageDirectoryEntryValue(
        Pml4,
        LowPml4Index,
        MakePageDirectoryEntryValue(
            LowRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            LowRegion.Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Pml4,
        KernelPml4Index,
        MakePageDirectoryEntryValue(
            KernelRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Pml4,
        TaskRunnerPml4Index,
        MakePageDirectoryEntryValue(
            TaskRunnerRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        Pml4,
        PML4_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            Pml4Physical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    U64 LowEntry = ReadPageDirectoryEntryValue(Pml4, LowPml4Index);
    U64 KernelEntry = ReadPageDirectoryEntryValue(Pml4, KernelPml4Index);
    U64 TaskEntry = ReadPageDirectoryEntryValue(Pml4, TaskRunnerPml4Index);
    U64 RecursiveEntry = ReadPageDirectoryEntryValue(Pml4, PML4_RECURSIVE_SLOT);

    DEBUG(TEXT("[AllocPageDirectory] PML4 entries set (low=%p, kernel=%p, task=%p, recursive=%p)"),
        (LINEAR)LowEntry,
        (LINEAR)KernelEntry,
        (LINEAR)TaskEntry,
        (LINEAR)RecursiveEntry);

    FlushTLB();

    Success = TRUE;

Out:
    if (!Success) {
        if (Pml4Physical != NULL) {
            FreePhysicalPage(Pml4Physical);
        }
        ReleaseRegionSetup(&LowRegion);
        ReleaseRegionSetup(&KernelRegion);
        ReleaseRegionSetup(&TaskRunnerRegion);
        return NULL;
    }

    DEBUG(TEXT("[AllocPageDirectory] Exit"));
    return Pml4Physical;
}

/************************************************************************/

/**
 * @brief Create a user-mode page directory derived from the current context.
 *
 * Kernel mappings are cloned from the active CR3 while the low region is
 * seeded with identity tables and the task runner trampoline is prepared as
 * needed.
 *
 * @return Physical address of the allocated PML4, or NULL on failure.
 */
PHYSICAL AllocUserPageDirectory(void) {
    REGION_SETUP LowRegion;
    REGION_SETUP KernelRegion;
    REGION_SETUP TaskRunnerRegion;
    PHYSICAL Pml4Physical = NULL;
    BOOL Success = FALSE;
    BOOL TaskRunnerReused = FALSE;

    DEBUG(TEXT("[AllocUserPageDirectory] Enter"));

    if (EnsureCurrentStackSpace(N_32KB) == FALSE) {
        ERROR(TEXT("[AllocUserPageDirectory] Unable to ensure stack availability"));
        return NULL;
    }

    ResetRegionSetup(&LowRegion);
    ResetRegionSetup(&KernelRegion);
    ResetRegionSetup(&TaskRunnerRegion);

    UINT LowPml4Index = GetPml4Entry(0);
    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);
    UINT TaskRunnerPml4Index = GetPml4Entry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerTableIndex = GetTableEntry((U64)VMA_TASK_RUNNER);

    if (SetupLowRegion(&LowRegion, USERLAND_SEEDED_TABLES) == FALSE) goto Out;
    DEBUG(TEXT("[AllocUserPageDirectory] Low region tables=%u"), LowRegion.TableCount);

    Pml4Physical = AllocPhysicalPage();

    if (Pml4Physical == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] Out of physical pages"));
        goto Out;
    }

    LINEAR Pml4Linear = MapTemporaryPhysicalPage1(Pml4Physical);

    if (Pml4Linear == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] MapTemporaryPhysicalPage1 failed on PML4"));
        goto Out;
    }

    LPPAGE_DIRECTORY Pml4 = (LPPAGE_DIRECTORY)Pml4Linear;
    MemorySet(Pml4, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocUserPageDirectory] PML4 mapped at %p"), Pml4);

    LPPML4 CurrentPml4 = GetCurrentPml4VA();
    if (CurrentPml4 == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] Current PML4 pointer is NULL"));
        goto Out;
    }

    WritePageDirectoryEntryValue(
        Pml4,
        LowPml4Index,
        MakePageDirectoryEntryValue(
            LowRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            LowRegion.Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    UINT KernelBaseIndex = PML4_ENTRY_COUNT / 2u;
    UINT ClonedKernelEntries = 0u;
    for (UINT Index = KernelBaseIndex; Index < PML4_ENTRY_COUNT; Index++) {
        if (Index == PML4_RECURSIVE_SLOT) continue;

        U64 EntryValue = ReadPageDirectoryEntryValue(CurrentPml4, Index);
        if ((EntryValue & PAGE_FLAG_PRESENT) == 0) continue;

        WritePageDirectoryEntryValue(Pml4, Index, EntryValue);
        ClonedKernelEntries++;
    }

    if (ClonedKernelEntries == 0u) {
        ERROR(TEXT("[AllocUserPageDirectory] No kernel PML4 entries copied from current directory"));
        goto Out;
    }

    DEBUG(TEXT("[AllocUserPageDirectory] Cloned %u kernel PML4 entries from index %u"),
        ClonedKernelEntries,
        KernelBaseIndex);

    U64 TaskRunnerEntryValue = ReadPageDirectoryEntryValue(CurrentPml4, TaskRunnerPml4Index);
    if ((TaskRunnerEntryValue & PAGE_FLAG_PRESENT) != 0 && (TaskRunnerEntryValue & PAGE_FLAG_USER) != 0) {
        TaskRunnerReused = TRUE;
        DEBUG(TEXT("[AllocUserPageDirectory] Reusing existing task runner entry %p from current CR3"),
            (LINEAR)TaskRunnerEntryValue);
    } else {
        LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
        PHYSICAL TaskRunnerPhysical = KernelToPhysical(TaskRunnerLinear);

        DEBUG(TEXT("[AllocUserPageDirectory] TaskRunnerPhysical = %p + (%p - %p) = %p"),
            KernelStartup.KernelPhysicalBase,
            TaskRunnerLinear,
            VMA_KERNEL,
            TaskRunnerPhysical);

        if (SetupTaskRunnerRegion(&TaskRunnerRegion, TaskRunnerPhysical, TaskRunnerTableIndex) == FALSE) goto Out;
        DEBUG(TEXT("[AllocUserPageDirectory] TaskRunner tables=%u"), TaskRunnerRegion.TableCount);
        DEBUG(TEXT("[AllocUserPageDirectory] Regions low(pdpt=%p dir=%p priv=%u tables=%u) kernel(reuse existing) task(pdpt=%p dir=%p)"),
            LowRegion.PdptPhysical,
            LowRegion.DirectoryPhysical,
            LowRegion.Privilege,
            LowRegion.TableCount,
            TaskRunnerRegion.PdptPhysical,
            TaskRunnerRegion.DirectoryPhysical);

        TaskRunnerEntryValue = MakePageDirectoryEntryValue(
            TaskRunnerRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1);
    }

    WritePageDirectoryEntryValue(Pml4, TaskRunnerPml4Index, TaskRunnerEntryValue);

    if (!TaskRunnerReused) {
        LINEAR TaskRunnerDirectoryLinear = MapTemporaryPhysicalPage2(TaskRunnerRegion.DirectoryPhysical);
        LINEAR TaskRunnerTableLinear = MapTemporaryPhysicalPage3(TaskRunnerRegion.Tables[0].Physical);

        if (TaskRunnerDirectoryLinear != NULL && TaskRunnerTableLinear != NULL) {
            UINT TaskRunnerDirectoryIndex = GetDirectoryEntry((U64)VMA_TASK_RUNNER);
            U64 TaskDirectoryEntry =
                ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)TaskRunnerDirectoryLinear, TaskRunnerDirectoryIndex);
            U64 TaskTableEntry = ReadPageTableEntryValue((LPPAGE_TABLE)TaskRunnerTableLinear, TaskRunnerTableIndex);

            DEBUG(TEXT("[AllocUserPageDirectory] TaskRunner PDE[%u]=%p PTE[%u]=%p"),
                TaskRunnerDirectoryIndex,
                (LINEAR)TaskDirectoryEntry,
                TaskRunnerTableIndex,
                (LINEAR)TaskTableEntry);
        } else {
            ERROR(TEXT("[AllocUserPageDirectory] Unable to map TaskRunner directory/table snapshot"));
        }
    } else {
        DEBUG(TEXT("[AllocUserPageDirectory] Task runner entry reused without rebuilding tables"));
    }

    WritePageDirectoryEntryValue(
        Pml4,
        PML4_RECURSIVE_SLOT,
        MakePageDirectoryEntryValue(
            Pml4Physical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    U64 LowEntry = ReadPageDirectoryEntryValue(Pml4, LowPml4Index);
    U64 KernelEntry = ReadPageDirectoryEntryValue(Pml4, KernelPml4Index);
    U64 TaskEntry = ReadPageDirectoryEntryValue(Pml4, TaskRunnerPml4Index);
    U64 RecursiveEntry = ReadPageDirectoryEntryValue(Pml4, PML4_RECURSIVE_SLOT);

    DEBUG(TEXT("[AllocUserPageDirectory] PML4 entries set (low=%p, kernel=%p, task=%p, recursive=%p)"),
        (LINEAR)LowEntry,
        (LINEAR)KernelEntry,
        (LINEAR)TaskEntry,
        (LINEAR)RecursiveEntry);

    LogPageDirectory64(Pml4Physical);

    FlushTLB();

    Success = TRUE;

Out:
    if (!Success) {
        if (Pml4Physical != NULL) {
            FreePhysicalPage(Pml4Physical);
        }
        ReleaseRegionSetup(&LowRegion);
        ReleaseRegionSetup(&KernelRegion);
        ReleaseRegionSetup(&TaskRunnerRegion);
        return NULL;
    }

    DEBUG(TEXT("[AllocUserPageDirectory] Exit"));
    return Pml4Physical;
}

/************************************************************************/

/**
 * @brief Initialize the x86-64 memory manager and install the kernel mappings.
 *
 * The routine prepares the physical page bitmap, constructs a new kernel page
 * directory, loads it and finalizes the descriptor tables required for long
 * mode execution.
 */
void InitializeMemoryManager(void) {
    DEBUG(TEXT("[InitializeMemoryManager] Enter"));

    DEBUG(TEXT("[InitializeMemoryManager] Temp pages reserved: %p, %p, %p"),
        (LPVOID)(LINEAR)X86_64_TEMP_LINEAR_PAGE_1,
        (LPVOID)(LINEAR)X86_64_TEMP_LINEAR_PAGE_2,
        (LPVOID)(LINEAR)X86_64_TEMP_LINEAR_PAGE_3);

    UpdateKernelMemoryMetricsFromMultibootMap();

    if (KernelStartup.PageCount == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    UINT BitmapBytes = (KernelStartup.PageCount + 7) >> MUL_8;
    UINT BitmapBytesAligned = (UINT)PAGE_ALIGN(BitmapBytes);

    U64 KernelSpan = (U64)KernelStartup.KernelSize + (U64)N_512KB;
    PHYSICAL MapSize = (PHYSICAL)PAGE_ALIGN(KernelSpan);
    U64 TotalPages = (MapSize + PAGE_SIZE - 1) >> PAGE_SIZE_MUL;
    U64 TablesRequired = (TotalPages + (U64)PAGE_TABLE_NUM_ENTRIES - 1) / (U64)PAGE_TABLE_NUM_ENTRIES;
    PHYSICAL TablesSize = (PHYSICAL)(TablesRequired * (U64)PAGE_TABLE_SIZE);
    PHYSICAL LoaderReservedEnd = KernelStartup.KernelPhysicalBase + MapSize + TablesSize;
    PHYSICAL PpbPhysical = PAGE_ALIGN(LoaderReservedEnd);

    Kernel.PPB = (LPPAGEBITMAP)(UINT)PpbPhysical;
    Kernel.PPBSize = BitmapBytesAligned;

    DEBUG(TEXT("[InitializeMemoryManager] Kernel.PPB physical base: %p"), (LINEAR)Kernel.PPB);
    DEBUG(TEXT("[InitializeMemoryManager] Kernel.PPB size: %x"), Kernel.PPBSize);

    MemorySet(Kernel.PPB, 0, Kernel.PPBSize);

    MarkUsedPhysicalMemory();

    if (KernelStartup.MemorySize == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    PHYSICAL NewPageDirectory = AllocPageDirectory();

    DEBUG(TEXT("[InitializeMemoryManager] New page directory: %p"), (LINEAR)NewPageDirectory);

    if (NewPageDirectory == NULL) {
        ERROR(TEXT("[InitializeMemoryManager] AllocPageDirectory failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    LoadPageDirectory(NewPageDirectory);

    FlushTLB();

    LogPageDirectory64(NewPageDirectory);

    DEBUG(TEXT("[InitializeMemoryManager] TLB flushed"));

    Kernel_i386.GDT = (LPVOID)AllocKernelRegion(0, GDT_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Kernel_i386.GDT == NULL) {
        ERROR(TEXT("[InitializeMemoryManager] AllocRegion for GDT failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    InitializeGlobalDescriptorTable((LPSEGMENT_DESCRIPTOR)Kernel_i386.GDT);

    LogGlobalDescriptorTable((LPSEGMENT_DESCRIPTOR)Kernel_i386.GDT, 10);

    DEBUG(TEXT("[InitializeMemoryManager] Loading GDT"));

    LoadGlobalDescriptorTable((PHYSICAL)Kernel_i386.GDT, GDT_SIZE - 1);

    DEBUG(TEXT("[InitializeMemoryManager] Exit"));
}

/************************************************************************/

/**
 * @brief Resolve a canonical linear address to its physical counterpart.
 *
 * The lookup walks the paging hierarchy, accounting for large pages, and
 * returns the physical address when the mapping exists.
 *
 * @param Address Linear address to translate.
 * @return Physical address of the resolved page, or 0 when unmapped.
 */
PHYSICAL MapLinearToPhysical(LINEAR Address) {
    Address = CanonicalizeLinearAddress(Address);

    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Address);
    UINT Pml4Index = MemoryPageIteratorGetPml4Index(&Iterator);
    UINT PdptIndex = MemoryPageIteratorGetPdptIndex(&Iterator);
    UINT DirIndex = MemoryPageIteratorGetDirectoryIndex(&Iterator);
    UINT TabIndex = MemoryPageIteratorGetTableIndex(&Iterator);

    LPPML4 Pml4 = GetCurrentPml4VA();
    U64 Pml4EntryValue = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)Pml4, Pml4Index);
    if ((Pml4EntryValue & PAGE_FLAG_PRESENT) == 0) return 0;

    PHYSICAL PdptPhysical = (PHYSICAL)(Pml4EntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY PdptLinear = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(PdptPhysical);
    U64 PdptEntryValue = ReadPageDirectoryEntryValue(PdptLinear, PdptIndex);
    if ((PdptEntryValue & PAGE_FLAG_PRESENT) == 0) return 0;

    if ((PdptEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
        PHYSICAL LargeBase = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
        return (PHYSICAL)(LargeBase | (Address & (N_1GB - 1)));
    }

    PHYSICAL DirectoryPhysical = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY DirectoryLinear = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(DirectoryPhysical);
    U64 DirectoryEntryValue = ReadPageDirectoryEntryValue(DirectoryLinear, DirIndex);
    if ((DirectoryEntryValue & PAGE_FLAG_PRESENT) == 0) return 0;

    if ((DirectoryEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
        PHYSICAL LargeBase = (PHYSICAL)(DirectoryEntryValue & PAGE_MASK);
        return (PHYSICAL)(LargeBase | (Address & (N_2MB - 1)));
    }

    LPPAGE_TABLE Table = MemoryPageIteratorGetTable(&Iterator);
    if (!PageTableEntryIsPresent(Table, TabIndex)) return 0;

    PHYSICAL PagePhysical = PageTableEntryGetPhysical(Table, TabIndex);
    if (PagePhysical == 0) return 0;

    return (PHYSICAL)(PagePhysical | (Address & (PAGE_SIZE - 1)));
}

/************************************************************************/

/**
 * @brief Check if a linear address is mapped and accessible.
 * @param Address Linear address to test.
 * @return TRUE if the address resolves to a present page table entry.
 */
BOOL IsValidMemory(LINEAR Address) {
    LINEAR Canonical = CanonicalizeLinearAddress(Address);

    if (Canonical != Address) {
        return FALSE;
    }

    return MapLinearToPhysical(Canonical) != 0;
}
