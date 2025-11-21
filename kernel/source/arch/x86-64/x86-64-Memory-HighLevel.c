
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


    x86-64 memory high-level orchestration

\************************************************************************/


#include "arch/x86-64/x86-64-Memory-Internal.h"

/************************************************************************/

#define MEMORY_MANAGER_VER_MAJOR 1
#define MEMORY_MANAGER_VER_MINOR 0

static UINT MemoryManagerCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION MemoryManagerDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_OTHER,
    .VersionMajor = MEMORY_MANAGER_VER_MAJOR,
    .VersionMinor = MEMORY_MANAGER_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "MemoryManager",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = MemoryManagerCommands};

/************************************************************************/

typedef enum {
    PAGE_TABLE_POPULATE_IDENTITY,
    PAGE_TABLE_POPULATE_SINGLE_ENTRY,
    PAGE_TABLE_POPULATE_EMPTY
} PAGE_TABLE_POPULATE_MODE;

#define USERLAND_SEEDED_TABLES 1

typedef struct tag_PAGE_TABLE_SETUP {
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
} PAGE_TABLE_SETUP, *LPPAGE_TABLE_SETUP;

typedef struct tag_REGION_SETUP {
    LPCSTR Label;
    UINT PdptIndex;
    U32 ReadWrite;
    U32 Privilege;
    U32 Global;
    PHYSICAL PdptPhysical;
    PHYSICAL DirectoryPhysical;
    PAGE_TABLE_SETUP Tables[64];
    UINT TableCount;
} REGION_SETUP, *LPREGION_SETUP;

/************************************************************************/

typedef struct tag_LOW_REGION_SHARED_TABLES {
    PHYSICAL BiosTablePhysical;
    PHYSICAL IdentityTablePhysical;
} LOW_REGION_SHARED_TABLES;

LOW_REGION_SHARED_TABLES LowRegionSharedTables = {
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
BOOL EnsureSharedLowTable(
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
void ResetRegionSetup(REGION_SETUP* Region) {
    MemorySet(Region, 0, sizeof(REGION_SETUP));
}

/************************************************************************/

/**
 * @brief Release the physical resources owned by a REGION_SETUP.
 * @param Region Structure that tracks the allocated tables.
 */
void ReleaseRegionSetup(REGION_SETUP* Region) {
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
BOOL AllocateTableAndPopulate(
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
BOOL SetupLowRegion(REGION_SETUP* Region, UINT UserSeedTables) {
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
UINT ComputeKernelCoverageBytes(void) {
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
BOOL SetupKernelRegion(REGION_SETUP* Region, UINT TableCountRequired) {
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
BOOL SetupTaskRunnerRegion(
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
U64 ReadTableEntrySnapshot(PHYSICAL TablePhysical, UINT Index) {
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

/************************************************************************/

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
 * @brief Handles driver commands for the memory manager.
 *
 * DF_LOAD initializes the memory manager and marks the driver as ready.
 * DF_UNLOAD clears the ready flag; no shutdown routine is available.
 *
 * @param Function Driver command selector.
 * @param Parameter Unused.
 * @return DF_ERROR_SUCCESS on success, DF_ERROR_NOTIMPL otherwise.
 */
static UINT MemoryManagerCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((MemoryManagerDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_ERROR_SUCCESS;
            }

            InitializeMemoryManager();
            MemoryManagerDriver.Flags |= DRIVER_FLAG_READY;
            return DF_ERROR_SUCCESS;

        case DF_UNLOAD:
            if ((MemoryManagerDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_ERROR_SUCCESS;
            }

            MemoryManagerDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_ERROR_SUCCESS;

        case DF_GETVERSION:
            return MAKE_VERSION(MEMORY_MANAGER_VER_MAJOR, MEMORY_MANAGER_VER_MINOR);
    }

    return DF_ERROR_NOTIMPL;
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

    InitializeRegionDescriptorTracking();

    DEBUG(TEXT("[InitializeMemoryManager] Exit"));
}

/************************************************************************/

/**
 * @brief Find a free linear region starting from a base address.
 * @param StartBase Starting linear address.
 * @param Size Desired region size.
 * @return Base of free region or 0.
 */
LINEAR FindFreeRegion(LINEAR StartBase, UINT Size) {
    LINEAR Base = N_4MB;

    if (StartBase != 0) {
        LINEAR CanonStart = CanonicalizeLinearAddress(StartBase);
        if (CanonStart >= Base) {
            Base = CanonStart;
        }
    }

    while (TRUE) {
        if (IsRegionFree(Base, Size) == TRUE) {
            return Base;
        }

        LINEAR NextBase = CanonicalizeLinearAddress(Base + PAGE_SIZE);
        if (NextBase <= Base) {
            return NULL;
        }
        Base = NextBase;
    }
}

/************************************************************************/

/**
 * @brief Release page tables that no longer contain mappings.
 */
void FreeEmptyPageTables(void) {
    LPPML4 Pml4 = GetCurrentPml4VA();
    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);

    for (UINT Pml4Index = 0u; Pml4Index < KernelPml4Index; Pml4Index++) {
        if (Pml4Index == PML4_RECURSIVE_SLOT) {
            continue;
        }

        U64 Pml4EntryValue = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)Pml4, Pml4Index);
        if ((Pml4EntryValue & PAGE_FLAG_PRESENT) == 0u) {
            continue;
        }
        if ((Pml4EntryValue & PAGE_FLAG_PAGE_SIZE) != 0u) {
            continue;
        }

        PHYSICAL PdptPhysical = (PHYSICAL)(Pml4EntryValue & PAGE_MASK);
        LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(PdptPhysical);

        for (UINT PdptIndex = 0u; PdptIndex < PAGE_TABLE_NUM_ENTRIES; PdptIndex++) {
            U64 PdptEntryValue = ReadPageDirectoryEntryValue(Pdpt, PdptIndex);
            if ((PdptEntryValue & PAGE_FLAG_PRESENT) == 0u) {
                continue;
            }
            if ((PdptEntryValue & PAGE_FLAG_PAGE_SIZE) != 0u) {
                continue;
            }

            PHYSICAL DirectoryPhysical = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
            LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(DirectoryPhysical);

            for (UINT DirIndex = 0u; DirIndex < PAGE_TABLE_NUM_ENTRIES; DirIndex++) {
                U64 DirectoryEntryValue = ReadPageDirectoryEntryValue(Directory, DirIndex);
                if ((DirectoryEntryValue & PAGE_FLAG_PRESENT) == 0u) {
                    continue;
                }
                if ((DirectoryEntryValue & PAGE_FLAG_PAGE_SIZE) != 0u) {
                    continue;
                }

                PHYSICAL TablePhysical = (PHYSICAL)(DirectoryEntryValue & PAGE_MASK);
                if (TablePhysical == 0u) {
                    continue;
                }

                LPPAGE_TABLE Table = (LPPAGE_TABLE)MapTemporaryPhysicalPage3(TablePhysical);
                if (Table == NULL) {
                    ERROR(TEXT("[FreeEmptyPageTables] Failed to map table PML4=%u PDPT=%u Dir=%u phys=%p"),
                        Pml4Index, PdptIndex, DirIndex, (LPVOID)TablePhysical);
                    continue;
                }

                if (PageTableIsEmpty(Table)) {
                    DEBUG(TEXT("[FreeEmptyPageTables] Clearing PML4=%u PDPT=%u Dir=%u tablePhys=%p"),
                        Pml4Index, PdptIndex, DirIndex, (LPVOID)TablePhysical);
                    SetPhysicalPageMark((UINT)(TablePhysical >> PAGE_SIZE_MUL), 0u);
                    ClearPageDirectoryEntry(Directory, DirIndex);
                }
            }
        }
    }
}

/************************************************************************/

BOOL PopulateRegionPagesLegacy(LINEAR Base,
                                      PHYSICAL Target,
                                      UINT NumPages,
                                      U32 Flags,
                                      LINEAR RollbackBase,
                                      LPCSTR FunctionName) {
    LPPAGE_TABLE Table = NULL;
    PHYSICAL Physical = NULL;
    U32 ReadWrite = (Flags & ALLOC_PAGES_READWRITE) ? 1 : 0;
    U32 PteCacheDisabled = (Flags & ALLOC_PAGES_UC) ? 1 : 0;
    U32 PteWriteThrough = (Flags & ALLOC_PAGES_WC) ? 1 : 0;

    if (PteCacheDisabled) PteWriteThrough = 0;

    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Base);

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);
        LINEAR CurrentLinear = MemoryPageIteratorGetLinear(&Iterator);

        BOOL IsLargePage = FALSE;

        if (!TryGetPageTableForIterator(&Iterator, &Table, &IsLargePage)) {
            if (IsLargePage) {
                BOOL PreviousBootstrap = G_RegionDescriptorBootstrap;
                G_RegionDescriptorBootstrap = TRUE;
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                G_RegionDescriptorBootstrap = PreviousBootstrap;
                return FALSE;
            }

            if (AllocPageTable(CurrentLinear) == NULL) {
                BOOL PreviousBootstrap = G_RegionDescriptorBootstrap;
                G_RegionDescriptorBootstrap = TRUE;
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                G_RegionDescriptorBootstrap = PreviousBootstrap;
                return FALSE;
            }

            if (!TryGetPageTableForIterator(&Iterator, &Table, NULL)) {
                BOOL PreviousBootstrap = G_RegionDescriptorBootstrap;
                G_RegionDescriptorBootstrap = TRUE;
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                G_RegionDescriptorBootstrap = PreviousBootstrap;
                return FALSE;
            }
        }

        U32 Privilege = PAGE_PRIVILEGE(CurrentLinear);
        U32 FixedFlag = (Flags & ALLOC_PAGES_IO) ? 1u : 0u;
        U32 BaseFlags = BuildPageFlags(ReadWrite, Privilege, PteWriteThrough, PteCacheDisabled, 0, FixedFlag);
        U32 ReservedFlags = BaseFlags & ~PAGE_FLAG_PRESENT;
        PHYSICAL ReservedPhysical = (PHYSICAL)(MAX_U32 & ~(PAGE_SIZE - 1));

        WritePageTableEntryValue(Table, TabEntry, MakePageEntryRaw(ReservedPhysical, ReservedFlags));

        if (Flags & ALLOC_PAGES_COMMIT) {
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
                    SetPhysicalPageMark((UINT)(Physical >> PAGE_SIZE_MUL), 1);
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
                    BOOL PreviousBootstrap = G_RegionDescriptorBootstrap;
                    G_RegionDescriptorBootstrap = TRUE;
                    FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                    G_RegionDescriptorBootstrap = PreviousBootstrap;
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
        Base += PAGE_SIZE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Allocate and map a physical region into the linear address space.
 * @param Base Desired base address or 0. When zero and ALLOC_PAGES_AT_OR_OVER
 *             is not set, the allocator picks any free region.
 * @param Target Desired physical base address or 0. Requires
 *               ALLOC_PAGES_COMMIT when specified. Use with ALLOC_PAGES_IO to
 *               map device memory without touching the physical bitmap.
 * @param Size Size in bytes, rounded up to page granularity. Limited to 25% of
 *             the available physical memory.
 * @param Flags Mapping flags:
 *              - ALLOC_PAGES_COMMIT: allocate and map backing pages.
 *              - ALLOC_PAGES_READWRITE: request writable pages (read-only
 *                otherwise).
 *              - ALLOC_PAGES_AT_OR_OVER: accept any region starting at or
 *                above Base.
 *              - ALLOC_PAGES_UC / ALLOC_PAGES_WC: control cache attributes
 *                (UC has priority over WC).
 *              - ALLOC_PAGES_IO: keep physical pages marked fixed for MMIO.
 * @return Allocated linear base address or 0 on failure.
 */
LINEAR AllocRegion(LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags) {
    LINEAR Pointer = NULL;
    UINT NumPages = 0;
    DEBUG(TEXT("[AllocRegion] Enter: Base=%x Target=%x Size=%x Flags=%x"), Base, Target, Size, Flags);

    // Can't allocate more than 25% of total memory at once
    if (Size > KernelStartup.MemorySize / 4) {
        ERROR(TEXT("[AllocRegion] Size %x exceeds 25%% of memory (%lX)"), Size, KernelStartup.MemorySize / 4);
        return NULL;
    }

    // Rounding behavior for page count
    NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;  // ceil(Size / 4096)
    if (NumPages == 0) NumPages = 1;

    Base = CanonicalizeLinearAddress(Base);

    // If an exact physical mapping is requested, validate inputs
    if (Target != 0) {
        if ((Target & (PAGE_SIZE - 1)) != 0) {
            ERROR(TEXT("[AllocRegion] Target not page-aligned (%x)"), Target);
            return NULL;
        }

        if ((Flags & ALLOC_PAGES_IO) == 0 && (Flags & ALLOC_PAGES_COMMIT) == 0) {
            ERROR(TEXT("[AllocRegion] Exact PMA mapping requires COMMIT"));
            return NULL;
        }

        if (ValidatePhysicalTargetRange(Target, NumPages) == FALSE) {
            ERROR(TEXT("[AllocRegion] Target range cannot be addressed"));
            return NULL;
        }
        /* NOTE: Do not reject pages already marked used here.
           Target may come from AllocPhysicalPage(), which marks the page in the bitmap.
           We will just map it and keep the mark consistent. */
    }

    /* If the calling process requests that a linear address be mapped,
       see if the region is not already allocated. */
    if (Base != 0 && (Flags & ALLOC_PAGES_AT_OR_OVER) == 0) {
        if (IsRegionFree(Base, Size) == FALSE) {
            DEBUG(TEXT("[AllocRegion] No free region found with specified base : %x"), Base);
            return NULL;
        }
    }

    /* If the calling process does not care about the base address of
       the region, try to find a region which is at least as large as
       the "Size" parameter. */
    if (Base == 0 || (Flags & ALLOC_PAGES_AT_OR_OVER)) {
        DEBUG(TEXT("[AllocRegion] Calling FindFreeRegion with base = %x and size = %x"), Base, Size);

        LINEAR NewBase = FindFreeRegion(Base, Size);

        if (NewBase == NULL) {
            DEBUG(TEXT("[AllocRegion] No free region found with unspecified base from %x"), Base);
            return NULL;
        }

        Base = NewBase;

        DEBUG(TEXT("[AllocRegion] FindFreeRegion found with base = %x and size = %x"), Base, Size);
    }

    // Set the return value to "Base".
    Pointer = Base;

    DEBUG(TEXT("[AllocRegion] Allocating pages"));

#if EXOS_X86_64_FAST_VMM
    BOOL FastPathUsed = FALSE;
    if (G_RegionDescriptorsEnabled && G_RegionDescriptorBootstrap == FALSE) {
        MEMORY_REGION_DESCRIPTOR TempDescriptor;
        InitializeTransientDescriptor(&TempDescriptor, Pointer, NumPages, Target, Flags);

        UINT PagesProcessed = 0u;
        if (FastPopulateRegionFromDescriptor(&TempDescriptor,
                                             Target,
                                             Flags,
                                             TEXT("AllocRegion"),
                                             &PagesProcessed) == TRUE &&
            PagesProcessed == NumPages) {
            FastPathUsed = TRUE;
        } else {
            if (PagesProcessed != 0u) {
                MEMORY_REGION_DESCRIPTOR RollbackDescriptor;
                InitializeTransientDescriptor(&RollbackDescriptor, Pointer, PagesProcessed, Target, Flags);
                if (FastReleaseRegionFromDescriptor(&RollbackDescriptor, NULL) == FALSE) {
                    WARNING(TEXT("[AllocRegion] Fast rollback failed for base=%p pages=%u"),
                        (LPVOID)Pointer,
                        PagesProcessed);
                }
            }

            DEBUG(TEXT("[AllocRegion] Falling back to legacy population (processed=%u targetPages=%u)"),
                PagesProcessed,
                NumPages);
        }
    }
#else
    BOOL FastPathUsed = FALSE;
#endif

    if (FastPathUsed == FALSE) {
        if (PopulateRegionPagesLegacy(Base, Target, NumPages, Flags, Pointer, TEXT("AllocRegion")) == FALSE) {
            return NULL;
        }
    }

    if (G_RegionDescriptorsEnabled && G_RegionDescriptorBootstrap == FALSE) {
        if (RegisterRegionDescriptor(Pointer, NumPages, Target, Flags) == FALSE) {
            G_RegionDescriptorBootstrap = TRUE;
            FreeRegion(Pointer, NumPages << PAGE_SIZE_MUL);
            G_RegionDescriptorBootstrap = FALSE;
            return NULL;
        }
    }

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

    DEBUG(TEXT("[AllocRegion] Exit"));

    return Pointer;
}

/************************************************************************/

/**
 * @brief Resize an existing linear region.
 * @param Base Base linear address of the region.
 * @param Target Physical base address or 0. Must match the existing mapping
 *               when resizing committed regions.
 * @param Size Current size in bytes.
 * @param NewSize Desired size in bytes.
 * @param Flags Mapping flags used for the region (see AllocRegion).
 * @return TRUE on success, FALSE otherwise.
 */
BOOL ResizeRegion(LINEAR Base, PHYSICAL Target, UINT Size, UINT NewSize, U32 Flags) {
    DEBUG(TEXT("[ResizeRegion] Enter: Base=%x Target=%x Size=%x NewSize=%x Flags=%x"),
          Base,
          Target,
          Size,
          NewSize,
          Flags);

    if (Base == 0) {
        ERROR(TEXT("[ResizeRegion] Base cannot be null"));
        return FALSE;
    }

    Base = CanonicalizeLinearAddress(Base);

    if (NewSize > KernelStartup.MemorySize / 4) {
        ERROR(TEXT("[ResizeRegion] New size %x exceeds 25%% of memory (%u)"),
              NewSize,
              KernelStartup.MemorySize / 4);
        return FALSE;
    }

    LPMEMORY_REGION_DESCRIPTOR Descriptor = NULL;
    if (G_RegionDescriptorsEnabled && G_RegionDescriptorBootstrap == FALSE) {
        Descriptor = FindDescriptorForBase(ResolveCurrentAddressSpaceOwner(), Base);
        if (Descriptor == NULL) {
            WARNING(TEXT("[ResizeRegion] Missing descriptor for base=%p"),
                (LPVOID)Base);
        }
    }

    UINT CurrentPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;
    UINT RequestedPages = (NewSize + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;
    if (CurrentPages == 0) CurrentPages = 1;
    if (RequestedPages == 0) RequestedPages = 1;

    if (RequestedPages == CurrentPages) {
        DEBUG(TEXT("[ResizeRegion] No page count change"));
        return TRUE;
    }

    if (RequestedPages > CurrentPages) {
        UINT AdditionalPages = RequestedPages - CurrentPages;
        LINEAR NewBase = Base + ((LINEAR)CurrentPages << PAGE_SIZE_MUL);
        UINT AdditionalSize = AdditionalPages << PAGE_SIZE_MUL;

        if (IsRegionFree(NewBase, AdditionalSize) == FALSE) {
            DEBUG(TEXT("[ResizeRegion] Additional region not free at %x"), NewBase);
            return FALSE;
        }

        PHYSICAL AdditionalTarget = 0;
        if (Target != 0) {
            AdditionalTarget = Target + (PHYSICAL)(CurrentPages << PAGE_SIZE_MUL);
        }

        DEBUG(TEXT("[ResizeRegion] Expanding region by %x bytes"), AdditionalSize);

#if EXOS_X86_64_FAST_VMM
        BOOL ExpansionFastPathUsed = FALSE;
        if (Descriptor != NULL && G_RegionDescriptorBootstrap == FALSE) {
            MEMORY_REGION_DESCRIPTOR TempDescriptor;
            InitializeTransientDescriptor(&TempDescriptor, NewBase, AdditionalPages, AdditionalTarget, Flags);

            UINT PagesProcessed = 0u;
            if (FastPopulateRegionFromDescriptor(&TempDescriptor,
                                                 AdditionalTarget,
                                                 Flags,
                                                 TEXT("ResizeRegion"),
                                                 &PagesProcessed) == TRUE &&
                PagesProcessed == AdditionalPages) {
                ExpansionFastPathUsed = TRUE;
            } else {
                if (PagesProcessed != 0u) {
                    MEMORY_REGION_DESCRIPTOR RollbackDescriptor;
                    InitializeTransientDescriptor(&RollbackDescriptor,
                                                  NewBase,
                                                  PagesProcessed,
                                                  AdditionalTarget,
                                                  Flags);
                    if (FastReleaseRegionFromDescriptor(&RollbackDescriptor, NULL) == FALSE) {
                        WARNING(TEXT("[ResizeRegion] Fast rollback failed for base=%p pages=%u"),
                            (LPVOID)NewBase,
                            PagesProcessed);
                    }
                }

                DEBUG(TEXT("[ResizeRegion] Falling back to legacy population (processed=%u targetPages=%u)"),
                    PagesProcessed,
                    AdditionalPages);
            }
        }
#else
        BOOL ExpansionFastPathUsed = FALSE;
#endif

        if (ExpansionFastPathUsed == FALSE) {
            if (PopulateRegionPagesLegacy(NewBase,
                                          AdditionalTarget,
                                          AdditionalPages,
                                          Flags,
                                          NewBase,
                                          TEXT("ResizeRegion")) == FALSE) {
                return FALSE;
            }
        }

        if (Descriptor != NULL) {
            ExtendDescriptor(Descriptor, AdditionalPages);
        }

        FlushTLB();
    } else {
        UINT PagesToRelease = CurrentPages - RequestedPages;
        if (PagesToRelease != 0) {
            LINEAR ReleaseBase = Base + ((LINEAR)RequestedPages << PAGE_SIZE_MUL);
            UINT ReleaseSize = PagesToRelease << PAGE_SIZE_MUL;

            DEBUG(TEXT("[ResizeRegion] Shrinking region by %x bytes"), ReleaseSize);
            FreeRegion(ReleaseBase, ReleaseSize);
        }
    }

    DEBUG(TEXT("[ResizeRegion] Exit"));
    return TRUE;
}

/************************************************************************/
/**
 * @brief Unmap and free a linear region.
 * @param Base Base linear address.
 * @param Size Size of region.
 * @return TRUE on success.
 */
BOOL FreeRegion(LINEAR Base, UINT Size) {
    LINEAR OriginalBase = Base;
    UINT NumPages = (Size + (PAGE_SIZE - 1u)) >> PAGE_SIZE_MUL;
    if (NumPages == 0u) {
        NumPages = 1u;
    }

    DEBUG(TEXT("[FreeRegion] Enter base=%p size=%u pages=%u"),
        (LPVOID)OriginalBase,
        Size,
        NumPages);

    LINEAR CanonicalBase = CanonicalizeLinearAddress(Base);
    DEBUG(TEXT("[FreeRegion] Canonical base=%p"), (LPVOID)CanonicalBase);

#if EXOS_X86_64_FAST_VMM
    if (G_RegionDescriptorsEnabled && G_RegionDescriptorBootstrap == FALSE) {
        if (ReleaseRegionWithFastWalker(CanonicalBase, NumPages) == TRUE) {
            UpdateDescriptorsForFree(CanonicalBase, NumPages << PAGE_SIZE_MUL);
            FreeEmptyPageTables();
            FlushTLB();
            DEBUG(TEXT("[FreeRegion] Exit base=%p size=%u"), (LPVOID)OriginalBase, Size);
            return TRUE;
        }

        DEBUG(TEXT("[FreeRegion] Fast walker fallback engaged for base=%p size=%u"),
            (LPVOID)CanonicalBase,
            Size);
    }
#endif

    return FreeRegionLegacyInternal(CanonicalBase, NumPages, OriginalBase, Size);
}

/************************************************************************/

/**
 * @brief Map an I/O physical range into virtual memory.
 * @param PhysicalBase Physical base address.
 * @param Size Size in bytes.
 * @return Linear address or 0 on failure.
 */
LINEAR MapIOMemory(PHYSICAL PhysicalBase, UINT Size) {
    // Basic parameter checks
    if (PhysicalBase == 0 || Size == 0) {
        ERROR(TEXT("[MapIOMemory] Invalid parameters (PA=%x Size=%x)"), PhysicalBase, Size);
        return NULL;
    }

    // Calculate page-aligned base and adjusted size for non-aligned addresses
    UINT PageOffset = (UINT)(PhysicalBase & (PAGE_SIZE - 1));
    PHYSICAL AlignedPhysicalBase = PhysicalBase & ~(PAGE_SIZE - 1);
    UINT AdjustedSize = ((Size + PageOffset + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

    DEBUG(TEXT("[MapIOMemory] Original: PA=%x Size=%x"), PhysicalBase, Size);
    DEBUG(TEXT("[MapIOMemory] Aligned: PA=%x Size=%x Offset=%x"), AlignedPhysicalBase, AdjustedSize, PageOffset);

    // Map as Uncached, Read/Write, exact PMA mapping, IO semantics
    LINEAR AlignedResult = AllocRegion(
        VMA_KERNEL,          // Start search in kernel space to avoid user space
        AlignedPhysicalBase, // Page-aligned PMA
        AdjustedSize,        // Page-aligned size
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_UC |  // MMIO must be UC
            ALLOC_PAGES_IO |
            ALLOC_PAGES_AT_OR_OVER  // Do not touch RAM bitmap; mark PTE.Fixed; search at or over VMA_KERNEL
    );

    if (AlignedResult == NULL) {
        DEBUG(TEXT("[MapIOMemory] AllocRegion failed"));
        return NULL;
    }

    // Return the address adjusted for the original offset
    LINEAR CanonicalAligned = CanonicalizeLinearAddress(AlignedResult);
    LINEAR result = CanonicalizeLinearAddress(CanonicalAligned + (LINEAR)PageOffset);
    DEBUG(TEXT("[MapIOMemory] Mapped at aligned=%x, returning=%x"), AlignedResult, result);
    return result;
}

/************************************************************************/

/**
 * @brief Unmap a previously mapped I/O range.
 * @param LinearBase Linear base address.
 * @param Size Size in bytes.
 * @return TRUE on success.
 */
BOOL UnMapIOMemory(LINEAR LinearBase, UINT Size) {
    // Basic parameter checks
    if (LinearBase == 0 || Size == 0) {
        ERROR(TEXT("[UnMapIOMemory] Invalid parameters (LA=%x Size=%x)"), LinearBase, Size);
        return FALSE;
    }

    // Just unmap; FreeRegion will skip RAM bitmap if PTE.Fixed was set
    return FreeRegion(CanonicalizeLinearAddress(LinearBase), Size);
}

/************************************************************************/

/**
 * @brief Allocate a kernel region - wrapper around AllocRegion with VMA_KERNEL and AT_OR_OVER.
 * @param Target Physical base address (0 for any).
 * @param Size Size in bytes.
 * @param Flags Additional allocation flags.
 * @return Linear address or 0 on failure.
 */
LINEAR AllocKernelRegion(PHYSICAL Target, UINT Size, U32 Flags) {
    // Always use VMA_KERNEL base and add AT_OR_OVER flag
    return AllocRegion(VMA_KERNEL, Target, Size, Flags | ALLOC_PAGES_AT_OR_OVER);
}

/************************************************************************/

LINEAR ResizeKernelRegion(LINEAR Base, UINT Size, UINT NewSize, U32 Flags) {
    return ResizeRegion(Base, 0, Size, NewSize, Flags | ALLOC_PAGES_AT_OR_OVER);
}
