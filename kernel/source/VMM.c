
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/VMM.h"

#include "../include/Address.h"
#include "../include/Base.h"
#include "../include/Kernel.h"
#include "../include/Log.h"

/***************************************************************************/

U32 Memory = 1;
U32 Pages = 1;

/***************************************************************************\

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

\***************************************************************************/

void InitializeVirtualMemoryManager() {
    U32 NumPagesUsed = 0;
    U32 Index = 0;
    U32 Byte = 0;
    U32 Value = 0;

    KernelLogText(LOG_DEBUG, TEXT("[InitializeVirtualMemoryManager] Enter"));

    Memory = KernelStartup.MemorySize;
    Pages = Memory >> PAGE_SIZE_MUL;

    NumPagesUsed = (PA_KER + N_2MB) >> PAGE_SIZE_MUL;

    for (Index = 0; Index < NumPagesUsed; Index++) {
        Byte = Index >> MUL_8;
        Value = (U32)0x01 << (Index & 0x07);
        PPB[Byte] |= (U8)Value;
    }

    KernelLogText(LOG_DEBUG, TEXT("[InitializeVirtualMemoryManager] Exit"));
}

/***************************************************************************/

void SetPhysicalPageMark(U32 Page, U32 Used) {
    U32 Offset = 0;
    U32 Value = 0;

    if (Page >= Pages) return;

    LockMutex(MUTEX_MEMORY, INFINITY);

    Offset = Page >> MUL_8;
    Value = (U32)0x01 << (Page & 0x07);

    if (Used) {
        PPB[Offset] |= (U8)Value;
    } else {
        PPB[Offset] &= (U8)(~Value);
    }

    UnlockMutex(MUTEX_MEMORY);
}

/***************************************************************************/

U32 GetPhysicalPageMark(U32 Page) {
    U32 Offset = 0;
    U32 Value = 0;
    U32 RetVal = 0;

    if (Page >= Pages) return 0;

    LockMutex(MUTEX_MEMORY, INFINITY);

    Offset = Page >> MUL_8;
    Value = (U32)0x01 << (Page & 0x07);

    if (PPB[Offset] & Value) RetVal = 1;

    UnlockMutex(MUTEX_MEMORY);

    return RetVal;
}

/***************************************************************************/

PHYSICAL AllocPhysicalPage() {
    U32 Index = 0;
    U32 Start = 0;
    U32 Maximum = 0;
    U32 Value = 0;
    U32 Bit = 0;
    U32 Page = 0;
    U32 Mask = 0;
    PHYSICAL Pointer = 0;

    LockMutex(MUTEX_MEMORY, INFINITY);

    // KernelLogText(LOG_DEBUG, TEXT("[AllocPhysicalPage] Pages : %X"), Pages);

    Start = SYS_SIZE_PAGES >> MUL_8;
    Maximum = Pages >> MUL_8;

    for (Index = Start; Index < Maximum; Index++) {
        Value = PPB[Index];
        if (Value != 0xFF) {
            Page = Index << MUL_8;
            for (Bit = 0; Bit < 8; Bit++) {
                Mask = (U32)0x01 << Bit;
                if ((Value & Mask) == 0 && Page < Pages) {
                    PPB[Index] |= Mask;
                    Pointer = Page << PAGE_SIZE_MUL;
                    KernelLogText(LOG_DEBUG, TEXT("[AllocPhysicalPage] Found : %X"), Pointer);
                    goto Out;
                }
                Page++;
            }
        }
    }

Out:

    UnlockMutex(MUTEX_MEMORY);

    return Pointer;
}

/***************************************************************************/

static inline U32 GetDirectoryEntry(LINEAR Address) {
    return Address >> PAGE_TABLE_CAPACITY_MUL;
}

/***************************************************************************/

static inline U32 GetTableEntry(LINEAR Address) {
    return (Address & PAGE_TABLE_CAPACITY_MASK) >> PAGE_SIZE_MUL;
}

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

/***************************************************************************/

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

/***************************************************************************/

PHYSICAL AllocPageDirectory() {
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

        KernelLogText(LOG_ERROR, TEXT("[AllocPageDirectory] PA_Directory is null or PA_SysTable is null"));
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
    Directory[DirEntry].Address = PA_PGL >> PAGE_SIZE_MUL;

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
    Directory[DirEntry].Address = PA_PGH >> PAGE_SIZE_MUL;

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
    Directory[DirEntry].Address = PA_PGK >> PAGE_SIZE_MUL;

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

/***************************************************************************\

  AllocPageTable expands the page tables of the calling process by
  allocating a page table to map the "Base" linear address.

\***************************************************************************/

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

/***************************************************************************/

static BOOL IsRegionFree(LINEAR Base, U32 Size) {
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

        if (Directory[DirEntry].Address != NULL) {
            Table = (LPPAGETABLE)(LA_PAGETABLE + (DirEntry << PAGE_SIZE_MUL));
            if (Table[TabEntry].Address != NULL) return FALSE;
        }

        Base += PAGE_SIZE;
    }

    return TRUE;
}

/***************************************************************************\

  FindFreeRegion tries to find a linear address region of "Size" bytes
  which is not mapped in the page tables

\***************************************************************************/

static LINEAR FindFreeRegion(U32 Size) {
    U32 Base = N_4MB;

    while (1) {
        if (IsRegionFree(Base, Size) == TRUE) return Base;
        Base += PAGE_SIZE;
        if (Base >= LA_KERNEL) return NULL;
    }

    return NULL;
}

/***************************************************************************/

static void FreeEmptyPageTables() {
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

/***************************************************************************\

  VirtualAlloc is the most important memory management function.
  It allocates a linear address region to the calling process
  and sets up the page tables.
  If the user supplies a linear address as a base for the region,
  VirtualAlloc returns NULL if the region is already allocated.
  The pages can be physically allocated if the flags include
  ALLOC_PAGES_COMMIT or can be reserved (not present in physical
  memory) with the ALLOC_PAGES_RESERVE flag.

\***************************************************************************/

LINEAR VirtualAlloc(LINEAR Base, U32 Size, U32 Flags) {
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

    KernelLogText(LOG_DEBUG, TEXT("[VirtualAlloc] Enter"));

    Directory = (LPPAGEDIRECTORY) LA_DIRECTORY;
    NumPages = (((Size / 4096) + 1) * 4096) >> PAGE_SIZE_MUL;
    ReadWrite = (Flags & ALLOC_PAGES_READWRITE) ? 1 : 0;
    Privilege = PAGE_PRIVILEGE_USER;

    //-------------------------------------
    // If the calling process requests that a linear address be mapped,
    // see if the region is not already allocated.

    if (Base != MAX_U32) {
        if (IsRegionFree(Base, Size) == FALSE) {
            KernelLogText(LOG_DEBUG, TEXT("[VirtualAlloc] No free region found with specified base"));
            goto Out;
        }
    }

    //-------------------------------------
    // If the calling process does not care about the base address of
    // the region, try to find a region which is at least as large as
    // the "Size" parameter.

    if (Base == MAX_U32) {
        Base = FindFreeRegion(Size);
        if (Base == NULL) {
            KernelLogText(LOG_DEBUG, TEXT("[VirtualAlloc] No free region found with unspecified base"));
            goto Out;
        }
    }

    //-------------------------------------
    // Set the return value to "Base".

    Pointer = Base;

    //-------------------------------------
    // Allocate each page in turn.

    for (Index = 0; Index < NumPages; Index++) {
        DirEntry = GetDirectoryEntry(Base);
        TabEntry = GetTableEntry(Base);

        if (Directory[DirEntry].Address == NULL) {
            if (AllocPageTable(Base) == NULL) {
                VirtualFree(Pointer, Size);
                Pointer = NULL;
                KernelLogText(LOG_DEBUG, TEXT("[VirtualAlloc] AllocPageTable failed "));
                goto Out;
            }
        }

        Table = (LPPAGETABLE)(LA_PAGETABLE + (DirEntry << PAGE_SIZE_MUL));

        Table[TabEntry].Present = 0;
        Table[TabEntry].ReadWrite = ReadWrite;
        Table[TabEntry].Privilege = Privilege;
        Table[TabEntry].WriteThrough = 0;
        Table[TabEntry].CacheDisabled = 0;
        Table[TabEntry].Accessed = 0;
        Table[TabEntry].Dirty = 0;
        Table[TabEntry].Reserved = 0;
        Table[TabEntry].Global = 0;
        Table[TabEntry].User = 0;
        Table[TabEntry].Fixed = 0;
        Table[TabEntry].Address = MAX_U32 >> PAGE_SIZE_MUL;

        if (Flags & ALLOC_PAGES_COMMIT) {
            //-------------------------------------
            // Get a free physical page

            Physical = AllocPhysicalPage();

            Table[TabEntry].Present = 1;
            Table[TabEntry].Address = Physical >> PAGE_SIZE_MUL;
        }

        //-------------------------------------
        // Advance to next page

        Base += PAGE_SIZE;
    }

Out:

    //-------------------------------------
    // Flush the Translation Look-up Buffer of the CPU

    FlushTLB();

    KernelLogText(LOG_DEBUG, TEXT("[VirtualAlloc] Exit"));

    return Pointer;
}

/***************************************************************************/

BOOL VirtualFree(LINEAR Base, U32 Size) {
    LPPAGETABLE Directory = NULL;
    LPPAGETABLE Table = NULL;
    U32 DirEntry = 0;
    U32 TabEntry = 0;
    U32 NumPages = 0;
    U32 Index = 0;

    KernelLogText(LOG_DEBUG, TEXT("Entering VirtualFree"));

    Directory = (LPPAGETABLE)LA_DIRECTORY;
    NumPages = (((Size / 4096) + 1) * 4096) >> PAGE_SIZE_MUL;

    //-------------------------------------
    // Free each page in turn.

    for (Index = 0; Index < NumPages; Index++) {
        DirEntry = GetDirectoryEntry(Base);
        TabEntry = GetTableEntry(Base);

        if (Directory[DirEntry].Address != NULL) {
            Table = (LPPAGETABLE)(LA_PAGETABLE + (DirEntry << PAGE_SIZE_MUL));

            if (Table[TabEntry].Address != NULL) {
                SetPhysicalPageMark(Table[TabEntry].Address, 0);

                Table[TabEntry].Present = 0;
                Table[TabEntry].Address = NULL;
            }
        }

        Base += PAGE_SIZE;
    }

    FreeEmptyPageTables();

    //-------------------------------------
    // Flush the Translation Look-up Buffer of the CPU

    FlushTLB();

    KernelLogText(LOG_DEBUG, TEXT("Exiting VirtualFree"));

    return TRUE;
}

/***************************************************************************/
