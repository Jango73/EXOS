
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
#include "System.h"

#ifndef TEMP_LINEAR_PAGE_1
#define TEMP_LINEAR_PAGE_1 0
#endif

#ifndef TEMP_LINEAR_PAGE_2
#define TEMP_LINEAR_PAGE_2 0
#endif

#ifndef TEMP_LINEAR_PAGE_3
#define TEMP_LINEAR_PAGE_3 0
#endif

/************************************************************************/
// INTERNAL SELF-MAP + TEMP MAPPING ]

static LINEAR G_TempLinear1 = TEMP_LINEAR_PAGE_1;
static LINEAR G_TempLinear2 = TEMP_LINEAR_PAGE_2;
static LINEAR G_TempLinear3 = TEMP_LINEAR_PAGE_3;

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

void SetPhysicalPageUsage(UINT PageIndex, BOOL Used) {
    SetPhysicalPageMark(PageIndex, Used ? 1u : 0u);
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

        if (ClipPhysicalRange(Entry->Base, Entry->Length, &Base, &Size) == FALSE) {
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

        if (ClipPhysicalRange(Entry->Base, Entry->Length, &Base, &Size) == FALSE) {
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

    ArchRemapTemporaryPage(G_TempLinear1, Physical);

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

    ArchRemapTemporaryPage(G_TempLinear2, Physical);

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

    ArchRemapTemporaryPage(G_TempLinear3, Physical);

#if defined(__EXOS_ARCH_X86_64__)
    // Ensure the CPU stops using the previous translation before callers touch the
    // new physical page through the shared temporary slot.
    FlushTLB();
#endif

    return G_TempLinear3;
}

/************************************************************************/

    
/*
 * Architecture-specific region management is delegated to backend files.
 * Keep high-level wrappers here for validation and logging.
 */

/************************************************************************/

/**
 * @brief Allocate and map a physical region into the linear address space.
 * @param Base Desired base address or 0. When zero and ALLOC_PAGES_AT_OR_OVER
 *             is not set, the architecture backend chooses the location.
 * @param Target Desired physical base address or 0. Requires
 *               ALLOC_PAGES_COMMIT when specified. Use with ALLOC_PAGES_IO to
 *               map device memory without touching the physical bitmap.
 * @param Size Size in bytes, rounded up to page granularity. Limited to 25% of
 *             the available physical memory.
 * @param Flags Mapping flags: see Memory.h for details.
 * @return Allocated linear base address or 0 on failure.
 */
LINEAR AllocRegion(LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags) {
    DEBUG(TEXT("[AllocRegion] Enter: Base=%x Target=%x Size=%x Flags=%x"), Base, Target, Size, Flags);

    if (Size > KernelStartup.MemorySize / 4) {
        ERROR(TEXT("[AllocRegion] Size %x exceeds 25%% of memory (%lX)"), Size, KernelStartup.MemorySize / 4);
        return NULL;
    }

    UINT NumPages = (Size + (PAGE_SIZE - 1)) >> PAGE_SIZE_MUL;
    if (NumPages == 0) NumPages = 1;

    Base = CanonicalizeLinearAddress(Base);

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
    }

    LINEAR Result = ArchAllocRegion(Base, Target, Size, Flags);

    if (Result == NULL) {
        DEBUG(TEXT("[AllocRegion] ArchAllocRegion failed"));
        return NULL;
    }

    DEBUG(TEXT("[AllocRegion] Exit"));
    return Result;
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

    LINEAR CanonicalBase = CanonicalizeLinearAddress(Base);
    BOOL Result = ArchResizeRegion(CanonicalBase, Target, Size, NewSize, Flags);

    if (Result == FALSE) {
        DEBUG(TEXT("[ResizeRegion] ArchResizeRegion failed"));
        return FALSE;
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
    DEBUG(TEXT("[FreeRegion] Enter base=%p size=%u"), (LPVOID)Base, Size);

    LINEAR CanonicalBase = CanonicalizeLinearAddress(Base);
    BOOL Result = ArchFreeRegion(CanonicalBase, Size);

    DEBUG(TEXT("[FreeRegion] Exit base=%p size=%u result=%u"), (LPVOID)Base, Size, (UINT)(Result ? 1u : 0u));
    return Result;
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
