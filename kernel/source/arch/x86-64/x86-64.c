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

#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "String.h"
#include "System.h"
#include "Text.h"

/************************************************************************/

/**
 * @brief Allocate a new page directory.
 * @return Physical address of the page directory or MAX_U32 on failure.
 */
PHYSICAL AllocPageDirectory(void) {
    PHYSICAL Pml4Physical = NULL;
    PHYSICAL LowPdptPhysical = NULL;
    PHYSICAL KernelPdptPhysical = NULL;
    PHYSICAL TaskRunnerPdptPhysical = NULL;
    PHYSICAL LowDirectoryPhysical = NULL;
    PHYSICAL KernelDirectoryPhysical = NULL;
    PHYSICAL TaskRunnerDirectoryPhysical = NULL;
    PHYSICAL PMA_LowTable = NULL;
    PHYSICAL PMA_KernelTable = NULL;
    PHYSICAL PMA_TaskRunnerTable = NULL;

    DEBUG(TEXT("[AllocPageDirectory] Enter"));

    PHYSICAL PhysBaseKernel = KernelStartup.StubAddress;

    UINT LowPml4Index = GetPml4Entry(0ull);
    UINT LowPdptIndex = GetPdptEntry(0ull);
    UINT LowDirectoryIndex = GetDirectoryEntry(0ull);

    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);
    UINT KernelPdptIndex = GetPdptEntry((U64)VMA_KERNEL);
    UINT KernelDirectoryIndex = GetDirectoryEntry((U64)VMA_KERNEL);

    UINT TaskRunnerPml4Index = GetPml4Entry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerPdptIndex = GetPdptEntry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerDirectoryIndex = GetDirectoryEntry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerTableIndex = GetTableEntry((U64)VMA_TASK_RUNNER);

    Pml4Physical = AllocPhysicalPage();
    LowPdptPhysical = AllocPhysicalPage();
    KernelPdptPhysical = AllocPhysicalPage();
    TaskRunnerPdptPhysical = AllocPhysicalPage();
    LowDirectoryPhysical = AllocPhysicalPage();
    KernelDirectoryPhysical = AllocPhysicalPage();
    TaskRunnerDirectoryPhysical = AllocPhysicalPage();
    PMA_LowTable = AllocPhysicalPage();
    PMA_KernelTable = AllocPhysicalPage();
    PMA_TaskRunnerTable = AllocPhysicalPage();

    if (Pml4Physical == NULL || LowPdptPhysical == NULL || KernelPdptPhysical == NULL ||
        TaskRunnerPdptPhysical == NULL || LowDirectoryPhysical == NULL || KernelDirectoryPhysical == NULL ||
        TaskRunnerDirectoryPhysical == NULL || PMA_LowTable == NULL || PMA_KernelTable == NULL ||
        PMA_TaskRunnerTable == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Out of physical pages"));
        goto Out_Error64;
    }

    LINEAR VMA_LowPdpt = MapTempPhysicalPage(LowPdptPhysical);
    if (VMA_LowPdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage failed on LowPdpt"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY LowPdpt = (LPPAGE_DIRECTORY)VMA_LowPdpt;
    MemorySet(LowPdpt, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] Low PDPT cleared"));

    LINEAR VMA_LowDirectory = MapTempPhysicalPage2(LowDirectoryPhysical);
    if (VMA_LowDirectory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage2 failed on LowDirectory"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY LowDirectory = (LPPAGE_DIRECTORY)VMA_LowDirectory;
    MemorySet(LowDirectory, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] Low directory cleared"));

    LINEAR VMA_LowTable = MapTempPhysicalPage3(PMA_LowTable);
    if (VMA_LowTable == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage3 failed on LowTable"));
        goto Out_Error64;
    }
    LPPAGE_TABLE LowTable = (LPPAGE_TABLE)VMA_LowTable;
    MemorySet(LowTable, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] Low table cleared"));

    for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
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

    WritePageDirectoryEntryValue(
        LowDirectory,
        LowDirectoryIndex,
        MakePageDirectoryEntryValue(
            PMA_LowTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        LowPdpt,
        LowPdptIndex,
        MakePageDirectoryEntryValue(
            LowDirectoryPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    LINEAR VMA_KernelPdpt = MapTempPhysicalPage(KernelPdptPhysical);
    if (VMA_KernelPdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage failed on KernelPdpt"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY KernelPdpt = (LPPAGE_DIRECTORY)VMA_KernelPdpt;
    MemorySet(KernelPdpt, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] Kernel PDPT cleared"));

    LINEAR VMA_KernelDirectory = MapTempPhysicalPage2(KernelDirectoryPhysical);
    if (VMA_KernelDirectory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage2 failed on KernelDirectory"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY KernelDirectory = (LPPAGE_DIRECTORY)VMA_KernelDirectory;
    MemorySet(KernelDirectory, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] Kernel directory cleared"));

    LINEAR VMA_KernelTable = MapTempPhysicalPage3(PMA_KernelTable);
    if (VMA_KernelTable == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage3 failed on KernelTable"));
        goto Out_Error64;
    }
    LPPAGE_TABLE KernelTable = (LPPAGE_TABLE)VMA_KernelTable;
    MemorySet(KernelTable, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] Kernel table cleared"));

    for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
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

    WritePageDirectoryEntryValue(
        KernelDirectory,
        KernelDirectoryIndex,
        MakePageDirectoryEntryValue(
            PMA_KernelTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        KernelPdpt,
        KernelPdptIndex,
        MakePageDirectoryEntryValue(
            KernelDirectoryPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    LINEAR VMA_TaskRunnerPdpt = MapTempPhysicalPage(TaskRunnerPdptPhysical);
    if (VMA_TaskRunnerPdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage failed on TaskRunnerPdpt"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY TaskRunnerPdpt = (LPPAGE_DIRECTORY)VMA_TaskRunnerPdpt;
    MemorySet(TaskRunnerPdpt, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] TaskRunner PDPT cleared"));

    LINEAR VMA_TaskRunnerDirectory = MapTempPhysicalPage2(TaskRunnerDirectoryPhysical);
    if (VMA_TaskRunnerDirectory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage2 failed on TaskRunnerDirectory"));
        goto Out_Error64;
    }
    LPPAGE_DIRECTORY TaskRunnerDirectory = (LPPAGE_DIRECTORY)VMA_TaskRunnerDirectory;
    MemorySet(TaskRunnerDirectory, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] TaskRunner directory cleared"));

    LINEAR VMA_TaskRunnerTable = MapTempPhysicalPage3(PMA_TaskRunnerTable);
    if (VMA_TaskRunnerTable == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTempPhysicalPage3 failed on TaskRunnerTable"));
        goto Out_Error64;
    }
    LPPAGE_TABLE TaskRunnerTable = (LPPAGE_TABLE)VMA_TaskRunnerTable;
    MemorySet(TaskRunnerTable, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] TaskRunner table cleared"));

    U64 TaskRunnerLinear = (U64)(unsigned long long)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = PhysBaseKernel + (PHYSICAL)(TaskRunnerLinear - (U64)VMA_KERNEL);

    DEBUG(TEXT("[AllocPageDirectory] TaskRunnerPhysical = %x + (%llx - %llx) = %x"),
        (UINT)PhysBaseKernel,
        (unsigned long long)TaskRunnerLinear,
        (unsigned long long)VMA_KERNEL,
        (UINT)TaskRunnerPhysical);

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

    WritePageDirectoryEntryValue(
        TaskRunnerDirectory,
        TaskRunnerDirectoryIndex,
        MakePageDirectoryEntryValue(
            PMA_TaskRunnerTable,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    WritePageDirectoryEntryValue(
        TaskRunnerPdpt,
        TaskRunnerPdptIndex,
        MakePageDirectoryEntryValue(
            TaskRunnerDirectoryPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

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
            LowPdptPhysical,
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
            KernelPdptPhysical,
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
            TaskRunnerPdptPhysical,
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

    DEBUG(TEXT("[AllocPageDirectory] PML4[%u]=%llx, PML4[%u]=%llx, PML4[%u]=%llx, PML4[%u]=%llx"),
        LowPml4Index,
        (unsigned long long)ReadPageDirectoryEntryValue(Pml4, LowPml4Index),
        KernelPml4Index,
        (unsigned long long)ReadPageDirectoryEntryValue(Pml4, KernelPml4Index),
        TaskRunnerPml4Index,
        (unsigned long long)ReadPageDirectoryEntryValue(Pml4, TaskRunnerPml4Index),
        PML4_RECURSIVE_SLOT,
        (unsigned long long)ReadPageDirectoryEntryValue(Pml4, PML4_RECURSIVE_SLOT));

    DEBUG(TEXT("[AllocPageDirectory] LowTable[0]=%llx, KernelTable[0]=%llx, TaskRunnerTable[%u]=%llx"),
        (unsigned long long)ReadPageTableEntryValue(LowTable, 0),
        (unsigned long long)ReadPageTableEntryValue(KernelTable, 0),
        TaskRunnerTableIndex,
        (unsigned long long)ReadPageTableEntryValue(TaskRunnerTable, TaskRunnerTableIndex));

    DEBUG(TEXT("[AllocPageDirectory] TaskRunner VMA=%llx -> Physical=%x"),
        (unsigned long long)VMA_TASK_RUNNER,
        (UINT)TaskRunnerPhysical);

    DEBUG(TEXT("[AllocPageDirectory] Exit"));
    return Pml4Physical;

Out_Error64:

    if (Pml4Physical) FreePhysicalPage(Pml4Physical);
    if (LowPdptPhysical) FreePhysicalPage(LowPdptPhysical);
    if (KernelPdptPhysical) FreePhysicalPage(KernelPdptPhysical);
    if (TaskRunnerPdptPhysical) FreePhysicalPage(TaskRunnerPdptPhysical);
    if (LowDirectoryPhysical) FreePhysicalPage(LowDirectoryPhysical);
    if (KernelDirectoryPhysical) FreePhysicalPage(KernelDirectoryPhysical);
    if (TaskRunnerDirectoryPhysical) FreePhysicalPage(TaskRunnerDirectoryPhysical);
    if (PMA_LowTable) FreePhysicalPage(PMA_LowTable);
    if (PMA_KernelTable) FreePhysicalPage(PMA_KernelTable);
    if (PMA_TaskRunnerTable) FreePhysicalPage(PMA_TaskRunnerTable);

    return NULL;
}

/************************************************************************/

/**
 * @brief Placeholder memory manager initialization for x86-64.
 */
void MemoryArchInitializeManager(void) {
    // TODO: provide x86-64 specific memory initialization.
}

/************************************************************************/
