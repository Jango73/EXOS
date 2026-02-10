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

#include "Arch.h"
#include "Base.h"
#include "BuddyAllocator.h"
#include "CoreString.h"
#include "EarlyBootConsole.h"
#include "Kernel.h"
#include "Log.h"
#include "System.h"
#include "process/Schedule.h"

/************************************************************************/

static PHYSICAL G_LoaderReservedStart = 0;
static PHYSICAL G_LoaderReservedEnd = 0;
static PHYSICAL G_PhysicalAllocatorMetadataStart = 0;
static PHYSICAL G_PhysicalAllocatorMetadataEnd = 0;
static BOOL G_AllocPhysicalPageTraceEnabled = FALSE;

/************************************************************************/

/**
 * @brief Enable or disable focused early-boot tracing in AllocPhysicalPage.
 * @param Enabled TRUE to print trace lines, FALSE otherwise.
 */
void SetAllocPhysicalPageTraceEnabled(BOOL Enabled) {
    G_AllocPhysicalPageTraceEnabled = Enabled;
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
 * @brief Mark a physical page as used or free in the allocator.
 * @param Page Page index.
 * @param Used Non-zero to mark used.
 */
void SetPhysicalPageMark(UINT Page, UINT Used) {
    if (Page >= KernelStartup.PageCount) {
        return;
    }

    LockMutex(MUTEX_MEMORY, INFINITY);
    BuddySetRange(Page, 1, Used);
    UnlockMutex(MUTEX_MEMORY);
}

/************************************************************************/

/**
 * @brief Mark a range of physical pages as used or free.
 * @param FirstPage First page index.
 * @param PageCount Number of pages.
 * @param Used Non-zero to mark used.
 */
void SetPhysicalPageRangeMark(UINT FirstPage, UINT PageCount, UINT Used) {
    BuddySetRange(FirstPage, PageCount, Used);
}

/************************************************************************/

/**
 * @brief Configure the early loader-owned physical range to keep reserved.
 * @param Start Inclusive physical start.
 * @param End Exclusive physical end.
 */
void SetLoaderReservedPhysicalRange(PHYSICAL Start, PHYSICAL End) {
    Start &= ~((PHYSICAL)(PAGE_SIZE - 1));
    End = PAGE_ALIGN(End);

    if (End <= Start) {
        G_LoaderReservedStart = 0;
        G_LoaderReservedEnd = 0;
        return;
    }

    G_LoaderReservedStart = Start;
    G_LoaderReservedEnd = End;
}

/************************************************************************/

/**
 * @brief Configure physical range used by allocator metadata.
 * @param Start Inclusive physical start.
 * @param End Exclusive physical end.
 */
void SetPhysicalAllocatorMetadataRange(PHYSICAL Start, PHYSICAL End) {
    Start &= ~((PHYSICAL)(PAGE_SIZE - 1));
    End = PAGE_ALIGN(End);

    if (End <= Start) {
        G_PhysicalAllocatorMetadataStart = 0;
        G_PhysicalAllocatorMetadataEnd = 0;
        return;
    }

    G_PhysicalAllocatorMetadataStart = Start;
    G_PhysicalAllocatorMetadataEnd = End;
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
    UpdateKernelMemoryMetricsFromMultibootMap();

    if (KernelStartup.PageCount == 0) {
        DEBUG(TEXT("[MarkUsedPhysicalMemory] No physical memory detected"));
        return;
    }

    if (BuddyIsReady() == FALSE) {
        ERROR(TEXT("[MarkUsedPhysicalMemory] Buddy allocator not initialized"));
        return;
    }

    BuddyResetAllReserved();

    PHYSICAL ReservedLowEnd = (PHYSICAL)RESERVED_LOW_MEMORY;
    PHYSICAL LoaderReservedStart = G_LoaderReservedStart;
    PHYSICAL LoaderReservedEnd = G_LoaderReservedEnd;
    PHYSICAL MetadataStart = G_PhysicalAllocatorMetadataStart;
    PHYSICAL MetadataEnd = G_PhysicalAllocatorMetadataEnd;

    SetPhysicalPageRangeMark(0, (UINT)(ReservedLowEnd >> PAGE_SIZE_MUL), 1);

    if (LoaderReservedStart == 0 || LoaderReservedEnd <= LoaderReservedStart) {
        LoaderReservedStart = KernelStartup.KernelPhysicalBase;
        LoaderReservedEnd = MetadataEnd;
    }

    if (LoaderReservedStart != 0 && LoaderReservedEnd > LoaderReservedStart) {
        UINT FirstPage = (UINT)(LoaderReservedStart >> PAGE_SIZE_MUL);
        UINT PageCount = (UINT)((LoaderReservedEnd - LoaderReservedStart) >> PAGE_SIZE_MUL);
        SetPhysicalPageRangeMark(FirstPage, PageCount, 1);
    }

    if (MetadataStart != 0 && MetadataEnd > MetadataStart) {
        UINT FirstPage = (UINT)(MetadataStart >> PAGE_SIZE_MUL);
        UINT PageCount = (UINT)((MetadataEnd - MetadataStart) >> PAGE_SIZE_MUL);
        SetPhysicalPageRangeMark(FirstPage, PageCount, 1);
    }

    for (UINT Index = 0; Index < KernelStartup.MultibootMemoryEntryCount; Index++) {
        const MULTIBOOTMEMORYENTRY* Entry = &KernelStartup.MultibootMemoryEntries[Index];
        PHYSICAL Base = 0;
        UINT Size = 0;

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
 * @return Physical page number or 0 on failure.
 */
PHYSICAL AllocPhysicalPage(void) {
    PHYSICAL Result = 0;

    if (G_AllocPhysicalPageTraceEnabled) {
        EarlyBootConsoleWriteLine(TEXT("[AllocPhysicalPage] Enter"));
    }

    if (BuddyIsReady() == FALSE) {
        return 0;
    }

    LockMutex(MUTEX_MEMORY, INFINITY);
    Result = BuddyAllocPage();
    UnlockMutex(MUTEX_MEMORY);

    if (G_AllocPhysicalPageTraceEnabled) {
        EarlyBootConsoleWriteLine(TEXT("[AllocPhysicalPage] Return"));
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Release a previously allocated physical page.
 * @param Page Page address to free.
 */
void FreePhysicalPage(PHYSICAL Page) {
    UINT StartPage = RESERVED_LOW_MEMORY >> PAGE_SIZE_MUL;
    UINT PageIndex = 0;

    if ((Page & (PAGE_SIZE - 1)) != 0) {
        ERROR(TEXT("[FreePhysicalPage] Physical address not page-aligned (%x)"), Page);
        return;
    }

    PageIndex = (UINT)(Page >> PAGE_SIZE_MUL);

    if (PageIndex < StartPage) {
        return;
    }

    if (PageIndex == 0) {
        ERROR(TEXT("[FreePhysicalPage] Attempt to free page 0"));
        return;
    }

    if (PageIndex >= KernelStartup.PageCount) {
        ERROR(TEXT("[FreePhysicalPage] Page index out of range (%x)"), PageIndex);
        return;
    }

    LockMutex(MUTEX_MEMORY, INFINITY);
    if (BuddyFreePage(Page) == FALSE) {
        UnlockMutex(MUTEX_MEMORY);
        DEBUG(TEXT("[FreePhysicalPage] Page already free or invalid (PA=%x)"), Page);
        return;
    }
    UnlockMutex(MUTEX_MEMORY);
}
