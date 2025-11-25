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


    x86-64 memory helpers

\************************************************************************/

#include "arch/x86-64/x86-64-Memory.h"

#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "Mutex.h"
#include "System.h"
#include "User.h"
#include "process/Process.h"

/************************************************************************/

/**
 * @brief Compute the size of the 4 KiB page window reserved in low memory.
 * @param TotalMemoryBytes Total detected physical memory size in bytes.
 * @return Window size rounded up to 2 MiB and clamped to total memory.
 */
PHYSICAL ComputeLowMemoryWindowLimit(UINT TotalMemoryBytes) {
    if (TotalMemoryBytes == 0) {
        return 0;
    }

    UINT OnePercent = TotalMemoryBytes / 100u;
    if ((TotalMemoryBytes % 100u) != 0u) {
        OnePercent++;
    }

    UINT Window = (OnePercent + (PAGE_2M_SIZE - 1u)) & ~(PAGE_2M_MASK);

    if (Window > TotalMemoryBytes) {
        Window = TotalMemoryBytes;
    }

    return (PHYSICAL)Window;
}

/************************************************************************/

/**
 * @brief Convenience wrapper to compute the low window limit from startup info.
 * @return Window size rounded up to 2 MiB and clamped to total memory.
 */
PHYSICAL GetLowMemoryWindowLimit(void) {
    return ComputeLowMemoryWindowLimit(KernelStartup.MemorySize);
}

/************************************************************************/

static LINEAR G_TempLinear1 = TEMP_LINEAR_PAGE_1;
static LINEAR G_TempLinear2 = TEMP_LINEAR_PAGE_2;
static LINEAR G_TempLinear3 = TEMP_LINEAR_PAGE_3;

/************************************************************************/

static inline void SetPageFlags(LPX86_64_PAGE_TABLE_ENTRY Entry, U64 Flags, PHYSICAL Physical) {
    Entry->Present = (Flags & PAGE_FLAG_PRESENT) ? 1u : 0u;
    Entry->ReadWrite = (Flags & PAGE_FLAG_READ_WRITE) ? 1u : 0u;
    Entry->Privilege = (Flags & PAGE_FLAG_USER) ? 1u : 0u;
    Entry->WriteThrough = (Flags & PAGE_FLAG_WRITE_THROUGH) ? 1u : 0u;
    Entry->CacheDisabled = (Flags & PAGE_FLAG_CACHE_DISABLED) ? 1u : 0u;
    Entry->Accessed = 0u;
    Entry->Dirty = 0u;
    Entry->PageSize = 0u;
    Entry->Global = (Flags & PAGE_FLAG_GLOBAL) ? 1u : 0u;
    Entry->Address = (U64)Physical >> PAGE_SIZE_MUL;
    Entry->Available = 0u;
    Entry->AvailableHigh = 0u;
    Entry->NoExecute = (Flags & PAGE_FLAG_NO_EXECUTE) ? 1u : 0u;
}

/************************************************************************/

static inline void InvalidatePage(LINEAR Linear) {
    __asm__ volatile("invlpg (%0)" ::"r"(Linear) : "memory");
}

/************************************************************************/

static inline BOOL IsInLowWindow(PHYSICAL Physical) {
    return (Physical < GetCachedLowMemoryWindowLimit());
}

/************************************************************************/
// Paging walkers

static inline UINT GetPml4Index(LINEAR Linear) { return (UINT)((Linear >> 39) & 0x1FFu); }
static inline UINT GetPdptIndex(LINEAR Linear) { return (UINT)((Linear >> 30) & 0x1FFu); }
static inline UINT GetDirectoryIndex(LINEAR Linear) { return (UINT)((Linear >> 21) & 0x1FFu); }
static inline UINT GetTableIndex(LINEAR Linear) { return (UINT)((Linear >> 12) & 0x1FFu); }

static inline LPPML4 GetCurrentPml4VA(void) {
    return (LPPML4)BuildRecursiveAddress(PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT);
}

static inline LPPDPT GetPageDirectoryPointerVAFor(UINT Pml4Index, UINT PdptIndex) {
    UNUSED(Pml4Index);
    return (LPPDPT)BuildRecursiveAddress(PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PdptIndex);
}

static inline LPPAGE_DIRECTORY GetPageDirectoryVAFor(UINT PdptIndex, UINT DirectoryIndex) {
    return (LPPAGE_DIRECTORY)BuildRecursiveAddress(PML4_RECURSIVE_SLOT, PML4_RECURSIVE_SLOT, PdptIndex, DirectoryIndex);
}

static inline LPPAGE_TABLE GetPageTableVAFor(UINT PdptIndex, UINT DirectoryIndex) {
    return (LPPAGE_TABLE)BuildRecursiveAddress(PML4_RECURSIVE_SLOT, PdptIndex, DirectoryIndex, 0);
}

/************************************************************************/

static BOOL MapLargePage(LINEAR Linear, PHYSICAL Physical, U64 Flags) {
    if ((Linear & PAGE_2M_MASK) != 0 || (Physical & PAGE_2M_MASK) != 0) {
        ERROR(TEXT("[MapLargePage] Alignment error VA=%p PA=%p"), (LPVOID)Linear, (LPVOID)Physical);
        return FALSE;
    }

    UINT Pml4Index = GetPml4Index(Linear);
    UINT PdptIndex = GetPdptIndex(Linear);
    UINT DirectoryIndex = GetDirectoryIndex(Linear);

    LPPML4 Pml4 = GetCurrentPml4VA();
    LPPDPT Pdpt = GetPageDirectoryPointerVAFor(Pml4Index, PdptIndex);
    LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(PdptIndex, DirectoryIndex);

    if (!Pml4[Pml4Index].Present || !Pdpt[PdptIndex].Present) {
        ERROR(TEXT("[MapLargePage] Missing PML4/PDPT for VA %p"), (LPVOID)Linear);
        return FALSE;
    }

    X86_64_PAGE_DIRECTORY_ENTRY* DirEntry = &Directory[DirectoryIndex];
    if (DirEntry->Present && DirEntry->PageSize == 0) {
        ERROR(TEXT("[MapLargePage] Existing page table prevents 2M map for VA %p"), (LPVOID)Linear);
        return FALSE;
    }

    DirEntry->Present = (Flags & PAGE_FLAG_PRESENT) ? 1u : 0u;
    DirEntry->ReadWrite = (Flags & PAGE_FLAG_READ_WRITE) ? 1u : 0u;
    DirEntry->Privilege = (Flags & PAGE_FLAG_USER) ? 1u : 0u;
    DirEntry->WriteThrough = (Flags & PAGE_FLAG_WRITE_THROUGH) ? 1u : 0u;
    DirEntry->CacheDisabled = (Flags & PAGE_FLAG_CACHE_DISABLED) ? 1u : 0u;
    DirEntry->Accessed = 0u;
    DirEntry->Dirty = 0u;
    DirEntry->PageSize = 1u;
    DirEntry->Global = (Flags & PAGE_FLAG_GLOBAL) ? 1u : 0u;
    DirEntry->Address = (U64)Physical >> PAGE_SIZE_MUL;
    DirEntry->Available = 0u;
    DirEntry->AvailableHigh = 0u;
    DirEntry->NoExecute = (Flags & PAGE_FLAG_NO_EXECUTE) ? 1u : 0u;

    InvalidatePage(Linear);
    return TRUE;
}

/************************************************************************/

static LINEAR MapTemporaryPhysicalPage(LINEAR TargetLinear, PHYSICAL Physical) {
    if (!IsInLowWindow(Physical)) {
        ERROR(TEXT("[MapTemporaryPhysicalPage] Physical %p outside low window"), (LPVOID)Physical);
        return 0;
    }

    UINT Pml4Index = GetPml4Index(TargetLinear);
    UINT PdptIndex = GetPdptIndex(TargetLinear);
    UINT DirectoryIndex = GetDirectoryIndex(TargetLinear);
    UINT TableIndex = GetTableIndex(TargetLinear);

    LPPML4 Pml4 = GetCurrentPml4VA();
    LPPDPT Pdpt = GetPageDirectoryPointerVAFor(Pml4Index, PdptIndex);
    LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(PdptIndex, DirectoryIndex);
    LPPAGE_TABLE Table = GetPageTableVAFor(PdptIndex, DirectoryIndex);

    X86_64_PML4_ENTRY* Pml4Entry = &Pml4[Pml4Index];
    X86_64_PDPT_ENTRY* PdptEntry = &Pdpt[PdptIndex];
    X86_64_PAGE_DIRECTORY_ENTRY* DirEntry = &Directory[DirectoryIndex];
    X86_64_PAGE_TABLE_ENTRY* TabEntry = &Table[TableIndex];

    if (Pml4Entry->Present == 0 || PdptEntry->Present == 0 || DirEntry->Present == 0) {
        ERROR(TEXT("[MapTemporaryPhysicalPage] Missing paging structure for VA %p"), (LPVOID)TargetLinear);
        return 0;
    }

    U64 Flags = PAGE_FLAG_PRESENT | PAGE_FLAG_READ_WRITE | PAGE_FLAG_GLOBAL;
    SetPageFlags(TabEntry, Flags, Physical);
    InvalidatePage(TargetLinear);

    return TargetLinear;
}

/************************************************************************/

LINEAR MapTemporaryPhysicalPage1(PHYSICAL Physical) {
    return MapTemporaryPhysicalPage(G_TempLinear1, Physical);
}

/************************************************************************/

LINEAR MapTemporaryPhysicalPage2(PHYSICAL Physical) {
    return MapTemporaryPhysicalPage(G_TempLinear2, Physical);
}

/************************************************************************/

LINEAR MapTemporaryPhysicalPage3(PHYSICAL Physical) {
    return MapTemporaryPhysicalPage(G_TempLinear3, Physical);
}

/************************************************************************/

static void MarkLargePage(UINT Index, UINT Used) {
    LPPAGEBITMAP Bitmap = GetPhysicalPageBitmap2M();
    UINT BitmapSize = GetPhysicalPageBitmap2MSize();
    if (Bitmap == NULL || BitmapSize == 0) return;

    UINT MaxPages = BitmapSize << MUL_8;
    if (Index >= MaxPages) return;

    LockMutex(MUTEX_MEMORY, INFINITY);
    UINT Byte = Index >> MUL_8;
    U8 Mask = (U8)(1u << (Index & 0x07));

    if (Used) {
        Bitmap[Byte] |= Mask;
    } else {
        Bitmap[Byte] &= (U8)~Mask;
    }
    UnlockMutex(MUTEX_MEMORY);
}

/************************************************************************/

static PHYSICAL AllocPhysicalPage4K(void) {
    LPPAGEBITMAP Bitmap = GetPhysicalPageBitmap();
    UINT BitmapSize = GetPhysicalPageBitmapSize();

    if (Bitmap == NULL || BitmapSize == 0) {
        return 0;
    }

    UINT MaxPages = BitmapSize << MUL_8;
    UINT StartPage = RESERVED_LOW_MEMORY >> PAGE_SIZE_MUL;
    UINT StartByte = StartPage >> MUL_8;
    UINT MaxByte = (MaxPages + 7u) >> MUL_8;

    LockMutex(MUTEX_MEMORY, INFINITY);

    for (UINT i = StartByte; i < MaxByte; i++) {
        U8 v = Bitmap[i];
        if (v != 0xFF) {
            UINT page = (i << MUL_8);
            for (UINT bit = 0; bit < 8 && page < MaxPages; bit++, page++) {
                U8 mask = (U8)(1u << bit);
                if ((v & mask) == 0) {
                    Bitmap[i] = (U8)(v | mask);
                    UnlockMutex(MUTEX_MEMORY);
                    return (PHYSICAL)(page << PAGE_SIZE_MUL);
                }
            }
        }
    }

    UnlockMutex(MUTEX_MEMORY);
    return 0;
}

/************************************************************************/

static PHYSICAL AllocPhysicalPage2M(void) {
    LPPAGEBITMAP Bitmap = GetPhysicalPageBitmap2M();
    UINT BitmapSize = GetPhysicalPageBitmap2MSize();
    PHYSICAL LowWindow = GetCachedLowMemoryWindowLimit();

    if (Bitmap == NULL || BitmapSize == 0 || LowWindow == 0) {
        return 0;
    }

    UINT MaxPages = BitmapSize << MUL_8;
    LockMutex(MUTEX_MEMORY, INFINITY);

    for (UINT i = 0; i < BitmapSize; i++) {
        U8 v = Bitmap[i];
        if (v != 0xFF) {
            UINT baseIndex = (i << MUL_8);
            for (UINT bit = 0; bit < 8 && baseIndex < MaxPages; bit++, baseIndex++) {
                U8 mask = (U8)(1u << bit);
                if ((v & mask) == 0) {
                    Bitmap[i] = (U8)(v | mask);
                    UnlockMutex(MUTEX_MEMORY);
                    return LowWindow + ((PHYSICAL)baseIndex * PAGE_2M_SIZE);
                }
            }
        }
    }

    UnlockMutex(MUTEX_MEMORY);
    return 0;
}

/************************************************************************/

PHYSICAL AllocPhysicalPage(void) {
    return AllocPhysicalPage4K();
}

/************************************************************************/

void FreePhysicalPage(PHYSICAL Page) {
    PHYSICAL LowWindow = GetCachedLowMemoryWindowLimit();

    if ((Page & PAGE_SIZE_MASK) != 0) {
        ERROR(TEXT("[FreePhysicalPage] Physical address not 4K aligned (%p)"), (LPVOID)Page);
        return;
    }

    if (Page < LowWindow) {
        UINT PageIndex = (UINT)(Page >> PAGE_SIZE_MUL);
        SetPhysicalPageRangeMark(PageIndex, 1, 0);
        return;
    }

    if ((Page & PAGE_2M_MASK) != 0) {
        ERROR(TEXT("[FreePhysicalPage] Physical address not 2M aligned for high region (%p)"), (LPVOID)Page);
        return;
    }

    UINT LargeIndex = (UINT)((Page - LowWindow) >> MUL_2MB);
    MarkLargePage(LargeIndex, 0);
}

/************************************************************************/

static BOOL IsRegionFree(LINEAR Base, UINT Size, MEMORY_REGION_GRANULARITY Granularity) {
    UINT StepMul = (Granularity == MEMORY_REGION_GRANULARITY_2M) ? MUL_2MB : PAGE_SIZE_MUL;
    UINT StepSize = (Granularity == MEMORY_REGION_GRANULARITY_2M) ? PAGE_2M_SIZE : PAGE_SIZE;
    UINT PageCount = (UINT)(((U64)Size + (StepSize - 1u)) >> StepMul);
    LINEAR Current = Base;

    for (UINT i = 0; i < PageCount; i++) {
        UINT Pml4Index = GetPml4Index(Current);
        UINT PdptIndex = GetPdptIndex(Current);
        UINT DirectoryIndex = GetDirectoryIndex(Current);
        UINT TableIndex = GetTableIndex(Current);

        LPPML4 Pml4 = GetCurrentPml4VA();
        if (!Pml4[Pml4Index].Present) return TRUE;

        LPPDPT Pdpt = GetPageDirectoryPointerVAFor(Pml4Index, PdptIndex);
        if (!Pdpt[PdptIndex].Present) return TRUE;

        LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(PdptIndex, DirectoryIndex);
        const X86_64_PAGE_DIRECTORY_ENTRY* DirEntry = &Directory[DirectoryIndex];

        if (!DirEntry->Present) {
            Current += (LINEAR)StepSize;
            continue;
        }

        if (DirEntry->PageSize) {
            return FALSE;
        }

        LPPAGE_TABLE Table = GetPageTableVAFor(PdptIndex, DirectoryIndex);
        if (Table[TableIndex].Present) {
            return FALSE;
        }

        Current += (LINEAR)StepSize;
    }

    return TRUE;
}

/************************************************************************/

static LINEAR FindFreeRegion(LINEAR StartBase, UINT Size, MEMORY_REGION_GRANULARITY Granularity) {
    LINEAR Base = StartBase;
    LINEAR AlignMask = (Granularity == MEMORY_REGION_GRANULARITY_2M) ? (LINEAR)PAGE_2M_MASK : (LINEAR)PAGE_SIZE_MASK;
    LINEAR Step = (Granularity == MEMORY_REGION_GRANULARITY_2M) ? (LINEAR)PAGE_2M_SIZE : (LINEAR)PAGE_SIZE;

    if (Base < VMA_USER) Base = VMA_USER;
    Base = (Base + AlignMask) & ~AlignMask;

    FOREVER {
        if (IsRegionFree(Base, Size, Granularity)) {
            return Base;
        }
        Base += Step;
    }
}

/************************************************************************/

static BOOL PopulateRegionPages(LINEAR Base,
                                PHYSICAL Target,
                                UINT NumPages,
                                UINT Flags,
                                MEMORY_REGION_GRANULARITY Granularity) {
    U64 PageFlags = PAGE_FLAG_PRESENT;
    if (Flags & ALLOC_PAGES_READWRITE) PageFlags |= PAGE_FLAG_READ_WRITE;
    if (Flags & ALLOC_PAGES_WC) PageFlags |= PAGE_FLAG_WRITE_THROUGH;
    if (Flags & ALLOC_PAGES_UC) PageFlags |= PAGE_FLAG_CACHE_DISABLED;

    for (UINT Index = 0; Index < NumPages; Index++) {
        LINEAR Current = Base + ((LINEAR)Index << ((Granularity == MEMORY_REGION_GRANULARITY_2M) ? MUL_2MB : PAGE_SIZE_MUL));
        PHYSICAL Physical = 0;
        U64 FlagsForPage = PageFlags;

        if (PAGE_PRIVILEGE(Current) == PAGE_PRIVILEGE_USER) {
            FlagsForPage |= PAGE_FLAG_USER;
        }

        if (Granularity == MEMORY_REGION_GRANULARITY_2M) {
            if (Target != 0) {
                Physical = Target + ((PHYSICAL)Index << MUL_2MB);
            } else {
                Physical = AllocPhysicalPage2M();
            }

            if (Physical == 0) {
                ERROR(TEXT("[PopulateRegionPages] Failed to allocate 2M page"));
                return FALSE;
            }

            if (!MapLargePage(Current, Physical, FlagsForPage)) {
                return FALSE;
            }
            UINT CurPdpt = GetPdptIndex(Current);
            UINT CurDir = GetDirectoryIndex(Current);
            LPPAGE_DIRECTORY CurDirectory = GetPageDirectoryVAFor(CurPdpt, CurDir);
            CurDirectory[CurDir].Available = (Target == 0 && (Flags & ALLOC_PAGES_IO) == 0) ? 1u : 0u;
        } else {
            UINT PdptIndex = GetPdptIndex(Current);
            UINT DirectoryIndex = GetDirectoryIndex(Current);
            LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(PdptIndex, DirectoryIndex);
            X86_64_PAGE_DIRECTORY_ENTRY* DirEntry = &Directory[DirectoryIndex];

            if (DirEntry->Present == 0) {
                PHYSICAL PtPhysical = AllocPhysicalPage4K();
                if (PtPhysical == 0) {
                    ERROR(TEXT("[PopulateRegionPages] AllocPageTable failed"));
                    return FALSE;
                }

                DirEntry->Present = 1;
                DirEntry->ReadWrite = 1;
                DirEntry->Privilege = PAGE_PRIVILEGE_KERNEL;
                DirEntry->WriteThrough = 0;
                DirEntry->CacheDisabled = 0;
                DirEntry->Accessed = 0;
                DirEntry->Dirty = 0;
                DirEntry->PageSize = 0;
                DirEntry->Global = 0;
                DirEntry->Address = (U64)PtPhysical >> PAGE_SIZE_MUL;
                DirEntry->Available = 0;
                DirEntry->AvailableHigh = 0;
                DirEntry->NoExecute = 0;

                LPPAGE_TABLE NewTable = GetPageTableVAFor(PdptIndex, DirectoryIndex);
                MemorySet(NewTable, 0, PAGE_TABLE_SIZE);
            } else if (DirEntry->PageSize) {
                ERROR(TEXT("[PopulateRegionPages] Cannot place 4K page over 2M mapping"));
                return FALSE;
            }

            if (Target != 0) {
                Physical = Target + ((PHYSICAL)Index << PAGE_SIZE_MUL);
            } else {
                Physical = AllocPhysicalPage4K();
            }

            if (Physical == 0) {
                ERROR(TEXT("[PopulateRegionPages] Failed to allocate 4K page"));
                return FALSE;
            }

            if (!MapOnePage(Current, Physical, FlagsForPage)) {
                return FALSE;
            }
            LPPAGE_TABLE Table = GetPageTableVAFor(PdptIndex, DirectoryIndex);
            Table[GetTableIndex(Current)].Available = (Target == 0 && (Flags & ALLOC_PAGES_IO) == 0) ? 1u : 0u;
        }
    }

    return TRUE;
}

/************************************************************************/

static BOOL SelectGranularity(PHYSICAL Target, UINT Size, MEMORY_REGION_GRANULARITY* OutGranularity) {
    PHYSICAL LowWindow = GetCachedLowMemoryWindowLimit();
    if (OutGranularity == NULL) return FALSE;

    if (Target != 0) {
        PHYSICAL End = Target + Size;
        if (Target < LowWindow && End > LowWindow) {
            ERROR(TEXT("[SelectGranularity] Request straddles low window (%p-%p)"), (LPVOID)Target, (LPVOID)End);
            return FALSE;
        }

        if (Target < LowWindow) {
            *OutGranularity = MEMORY_REGION_GRANULARITY_4K;
            return TRUE;
        }

        if ((Target & PAGE_2M_MASK) != 0) {
            ERROR(TEXT("[SelectGranularity] 2M mapping requires aligned physical base (%p)"), (LPVOID)Target);
            return FALSE;
        }

        *OutGranularity = MEMORY_REGION_GRANULARITY_2M;
        return TRUE;
    }

    *OutGranularity = MEMORY_REGION_GRANULARITY_2M;
    return TRUE;
}

/************************************************************************/

LINEAR AllocRegion(LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags) {
    DEBUG(TEXT("[AllocRegion] Enter: Base=%p Target=%p Size=%x Flags=%x"), (LPVOID)Base, (LPVOID)Target, Size, Flags);

    if (Size == 0 || Size > KernelStartup.MemorySize / 4) {
        ERROR(TEXT("[AllocRegion] Invalid size %x"), Size);
        return 0;
    }

    MEMORY_REGION_GRANULARITY Granularity;
    if (SelectGranularity(Target, Size, &Granularity) == FALSE) {
        return 0;
    }

    UINT PageSizeMul = (Granularity == MEMORY_REGION_GRANULARITY_2M) ? MUL_2MB : PAGE_SIZE_MUL;
    UINT PageSizeBytes = (Granularity == MEMORY_REGION_GRANULARITY_2M) ? PAGE_2M_SIZE : PAGE_SIZE;
    UINT NumPages = (UINT)(((U64)Size + (PageSizeBytes - 1u)) >> PageSizeMul);
    UINT AlignedSize = NumPages << PageSizeMul;

    if (Granularity == MEMORY_REGION_GRANULARITY_2M) {
        Base = (Base + PAGE_2M_MASK) & ~PAGE_2M_MASK;
    } else {
        Base = (Base + PAGE_SIZE_MASK) & ~PAGE_SIZE_MASK;
    }

    if (Base != 0 && (Flags & ALLOC_PAGES_AT_OR_OVER) == 0) {
        if (IsRegionFree(Base, AlignedSize, Granularity) == FALSE) {
            DEBUG(TEXT("[AllocRegion] Requested base not free (%p)"), (LPVOID)Base);
            return 0;
        }
    }

    if (Base == 0 || (Flags & ALLOC_PAGES_AT_OR_OVER)) {
        Base = FindFreeRegion(Base, AlignedSize, Granularity);
        if (Base == 0) {
            DEBUG(TEXT("[AllocRegion] No free region found"));
            return 0;
        }
    }

    if ((Flags & ALLOC_PAGES_COMMIT) != 0) {
        if (PopulateRegionPages(Base, Target, NumPages, Flags, Granularity) == FALSE) {
            return 0;
        }
    }

    FlushTLB();
    return Base;
}

/************************************************************************/

BOOL FreeRegion(LINEAR Base, UINT Size) {
    if (Base == 0 || Size == 0) return FALSE;

    UINT Pml4Index = GetPml4Index(Base);
    UINT PdptIndex = GetPdptIndex(Base);
    UINT DirectoryIndex = GetDirectoryIndex(Base);
    LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(PdptIndex, DirectoryIndex);
    const X86_64_PAGE_DIRECTORY_ENTRY* DirEntry = &Directory[DirectoryIndex];
    MEMORY_REGION_GRANULARITY Granularity = (DirEntry != NULL && DirEntry->Present && DirEntry->PageSize) ? MEMORY_REGION_GRANULARITY_2M : MEMORY_REGION_GRANULARITY_4K;

    UINT PageSizeMul = (Granularity == MEMORY_REGION_GRANULARITY_2M) ? MUL_2MB : PAGE_SIZE_MUL;
    UINT PageSizeBytes = (Granularity == MEMORY_REGION_GRANULARITY_2M) ? PAGE_2M_SIZE : PAGE_SIZE;
    UINT NumPages = (UINT)(((U64)Size + (PageSizeBytes - 1u)) >> PageSizeMul);

    for (UINT Index = 0; Index < NumPages; Index++) {
        LINEAR Current = Base + ((LINEAR)Index << PageSizeMul);

        if (Granularity == MEMORY_REGION_GRANULARITY_2M) {
            UINT CurPdpt = GetPdptIndex(Current);
            UINT CurDir = GetDirectoryIndex(Current);
            LPPAGE_DIRECTORY CurDirectory = GetPageDirectoryVAFor(CurPdpt, CurDir);
            X86_64_PAGE_DIRECTORY_ENTRY* Entry = &CurDirectory[CurDir];

            if (Entry->Present && Entry->PageSize) {
                PHYSICAL Physical = (PHYSICAL)(Entry->Address << PAGE_SIZE_MUL);
                if (Entry->Available) {
                    FreePhysicalPage(Physical);
                }
                Entry->Present = 0;
                InvalidatePage(Current);
            }
        } else {
            UINT CurPdpt = GetPdptIndex(Current);
            UINT CurDir = GetDirectoryIndex(Current);
            UINT CurTab = GetTableIndex(Current);
            LPPAGE_TABLE Table = GetPageTableVAFor(CurPdpt, CurDir);
            X86_64_PAGE_TABLE_ENTRY* Entry = &Table[CurTab];

            if (Entry->Present) {
                PHYSICAL Physical = (PHYSICAL)(Entry->Address << PAGE_SIZE_MUL);
                if (Entry->Available) {
                    FreePhysicalPage(Physical);
                }
                Entry->Present = 0;
                InvalidatePage(Current);
            }
        }
    }

    FlushTLB();
    return TRUE;
}

/************************************************************************/

BOOL ResizeRegion(LINEAR Base, PHYSICAL Target, UINT Size, UINT NewSize, U32 Flags) {
    if (Base == 0) return FALSE;

    UINT Pml4Index = GetPml4Index(Base);
    UINT PdptIndex = GetPdptIndex(Base);
    UINT DirectoryIndex = GetDirectoryIndex(Base);
    LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(PdptIndex, DirectoryIndex);
    const X86_64_PAGE_DIRECTORY_ENTRY* DirEntry = &Directory[DirectoryIndex];
    MEMORY_REGION_GRANULARITY Granularity = (DirEntry != NULL && DirEntry->Present && DirEntry->PageSize) ? MEMORY_REGION_GRANULARITY_2M : MEMORY_REGION_GRANULARITY_4K;

    UINT PageSizeMul = (Granularity == MEMORY_REGION_GRANULARITY_2M) ? MUL_2MB : PAGE_SIZE_MUL;
    UINT PageSizeBytes = (Granularity == MEMORY_REGION_GRANULARITY_2M) ? PAGE_2M_SIZE : PAGE_SIZE;

    UINT CurrentPages = (UINT)(((U64)Size + (PageSizeBytes - 1u)) >> PageSizeMul);
    UINT RequestedPages = (UINT)(((U64)NewSize + (PageSizeBytes - 1u)) >> PageSizeMul);

    if (RequestedPages == CurrentPages) {
        return TRUE;
    }

    if (RequestedPages < CurrentPages) {
        UINT ReleasePages = CurrentPages - RequestedPages;
        LINEAR FreeBase = Base + ((LINEAR)RequestedPages << PageSizeMul);
        return FreeRegion(FreeBase, ReleasePages << PageSizeMul);
    }

    UINT AdditionalPages = RequestedPages - CurrentPages;
    LINEAR AdditionalBase = Base + ((LINEAR)CurrentPages << PageSizeMul);

    if (IsRegionFree(AdditionalBase, AdditionalPages << PageSizeMul, Granularity) == FALSE) {
        ERROR(TEXT("[ResizeRegion] Additional region not free at %p"), (LPVOID)AdditionalBase);
        return FALSE;
    }

    if ((Flags & ALLOC_PAGES_COMMIT) != 0) {
        if (PopulateRegionPages(AdditionalBase, Target ? Target + ((PHYSICAL)CurrentPages << PageSizeMul) : 0, AdditionalPages, Flags, Granularity) == FALSE) {
            return FALSE;
        }
    }

    FlushTLB();
    return TRUE;
}

/************************************************************************/

static void CopyKernelPml4Entries(LPX86_64_PML4_ENTRY Dest, LPPML4 Src) {
    for (UINT Index = 256; Index < PML4_ENTRY_COUNT; Index++) {
        Dest[Index] = Src[Index];
    }

    Dest[0] = Src[0];
}

/************************************************************************/

PHYSICAL AllocPageDirectory(void) {
    PHYSICAL Pml4Physical = AllocPhysicalPage4K();
    if (Pml4Physical == 0) {
        ERROR(TEXT("[AllocPageDirectory] Failed to allocate PML4"));
        return 0;
    }

    LINEAR Pml4Linear = MapTemporaryPhysicalPage1(Pml4Physical);
    if (Pml4Linear == 0) {
        ERROR(TEXT("[AllocPageDirectory] Unable to map new PML4"));
        FreePhysicalPage(Pml4Physical);
        return 0;
    }

    LPPML4 NewPml4 = (LPPML4)Pml4Linear;
    LPPML4 CurrentPml4 = GetCurrentPml4VA();
    MemorySet(NewPml4, 0, PAGE_TABLE_SIZE);

    CopyKernelPml4Entries(NewPml4, CurrentPml4);

    X86_64_PML4_ENTRY* Recursive = &NewPml4[PML4_RECURSIVE_SLOT];
    Recursive->Present = 1;
    Recursive->ReadWrite = 1;
    Recursive->Privilege = PAGE_PRIVILEGE_KERNEL;
    Recursive->WriteThrough = 0;
    Recursive->CacheDisabled = 0;
    Recursive->Accessed = 0;
    Recursive->Dirty = 0;
    Recursive->PageSize = 0;
    Recursive->Global = 0;
    Recursive->Address = (U64)Pml4Physical >> PAGE_SIZE_MUL;
    Recursive->Available = 0;
    Recursive->AvailableHigh = 0;
    Recursive->NoExecute = 0;

    return Pml4Physical;
}

/************************************************************************/

PHYSICAL AllocUserPageDirectory(void) {
    PHYSICAL Pml4Physical = AllocPageDirectory();
    if (Pml4Physical == 0) {
        return 0;
    }

    LINEAR Pml4Linear = MapTemporaryPhysicalPage1(Pml4Physical);
    if (Pml4Linear == 0) {
        ERROR(TEXT("[AllocUserPageDirectory] Unable to map new PML4"));
        FreePhysicalPage(Pml4Physical);
        return 0;
    }

    LPPML4 NewPml4 = (LPPML4)Pml4Linear;

    for (UINT Index = 1; Index < 256; Index++) {
        MemorySet(&NewPml4[Index], 0, sizeof(X86_64_PML4_ENTRY));
    }

    return Pml4Physical;
}

/************************************************************************/

BOOL ResolveKernelPageFault(LINEAR FaultAddress) {
    if (FaultAddress < VMA_KERNEL) {
        DEBUG(TEXT("[ResolveKernelPageFault] Address %p below kernel VMA"), (LPVOID)FaultAddress);
        return FALSE;
    }

    PHYSICAL KernelDirectoryPhysical = KernelProcess.PageDirectory;
    if (KernelDirectoryPhysical == 0) {
        KernelDirectoryPhysical = KernelStartup.PageDirectory;
    }

    if (KernelDirectoryPhysical == 0) {
        DEBUG(TEXT("[ResolveKernelPageFault] No kernel directory available (Fault=%p)"), (LPVOID)FaultAddress);
        return FALSE;
    }

    PHYSICAL CurrentDirectoryPhysical = (PHYSICAL)GetPageDirectory();
    if (CurrentDirectoryPhysical == 0 || CurrentDirectoryPhysical == KernelDirectoryPhysical) {
        DEBUG(TEXT("[ResolveKernelPageFault] CR3=%p matches kernel directory %p (Fault=%p)"),
              (LPVOID)CurrentDirectoryPhysical,
              (LPVOID)KernelDirectoryPhysical,
              (LPVOID)FaultAddress);
        return FALSE;
    }

    UINT Pml4Index = GetPml4Index(FaultAddress);
    UINT PdptIndex = GetPdptIndex(FaultAddress);
    UINT DirectoryIndex = GetDirectoryIndex(FaultAddress);
    UINT TableIndex = GetTableIndex(FaultAddress);

    LINEAR KernelPml4Linear = MapTemporaryPhysicalPage1(KernelDirectoryPhysical);
    if (KernelPml4Linear == 0) {
        ERROR(TEXT("[ResolveKernelPageFault] Unable to map kernel PML4"));
        return FALSE;
    }

    LPPML4 KernelPml4 = (LPPML4)KernelPml4Linear;
    const X86_64_PML4_ENTRY* KernelPml4Entry = &KernelPml4[Pml4Index];
    if (!KernelPml4Entry->Present) {
        DEBUG(TEXT("[ResolveKernelPageFault] Kernel PML4[%u] not present"), Pml4Index);
        return FALSE;
    }

    PHYSICAL KernelPdptPhysical = (PHYSICAL)(KernelPml4Entry->Address << PAGE_SIZE_MUL);
    LINEAR KernelPdptLinear = MapTemporaryPhysicalPage2(KernelPdptPhysical);
    if (KernelPdptLinear == 0) {
        ERROR(TEXT("[ResolveKernelPageFault] Unable to map kernel PDPT"));
        return FALSE;
    }

    LPPDPT KernelPdpt = (LPPDPT)KernelPdptLinear;
    const X86_64_PDPT_ENTRY* KernelPdptEntry = &KernelPdpt[PdptIndex];
    if (!KernelPdptEntry->Present) {
        DEBUG(TEXT("[ResolveKernelPageFault] Kernel PDPT[%u] not present"), PdptIndex);
        return FALSE;
    }

    LPPML4 CurrentPml4 = GetCurrentPml4VA();
    BOOL Updated = FALSE;
    BOOL NeedsFullFlush = FALSE;

    X86_64_PML4_ENTRY* CurrentPml4Entry = &CurrentPml4[Pml4Index];
    if (CurrentPml4Entry->Present == 0 || CurrentPml4Entry->Address != KernelPml4Entry->Address) {
        *CurrentPml4Entry = *KernelPml4Entry;
        Updated = TRUE;
        NeedsFullFlush = TRUE;
    }

    if (KernelPdptEntry->PageSize) {
        LPPDPT CurrentPdpt = GetPageDirectoryPointerVAFor(Pml4Index, PdptIndex);
        CurrentPdpt[PdptIndex] = *KernelPdptEntry;
    } else {
        PHYSICAL KernelDirectoryPhysical = (PHYSICAL)(KernelPdptEntry->Address << PAGE_SIZE_MUL);
        LINEAR KernelDirectoryLinear = MapTemporaryPhysicalPage3(KernelDirectoryPhysical);
        if (KernelDirectoryLinear == 0) {
            ERROR(TEXT("[ResolveKernelPageFault] Unable to map kernel directory"));
            return FALSE;
        }

        LPPAGE_DIRECTORY KernelDirectory = (LPPAGE_DIRECTORY)KernelDirectoryLinear;
        const X86_64_PAGE_DIRECTORY_ENTRY* KernelDirEntry = &KernelDirectory[DirectoryIndex];
        if (!KernelDirEntry->Present) {
            DEBUG(TEXT("[ResolveKernelPageFault] Kernel PDE[%u] not present"), DirectoryIndex);
            return FALSE;
        }

        LPPDPT CurrentPdpt = GetPageDirectoryPointerVAFor(Pml4Index, PdptIndex);
        LPPAGE_DIRECTORY CurrentDirectory = GetPageDirectoryVAFor(PdptIndex, DirectoryIndex);
        X86_64_PAGE_DIRECTORY_ENTRY* CurrentDirEntry = &CurrentDirectory[DirectoryIndex];

        if (KernelDirEntry->PageSize) {
            if (CurrentDirEntry->Present == 0 || CurrentDirEntry->Address != KernelDirEntry->Address || CurrentDirEntry->PageSize == 0) {
                *CurrentDirEntry = *KernelDirEntry;
                Updated = TRUE;
                NeedsFullFlush = TRUE;
            }
        } else {
            PHYSICAL KernelTablePhysical = (PHYSICAL)(KernelDirEntry->Address << PAGE_SIZE_MUL);
            LINEAR KernelTableLinear = MapTemporaryPhysicalPage2(KernelTablePhysical);
            if (KernelTableLinear == 0) {
                ERROR(TEXT("[ResolveKernelPageFault] Unable to map kernel table"));
                return FALSE;
            }

            LPPAGE_TABLE KernelTable = (LPPAGE_TABLE)KernelTableLinear;
            const X86_64_PAGE_TABLE_ENTRY* KernelTableEntry = &KernelTable[TableIndex];
            if (!KernelTableEntry->Present) {
                return FALSE;
            }

            if (CurrentDirEntry->Present == 0 || CurrentDirEntry->PageSize != 0) {
                *CurrentDirEntry = *KernelDirEntry;
                Updated = TRUE;
                NeedsFullFlush = TRUE;
            }

            LPPAGE_TABLE CurrentTable = GetPageTableVAFor(PdptIndex, DirectoryIndex);
            X86_64_PAGE_TABLE_ENTRY* CurrentTableEntry = &CurrentTable[TableIndex];
            if (CurrentTableEntry->Present == 0 || CurrentTableEntry->Address != KernelTableEntry->Address) {
                *CurrentTableEntry = *KernelTableEntry;
                Updated = TRUE;
            }
        }
    }

    if (!Updated) {
        DEBUG(TEXT("[ResolveKernelPageFault] No update required for %p"), (LPVOID)FaultAddress);
        return FALSE;
    }

    if (NeedsFullFlush) {
        FlushTLB();
    } else {
        InvalidatePage(FaultAddress);
    }

    DEBUG(TEXT("[ResolveKernelPageFault] Mirrored kernel mapping for %p"), (LPVOID)FaultAddress);
    return TRUE;
}

/************************************************************************/

LINEAR MapIOMemory(PHYSICAL PhysicalBase, UINT Size) {
    if (PhysicalBase == 0 || Size == 0) {
        ERROR(TEXT("[MapIOMemory] Invalid parameters (PA=%p Size=%x)"), (LPVOID)PhysicalBase, Size);
        return 0;
    }

    PHYSICAL LowWindow = GetCachedLowMemoryWindowLimit();
    PHYSICAL PageOffset = PhysicalBase & (PAGE_SIZE - 1u);
    PHYSICAL AlignedPhysicalBase = PhysicalBase & ~(PAGE_SIZE - 1u);

    UINT AdjustedSize = Size + (UINT)PageOffset;
    if (AlignedPhysicalBase < LowWindow) {
        AdjustedSize = (AdjustedSize + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
    } else {
        if ((AlignedPhysicalBase & PAGE_2M_MASK) != 0) {
            ERROR(TEXT("[MapIOMemory] Physical base must be 2M aligned above low window (%p)"), (LPVOID)PhysicalBase);
            return 0;
        }
        AdjustedSize = (AdjustedSize + PAGE_2M_MASK) & ~PAGE_2M_MASK;
    }

    U32 Flags = ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_UC | ALLOC_PAGES_IO | ALLOC_PAGES_AT_OR_OVER;

    LINEAR AlignedResult = AllocRegion(VMA_KERNEL, AlignedPhysicalBase, AdjustedSize, Flags);
    if (AlignedResult == 0) {
        DEBUG(TEXT("[MapIOMemory] AllocRegion failed"));
        return 0;
    }

    return AlignedResult + PageOffset;
}

/************************************************************************/

BOOL UnMapIOMemory(LINEAR LinearBase, UINT Size) {
    if (LinearBase == 0 || Size == 0) {
        ERROR(TEXT("[UnMapIOMemory] Invalid parameters (LA=%p Size=%u)"), (LPVOID)LinearBase, Size);
        return FALSE;
    }

    return FreeRegion(LinearBase, Size);
}

/************************************************************************/


/**
 * @brief Map a linear address to a physical address in the current CR3.
 * @param Linear Linear address to resolve.
 * @return Physical address or 0 on failure.
 */
PHYSICAL MapLinearToPhysical(LINEAR Linear) {
    UINT Pml4Index = GetPml4Index(Linear);
    UINT PdptIndex = GetPdptIndex(Linear);
    UINT DirectoryIndex = GetDirectoryIndex(Linear);
    UINT TableIndex = GetTableIndex(Linear);

    LPPML4 Pml4 = GetCurrentPml4VA();
    const X86_64_PML4_ENTRY* Pml4Entry = &Pml4[Pml4Index];
    if (!Pml4Entry->Present) return 0;

    LPPDPT Pdpt = GetPageDirectoryPointerVAFor(Pml4Index, PdptIndex);
    const X86_64_PDPT_ENTRY* PdptEntry = &Pdpt[PdptIndex];
    if (!PdptEntry->Present) return 0;

    if (PdptEntry->PageSize) {
        PHYSICAL Base = (PHYSICAL)(PdptEntry->Address << 12);
        PHYSICAL Offset = (PHYSICAL)(Linear & (((PHYSICAL)1u << 30) - 1u));
        return Base + Offset;
    }

    LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(PdptIndex, DirectoryIndex);
    const X86_64_PAGE_DIRECTORY_ENTRY* DirEntry = &Directory[DirectoryIndex];
    if (!DirEntry->Present) return 0;

    if (DirEntry->PageSize) {
        PHYSICAL Base = (PHYSICAL)(DirEntry->Address << 12);
        PHYSICAL Offset = (PHYSICAL)(Linear & PAGE_2M_MASK);
        return Base + Offset;
    }

    LPPAGE_TABLE Table = GetPageTableVAFor(PdptIndex, DirectoryIndex);
    const X86_64_PAGE_TABLE_ENTRY* TabEntry = &Table[TableIndex];
    if (!TabEntry->Present) return 0;

    PHYSICAL Base = (PHYSICAL)(TabEntry->Address << PAGE_SIZE_MUL);
    PHYSICAL Offset = (PHYSICAL)(Linear & PAGE_SIZE_MASK);
    return Base + Offset;
}

/************************************************************************/

/**
 * @brief Check if a linear address is mapped and accessible.
 * @param Pointer Linear address to test.
 * @return TRUE if address is valid.
 */
BOOL IsValidMemory(LINEAR Pointer) {
    return MapLinearToPhysical(Pointer) != 0;
}

/************************************************************************/

/**
 * @brief Map or remap a single page (4K) in the low window.
 * @param Linear Target linear address.
 * @param Physical Physical address to map.
 * @param Flags Page flags.
 * @return TRUE on success.
 */
static BOOL MapOnePage(LINEAR Linear, PHYSICAL Physical, U64 Flags) {
    UINT Pml4Index = GetPml4Index(Linear);
    UINT PdptIndex = GetPdptIndex(Linear);
    UINT DirectoryIndex = GetDirectoryIndex(Linear);
    UINT TableIndex = GetTableIndex(Linear);

    LPPML4 Pml4 = GetCurrentPml4VA();
    LPPDPT Pdpt = GetPageDirectoryPointerVAFor(Pml4Index, PdptIndex);
    LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(PdptIndex, DirectoryIndex);
    LPPAGE_TABLE Table = GetPageTableVAFor(PdptIndex, DirectoryIndex);

    if (!Pml4[Pml4Index].Present || !Pdpt[PdptIndex].Present || !Directory[DirectoryIndex].Present) {
        ERROR(TEXT("[MapOnePage] Missing paging structures for VA %p"), (LPVOID)Linear);
        return FALSE;
    }

    SetPageFlags(&Table[TableIndex], Flags, Physical);
    InvalidatePage(Linear);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Unmap a 4K page.
 * @param Linear Linear address.
 */
static void UnmapOnePage(LINEAR Linear) {
    UINT Pml4Index = GetPml4Index(Linear);
    UINT PdptIndex = GetPdptIndex(Linear);
    UINT DirectoryIndex = GetDirectoryIndex(Linear);
    UINT TableIndex = GetTableIndex(Linear);

    LPPAGE_DIRECTORY Directory = GetPageDirectoryVAFor(PdptIndex, DirectoryIndex);
    X86_64_PAGE_DIRECTORY_ENTRY* DirEntry = &Directory[DirectoryIndex];

    if (DirEntry->Present && DirEntry->PageSize) {
        DirEntry->Present = 0;
        InvalidatePage(Linear);
        return;
    }

    LPPAGE_TABLE Table = GetPageTableVAFor(PdptIndex, DirectoryIndex);
    Table[TableIndex].Present = 0;
    InvalidatePage(Linear);
}

/************************************************************************/
