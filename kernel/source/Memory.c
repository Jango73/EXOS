
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\************************************************************************/

#include "../include/Memory.h"

#include "../include/Address.h"
#include "../include/Base.h"
#include "../include/Kernel.h"
#include "../include/Log.h"

/************************************************************************\

    Organization of page tables

    For every process, a linear address of FF800000 maps to it's
    page directory, FF801000 points to the system page table,
    FF802000 points to the first page table, and so on...
    An access to those addresses in user privilege will emit
    a page fault because the pages have a kernel privilege level.
    Also note that they are marked as fixed, i.e. they do not go
    to the swap file.

             Page directory - 4096 bytes
             --------------------------------
    |--------| Entry 1 (00000000)           |<-|
    |  |-----| Entry 2 (00400000)           |  |
    |  |     | ...                          |  |
    |  |  |--| Entry 1022 (FF800000)        |  |
    |  |  |  --------------------------------  |
    |  |  |                                    |
    |  |  |  System page table - 4096 bytes    |
    |  |  |  Maps linear addresses             |
    |  |  |  FF800000 to FFBFFFFF              |
    |  |  |  Used to modify the pages          |
    |  |  |  --------------------------------  |
    |  |  |->| Entry 1                      |--|<--|
    |  |     | Entry 2                      |------|
    |  |     | Entry 3                      |--|
    |  |     | ...                          |--|--|
    |  |     --------------------------------  |  |
    |  |                                       |  |
    |  |     Page 0 - 4096 bytes               |  |
    |  |     Maps linear addresses             |  |
    |  |     00000000 to 003FFFFF              |  |
    |  |     --------------------------------  |  |
    |--|---->| Entry 1                      |<-|  |
       |     | Entry 2                      |     |
       |     | Entry 3                      |     |
       |     | ...                          |     |
       |     --------------------------------     |
       |                                          |
       |     Page 1 - 4096 bytes                  |
       |     Maps linear addresses                |
       |     00400000 to 007FFFFF                 |
       |     --------------------------------     |
       |---->| Entry 1                      |<----|
             | Entry 2                      |
             | Entry 3                      |
             | ...                          |
             --------------------------------

\************************************************************************/


/************************************************************************/

static void SetPhysicalPageMark(U32 Page, U32 Used) {
    U32 Offset = 0;
    U32 Value = 0;

    if (Page >= KernelStartup.PageCount) return;

    LockMutex(MUTEX_MEMORY, INFINITY);

    Offset = Page >> MUL_8;
    Value = (U32)0x01 << (Page & 0x07);

    if (Used) {
        Kernel_i386.PPB[Offset] |= (U8)Value;
    } else {
        Kernel_i386.PPB[Offset] &= (U8)(~Value);
    }

    UnlockMutex(MUTEX_MEMORY);
}

/***************************************************************************/

static U32 GetPhysicalPageMark(U32 Page) {
    U32 Offset = 0;
    U32 Value = 0;
    U32 RetVal = 0;

    if (Page >= KernelStartup.PageCount) return 0;

    LockMutex(MUTEX_MEMORY, INFINITY);

    Offset = Page >> MUL_8;
    Value = (U32)0x01 << (Page & 0x07);

    if (Kernel_i386.PPB[Offset] & Value) RetVal = 1;

    UnlockMutex(MUTEX_MEMORY);

    return RetVal;
}

/***************************************************************************/

// Marks a contiguous range of pages in the PPB (1 = used, 0 = free)
static void SetPhysicalPageRangeMark(U32 FirstPage, U32 PageCount, U32 Used) {

    KernelLogText(LOG_DEBUG, TEXT("[SetPhysicalPageRangeMark] Enter"));

    U32 End = FirstPage + PageCount;
    if (FirstPage >= KernelStartup.PageCount) return;
    if (End > KernelStartup.PageCount) End = KernelStartup.PageCount;

    KernelLogText(LOG_DEBUG, TEXT("[SetPhysicalPageRangeMark] Start, End : %X, %X"), FirstPage, End);

    for (U32 Page = FirstPage; Page < End; Page++) {
        U32 Byte = Page >> MUL_8;
        U8  Mask = (U8)(1u << (Page & 0x07));    // bit within byte
        if (Used) {
            Kernel_i386.PPB[Byte] |= Mask;
        } else {
            Kernel_i386.PPB[Byte] &= (U8)~Mask;
        }
    }
}

/************************************************************************/

void InitializeMemoryManager(void) {
    // KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] Enter"));

    KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] PPB : %X, %X"), Kernel_i386.PPB, KernelStartup.SI_Size_PPB);

    // 1) Clear PPB: start from "all free"
    //    PPB covers PageCount pages -> SI_Size_PPB bytes
    MemorySet(Kernel_i386.PPB, 0, KernelStartup.SI_Size_PPB);

    KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] Bitmap cleared"));

    // 2) Apply E820 map:
    //    - Mark non-USABLE ranges as used in PPB
    //    - Leave USABLE as free
    for (U32 i = 0; i < KernelStartup.E820_Count; i++) {
        U32 Base = KernelStartup.E820[i].Base.LO;
        U32 Size = KernelStartup.E820[i].Size.LO;
        U32 Type = KernelStartup.E820[i].Type;

        if (Size == 0) continue;

        // clip to our addressable page space
        U32 FirstPage = Base >> PAGE_SIZE_MUL;                          // floor(base/4K)
        U32 LastPage  = (Base + Size + PAGE_SIZE - 1) >> PAGE_SIZE_MUL; // ceil((base+size)/4K)

        if (FirstPage >= KernelStartup.PageCount) continue;
        if (LastPage > KernelStartup.PageCount) LastPage = KernelStartup.PageCount;
        if (LastPage <= FirstPage) continue;

        U32 PageCount = (LastPage - FirstPage);

        // Everything that is NOT "usable" must be reserved
        if (Type != BIOS_E820_TYPE_USABLE) {
            SetPhysicalPageRangeMark(FirstPage, PageCount, 1);
        }
    }

    KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] E820 reserved memory marked"));

    // 3) Force-reserve the whole kernel/system block (IDT..STK),
    //    regardless of E820 (it includes PPB, kernel, BSS, stack, etc.)
    //    Layout fields: SI_Phys_SYS (base), SI_Size_SYS (size). :contentReference[oaicite:2]{index=2}
    {
        U32 SysFirst = KernelStartup.SI_Phys_SYS >> PAGE_SIZE_MUL;
        U32 SysCount = (KernelStartup.SI_Size_SYS + PAGE_SIZE - 1) >> PAGE_SIZE_MUL;
        SetPhysicalPageRangeMark(SysFirst, SysCount, 1);
    }

    KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] System memory marked"));

    // 4) Keep pages up to (KER + 2MB) reserved,
    //    (prevents early allocator from dipping near the kernel image before drivers are up).
    /*
    {
        U32 GuardLastByte = (KernelStartup.SI_Phys_KER + N_2MB);
        U32 GuardPages    = GuardLastByte >> PAGE_SIZE_MUL;
        SetPhysicalPageRangeMark(0, GuardPages, 1);
    }
    */

    // 5) Make sure page 0 is never handed out.
    SetPhysicalPageRangeMark(0, 1, 1);

    // KernelLogText(LOG_DEBUG, TEXT("[InitializeMemoryManager] Exit"));
}

/***************************************************************************/

// Allocate one free physical page and mark it used in PPB.
// Returns the physical address (page-aligned) or 0 on failure.
PHYSICAL AllocPhysicalPage(void) {
    U32 i, v, bit, page, mask;
    U32 StartPage, StartByte, MaxByte;
    PHYSICAL result = 0;

    LockMutex(MUTEX_MEMORY, INFINITY);

    // Start from end of kernel region (KER + BSS + STK), in pages
    StartPage =
        (KernelStartup.SI_Phys_KER +
         KernelStartup.SI_Size_KER +
         KernelStartup.SI_Size_BSS +
         KernelStartup.SI_Size_STK) >> PAGE_SIZE_MUL;

    // Convert to PPB byte index
    StartByte = StartPage >> MUL_8;                 // == ((... >> 12) >> 3)
    MaxByte   = (KernelStartup.PageCount + 7) >> MUL_8;

    // Scan from StartByte upward
    for (i = StartByte; i < MaxByte; i++) {
        v = Kernel_i386.PPB[i];
        if (v != 0xFF) {
            page = (i << MUL_8);                    // first page covered by this byte
            for (bit = 0; bit < 8 && page < KernelStartup.PageCount; bit++, page++) {
                mask = 1u << bit;
                if ((v & mask) == 0) {
                    Kernel_i386.PPB[i] = (U8)(v | mask);
                    result = (PHYSICAL)page << PAGE_SIZE_MUL;  // page * 4096
                    goto Out;
                }
            }
        }
    }

Out:
    UnlockMutex(MUTEX_MEMORY);
    return result;
}

/***************************************************************************/

// Frees one physical page and mark it free in PPB.
// Returns the physical address (page-aligned) or 0 on failure.
void FreePhysicalPage(PHYSICAL Page) {
    UNUSED(Page);

    LockMutex(MUTEX_MEMORY, INFINITY);

    // TODO

    UnlockMutex(MUTEX_MEMORY);
    return;
}

/***************************************************************************/

static inline U32 GetDirectoryEntry(LINEAR Address) { return Address >> PAGE_TABLE_CAPACITY_MUL; }

/***************************************************************************/

static inline U32 GetTableEntry(LINEAR Address) { return (Address & PAGE_TABLE_CAPACITY_MASK) >> PAGE_SIZE_MUL; }

/***************************************************************************/

/*
static BOOL IsValidRegion(LINEAR Base, U32 Size) {
    LPPAGEDIRECTORY Directory = NULL;
    LPPAGETABLE Table = NULL;
    U32 DirEntry = 0;
    U32 TabEntry = 0;
    U32 NumPages = 0;
    U32 Index = 0;

    Directory = (LPPAGEDIRECTORY)LA_DIRECTORY;
    NumPages = Size >> PAGE_SIZE_MUL;
    if (NumPages == 0) NumPages = 1;

    for (Index = 0; Index < NumPages; Index++) {
        DirEntry = GetDirectoryEntry(Base);
        TabEntry = GetTableEntry(Base);

        if (Directory[DirEntry].Address == NULL) return FALSE;

        Table = (LPPAGETABLE)(LA_PAGETABLE + (DirEntry * PAGE_SIZE));

        if (Table[TabEntry].Address == NULL) return FALSE;

        Base += PAGE_SIZE;
    }

    return TRUE;
}
*/

/*************************************************************************/

static BOOL SetTempPage(PHYSICAL Physical) {
    LPPAGETABLE SysTable = NULL;

    SysTable = (LPPAGETABLE)LA_SYSTABLE;

    SysTable[1023].Present = 1;
    SysTable[1023].ReadWrite = 1;
    SysTable[1023].Privilege = PAGE_PRIVILEGE_KERNEL;
    SysTable[1023].WriteThrough = 0;
    SysTable[1023].CacheDisabled = 0;
    SysTable[1023].Accessed = 0;
    SysTable[1023].Dirty = 0;
    SysTable[1023].Reserved = 0;
    SysTable[1023].Global = 0;
    SysTable[1023].User = 0;
    SysTable[1023].Fixed = 1;
    SysTable[1023].Address = Physical >> PAGE_SIZE_MUL;

    FlushTLB();

    return TRUE;
}

/*************************************************************************/

LINEAR MapPhysicalPage(PHYSICAL Physical) {
    SetTempPage(Physical);
    return LA_TEMP;
}

/*************************************************************************/

PHYSICAL AllocPageDirectory(void) {
    PHYSICAL PA_Directory = NULL;
    PHYSICAL PA_SysTable = NULL;
    LPPAGEDIRECTORY Directory = NULL;
    LPPAGETABLE SysTable = NULL;
    U32 DirEntry = 0;

    //-------------------------------------
    // Allocate physical pages

    PA_Directory = AllocPhysicalPage();
    PA_SysTable = AllocPhysicalPage();

    if (PA_Directory == NULL || PA_SysTable == NULL) {
        SetPhysicalPageMark(PA_Directory >> PAGE_SIZE_MUL, 0);
        SetPhysicalPageMark(PA_SysTable >> PAGE_SIZE_MUL, 0);

        KernelLogText(
            LOG_ERROR, TEXT("[AllocPageDirectory] PA_Directory is "
                            "null or PA_SysTable is null"));
        goto Out;
    }

    //-------------------------------------
    // Fill the page directory

    SetTempPage(PA_Directory);

    Directory = (LPPAGEDIRECTORY)LA_TEMP;

    MemorySet(Directory, 0, PAGE_SIZE);

    //-------------------------------------
    // Map the system table

    DirEntry = LA_DIRECTORY >> PAGE_TABLE_CAPACITY_MUL;

    Directory[DirEntry].Present = 1;
    Directory[DirEntry].ReadWrite = 1;
    Directory[DirEntry].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[DirEntry].WriteThrough = 0;
    Directory[DirEntry].CacheDisabled = 0;
    Directory[DirEntry].Accessed = 0;
    Directory[DirEntry].Reserved = 0;
    Directory[DirEntry].PageSize = 0;
    Directory[DirEntry].Global = 0;
    Directory[DirEntry].User = 0;
    Directory[DirEntry].Fixed = 1;
    Directory[DirEntry].Address = PA_SysTable >> PAGE_SIZE_MUL;

    //-------------------------------------
    // Map the low memory

    DirEntry = 0;

    Directory[DirEntry].Present = 1;
    Directory[DirEntry].ReadWrite = 1;
    Directory[DirEntry].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[DirEntry].WriteThrough = 0;
    Directory[DirEntry].CacheDisabled = 0;
    Directory[DirEntry].Accessed = 0;
    Directory[DirEntry].Reserved = 0;
    Directory[DirEntry].PageSize = 0;
    Directory[DirEntry].Global = 0;
    Directory[DirEntry].User = 0;
    Directory[DirEntry].Fixed = 1;
    Directory[DirEntry].Address = KernelStartup.SI_Phys_PGL >> PAGE_SIZE_MUL;

    //-------------------------------------
    // Map the system

    DirEntry = LA_SYSTEM >> PAGE_TABLE_CAPACITY_MUL;

    Directory[DirEntry].Present = 1;
    Directory[DirEntry].ReadWrite = 1;
    Directory[DirEntry].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[DirEntry].WriteThrough = 0;
    Directory[DirEntry].CacheDisabled = 0;
    Directory[DirEntry].Accessed = 0;
    Directory[DirEntry].Reserved = 0;
    Directory[DirEntry].PageSize = 0;
    Directory[DirEntry].Global = 0;
    Directory[DirEntry].User = 0;
    Directory[DirEntry].Fixed = 1;
    Directory[DirEntry].Address = KernelStartup.SI_Phys_PGH >> PAGE_SIZE_MUL;

    //-------------------------------------
    // Map the kernel

    DirEntry = LA_KERNEL >> PAGE_TABLE_CAPACITY_MUL;

    Directory[DirEntry].Present = 1;
    Directory[DirEntry].ReadWrite = 1;
    Directory[DirEntry].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[DirEntry].WriteThrough = 0;
    Directory[DirEntry].CacheDisabled = 0;
    Directory[DirEntry].Accessed = 0;
    Directory[DirEntry].Reserved = 0;
    Directory[DirEntry].PageSize = 0;
    Directory[DirEntry].Global = 0;
    Directory[DirEntry].User = 0;
    Directory[DirEntry].Fixed = 1;
    Directory[DirEntry].Address = KernelStartup.SI_Phys_PGK >> PAGE_SIZE_MUL;

    //-------------------------------------
    // Fill the system page

    SetTempPage(PA_SysTable);

    SysTable = (LPPAGETABLE)LA_TEMP;

    MemorySet(SysTable, 0, PAGE_SIZE);

    SysTable[0].Present = 1;
    SysTable[0].ReadWrite = 1;
    SysTable[0].Privilege = PAGE_PRIVILEGE_KERNEL;
    SysTable[0].WriteThrough = 0;
    SysTable[0].CacheDisabled = 0;
    SysTable[0].Accessed = 0;
    SysTable[0].Dirty = 0;
    SysTable[0].Reserved = 0;
    SysTable[0].Global = 0;
    SysTable[0].User = 0;
    SysTable[0].Fixed = 1;
    SysTable[0].Address = PA_Directory >> PAGE_SIZE_MUL;

    SysTable[1].Present = 1;
    SysTable[1].ReadWrite = 1;
    SysTable[1].Privilege = PAGE_PRIVILEGE_KERNEL;
    SysTable[1].WriteThrough = 0;
    SysTable[1].CacheDisabled = 0;
    SysTable[1].Accessed = 0;
    SysTable[1].Dirty = 0;
    SysTable[1].Reserved = 0;
    SysTable[1].Global = 0;
    SysTable[1].User = 0;
    SysTable[1].Fixed = 1;
    SysTable[1].Address = PA_SysTable >> PAGE_SIZE_MUL;

Out:

    return PA_Directory;
}

/*************************************************************************/
// AllocPageTable expands the page tables of the calling process by
// allocating a page table to map the "Base" linear address.

static LINEAR AllocPageTable(LINEAR Base) {
    LPPAGEDIRECTORY Directory = NULL;
    LPPAGETABLE SysTable = NULL;
    PHYSICAL PA_Table = NULL;
    LINEAR LA_Table = NULL;
    U32 DirEntry = 0;
    U32 SysEntry = 0;

    Directory = (LPPAGEDIRECTORY)LA_DIRECTORY;
    DirEntry = GetDirectoryEntry(Base);

    if (Directory[DirEntry].Address != NULL) {
        KernelLogText(LOG_ERROR, TEXT("[AllocPageTable] Directory[DirEntry].Address is not null"));
        return NULL;
    }

    //-------------------------------------
    // Allocate a physical page to store the new table

    PA_Table = AllocPhysicalPage();

    if (PA_Table == NULL) {
        KernelLogText(LOG_ERROR, TEXT("[AllocPageTable] AllocPhysicalPage failed"));
        return NULL;
    }

    //-------------------------------------
    // Fill the directory entry that describes the new table

    Directory[DirEntry].Present = 1;
    Directory[DirEntry].ReadWrite = 1;
    Directory[DirEntry].Privilege = PAGE_PRIVILEGE_KERNEL;
    Directory[DirEntry].WriteThrough = 0;
    Directory[DirEntry].CacheDisabled = 0;
    Directory[DirEntry].Accessed = 0;
    Directory[DirEntry].Reserved = 0;
    Directory[DirEntry].PageSize = 0;
    Directory[DirEntry].Global = 0;
    Directory[DirEntry].User = 0;
    Directory[DirEntry].Fixed = 1;
    Directory[DirEntry].Address = PA_Table >> PAGE_SIZE_MUL;

    //-------------------------------------
    // Compute the linear address of the table
    // Linear address 0xFF800000 is the page directory
    // Linear address 0xFF801000 is the system page table
    // Linear address 0xFF802000 is the first page table
    // Linear address 0xFF803000 is the second page table
    // ...
    // LA_Table should be between 0xFF801000 and 0xFFFFFFFF

    LA_Table = (LA_PAGETABLE + (DirEntry << PAGE_SIZE_MUL));

    //-------------------------------------
    // Map PA_Table in the system page table

    SysTable = (LPPAGETABLE)LA_SYSTABLE;
    SysEntry = DirEntry + 2;

    SysTable[SysEntry].Present = 1;
    SysTable[SysEntry].ReadWrite = 1;
    SysTable[SysEntry].Privilege = PAGE_PRIVILEGE_KERNEL;
    SysTable[SysEntry].WriteThrough = 0;
    SysTable[SysEntry].CacheDisabled = 0;
    SysTable[SysEntry].Accessed = 0;
    SysTable[SysEntry].Dirty = 0;
    SysTable[SysEntry].Reserved = 0;
    SysTable[SysEntry].Global = 0;
    SysTable[SysEntry].User = 0;
    SysTable[SysEntry].Fixed = 1;
    SysTable[SysEntry].Address = PA_Table >> PAGE_SIZE_MUL;

    //-------------------------------------
    // Clear the new table

    MemorySet((LPVOID)LA_Table, 0, PAGE_SIZE);

    //-------------------------------------
    // Flush the Translation Look-up Buffer of the CPU

    FlushTLB();

    return LA_Table;
}

/*************************************************************************/

static BOOL IsRegionFree(LINEAR Base, U32 Size) {
    LPPAGEDIRECTORY Directory = NULL;
    LPPAGETABLE Table = NULL;
    U32 DirEntry = 0;
    U32 TabEntry = 0;
    U32 NumPages = 0;
    U32 Index = 0;

    Directory = (LPPAGEDIRECTORY)LA_DIRECTORY;

    NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;   // ceil(Size / 4096)
    if (NumPages == 0) NumPages = 1;

    for (Index = 0; Index < NumPages; Index++) {
        DirEntry = GetDirectoryEntry(Base);
        TabEntry = GetTableEntry(Base);

        if (Directory[DirEntry].Address != NULL) {
            Table = (LPPAGETABLE)(LA_PAGETABLE + (DirEntry << PAGE_SIZE_MUL));
            if (Table[TabEntry].Address != NULL) return FALSE;
        }

        Base += PAGE_SIZE;
    }

    return TRUE;
}

/*************************************************************************/
// Tries to find a linear address region of "Size" bytes
// which is not mapped in the page tables

static LINEAR FindFreeRegion(U32 Size) {
    U32 Base = N_4MB;

    while (1) {
        if (IsRegionFree(Base, Size) == TRUE) return Base;
        Base += PAGE_SIZE;
        if (Base >= LA_KERNEL) return NULL;
    }

    return NULL;
}

/*************************************************************************/
// Checks if a physical memory range is free

static BOOL IsPhysicalRangeFree(PHYSICAL Target, U32 NumPages) {
    U32 PageIndex = 0;

    if ((Target & (PAGE_SIZE - 1)) != 0) return FALSE;
    for (PageIndex = 0; PageIndex < NumPages; PageIndex++) {
        U32 Page = (Target >> PAGE_SIZE_MUL) + PageIndex;
        if (GetPhysicalPageMark(Page)) return FALSE;
    }
    return TRUE;
}

/*************************************************************************/

static void FreeEmptyPageTables(void) {
    LPPAGEDIRECTORY Directory = NULL;
    LPPAGETABLE Table = NULL;
    LINEAR Base = N_4MB;
    U32 DirEntry = 0;
    U32 Index = 0;
    U32 DestroyIt = 0;

    Directory = (LPPAGEDIRECTORY)LA_DIRECTORY;

    while (Base < LA_KERNEL) {
        DestroyIt = 1;
        DirEntry = GetDirectoryEntry(Base);

        if (Directory[DirEntry].Address != NULL) {
            Table = (LPPAGETABLE)(LA_PAGETABLE + (DirEntry << PAGE_SIZE_MUL));

            for (Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
                if (Table[Index].Address != NULL) DestroyIt = 0;
            }

            if (DestroyIt) {
                SetPhysicalPageMark(Directory[DirEntry].Address, 0);
                Directory[DirEntry].Present = 0;
                Directory[DirEntry].Address = NULL;
            }
        }

        Base += PAGE_TABLE_CAPACITY;
    }
}

/*************************************************************************/
// Maps a linear address to its physical address (page-level granularity).
// Returns 0 on failure.

PHYSICAL MapLinearToPhysical(LINEAR Address) {
    LPPAGEDIRECTORY Directory = (LPPAGEDIRECTORY)LA_DIRECTORY;
    LPPAGETABLE Table;
    U32 DirEntry = GetDirectoryEntry(Address);
    U32 TabEntry = GetTableEntry(Address);

    if (Directory[DirEntry].Address == 0) return 0;

    Table = (LPPAGETABLE)(LA_PAGETABLE + (DirEntry << PAGE_SIZE_MUL));
    if (Table[TabEntry].Address == 0) return 0;

    // Compose physical: page frame << 12 | offset-in-page
    return (PHYSICAL)((Table[TabEntry].Address << PAGE_SIZE_MUL) | (Address & (PAGE_SIZE - 1)));
}

/***************************************************************************\

  AllocRegion is the most important memory management function.
  It allocates a linear address memory region to the calling process
  and sets up the page tables.

  If the user supplies a linear address as a base for the region,
  AllocRegion returns NULL if the region is already allocated.

  If the user supplies a physical target address, AllocRegion returns
  NULL if the physical region is not free.

  The pages can be physically allocated if the flags include
  ALLOC_PAGES_COMMIT or can be reserved (not present in physical
  memory) with the ALLOC_PAGES_RESERVE flag.

\***************************************************************************/

LINEAR AllocRegion(LINEAR Base, PHYSICAL Target, U32 Size, U32 Flags) {
    LPPAGEDIRECTORY Directory = NULL;
    LPPAGETABLE Table = NULL;
    LINEAR Pointer = NULL;
    PHYSICAL Physical = NULL;
    U32 DirEntry = 0;
    U32 TabEntry = 0;
    U32 NumPages = 0;
    U32 ReadWrite = 0;
    U32 Privilege = 0;
    U32 Index = 0;

    // KernelLogText(LOG_DEBUG, TEXT("[AllocRegion] Enter"));

    // Can't allocate more than 25% of total memory at once
    if (Size > KernelStartup.MemorySize) {
        Pointer = NULL;
        goto Out;
    }

    Directory = (LPPAGEDIRECTORY)LA_DIRECTORY;

    // Rounding behavior for page count
    NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;   // ceil(Size / 4096)
    if (NumPages == 0) NumPages = 1;

    ReadWrite = (Flags & ALLOC_PAGES_READWRITE) ? 1 : 0;
    Privilege = PAGE_PRIVILEGE_USER;

    // Derive cache policy flags for PTE
    U32 PteCacheDisabled = (Flags & ALLOC_PAGES_UC) ? 1 : 0;
    U32 PteWriteThrough = (Flags & ALLOC_PAGES_WC) ? 1 : 0;

    // If UC is set, it dominates; WT must be 0 in that case.
    if (PteCacheDisabled) PteWriteThrough = 0;

    // If an exact physical mapping is requested, validate inputs
    if (Target != 0 && (Flags & ALLOC_PAGES_IO) == 0) {
        if ((Target & (PAGE_SIZE - 1)) != 0) {
            KernelLogText(LOG_ERROR, TEXT("[AllocRegion] Target not page-aligned (%X)"), Target);
            goto Out;
        }
        if ((Flags & ALLOC_PAGES_COMMIT) == 0) {
            KernelLogText(LOG_ERROR, TEXT("[AllocRegion] Exact PMA mapping requires COMMIT"));
            goto Out;
        }
        // NOTE: Do not reject pages already marked used here.
        // Target may come from AllocPhysicalPage(), which marks the page in the bitmap.
        // We will just map it and keep the mark consistent.
    }

    // If the calling process requests that a linear address be mapped,
    // see if the region is not already allocated.
    if (Base != 0) {
        if (IsRegionFree(Base, Size) == FALSE) {
            KernelLogText(LOG_DEBUG, TEXT("[AllocRegion] No free region found with specified base"));
            goto Out;
        }
    }

    // If the calling process does not care about the base address of
    // the region, try to find a region which is at least as large as
    // the "Size" parameter.
    if (Base == 0) {
        Base = FindFreeRegion(Size);
        if (Base == NULL) {
            KernelLogText(LOG_DEBUG, TEXT("[AllocRegion] No free region found with unspecified base"));
            goto Out;
        }
    }

    // Set the return value to "Base".
    Pointer = Base;

    // Allocate each page in turn.
    for (Index = 0; Index < NumPages; Index++) {
        DirEntry = GetDirectoryEntry(Base);
        TabEntry = GetTableEntry(Base);

        if (Directory[DirEntry].Address == NULL) {
            if (AllocPageTable(Base) == NULL) {
                FreeRegion(Pointer, Size);
                Pointer = NULL;
                KernelLogText(LOG_DEBUG, TEXT("[AllocRegion] AllocPageTable failed "));
                goto Out;
            }
        }

        Table = (LPPAGETABLE)(LA_PAGETABLE + (DirEntry << PAGE_SIZE_MUL));

        Table[TabEntry].Present = 0;
        Table[TabEntry].ReadWrite = ReadWrite;
        Table[TabEntry].Privilege = Privilege;
        Table[TabEntry].WriteThrough = PteWriteThrough;
        Table[TabEntry].CacheDisabled = PteCacheDisabled;
        Table[TabEntry].Accessed = 0;
        Table[TabEntry].Dirty = 0;
        Table[TabEntry].Reserved = 0;
        Table[TabEntry].Global = 0;
        Table[TabEntry].User = 0;
        Table[TabEntry].Fixed = 0;
        Table[TabEntry].Address = MAX_U32 >> PAGE_SIZE_MUL;

        if (Flags & ALLOC_PAGES_COMMIT) {
            if (Target != 0) {
                Physical = Target + (Index << PAGE_SIZE_MUL);

                if (Flags & ALLOC_PAGES_IO) {
                    // IO mapping (BAR) -> no bitmap mark, Fixed=1
                    Table[TabEntry].Fixed = 1;
                    Table[TabEntry].Present = 1;
                    Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
                } else {
                    // RAM mapping
                    SetPhysicalPageMark(Physical >> PAGE_SIZE_MUL, 1);
                    Table[TabEntry].Present = 1;
                    Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
                }
            } else {
                // Legacy path: allocate any free physical page
                Physical = AllocPhysicalPage();

                if (Physical == NULL) {
                    KernelLogText(LOG_ERROR, TEXT("[AllocRegion] AllocPhysicalPage failed"));
                    // Roll back pages mapped so far
                    FreeRegion(Pointer, (Index << PAGE_SIZE_MUL));
                    Pointer = NULL;
                    goto Out;
                }

                Table[TabEntry].Present = 1;
                Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
            }
        }

        // Advance to next page
        Base += PAGE_SIZE;
    }

Out:

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

    // KernelLogText(LOG_DEBUG, TEXT("[AllocRegion] Exit"));

    return Pointer;
}

/***************************************************************************/

BOOL FreeRegion(LINEAR Base, U32 Size) {
    LPPAGETABLE Directory = NULL;
    LPPAGETABLE Table = NULL;
    U32 DirEntry = 0;
    U32 TabEntry = 0;
    U32 NumPages = 0;
    U32 Index = 0;

    // KernelLogText(LOG_DEBUG, TEXT("Entering FreeRegion"));

    Directory = (LPPAGETABLE)LA_DIRECTORY;

    NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;   // ceil(Size / 4096)
    if (NumPages == 0) NumPages = 1;

    //-------------------------------------
    // Free each page in turn.

    for (Index = 0; Index < NumPages; Index++) {
        DirEntry = GetDirectoryEntry(Base);
        TabEntry = GetTableEntry(Base);

        if (Directory[DirEntry].Address != NULL) {
            Table = (LPPAGETABLE)(LA_PAGETABLE + (DirEntry << PAGE_SIZE_MUL));

            if (Table[TabEntry].Address != NULL) {
                // Skip bitmap mark if it was an IO mapping (BAR)
                if (Table[TabEntry].Fixed == 0) {
                    SetPhysicalPageMark(Table[TabEntry].Address, 0);
                }

                Table[TabEntry].Present = 0;
                Table[TabEntry].Address = NULL;
                Table[TabEntry].Fixed = 0;
            }
        }

        Base += PAGE_SIZE;
    }

    FreeEmptyPageTables();

    //-------------------------------------
    // Flush the Translation Look-up Buffer of the CPU

    FlushTLB();

    // KernelLogText(LOG_DEBUG, TEXT("Exiting FreeRegion"));

    return TRUE;
}

/***************************************************************************\

  PCI BAR mapping process (example: Intel E1000 NIC)

  ┌───────────────────────────┐
  │  PCI Configuration Space  │
  │  (accessed via PCI config │
  │   reads/writes)           │
  └─────────────┬─────────────┘
                │
                │ Read BAR0 (Base Address Register #0)
                ▼
       ┌────────────────────────────────┐
       │ BAR0 value = Physical address  │
       │ of device registers (MMIO)     │
       │ + resource size                │
       └─────────────┬──────────────────┘
                     │
                     │ Map physical MMIO region into
                     │ kernel virtual space
                     │ (uncached for DMA safety)
                     │
                     ▼
         ┌───────────────────────────┐
         │ AllocRegion(Base=0,      │
         │   Target=BAR0,            │
         │   Size=MMIO size,         │
         │   Flags=ALLOC_PAGES_COMMIT│
         │         | ALLOC_PAGES_UC) │
         └─────────┬─────────────────┘
                   │
                   │ Returns Linear (VMA) address
                   │ where the driver can access MMIO
                   ▼
       ┌───────────────────────────────┐
       │ Driver reads/writes registers │
       │ via *(volatile U32*)(VMA+ofs) │
       │ Example: E1000_CTRL register  │
       └───────────────────────────────┘

  NOTES:
   - MMIO (Memory-Mapped I/O) must be UNCACHED (UC) to avoid
     stale data and incorrect ordering.
   - BARs can also point to I/O port ranges instead of MMIO.
   - PCI devices can have multiple BARs for different resources.

\***************************************************************************/

LINEAR MmMapIo(PHYSICAL PhysicalBase, U32 Size) {
    // Basic parameter checks
    if (PhysicalBase == 0 || Size == 0) {
        KernelLogText(LOG_ERROR, TEXT("[MmMapIo] Invalid parameters (PA=%X Size=%X)"), PhysicalBase, Size);
        return NULL;
    }

    if ((PhysicalBase & (PAGE_SIZE - 1)) != 0) {
        KernelLogText(LOG_ERROR, TEXT("[MmMapIo] Physical base not page-aligned (%X)"), PhysicalBase);
        return NULL;
    }

    // Map as Uncached, Read/Write, exact PMA mapping, IO semantics
    return AllocRegion(
        0,             // Let AllocRegion choose virtual address
        PhysicalBase,  // Exact PMA (BAR)
        Size,
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_UC |  // MMIO must be UC
            ALLOC_PAGES_IO                                             // Do not touch RAM bitmap; mark PTE.Fixed
    );
}

/***************************************************************************/

BOOL MmUnmapIo(LINEAR LinearBase, U32 Size) {
    // Basic parameter checks
    if (LinearBase == 0 || Size == 0) {
        KernelLogText(LOG_ERROR, TEXT("[MmUnmapIo] Invalid parameters (LA=%X Size=%X)"), LinearBase, Size);
        return FALSE;
    }

    // Just unmap; FreeRegion will skip RAM bitmap if PTE.Fixed was set
    return FreeRegion(LinearBase, Size);
}

/***************************************************************************/
