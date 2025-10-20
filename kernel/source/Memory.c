
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


    Memory

\************************************************************************/

#include "Memory.h"

#include "Base.h"
#include "Console.h"
#include "Kernel.h"
#include "Arch.h"
#include "Log.h"
#include "CoreString.h"
#if defined(__EXOS_ARCH_I386__)
#include "arch/i386/i386-Log.h"
#endif
#include "Schedule.h"
#include "System.h"

/************************************************************************/
// INTERNAL SELF-MAP + TEMP MAPPING ]

#if defined(__EXOS_ARCH_X86_64__)
static LINEAR G_TempLinear1 = (LINEAR)X86_64_TEMP_LINEAR_PAGE_1;
static LINEAR G_TempLinear2 = (LINEAR)X86_64_TEMP_LINEAR_PAGE_2;
static LINEAR G_TempLinear3 = (LINEAR)X86_64_TEMP_LINEAR_PAGE_3;
#elif defined(__EXOS_ARCH_I386__)
static LINEAR G_TempLinear1 = I386_TEMP_LINEAR_PAGE_1;
static LINEAR G_TempLinear2 = I386_TEMP_LINEAR_PAGE_2;
static LINEAR G_TempLinear3 = I386_TEMP_LINEAR_PAGE_3;
#else
static LINEAR G_TempLinear1 = 0;
static LINEAR G_TempLinear2 = 0;
static LINEAR G_TempLinear3 = 0;
#endif

/************************************************************************/

static BOOL ValidatePhysicalTargetRange(PHYSICAL Base, UINT NumPages) {
    if (Base == 0 || NumPages == 0) return TRUE;

    UINT RequestedLength = NumPages << PAGE_SIZE_MUL;

#if defined(__EXOS_ARCH_I386__)
    U64 RangeBase;
    RangeBase.LO = (U32)Base;
    RangeBase.HI = 0;

    U64 RangeLength;
    RangeLength.LO = (U32)RequestedLength;
    RangeLength.HI = 0;

    PHYSICAL ClippedBase = 0;
    UINT ClippedLength = 0;

    if (ArchClipPhysicalRange(RangeBase, RangeLength, &ClippedBase, &ClippedLength) == FALSE) return FALSE;

    return (ClippedBase == Base && ClippedLength == RequestedLength);
#else
    PHYSICAL ClippedBase = 0;
    UINT ClippedLength = 0;

    if (ArchClipPhysicalRange((U64)Base, (U64)RequestedLength, &ClippedBase, &ClippedLength) == FALSE) return FALSE;

    return (ClippedBase == Base && ClippedLength == RequestedLength);
#endif
}

/************************************************************************/

/**
 * @brief Read physical memory into a caller-provided buffer.
 * @param PhysicalAddress Physical address to read from.
 * @param Buffer Destination buffer.
 * @param Length Number of bytes to copy.
 * @return TRUE when the entire range was copied, FALSE otherwise.
 */
BOOL ReadPhysicalMemory(PHYSICAL PhysicalAddress, LPVOID Buffer, UINT Length) {
    if (Length == 0 || Buffer == NULL) {
        return FALSE;
    }

    UINT Remaining = Length;
    UINT Copied = 0;

    while (Remaining > 0) {
        PHYSICAL PagePhysical = (PhysicalAddress + Copied) & ~((PHYSICAL)(PAGE_SIZE - 1));
        LINEAR Mapping = MapTemporaryPhysicalPage1(PagePhysical);
        if (Mapping == 0) {
            DEBUG(TEXT("[ReadPhysicalMemory] Failed to map physical %p"),
                  (LPVOID)(LINEAR)(PhysicalAddress + Copied));
            return FALSE;
        }

        UINT PageOffset = (UINT)((PhysicalAddress + Copied) - PagePhysical);
        UINT Chunk = PAGE_SIZE - PageOffset;
        if (Chunk > Remaining) {
            Chunk = Remaining;
        }

        MemoryCopy((U8*)Buffer + Copied, (LPCVOID)(Mapping + PageOffset), Chunk);

        Copied += Chunk;
        Remaining -= Chunk;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Mark a physical page as used or free in the PPB.
 * @param Page Page index.
 * @param Used Non-zero to mark used.
 */
static void SetPhysicalPageMark(UINT Page, UINT Used) {
    UINT Offset = 0;
    UINT Value = 0;

    if (Page >= KernelStartup.PageCount) return;

    LockMutex(MUTEX_MEMORY, INFINITY);

    Offset = Page >> MUL_8;
    Value = (UINT)0x01 << (Page & 0x07);

    if (Used) {
        Kernel.PPB[Offset] |= (U8)Value;
    } else {
        Kernel.PPB[Offset] &= (U8)(~Value);
    }

    UnlockMutex(MUTEX_MEMORY);
}

/************************************************************************/

/**
 * @brief Query the usage mark of a physical page.
 * @param Page Page index.
 * @return Non-zero if page is used.
 */
/*
static U32 GetPhysicalPageMark(U32 Page) {
    U32 Offset = 0;
    U32 Value = 0;
    U32 RetVal = 0;

    if (Page >= KernelStartup.PageCount) return 0;

    LockMutex(MUTEX_MEMORY, INFINITY);

    Offset = Page >> MUL_8;
    Value = (U32)0x01 << (Page & 0x07);

    if (Kernel.PPB[Offset] & Value) RetVal = 1;

    UnlockMutex(MUTEX_MEMORY);

    return RetVal;
}
*/

/************************************************************************/

/**
 * @brief Mark a range of physical pages as used or free.
 * @param FirstPage First page index.
 * @param PageCount Number of pages.
 * @param Used Non-zero to mark used.
 */
static void SetPhysicalPageRangeMark(UINT FirstPage, UINT PageCount, UINT Used) {
    DEBUG(TEXT("[SetPhysicalPageRangeMark] Enter"));

    UINT End = FirstPage + PageCount;
    if (FirstPage >= KernelStartup.PageCount) return;
    if (End > KernelStartup.PageCount) End = KernelStartup.PageCount;

    DEBUG(TEXT("[SetPhysicalPageRangeMark] Start, End : %x, %x"), FirstPage, End);

    for (UINT Page = FirstPage; Page < End; Page++) {
        UINT Byte = Page >> MUL_8;
        U8 Mask = (U8)(1u << (Page & 0x07)); /* bit within byte */
        if (Used) {
            Kernel.PPB[Byte] |= Mask;
        } else {
            Kernel.PPB[Byte] &= (U8)~Mask;
        }
    }
}

/************************************************************************/

/**
 * @brief Update kernel memory metrics from the Multiboot memory map.
 */
void UpdateKernelMemoryMetricsFromMultibootMap(void) {
    PHYSICAL MaxUsableRAM = 0;

    for (UINT Index = 0; Index < KernelStartup.MultibootMemoryEntryCount; Index++) {
        const MULTIBOOTMEMORYENTRY* Entry = &KernelStartup.MultibootMemoryEntries[Index];
        PHYSICAL Base = 0;
        UINT Size = 0;

        if (ArchClipPhysicalRange(Entry->Base, Entry->Length, &Base, &Size) == FALSE) {
            continue;
        }

        if (Entry->Type == MULTIBOOT_MEMORY_AVAILABLE) {
            PHYSICAL EntryEnd = Base + Size;
            if (EntryEnd > MaxUsableRAM) {
                MaxUsableRAM = EntryEnd;
            }
        }
    }

    KernelStartup.MemorySize = MaxUsableRAM;
    if (KernelStartup.MemorySize == 0) {
        KernelStartup.PageCount = 0;
    } else {
        KernelStartup.PageCount = (KernelStartup.MemorySize + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;
    }
}

/************************************************************************/

/**
 * @brief Public wrapper to mark reserved and used physical pages.
 */
void MarkUsedPhysicalMemory(void) {
    DEBUG(TEXT("[MarkUsedPhysicalMemory] Enter"));

    UpdateKernelMemoryMetricsFromMultibootMap();

    if (KernelStartup.PageCount == 0) {
        DEBUG(TEXT("[MarkUsedPhysicalMemory] No physical memory detected"));
        return;
    }

    PHYSICAL PpbPhysicalBase = (PHYSICAL)(UINT)(Kernel.PPB);
    PHYSICAL ReservedEnd = PAGE_ALIGN(PpbPhysicalBase + (PHYSICAL)Kernel.PPBSize);
    UINT ReservedPageCount = (UINT)(ReservedEnd >> PAGE_SIZE_MUL);

    SetPhysicalPageRangeMark(0, ReservedPageCount, 1);

    // Derive total memory size and number of pages from the Multiboot map
    for (UINT i = 0; i < KernelStartup.MultibootMemoryEntryCount; i++) {
        const MULTIBOOTMEMORYENTRY* Entry = &KernelStartup.MultibootMemoryEntries[i];
        PHYSICAL Base = 0;
        UINT Size = 0;

        DEBUG(TEXT("[MarkUsedPhysicalMemory] Entry base = %p, size = %x, type = %x"), Entry->Base, Entry->Length, Entry->Type);

        if (ArchClipPhysicalRange(Entry->Base, Entry->Length, &Base, &Size) == FALSE) {
            continue;
        }

        if (Entry->Type != MULTIBOOT_MEMORY_AVAILABLE) {
            UINT FirstPage = (UINT)(Base >> PAGE_SIZE_MUL);
            UINT PageCount = (UINT)((Size + PAGE_SIZE - 1) >> PAGE_SIZE_MUL);
            SetPhysicalPageRangeMark(FirstPage, PageCount, 1);
        }
    }

    DEBUG(TEXT("[MarkUsedPhysicalMemory] Memory size = %u"), KernelStartup.MemorySize);
}

/************************************************************************/

/**
 * @brief Allocate a free physical page.
 * @return Physical page number or MAX_U32 on failure.
 */
PHYSICAL AllocPhysicalPage(void) {
    UINT i = 0;
    UINT bit = 0;
    UINT page = 0;
    UINT mask = 0;
    UINT StartPage = 0;
    UINT StartByte = 0;
    UINT MaxByte = 0;
    PHYSICAL result = 0;

    // DEBUG(TEXT("[AllocPhysicalPage] Enter"));

    LockMutex(MUTEX_MEMORY, INFINITY);

    // Start from end of kernel region
    StartPage = RESERVED_LOW_MEMORY >> PAGE_SIZE_MUL;

    // Convert to PPB byte index
    StartByte = StartPage >> MUL_8; /* == ((... >> 12) >> 3) */
    MaxByte = (KernelStartup.PageCount + 7) >> MUL_8;

    /* Scan from StartByte upward */
    for (i = StartByte; i < MaxByte; i++) {
        U8 v = Kernel.PPB[i];
        if (v != 0xFF) {
            page = (i << MUL_8); /* first page covered by this byte */
            for (bit = 0; bit < 8 && page < KernelStartup.PageCount; bit++, page++) {
                mask = 1u << bit;
                if ((v & mask) == 0) {
                    Kernel.PPB[i] = (U8)(v | (U8)mask);
                    result = (PHYSICAL)(page << PAGE_SIZE_MUL); /* page * 4096 */
                    goto Out;
                }
            }
        }
    }

Out:
    // DEBUG(TEXT("[AllocPhysicalPage] Exit"));

    UnlockMutex(MUTEX_MEMORY);
    return result;
}

/************************************************************************/

/**
 * @brief Release a previously allocated physical page.
 * @param Page Page number to free.
 */
void FreePhysicalPage(PHYSICAL Page) {
    UINT StartPage = 0;
    UINT PageIndex = 0;

    if ((Page & (PAGE_SIZE - 1)) != 0) {
        ERROR(TEXT("[FreePhysicalPage] Physical address not page-aligned (%x)"), Page);
        return;
    }

    // Start from end of kernel region (KER + BSS + STK), in pages
    StartPage = RESERVED_LOW_MEMORY >> PAGE_SIZE_MUL;

    // Translate PA -> page index
    PageIndex = (U32)(Page >> PAGE_SIZE_MUL);

    // Guard: null and alignment
    if (PageIndex < StartPage) return;

    // Guard: never free page 0 (kept reserved on purpose)
    if (PageIndex == 0) {
        ERROR(TEXT("[FreePhysicalPage] Attempt to free page 0"));
        return;
    }

    // Bounds check
    if (PageIndex >= KernelStartup.PageCount) {
        ERROR(TEXT("[FreePhysicalPage] Page index out of range (%x)"), PageIndex);
        return;
    }

    LockMutex(MUTEX_MEMORY, INFINITY);

    // Bitmap math: 8 pages per byte
    UINT ByteIndex = PageIndex >> MUL_8;        // == PageIndex / 8
    U8 mask = (U8)(1u << (PageIndex & 0x07));  // bit within the byte

    // If already free, nothing to do
    if ((Kernel.PPB[ByteIndex] & mask) == 0) {
        UnlockMutex(MUTEX_MEMORY);
        DEBUG(TEXT("[FreePhysicalPage] Page already free (PA=%x)"), Page);
        return;
    }

    // Mark page as free
    Kernel.PPB[ByteIndex] = (U8)(Kernel.PPB[ByteIndex] & (U8)~mask);

    UnlockMutex(MUTEX_MEMORY);
}

/************************************************************************/
// Map or remap a single virtual page by directly editing its PTE via the self-map.

static inline void MapOnePage(
    LINEAR Linear, PHYSICAL Physical, U32 ReadWrite, U32 Privilege, U32 WriteThrough, U32 CacheDisabled, U32 Global,
    U32 Fixed) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    UINT dir = GetDirectoryEntry(Linear);

    if (!PageDirectoryEntryIsPresent(Directory, dir)) {
        ConsolePanic(TEXT("[MapOnePage] PDE not present for VA %p (dir=%d)"), Linear, dir);
    }

    LPPAGE_TABLE Table = GetPageTableVAFor(Linear);
    UINT tab = GetTableEntry(Linear);

    WritePageTableEntryValue(
        Table, tab, MakePageTableEntryValue(Physical, ReadWrite, Privilege, WriteThrough, CacheDisabled, Global, Fixed));

    InvalidatePage(Linear);
}

/************************************************************************/

/**
 * @brief Unmap a single page from the current address space.
 * @param Linear Linear address to unmap.
 */
static inline void UnmapOnePage(LINEAR Linear) {
    LPPAGE_TABLE Table = GetPageTableVAFor(Linear);
    UINT tab = GetTableEntry(Linear);
    ClearPageTableEntry(Table, tab);
    InvalidatePage(Linear);
}

// Public temporary map #1

/**
 * @brief Map a physical page to a temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
LINEAR MapTemporaryPhysicalPage1(PHYSICAL Physical) {
    if (G_TempLinear1 == 0) {
        ConsolePanic(TEXT("[MapTemporaryPhysicalPage1] Temp slot #1 not reserved"));
        return NULL;
    }

    MapOnePage(
        G_TempLinear1, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

#if defined(__EXOS_ARCH_X86_64__)
    // Ensure the CPU stops using the previous translation before callers touch the
    // new physical page through the shared temporary slot.
    FlushTLB();
#endif

    return G_TempLinear1;
}

/************************************************************************/
// Public temporary map #2

/**
 * @brief Map a physical page to the second temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
LINEAR MapTemporaryPhysicalPage2(PHYSICAL Physical) {
    if (G_TempLinear2 == 0) {
        ConsolePanic(TEXT("[MapTemporaryPhysicalPage2] Temp slot #2 not reserved"));
        return NULL;
    }

    MapOnePage(
        G_TempLinear2, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

#if defined(__EXOS_ARCH_X86_64__)
    // Ensure the CPU stops using the previous translation before callers touch the
    // new physical page through the shared temporary slot.
    FlushTLB();
#endif

    return G_TempLinear2;
}

/************************************************************************/
// Public temporary map #3

/**
 * @brief Map a physical page to the third temporary linear address.
 * @param Physical Physical page number.
 * @return Linear address mapping or 0 on failure.
 */
LINEAR MapTemporaryPhysicalPage3(PHYSICAL Physical) {
    if (G_TempLinear3 == 0) {
        ConsolePanic(TEXT("[MapTemporaryPhysicalPage3] Temp slot #3 not reserved"));
        return NULL;
    }

    MapOnePage(
        G_TempLinear3, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

#if defined(__EXOS_ARCH_X86_64__)
    // Ensure the CPU stops using the previous translation before callers touch the
    // new physical page through the shared temporary slot.
    FlushTLB();
#endif

    return G_TempLinear3;
}

/************************************************************************/

#if defined(__EXOS_ARCH_X86_64__)
/**
 * @brief Allocate a page table for the given base address.
 * @param Base Base linear address.
 * @return Linear address of the new table or 0.
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
#else
/**
 * @brief Allocate a page table for the given base address.
 * @param Base Base linear address.
 * @return Linear address of the new table or 0.
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
#endif

/************************************************************************/

static BOOL TryGetPageTableForIterator(
    const ARCH_PAGE_ITERATOR* Iterator,
    LPPAGE_TABLE* OutTable,
    BOOL* OutLargePage);

/************************************************************************/

/**
 * @brief Check if a linear region is free of mappings.
 * @param Base Base linear address.
 * @param Size Size of region.
 * @return TRUE if region is free.
 */
BOOL IsRegionFree(LINEAR Base, UINT Size) {
    Base = CanonicalizeLinearAddress(Base);

    UINT NumPages = (Size + PAGE_SIZE - 1) >> PAGE_SIZE_MUL;
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Base);

    for (UINT i = 0; i < NumPages; i++) {
        UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);

        LPPAGE_TABLE Table = NULL;
        BOOL IsLargePage = FALSE;
        BOOL TableAvailable = TryGetPageTableForIterator(&Iterator, &Table, &IsLargePage);

        if (TableAvailable) {
            if (PageTableEntryIsPresent(Table, TabEntry)) {
                return FALSE;
            }
        } else {
            if (IsLargePage) {
                return FALSE;
            }
        }

        MemoryPageIteratorStepPage(&Iterator);
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Find a free linear region starting from a base address.
 * @param StartBase Starting linear address.
 * @param Size Desired region size.
 * @return Base of free region or 0.
 */
static LINEAR FindFreeRegion(LINEAR StartBase, UINT Size) {
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
static void FreeEmptyPageTables(void) {
    LPPAGE_DIRECTORY Directory = GetCurrentPageDirectoryVA();
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(N_4MB);
    MemoryPageIteratorAlignToTableStart(&Iterator);

    while (MemoryPageIteratorGetLinear(&Iterator) < VMA_KERNEL) {
        UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(&Iterator);
        PHYSICAL TablePhysical = PageDirectoryEntryGetPhysical(Directory, DirEntry);

        if (TablePhysical != 0) {
            LPPAGE_TABLE Table = MemoryPageIteratorGetTable(&Iterator);

            if (ArchPageTableIsEmpty(Table)) {
                SetPhysicalPageMark((UINT)(TablePhysical >> PAGE_SIZE_MUL), 0);
                ClearPageDirectoryEntry(Directory, DirEntry);
            }
        }

        MemoryPageIteratorNextTable(&Iterator);
    }
}

static BOOL PopulateRegionPages(LINEAR Base,
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
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                return FALSE;
            }

            if (AllocPageTable(CurrentLinear) == NULL) {
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                return FALSE;
            }

            if (!TryGetPageTableForIterator(&Iterator, &Table, NULL)) {
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
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
                    FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
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

    if (PopulateRegionPages(Base, Target, NumPages, Flags, Pointer, TEXT("AllocRegion")) == FALSE) {
        return NULL;
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

    if (NewSize > KernelStartup.MemorySize / 4) {
        ERROR(TEXT("[ResizeRegion] New size %x exceeds 25%% of memory (%lX)"),
              NewSize,
              KernelStartup.MemorySize / 4);
        return FALSE;
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

        if (PopulateRegionPages(NewBase,
                                AdditionalTarget,
                                AdditionalPages,
                                Flags,
                                NewBase,
                                TEXT("ResizeRegion")) == FALSE) {
            return FALSE;
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
 * @brief Resolve the page table targeted by an iterator when the hierarchy is present.
 * @param Iterator Page iterator referencing the page to access.
 * @param OutTable Receives the page table pointer when available.
 * @return TRUE when the table exists and is returned.
 */
#if defined(__EXOS_ARCH_X86_64__)
static BOOL TryGetPageTableForIterator(
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
#else
static BOOL TryGetPageTableForIterator(
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
#endif

/************************************************************************/

/**
 * @brief Unmap and free a linear region.
 * @param Base Base linear address.
 * @param Size Size of region.
 * @return TRUE on success.
 */
BOOL FreeRegion(LINEAR Base, UINT Size) {
    LPPAGE_TABLE Table = NULL;
    UINT NumPages = 0;

    NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL; /* ceil(Size / 4096) */
    if (NumPages == 0) NumPages = 1;

    // Free each page in turn.
    Base = CanonicalizeLinearAddress(Base);
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Base);

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);

        BOOL IsLargePage = FALSE;

        if (TryGetPageTableForIterator(&Iterator, &Table, &IsLargePage) && PageTableEntryIsPresent(Table, TabEntry)) {
            PHYSICAL EntryPhysical = PageTableEntryGetPhysical(Table, TabEntry);

            /* Skip bitmap mark if it was an IO mapping (BAR) */
            if (!PageTableEntryIsFixed(Table, TabEntry)) {
                SetPhysicalPageMark((UINT)(EntryPhysical >> PAGE_SIZE_MUL), 0);
            }

            ClearPageTableEntry(Table, TabEntry);
        } else if (IsLargePage == TRUE) {
            DEBUG(TEXT("[FreeRegion] Large mapping covers Dir=%u"),
                MemoryPageIteratorGetDirectoryIndex(&Iterator));
        }

        MemoryPageIteratorStepPage(&Iterator);
        Base += PAGE_SIZE;
    }

    FreeEmptyPageTables();

    // Flush the Translation Look-up Buffer of the CPU
    FlushTLB();

    return TRUE;
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

/**
 * @brief Initialize the kernel memory manager.
 */
void InitializeMemoryManager(void) {
    ArchInitializeMemoryManager();
}
