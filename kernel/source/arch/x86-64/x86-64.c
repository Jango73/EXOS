
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


    Intel x86-64 Architecture Support

\************************************************************************/

#include "arch/x86-64/x86-64.h"
#include "arch/x86-64/x86-64-Log.h"

#include "Console.h"
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "CoreString.h"
#include "System.h"
#include "Text.h"

/***************************************************************************/

KERNELDATA_X86_64 SECTION(".data") Kernel_i386 = {
    .IDT = NULL,
    .GDT = NULL,
    .TSS = NULL,
};

/************************************************************************/

/**
 * @brief Perform architecture-specific pre-initialization.
 */
void ArchPreInitializeKernel(void) {
    GDT_REGISTER Gdtr;

    ReadGlobalDescriptorTable(&Gdtr);
    Kernel_i386.GDT = (LPVOID)(LINEAR)Gdtr.Base;
}

/************************************************************************/

typedef void (*STARTUP_REGION_POPULATE)(LPPAGE_TABLE Table, void *Context);

#define STARTUP_REGION_MAX_ADDITIONAL_TABLES 7u

typedef struct _STARTUP_REGION {
    LPCSTR Name;
    UINT PdptIndex;
    U32 Privilege;
    BOOL ReadWrite;
    BOOL Global;
    PHYSICAL PdptPhysical;
    PHYSICAL DirectoryPhysical;
    PHYSICAL TablePhysical;
    PHYSICAL AdditionalTablesPhysical[STARTUP_REGION_MAX_ADDITIONAL_TABLES];
    UINT AdditionalTableCount;
    LPPAGE_DIRECTORY Pdpt;
    LPPAGE_DIRECTORY Directory;
    LPPAGE_TABLE Table;
} STARTUP_REGION;

/************************************************************************/

/**
 * @brief Free the physical pages allocated for a startup region.
 * @param Region Region descriptor to release.
 */
static void FreeStartupRegion(STARTUP_REGION *Region) {
    if (Region->PdptPhysical) FreePhysicalPage(Region->PdptPhysical);
    if (Region->DirectoryPhysical) FreePhysicalPage(Region->DirectoryPhysical);
    if (Region->TablePhysical) FreePhysicalPage(Region->TablePhysical);

    for (UINT Index = 0; Index < Region->AdditionalTableCount; Index++) {
        if (Region->AdditionalTablesPhysical[Index]) FreePhysicalPage(Region->AdditionalTablesPhysical[Index]);
    }

    MemorySet(Region, 0, sizeof(STARTUP_REGION));
}

/************************************************************************/

/**
 * @brief Populate the identity mapping for the low memory region.
 */
typedef struct _LOW_REGION_CONTEXT {
    PHYSICAL PhysicalBase;
} LOW_REGION_CONTEXT;

static BOOL CreateStartupRegionTable(STARTUP_REGION *Region,
    LPCSTR RegionName,
    UINT DirectoryIndex,
    STARTUP_REGION_POPULATE Populate,
    void *Context,
    BOOL Primary) {
    PHYSICAL TablePhysical = AllocPhysicalPage();

    if (TablePhysical == NULL) {
        ERROR(TEXT("[CreateStartupRegionTable] %s region out of physical pages"), RegionName);
        return FALSE;
    }

    LINEAR VmaTable = MapTempPhysicalPage3(TablePhysical);
    if (VmaTable == NULL) {
        ERROR(TEXT("[CreateStartupRegionTable] MapTempPhysicalPage3 failed for %s table"), RegionName);
        FreePhysicalPage(TablePhysical);
        return FALSE;
    }

    LPPAGE_TABLE Table = (LPPAGE_TABLE)VmaTable;
    MemorySet(Table, 0, PAGE_SIZE);
    DEBUG(TEXT("[CreateStartupRegionTable] %s directory[%u] table cleared"), RegionName, DirectoryIndex);

    if (Populate) {
        Populate(Table, Context);
    }

    WritePageDirectoryEntryValue(
        Region->Directory,
        DirectoryIndex,
        MakePageDirectoryEntryValue(
            TablePhysical,
            Region->ReadWrite,
            Region->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Region->Global,
            /*Fixed*/ 1));

    if (Primary) {
        Region->TablePhysical = TablePhysical;
        Region->Table = Table;
        return TRUE;
    }

    if (Region->AdditionalTableCount >= STARTUP_REGION_MAX_ADDITIONAL_TABLES) {
        ERROR(TEXT("[CreateStartupRegionTable] %s region exceeded table capacity"), RegionName);
        ClearPageDirectoryEntry(Region->Directory, DirectoryIndex);
        FreePhysicalPage(TablePhysical);

        if (Region->TablePhysical != NULL) {
            LINEAR PrimaryVma = MapTempPhysicalPage3(Region->TablePhysical);
            if (PrimaryVma != NULL) Region->Table = (LPPAGE_TABLE)PrimaryVma;
        }

        return FALSE;
    }

    Region->AdditionalTablesPhysical[Region->AdditionalTableCount] = TablePhysical;
    Region->AdditionalTableCount++;

    if (Region->TablePhysical != NULL) {
        LINEAR PrimaryVma = MapTempPhysicalPage3(Region->TablePhysical);

        if (PrimaryVma == NULL) {
            ERROR(TEXT("[CreateStartupRegionTable] MapTempPhysicalPage3 failed restoring %s primary table"), RegionName);

            Region->AdditionalTableCount--;
            Region->AdditionalTablesPhysical[Region->AdditionalTableCount] = NULL;
            ClearPageDirectoryEntry(Region->Directory, DirectoryIndex);
            FreePhysicalPage(TablePhysical);

            return FALSE;
        }

        Region->Table = (LPPAGE_TABLE)PrimaryVma;
    } else {
        Region->TablePhysical = TablePhysical;
        Region->Table = Table;
    }

    return TRUE;
}

/************************************************************************/

static BOOL ExtendStartupRegion(STARTUP_REGION *Region,
    UINT DirectoryIndex,
    STARTUP_REGION_POPULATE Populate,
    void *Context) {
    return CreateStartupRegionTable(Region, Region->Name, DirectoryIndex, Populate, Context, FALSE);
}

/************************************************************************/

static void PopulateLowRegionTable(LPPAGE_TABLE Table, void *Context) {
    LOW_REGION_CONTEXT *LowContext = (LOW_REGION_CONTEXT *)Context;
    PHYSICAL PhysicalBase = LowContext != NULL ? LowContext->PhysicalBase : 0;

    for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = PhysicalBase + ((PHYSICAL)Index << PAGE_SIZE_MUL);

#ifdef PROTECT_BIOS
        BOOL Protected = Physical == 0 || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);
#else
        BOOL Protected = FALSE;
#endif

        if (Protected) {
            ClearPageTableEntry(Table, Index);
        } else {
            WritePageTableEntryValue(
                Table,
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
}

typedef struct _KERNEL_REGION_CONTEXT {
    PHYSICAL PhysBase;
} KERNEL_REGION_CONTEXT;

/************************************************************************/

/**
 * @brief Populate the mapping for the kernel stub region.
 */
static void PopulateKernelRegionTable(LPPAGE_TABLE Table, void *Context) {
    KERNEL_REGION_CONTEXT *KernelContext = (KERNEL_REGION_CONTEXT *)Context;

    for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL Physical = KernelContext->PhysBase + ((PHYSICAL)Index << PAGE_SIZE_MUL);
        WritePageTableEntryValue(
            Table,
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

typedef struct _TASK_RUNNER_REGION_CONTEXT {
    UINT TableIndex;
    PHYSICAL Physical;
} TASK_RUNNER_REGION_CONTEXT;

/************************************************************************/

/**
 * @brief Populate the mapping for the task runner region.
 */
static void PopulateTaskRunnerRegionTable(LPPAGE_TABLE Table, void *Context) {
    TASK_RUNNER_REGION_CONTEXT *TaskRunnerContext = (TASK_RUNNER_REGION_CONTEXT *)Context;

    WritePageTableEntryValue(
        Table,
        TaskRunnerContext->TableIndex,
        MakePageTableEntryValue(
            TaskRunnerContext->Physical,
            /*ReadWrite*/ 0,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));
}

/************************************************************************//**
 * @brief Allocate and populate a startup region mapping.
 * @param Region Region descriptor to fill.
 * @param RegionName Region label for logging purposes.
 * @param PdptIndex Index inside the PDPT for the region.
 * @param DirectoryIndex Index inside the directory for the region.
 * @param Privilege Privilege level for the region.
 * @param ReadWrite Read/write flag for the directory entries.
 * @param Global Global flag for the directory entries.
 * @param Populate Population callback used to fill the page table.
 * @param Context Context passed to the population callback.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL MapStartupRegion(STARTUP_REGION *Region,
    LPCSTR RegionName,
    UINT PdptIndex,
    UINT DirectoryIndex,
    U32 Privilege,
    BOOL ReadWrite,
    BOOL Global,
    STARTUP_REGION_POPULATE Populate,
    void *Context) {
    MemorySet(Region, 0, sizeof(STARTUP_REGION));

    Region->Name = RegionName;
    Region->PdptIndex = PdptIndex;
    Region->Privilege = Privilege;
    Region->ReadWrite = ReadWrite;
    Region->Global = Global;

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();

    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("[MapStartupRegion] %s region out of physical pages"), RegionName);
        goto Out_Error;
    }

    LINEAR VmaPdpt = MapTempPhysicalPage(Region->PdptPhysical);
    if (VmaPdpt == NULL) {
        ERROR(TEXT("[MapStartupRegion] MapTempPhysicalPage failed for %s PDPT"), RegionName);
        goto Out_Error;
    }
    Region->Pdpt = (LPPAGE_DIRECTORY)VmaPdpt;
    MemorySet(Region->Pdpt, 0, PAGE_SIZE);
    DEBUG(TEXT("[MapStartupRegion] %s PDPT cleared"), RegionName);

    LINEAR VmaDirectory = MapTempPhysicalPage2(Region->DirectoryPhysical);
    if (VmaDirectory == NULL) {
        ERROR(TEXT("[MapStartupRegion] MapTempPhysicalPage2 failed for %s directory"), RegionName);
        goto Out_Error;
    }
    Region->Directory = (LPPAGE_DIRECTORY)VmaDirectory;
    MemorySet(Region->Directory, 0, PAGE_SIZE);
    DEBUG(TEXT("[MapStartupRegion] %s directory cleared"), RegionName);

    WritePageDirectoryEntryValue(
        Region->Pdpt,
        PdptIndex,
        MakePageDirectoryEntryValue(
            Region->DirectoryPhysical,
            ReadWrite,
            Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Global,
            /*Fixed*/ 1));

    if (CreateStartupRegionTable(Region, RegionName, DirectoryIndex, Populate, Context, TRUE) == FALSE) {
        goto Out_Error;
    }

    return TRUE;

Out_Error:

    FreeStartupRegion(Region);
    return FALSE;
}

/************************************************************************/

/**
 * @brief Allocate a new page directory.
 * @return Physical address of the page directory or MAX_U32 on failure.
 */
PHYSICAL AllocPageDirectory(void) {
    PHYSICAL Pml4Physical = NULL;
    STARTUP_REGION LowRegion = {0};
    STARTUP_REGION KernelRegion = {0};
    STARTUP_REGION TaskRunnerRegion = {0};

    DEBUG(TEXT("[AllocPageDirectory] Enter"));

    PHYSICAL PhysBaseKernel = KernelStartup.StubAddress;

    UINT LowPml4Index = GetPml4Entry(0);
    UINT LowPdptIndex = GetPdptEntry(0);
    UINT LowDirectoryIndex = GetDirectoryEntry(0);

    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);
    UINT KernelPdptIndex = GetPdptEntry((U64)VMA_KERNEL);
    UINT KernelDirectoryIndex = GetDirectoryEntry((U64)VMA_KERNEL);

    UINT TaskRunnerPml4Index = GetPml4Entry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerPdptIndex = GetPdptEntry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerDirectoryIndex = GetDirectoryEntry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerTableIndex = GetTableEntry((U64)VMA_TASK_RUNNER);

    Pml4Physical = AllocPhysicalPage();

    if (Pml4Physical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Out of physical pages"));
        goto Out_Error64;
    }

    LOW_REGION_CONTEXT LowPrimaryContext = {
        .PhysicalBase = 0,
    };

    if (MapStartupRegion(&LowRegion,
            TEXT("Low"),
            LowPdptIndex,
            LowDirectoryIndex,
            PAGE_PRIVILEGE_KERNEL,
            /*ReadWrite*/ 1,
            /*Global*/ 0,
            PopulateLowRegionTable,
            &LowPrimaryContext) == FALSE) {
        goto Out_Error64;
    }

    LOW_REGION_CONTEXT LowSecondaryContext = {
        .PhysicalBase = ((PHYSICAL)PAGE_TABLE_NUM_ENTRIES << PAGE_SIZE_MUL),
    };

    // Ensure the first 4 MB remain identity-mapped during startup.
    if (ExtendStartupRegion(&LowRegion,
            LowDirectoryIndex + 1u,
            PopulateLowRegionTable,
            &LowSecondaryContext) == FALSE) {
        goto Out_Error64;
    }

    KERNEL_REGION_CONTEXT KernelContext = {
        .PhysBase = PhysBaseKernel,
    };

    if (MapStartupRegion(&KernelRegion,
            TEXT("Kernel"),
            KernelPdptIndex,
            KernelDirectoryIndex,
            PAGE_PRIVILEGE_KERNEL,
            /*ReadWrite*/ 1,
            /*Global*/ 0,
            PopulateKernelRegionTable,
            &KernelContext) == FALSE) {
        goto Out_Error64;
    }

    LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = PhysBaseKernel + (PHYSICAL)(TaskRunnerLinear - (LINEAR)VMA_KERNEL);

    DEBUG(TEXT("[AllocPageDirectory] TaskRunnerPhysical = %p + (%p - %p) = %p"),
        PhysBaseKernel,
        TaskRunnerLinear,
        VMA_KERNEL,
        TaskRunnerPhysical);

    TASK_RUNNER_REGION_CONTEXT TaskRunnerContext = {
        .TableIndex = TaskRunnerTableIndex,
        .Physical = TaskRunnerPhysical,
    };

    if (MapStartupRegion(&TaskRunnerRegion,
            TEXT("TaskRunner"),
            TaskRunnerPdptIndex,
            TaskRunnerDirectoryIndex,
            PAGE_PRIVILEGE_USER,
            /*ReadWrite*/ 1,
            /*Global*/ 0,
            PopulateTaskRunnerRegionTable,
            &TaskRunnerContext) == FALSE) {
        goto Out_Error64;
    }

    LINEAR VMA_Pml4 = MapTempPhysicalPage(Pml4Physical);
    if (VMA_Pml4 == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage failed on PML4"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY Pml4 = (LPPAGE_DIRECTORY)VMA_Pml4;
    MemorySet(Pml4, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] PML4 cleared"));

    WritePageDirectoryEntryValue(
        Pml4,
        LowPml4Index,
        MakePageDirectoryEntryValue(
            LowRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
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

    FlushTLB();

    DEBUG(TEXT("[AllocPageDirectory] PML4[%u]=%p, PML4[%u]=%p, PML4[%u]=%p, PML4[%u]=%p"),
        LowPml4Index,
        (LINEAR)ReadPageDirectoryEntryValue(Pml4, LowPml4Index),
        KernelPml4Index,
        (LINEAR)ReadPageDirectoryEntryValue(Pml4, KernelPml4Index),
        TaskRunnerPml4Index,
        (LINEAR)ReadPageDirectoryEntryValue(Pml4, TaskRunnerPml4Index),
        PML4_RECURSIVE_SLOT,
        (LINEAR)ReadPageDirectoryEntryValue(Pml4, PML4_RECURSIVE_SLOT));

    DEBUG(TEXT("[AllocPageDirectory] LowTable[0]=%p, KernelTable[0]=%p, TaskRunnerTable[%u]=%p"),
        (LINEAR)ReadPageTableEntryValue(LowRegion.Table, 0),
        (LINEAR)ReadPageTableEntryValue(KernelRegion.Table, 0),
        TaskRunnerTableIndex,
        (LINEAR)ReadPageTableEntryValue(TaskRunnerRegion.Table, TaskRunnerTableIndex));

    DEBUG(TEXT("[AllocPageDirectory] TaskRunner VMA=%p -> Physical=%p"),
        (LINEAR)VMA_TASK_RUNNER,
        (UINT)TaskRunnerPhysical);

    DEBUG(TEXT("[AllocPageDirectory] Exit"));
    return Pml4Physical;

Out_Error64:

    if (Pml4Physical) FreePhysicalPage(Pml4Physical);
    FreeStartupRegion(&LowRegion);
    FreeStartupRegion(&KernelRegion);
    FreeStartupRegion(&TaskRunnerRegion);

    return NULL;
}

/************************************************************************/

/**
 * @brief Allocate a new page directory for userland processes.
 * @return Physical address of the page directory or NULL on failure.
 */
PHYSICAL AllocUserPageDirectory(void) {
    PHYSICAL Pml4Physical = NULL;
    STARTUP_REGION LowRegion = {0};
    STARTUP_REGION KernelRegion = {0};
    STARTUP_REGION TaskRunnerRegion = {0};

    DEBUG(TEXT("[AllocUserPageDirectory] Enter"));

    PHYSICAL PhysBaseKernel = KernelStartup.StubAddress;

    UINT LowPml4Index = GetPml4Entry(0);
    UINT LowPdptIndex = GetPdptEntry(0);
    UINT LowDirectoryIndex = GetDirectoryEntry(0);

    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);
    UINT KernelPdptIndex = GetPdptEntry((U64)VMA_KERNEL);
    UINT KernelDirectoryIndex = GetDirectoryEntry((U64)VMA_KERNEL);

    UINT TaskRunnerPml4Index = GetPml4Entry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerPdptIndex = GetPdptEntry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerDirectoryIndex = GetDirectoryEntry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerTableIndex = GetTableEntry((U64)VMA_TASK_RUNNER);

    Pml4Physical = AllocPhysicalPage();

    if (Pml4Physical == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] Out of physical pages"));
        goto Out_Error64;
    }

    LOW_REGION_CONTEXT LowPrimaryContext = {
        .PhysicalBase = 0,
    };

    if (MapStartupRegion(&LowRegion,
            TEXT("Low"),
            LowPdptIndex,
            LowDirectoryIndex,
            PAGE_PRIVILEGE_KERNEL,
            /*ReadWrite*/ 1,
            /*Global*/ 0,
            PopulateLowRegionTable,
            &LowPrimaryContext) == FALSE) {
        goto Out_Error64;
    }

    LOW_REGION_CONTEXT LowSecondaryContext = {
        .PhysicalBase = ((PHYSICAL)PAGE_TABLE_NUM_ENTRIES << PAGE_SIZE_MUL),
    };

    // Ensure the first 4 MB remain identity-mapped during startup.
    if (ExtendStartupRegion(&LowRegion,
            LowDirectoryIndex + 1u,
            PopulateLowRegionTable,
            &LowSecondaryContext) == FALSE) {
        goto Out_Error64;
    }

    KERNEL_REGION_CONTEXT KernelContext = {
        .PhysBase = PhysBaseKernel,
    };

    if (MapStartupRegion(&KernelRegion,
            TEXT("Kernel"),
            KernelPdptIndex,
            KernelDirectoryIndex,
            PAGE_PRIVILEGE_KERNEL,
            /*ReadWrite*/ 1,
            /*Global*/ 0,
            PopulateKernelRegionTable,
            &KernelContext) == FALSE) {
        goto Out_Error64;
    }

    LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = PhysBaseKernel + (PHYSICAL)(TaskRunnerLinear - (LINEAR)VMA_KERNEL);

    DEBUG(TEXT("[AllocUserPageDirectory] TaskRunnerPhysical = %p + (%p - %p) = %p"),
        PhysBaseKernel,
        TaskRunnerLinear,
        VMA_KERNEL,
        TaskRunnerPhysical);

    TASK_RUNNER_REGION_CONTEXT TaskRunnerContext = {
        .TableIndex = TaskRunnerTableIndex,
        .Physical = TaskRunnerPhysical,
    };

    if (MapStartupRegion(&TaskRunnerRegion,
            TEXT("TaskRunner"),
            TaskRunnerPdptIndex,
            TaskRunnerDirectoryIndex,
            PAGE_PRIVILEGE_USER,
            /*ReadWrite*/ 1,
            /*Global*/ 0,
            PopulateTaskRunnerRegionTable,
            &TaskRunnerContext) == FALSE) {
        goto Out_Error64;
    }

    LINEAR VMA_Pml4 = MapTempPhysicalPage(Pml4Physical);
    if (VMA_Pml4 == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] MapTempPhysicalPage failed on PML4"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY Pml4 = (LPPAGE_DIRECTORY)VMA_Pml4;
    MemorySet(Pml4, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocUserPageDirectory] PML4 cleared"));

    WritePageDirectoryEntryValue(
        Pml4,
        LowPml4Index,
        MakePageDirectoryEntryValue(
            LowRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
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

    FlushTLB();

    DEBUG(TEXT("[AllocUserPageDirectory] PML4[%u]=%p, PML4[%u]=%p, PML4[%u]=%p, PML4[%u]=%p"),
        LowPml4Index,
        (LINEAR)ReadPageDirectoryEntryValue(Pml4, LowPml4Index),
        KernelPml4Index,
        (LINEAR)ReadPageDirectoryEntryValue(Pml4, KernelPml4Index),
        TaskRunnerPml4Index,
        (LINEAR)ReadPageDirectoryEntryValue(Pml4, TaskRunnerPml4Index),
        PML4_RECURSIVE_SLOT,
        (LINEAR)ReadPageDirectoryEntryValue(Pml4, PML4_RECURSIVE_SLOT));

    DEBUG(TEXT("[AllocUserPageDirectory] LowTable[0]=%p, KernelTable[0]=%p, TaskRunnerTable[%u]=%p"),
        (LINEAR)ReadPageTableEntryValue(LowRegion.Table, 0),
        (LINEAR)ReadPageTableEntryValue(KernelRegion.Table, 0),
        TaskRunnerTableIndex,
        (LINEAR)ReadPageTableEntryValue(TaskRunnerRegion.Table, TaskRunnerTableIndex));

    DEBUG(TEXT("[AllocUserPageDirectory] TaskRunner VMA=%p -> Physical=%p"),
        (LINEAR)VMA_TASK_RUNNER,
        (UINT)TaskRunnerPhysical);

    DEBUG(TEXT("[AllocUserPageDirectory] Exit"));
    return Pml4Physical;

Out_Error64:

    if (Pml4Physical) FreePhysicalPage(Pml4Physical);
    FreeStartupRegion(&LowRegion);
    FreeStartupRegion(&KernelRegion);
    FreeStartupRegion(&TaskRunnerRegion);

    return NULL;
}


/************************************************************************/

static void InitLongModeSegmentDescriptor(LPSEGMENT_DESCRIPTOR Descriptor, BOOL Executable, U32 Privilege) {
    MemorySet(Descriptor, 0, sizeof(SEGMENT_DESCRIPTOR));

    Descriptor->Limit_00_15 = 0xFFFFu;
    Descriptor->Base_00_15 = 0x0000u;
    Descriptor->Base_16_23 = 0x00u;
    Descriptor->Accessed = 0u;
    Descriptor->CanWrite = 1u;
    Descriptor->ConformExpand = 0u;
    Descriptor->Type = Executable ? 1u : 0u;
    Descriptor->Segment = 1u;
    Descriptor->Privilege = Privilege;
    Descriptor->Present = 1u;
    Descriptor->Limit_16_19 = 0x0Fu;
    Descriptor->Available = 0u;
    Descriptor->Unused = Executable ? 1u : 0u;
    Descriptor->OperandSize = Executable ? 0u : 1u;
    Descriptor->Granularity = 1u;
    Descriptor->Base_24_31 = 0x00u;
}

/***************************************************************************/

static void InitLongModeDataDescriptor(LPSEGMENT_DESCRIPTOR Descriptor, U32 Privilege) {
    InitLongModeSegmentDescriptor(Descriptor, FALSE, Privilege);
    Descriptor->Unused = 0u;
    Descriptor->OperandSize = 1u;
}

/***************************************************************************/

static void InitLegacySegmentDescriptor(LPSEGMENT_DESCRIPTOR Descriptor, BOOL Executable) {
    MemorySet(Descriptor, 0, sizeof(SEGMENT_DESCRIPTOR));

    Descriptor->Limit_00_15 = 0xFFFFu;
    Descriptor->Limit_16_19 = 0x0Fu;
    Descriptor->Base_00_15 = 0x0000u;
    Descriptor->Base_16_23 = 0x00u;
    Descriptor->Base_24_31 = 0x00u;
    Descriptor->Accessed = 0u;
    Descriptor->CanWrite = 1u;
    Descriptor->ConformExpand = 0u;
    Descriptor->Type = Executable ? 1u : 0u;
    Descriptor->Segment = 1u;
    Descriptor->Privilege = PRIVILEGE_KERNEL;
    Descriptor->Present = 1u;
    Descriptor->Available = 0u;
    Descriptor->Unused = 0u;
    Descriptor->OperandSize = 0u;
    Descriptor->Granularity = 0u;
}

/***************************************************************************/

static void InitX86_64GlobalDescriptorTable(LPSEGMENT_DESCRIPTOR Table) {
    DEBUG(TEXT("[InitX86_64GlobalDescriptorTable] Enter"));

    MemorySet(Table, 0, GDT_SIZE);

    InitLongModeSegmentDescriptor(&Table[1], TRUE, PRIVILEGE_KERNEL);
    InitLongModeDataDescriptor(&Table[2], PRIVILEGE_KERNEL);
    InitLongModeSegmentDescriptor(&Table[3], TRUE, PRIVILEGE_USER);
    InitLongModeDataDescriptor(&Table[4], PRIVILEGE_USER);
    InitLegacySegmentDescriptor(&Table[5], TRUE);
    InitLegacySegmentDescriptor(&Table[6], FALSE);

    DEBUG(TEXT("[InitX86_64GlobalDescriptorTable] Exit"));
}

/***************************************************************************/

/**
 * @brief Initialize the architecture-specific context for a task.
 *
 * Allocates kernel and user stacks for the task, clears the interrupt frame,
 * and seeds the register snapshot so the generic scheduler can operate while
 * the long mode context-switching code is under construction.
 */
BOOL ArchSetupTask(struct tag_TASK* Task, struct tag_PROCESS* Process, struct tag_TASKINFO* Info) {
    LINEAR BaseVMA = VMA_KERNEL;
    SELECTOR CodeSelector = SELECTOR_KERNEL_CODE;
    SELECTOR DataSelector = SELECTOR_KERNEL_DATA;
    U64 StackTop;
    U64 SysStackTop;
    U64 ControlRegister4 = 0;

    DEBUG(TEXT("[ArchSetupTask] Enter"));

    if (Process->Privilege == PRIVILEGE_USER) {
        BaseVMA = VMA_USER;
        CodeSelector = SELECTOR_USER_CODE;
        DataSelector = SELECTOR_USER_DATA;
    }

    Task->Arch.StackSize = Info->StackSize;
    Task->Arch.SysStackSize = TASK_SYSTEM_STACK_SIZE * 4u;

    Task->Arch.StackBase = AllocRegion(BaseVMA, 0, Task->Arch.StackSize,
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER);
    Task->Arch.SysStackBase = AllocKernelRegion(
        0, Task->Arch.SysStackSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Task->Arch.StackBase == 0 || Task->Arch.SysStackBase == 0) {
        if (Task->Arch.StackBase != 0) {
            FreeRegion(Task->Arch.StackBase, Task->Arch.StackSize);
            Task->Arch.StackBase = 0;
            Task->Arch.StackSize = 0;
        }

        if (Task->Arch.SysStackBase != 0) {
            FreeRegion(Task->Arch.SysStackBase, Task->Arch.SysStackSize);
            Task->Arch.SysStackBase = 0;
            Task->Arch.SysStackSize = 0;
        }

        ERROR(TEXT("[ArchSetupTask] Stack allocation failed"));
        return FALSE;
    }

    DEBUG(TEXT("[ArchSetupTask] Stack (%x bytes) allocated at %p"), Task->Arch.StackSize,
        (LINEAR)Task->Arch.StackBase);
    DEBUG(TEXT("[ArchSetupTask] System stack (%x bytes) allocated at %p"), Task->Arch.SysStackSize,
        (LINEAR)Task->Arch.SysStackBase);

    MemorySet((void*)Task->Arch.StackBase, 0, Task->Arch.StackSize);
    MemorySet((void*)Task->Arch.SysStackBase, 0, Task->Arch.SysStackSize);
    MemorySet(&(Task->Arch.Context), 0, sizeof(Task->Arch.Context));

    Task->Arch.Context.Registers.RAX = (U64)(UINT)Task->Parameter;
    Task->Arch.Context.Registers.RBX = (U64)(UINT)Task->Function;
    Task->Arch.Context.Registers.CS = CodeSelector;
    Task->Arch.Context.Registers.DS = DataSelector;
    Task->Arch.Context.Registers.ES = DataSelector;
    Task->Arch.Context.Registers.FS = DataSelector;
    Task->Arch.Context.Registers.GS = DataSelector;
    Task->Arch.Context.Registers.SS = DataSelector;
    Task->Arch.Context.Registers.RFlags = RFLAGS_IF | RFLAGS_ALWAYS_1;
    Task->Arch.Context.Registers.CR3 = (U64)Process->PageDirectory;

    GetCR4(ControlRegister4);
    Task->Arch.Context.Registers.CR4 = ControlRegister4;
    Task->Arch.Context.Registers.RIP = (U64)VMA_TASK_RUNNER;

    StackTop = Task->Arch.StackBase + (U64)Task->Arch.StackSize;
    SysStackTop = Task->Arch.SysStackBase + (U64)Task->Arch.SysStackSize;

    if (Process->Privilege == PRIVILEGE_KERNEL) {
        Task->Arch.Context.Registers.RSP = StackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.RBP = StackTop - STACK_SAFETY_MARGIN;
    } else {
        Task->Arch.Context.Registers.RSP = SysStackTop - STACK_SAFETY_MARGIN;
        Task->Arch.Context.Registers.RBP = SysStackTop - STACK_SAFETY_MARGIN;
    }

    Task->Arch.Context.SS0 = SELECTOR_KERNEL_DATA;
    Task->Arch.Context.RSP0 = SysStackTop - STACK_SAFETY_MARGIN;

    if ((Info->Flags & TASK_CREATE_MAIN_KERNEL) != 0u) {
        Task->Status = TASK_STATUS_RUNNING;
        WARNING(TEXT("[ArchSetupTask] Main kernel stack handoff not implemented on x86-64"));
    }

    DEBUG(TEXT("[ArchSetupTask] Exit"));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Architecture-specific memory manager initialization for x86-64.
 */
void ArchInitializeMemoryManager(void) {
    DEBUG(TEXT("[ArchInitializeMemoryManager] Enter"));

    LINEAR TempLinear1 = (LINEAR)X86_64_TEMP_LINEAR_PAGE_1;
    LINEAR TempLinear2 = (LINEAR)X86_64_TEMP_LINEAR_PAGE_2;
    LINEAR TempLinear3 = (LINEAR)X86_64_TEMP_LINEAR_PAGE_3;

    MemorySetTemporaryLinearPages(TempLinear1, TempLinear2, TempLinear3);

    DEBUG(TEXT("[ArchInitializeMemoryManager] Temp pages reserved: %p, %p, %p"),
        (LPVOID)TempLinear1,
        (LPVOID)TempLinear2,
        (LPVOID)TempLinear3);

    PHYSICAL CurrentPageDirectory = (PHYSICAL)GetPageDirectory();
    LogPageDirectory64(CurrentPageDirectory);

    // Clear the physical page bitmap
    Kernel.PPB = (LPPAGEBITMAP)LOW_MEMORY_THREE_QUARTER;

    DEBUG(TEXT("[ArchInitializeMemoryManager] Kernel.PPB: %p"), (LPVOID)Kernel.PPB);

    MemorySet(Kernel.PPB, 0, N_1MB);

    MarkUsedPhysicalMemory();

    if (KernelStartup.MemorySize == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    PHYSICAL NewPageDirectory = AllocPageDirectory();

    DEBUG(TEXT("[ArchInitializeMemoryManager] New page directory: %p"), (LPVOID)NewPageDirectory);

    LogPageDirectory64(NewPageDirectory);

    DEBUG(TEXT("[ArchInitializeMemoryManager] Page directory ready"));

    if (NewPageDirectory == NULL) {
        ERROR(TEXT("[ArchInitializeMemoryManager] AllocPageDirectory failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    LoadPageDirectory(NewPageDirectory);

    DEBUG(TEXT("[ArchInitializeMemoryManager] Page directory set: %p"), (LPVOID)NewPageDirectory);

    FlushTLB();

    DEBUG(TEXT("[ArchInitializeMemoryManager] TLB flushed"));

    if (TempLinear1 == 0 || TempLinear2 == 0) {
        ERROR(TEXT("[ArchInitializeMemoryManager] Failed to reserve temp linear pages"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    Kernel_i386.GDT = (LPVOID)AllocKernelRegion(0, GDT_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Kernel_i386.GDT == NULL) {
        ERROR(TEXT("[ArchInitializeMemoryManager] AllocRegion for GDT failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    InitX86_64GlobalDescriptorTable((LPSEGMENT_DESCRIPTOR)Kernel_i386.GDT);

    DEBUG(TEXT("[ArchInitializeMemoryManager] Loading GDT"));

    LoadGlobalDescriptorTable((PHYSICAL)Kernel_i386.GDT, GDT_SIZE - 1);

    U64* RawEntries = (U64*)Kernel_i386.GDT;
    for (UINT Index = 0; Index < 10; Index++) {
        U64 Low = RawEntries[Index * 2u];
        U64 High = RawEntries[Index * 2u + 1u];
        DEBUG(TEXT("[InitializeMemoryManager] GDT[%u]=%p %p"), Index, (LPVOID)High, (LPVOID)Low);
    }

    DEBUG(TEXT("[ArchInitializeMemoryManager] Exit"));
}
