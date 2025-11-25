
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
#include "process/Schedule.h"
#include "System.h"

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
void SetPhysicalPageMark(UINT Page, UINT Used) {
    UINT Offset = 0;
    UINT Value = 0;

    if (Page >= KernelStartup.PageCount) return;

    LPPAGEBITMAP Bitmap = GetPhysicalPageBitmap();
    if (Bitmap == NULL) return;

    LockMutex(MUTEX_MEMORY, INFINITY);

    Offset = Page >> MUL_8;
    Value = (UINT)0x01 << (Page & 0x07);

    if (Used) {
        Bitmap[Offset] |= (U8)Value;
    } else {
        Bitmap[Offset] &= (U8)(~Value);
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
UINT GetPhysicalPageMark(UINT Page) {
    UINT Offset = 0;
    UINT Value = 0;
    UINT RetVal = 0;

    if (Page >= KernelStartup.PageCount) return 0;

    LockMutex(MUTEX_MEMORY, INFINITY);

    Offset = Page >> MUL_8;
    Value = (UINT)0x01 << (Page & 0x07);

    LPPAGEBITMAP Bitmap = GetPhysicalPageBitmap();
    if (Bitmap != NULL && (Bitmap[Offset] & Value)) RetVal = 1;

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
void SetPhysicalPageRangeMark(UINT FirstPage, UINT PageCount, UINT Used) {
    DEBUG(TEXT("[SetPhysicalPageRangeMark] Enter"));

    UINT End = FirstPage + PageCount;
    if (FirstPage >= KernelStartup.PageCount) return;
    if (End > KernelStartup.PageCount) End = KernelStartup.PageCount;

    DEBUG(TEXT("[SetPhysicalPageRangeMark] Start, End : %x, %x"), FirstPage, End);

    LPPAGEBITMAP Bitmap = GetPhysicalPageBitmap();
    if (Bitmap == NULL) return;

    for (UINT Page = FirstPage; Page < End; Page++) {
        UINT Byte = Page >> MUL_8;
        U8 Mask = (U8)(1u << (Page & 0x07)); /* bit within byte */
        if (Used) {
            Bitmap[Byte] |= Mask;
        } else {
            Bitmap[Byte] &= (U8)~Mask;
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

    LPPAGEBITMAP Bitmap = GetPhysicalPageBitmap();
    UINT BitmapSize = GetPhysicalPageBitmapSize();
    if (Bitmap == NULL || BitmapSize == 0) {
        ERROR(TEXT("[MarkUsedPhysicalMemory] PPB not initialized"));
        return;
    }

    PHYSICAL PpbPhysicalBase = (PHYSICAL)(UINT)(Bitmap);
    PHYSICAL ReservedEnd = PAGE_ALIGN(PpbPhysicalBase + (PHYSICAL)BitmapSize);
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
    LPPAGEBITMAP Bitmap = GetPhysicalPageBitmap();

    // DEBUG(TEXT("[AllocPhysicalPage] Enter"));

    if (Bitmap == NULL) {
        return result;
    }

    LockMutex(MUTEX_MEMORY, INFINITY);

    // Start from end of kernel region
    StartPage = RESERVED_LOW_MEMORY >> PAGE_SIZE_MUL;

    // Convert to PPB byte index
    StartByte = StartPage >> MUL_8; /* == ((... >> 12) >> 3) */
    MaxByte = (KernelStartup.PageCount + 7) >> MUL_8;

    /* Scan from StartByte upward */
    for (i = StartByte; i < MaxByte; i++) {
        U8 v = Bitmap[i];
        if (v != 0xFF) {
            page = (i << MUL_8); /* first page covered by this byte */
            for (bit = 0; bit < 8 && page < KernelStartup.PageCount; bit++, page++) {
                mask = 1u << bit;
                if ((v & mask) == 0) {
                    Bitmap[i] = (U8)(v | (U8)mask);
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
    PageIndex = (UINT)(Page >> PAGE_SIZE_MUL);

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
    LPPAGEBITMAP Bitmap = GetPhysicalPageBitmap();
    if (Bitmap == NULL) {
        UnlockMutex(MUTEX_MEMORY);
        return;
    }

    // Bitmap math: 8 pages per byte
    UINT ByteIndex = PageIndex >> MUL_8;        // == PageIndex / 8
    U8 mask = (U8)(1u << (PageIndex & 0x07));  // bit within the byte

    // If already free, nothing to do
    if ((Bitmap[ByteIndex] & mask) == 0) {
        UnlockMutex(MUTEX_MEMORY);
        DEBUG(TEXT("[FreePhysicalPage] Page already free (PA=%x)"), Page);
        return;
    }

    // Mark page as free
    Bitmap[ByteIndex] = (U8)(Bitmap[ByteIndex] & (U8)~mask);

    UnlockMutex(MUTEX_MEMORY);
}
