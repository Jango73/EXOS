
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


    x86-64 memory descriptors

\************************************************************************/


#include "arch/x86-64/x86-64-Memory-Internal.h"

/************************************************************************/
// Fast region walker constants

#define FAST_REGION_PAGES_PER_PT 1u
#define FAST_REGION_PAGES_PER_PD (PAGE_TABLE_NUM_ENTRIES)
#define FAST_REGION_PAGES_PER_PDPT (PAGE_TABLE_NUM_ENTRIES * PAGE_TABLE_NUM_ENTRIES)
#define FAST_REGION_PAGES_PER_PML4 (PAGE_TABLE_NUM_ENTRIES * PAGE_TABLE_NUM_ENTRIES * PAGE_TABLE_NUM_ENTRIES)

const U64 FAST_REGION_SPAN_BYTES_PD = (U64)PAGE_TABLE_CAPACITY;
const U64 FAST_REGION_SPAN_BYTES_PDPT = (U64)N_1GB;
const U64 FAST_REGION_SPAN_BYTES_PML4 = FAST_REGION_SPAN_BYTES_PDPT * (U64)PAGE_TABLE_NUM_ENTRIES;

/************************************************************************/
// Compiler hint for unused fast walker entry points

#if defined(__GNUC__)
    #define FAST_WALKER_UNUSED __attribute__((unused))
#else
    #define FAST_WALKER_UNUSED
#endif

/************************************************************************/
// Fast region walker types

typedef enum {
    MEMORY_REGION_FAST_LEVEL_PT = 0,
    MEMORY_REGION_FAST_LEVEL_PD = 1,
    MEMORY_REGION_FAST_LEVEL_PDPT = 2,
    MEMORY_REGION_FAST_LEVEL_PML4 = 3
} MEMORY_REGION_FAST_LEVEL;

typedef struct tag_MEMORY_REGION_FAST_SEGMENT {
    LINEAR CanonicalBase;
    UINT PageCount;
    MEMORY_REGION_FAST_LEVEL Level;
} MEMORY_REGION_FAST_SEGMENT, *LPMEMORY_REGION_FAST_SEGMENT;

typedef BOOL (*MEMORY_REGION_FAST_CALLBACK)(
    const MEMORY_REGION_DESCRIPTOR* Descriptor,
    const MEMORY_REGION_FAST_SEGMENT* Segment,
    LPVOID Context);

/************************************************************************/
// Fast walker contexts

typedef struct tag_FAST_ALLOC_CONTEXT {
    PHYSICAL TargetBase;
    U32 Flags;
    U32 ReadWrite;
    U32 PteCacheDisabled;
    U32 PteWriteThrough;
    LPCSTR FunctionName;
    UINT PagesProcessed;
    BOOL Success;
} FAST_ALLOC_CONTEXT, *LPFAST_ALLOC_CONTEXT;

typedef struct tag_FAST_RELEASE_CONTEXT {
    UINT PagesProcessed;
    BOOL Success;
} FAST_RELEASE_CONTEXT, *LPFAST_RELEASE_CONTEXT;

void MapOnePage(
    LINEAR Linear,
    PHYSICAL Physical,
    U32 ReadWrite,
    U32 Privilege,
    U32 WriteThrough,
    U32 CacheDisabled,
    U32 Global,
    U32 Fixed);

UINT ComputePagesUntilAlignment(LINEAR Base, U64 SpanSize);
BOOL ResolveRegionFast(
    const MEMORY_REGION_DESCRIPTOR* Descriptor,
    MEMORY_REGION_FAST_CALLBACK Callback,
    LPVOID Context);
BOOL FastPopulateChunk(LINEAR ChunkBase, UINT ChunkPages, LPFAST_ALLOC_CONTEXT Context);
BOOL FastReleaseChunk(LINEAR ChunkBase, UINT ChunkPages, LPFAST_RELEASE_CONTEXT Context);
BOOL FastPopulateRegionCallback(
    const MEMORY_REGION_DESCRIPTOR* Descriptor,
    const MEMORY_REGION_FAST_SEGMENT* Segment,
    LPVOID Context);
BOOL FastReleaseRegionCallback(
    const MEMORY_REGION_DESCRIPTOR* Descriptor,
    const MEMORY_REGION_FAST_SEGMENT* Segment,
    LPVOID Context);
BOOL FAST_WALKER_UNUSED FastPopulateRegionFromDescriptor(
    const MEMORY_REGION_DESCRIPTOR* Descriptor,
    PHYSICAL Target,
    U32 Flags,
    LPCSTR FunctionName,
    UINT* OutPagesProcessed);
BOOL FAST_WALKER_UNUSED FastReleaseRegionFromDescriptor(
    const MEMORY_REGION_DESCRIPTOR* Descriptor,
    UINT* OutPagesProcessed);
#if EXOS_X86_64_FAST_VMM
void InitializeTransientDescriptor(LPMEMORY_REGION_DESCRIPTOR Descriptor,
                                   LINEAR Base,
                                   UINT PageCount,
                                   PHYSICAL PhysicalBase,
                                   U32 Flags);
BOOL ReleaseRegionWithFastWalker(LINEAR CanonicalBase, UINT NumPages);
#endif
BOOL FreeRegionLegacyInternal(LINEAR CanonicalBase, UINT NumPages, LINEAR OriginalBase, UINT Size);

/************************************************************************/
/**
 * @brief Determine the largest paging granularity compatible with a region.
 * @param Base Canonical base of the region.
 * @param PageCount Number of pages described by the region.
 * @return Corresponding granularity.
 */
MEMORY_REGION_GRANULARITY ComputeDescriptorGranularity(LINEAR Base, UINT PageCount) {
    if (PageCount == 0u) {
        return MEMORY_REGION_GRANULARITY_4K;
    }

    if ((((U64)Base & (FAST_REGION_SPAN_BYTES_PDPT - (U64)1)) == 0u) &&
        (PageCount % FAST_REGION_PAGES_PER_PDPT) == 0u) {
        return MEMORY_REGION_GRANULARITY_1G;
    }

    if ((((U64)Base & (FAST_REGION_SPAN_BYTES_PD - (U64)1)) == 0u) &&
        (PageCount % FAST_REGION_PAGES_PER_PD) == 0u) {
        return MEMORY_REGION_GRANULARITY_2M;
    }

    return MEMORY_REGION_GRANULARITY_4K;
}

/************************************************************************/
#if EXOS_X86_64_FAST_VMM
/**
 * @brief Initialize a transient descriptor used for fast walker operations.
 * @param Descriptor Descriptor to populate.
 * @param Base Requested linear base (canonicalized inside).
 * @param PageCount Number of pages covered by the span.
 * @param PhysicalBase Physical base used for fixed mappings (0 for freshly allocated).
 * @param Flags Allocation flags that describe the mapping.
 */
void InitializeTransientDescriptor(LPMEMORY_REGION_DESCRIPTOR Descriptor,
                                          LINEAR Base,
                                          UINT PageCount,
                                          PHYSICAL PhysicalBase,
                                          U32 Flags) {
    if (Descriptor == NULL) {
        return;
    }

    MemorySet(Descriptor, 0, sizeof(MEMORY_REGION_DESCRIPTOR));

    LINEAR CanonicalBase = CanonicalizeLinearAddress(Base);
    Descriptor->Base = CanonicalBase;
    Descriptor->CanonicalBase = CanonicalBase;
    Descriptor->PhysicalBase = PhysicalBase;
    Descriptor->PageCount = PageCount;
    Descriptor->Size = PageCount << PAGE_SIZE_MUL;
    Descriptor->Flags = Flags;

    U32 Attributes = 0u;
    if ((Flags & ALLOC_PAGES_COMMIT) != 0u) {
        Attributes |= MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_COMMIT;
    }
    if ((Flags & ALLOC_PAGES_IO) != 0u) {
        Attributes |= MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_IO;
        Attributes |= MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_FIXED;
    }
    Descriptor->Attributes = Attributes;

    Descriptor->Granularity = ComputeDescriptorGranularity(CanonicalBase, PageCount);
}
#endif

/************************************************************************/
/**
 * @brief Walk a region descriptor using large aligned spans first.
 * @param Descriptor Region descriptor to process.
 * @param Callback Callback invoked for each resolved segment.
 * @param Context User-provided context pointer.
 * @return TRUE when the enumeration completes successfully.
 */
BOOL ResolveRegionFast(
    const MEMORY_REGION_DESCRIPTOR* Descriptor,
    MEMORY_REGION_FAST_CALLBACK Callback,
    LPVOID Context) {
    if (Descriptor == NULL || Callback == NULL) {
        return FALSE;
    }

    LINEAR Cursor = Descriptor->CanonicalBase;
    UINT RemainingPages = Descriptor->PageCount;

    while (RemainingPages != 0u) {
        MEMORY_REGION_FAST_SEGMENT Segment;
        Segment.CanonicalBase = Cursor;
        Segment.PageCount = 0u;
        Segment.Level = MEMORY_REGION_FAST_LEVEL_PT;

        if ((((U64)Cursor & (FAST_REGION_SPAN_BYTES_PML4 - (U64)1)) == 0u) &&
            RemainingPages >= FAST_REGION_PAGES_PER_PML4) {
            Segment.PageCount = FAST_REGION_PAGES_PER_PML4;
            Segment.Level = MEMORY_REGION_FAST_LEVEL_PML4;
        } else if ((((U64)Cursor & (FAST_REGION_SPAN_BYTES_PDPT - (U64)1)) == 0u) &&
                   RemainingPages >= FAST_REGION_PAGES_PER_PDPT) {
            Segment.PageCount = FAST_REGION_PAGES_PER_PDPT;
            Segment.Level = MEMORY_REGION_FAST_LEVEL_PDPT;
        } else if ((((U64)Cursor & (FAST_REGION_SPAN_BYTES_PD - (U64)1)) == 0u) &&
                   RemainingPages >= FAST_REGION_PAGES_PER_PD) {
            Segment.PageCount = FAST_REGION_PAGES_PER_PD;
            Segment.Level = MEMORY_REGION_FAST_LEVEL_PD;
        } else {
            UINT PagesToBoundary = ComputePagesUntilAlignment(Cursor, FAST_REGION_SPAN_BYTES_PD);
            UINT SpanPages = RemainingPages;
            if (PagesToBoundary != 0u && PagesToBoundary < SpanPages) {
                SpanPages = PagesToBoundary;
            }
            if (SpanPages == 0u) {
                SpanPages = RemainingPages;
            }
            Segment.PageCount = SpanPages;
            Segment.Level = MEMORY_REGION_FAST_LEVEL_PT;
        }

        if (Segment.PageCount == 0u) {
            Segment.PageCount = RemainingPages;
        }

        if (Callback(Descriptor, &Segment, Context) == FALSE) {
            return FALSE;
        }

        Cursor += ((LINEAR)Segment.PageCount << PAGE_SIZE_MUL);
        RemainingPages -= Segment.PageCount;
    }

    return TRUE;
}

/************************************************************************/
/**
 * @brief Populate a contiguous chunk of pages using the allocation context.
 * @param ChunkBase Canonical chunk base.
 * @param ChunkPages Number of pages in the chunk (<= 512).
 * @param Context Allocation context.
 * @return TRUE on success, FALSE on failure.
 */
BOOL FastPopulateChunk(LINEAR ChunkBase, UINT ChunkPages, LPFAST_ALLOC_CONTEXT Context) {
    if (ChunkPages == 0u || Context == NULL) {
        return FALSE;
    }

    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(ChunkBase);
    LPPAGE_TABLE Table = NULL;
    BOOL IsLargePage = FALSE;

    if (TryGetPageTableForIterator(&Iterator, &Table, &IsLargePage) == FALSE) {
        if (IsLargePage) {
            ERROR(TEXT("[FastPopulateChunk] Large page blocks allocation at base=%p"), (LPVOID)ChunkBase);
            return FALSE;
        }

        if (AllocPageTable(ChunkBase) == NULL) {
            ERROR(TEXT("[FastPopulateChunk] AllocPageTable failed for base=%p"), (LPVOID)ChunkBase);
            return FALSE;
        }

        if (TryGetPageTableForIterator(&Iterator, &Table, NULL) == FALSE) {
            ERROR(TEXT("[FastPopulateChunk] Unable to resolve page table after allocation (base=%p)"),
                (LPVOID)ChunkBase);
            return FALSE;
        }
    }

    UINT StartIndex = MemoryPageIteratorGetTableIndex(&Iterator);
    if (StartIndex + ChunkPages > PAGE_TABLE_NUM_ENTRIES) {
        ERROR(TEXT("[FastPopulateChunk] Chunk overruns table (base=%p start=%u pages=%u)"),
            (LPVOID)ChunkBase,
            StartIndex,
            ChunkPages);
        return FALSE;
    }

    for (UINT LocalPage = 0u; LocalPage < ChunkPages; LocalPage++) {
        UINT TabEntry = StartIndex + LocalPage;
        LINEAR CurrentLinear = ChunkBase + ((LINEAR)LocalPage << PAGE_SIZE_MUL);
        U32 Privilege = PAGE_PRIVILEGE(CurrentLinear);
        U32 FixedFlag = (Context->Flags & ALLOC_PAGES_IO) ? 1u : 0u;
        U32 BaseFlags = BuildPageFlags(
            Context->ReadWrite,
            Privilege,
            Context->PteWriteThrough,
            Context->PteCacheDisabled,
            0u,
            FixedFlag);
        U32 ReservedFlags = BaseFlags & ~PAGE_FLAG_PRESENT;
        PHYSICAL ReservedPhysical = (PHYSICAL)(MAX_U32 & ~(PAGE_SIZE - 1u));

        WritePageTableEntryValue(Table, TabEntry, MakePageEntryRaw(ReservedPhysical, ReservedFlags));

        if ((Context->Flags & ALLOC_PAGES_COMMIT) != 0u) {
            PHYSICAL Physical = 0;

            if (Context->TargetBase != 0) {
                Physical = Context->TargetBase + ((PHYSICAL)Context->PagesProcessed << PAGE_SIZE_MUL);

                if ((Context->Flags & ALLOC_PAGES_IO) == 0u) {
                    SetPhysicalPageMark((UINT)(Physical >> PAGE_SIZE_MUL), 1u);
                }

                WritePageTableEntryValue(
                    Table,
                    TabEntry,
                    MakePageTableEntryValue(
                        Physical,
                        Context->ReadWrite,
                        Privilege,
                        Context->PteWriteThrough,
                        Context->PteCacheDisabled,
                        0u,
                        FixedFlag));
            } else {
                Physical = AllocPhysicalPage();

                if (Physical == NULL) {
                    LPCSTR Label = (Context->FunctionName != NULL) ? Context->FunctionName : TEXT("FastPopulateChunk");
                    ERROR(TEXT("[%s] AllocPhysicalPage failed at linear=%p"), Label, (LPVOID)CurrentLinear);
                    return FALSE;
                }

                WritePageTableEntryValue(
                    Table,
                    TabEntry,
                    MakePageTableEntryValue(
                        Physical,
                        Context->ReadWrite,
                        Privilege,
                        Context->PteWriteThrough,
                        Context->PteCacheDisabled,
                        0u,
                        FixedFlag));
            }
        }

        Context->PagesProcessed++;
    }

    return TRUE;
}

/************************************************************************/
/**
 * @brief Resolve segments during allocation and populate each chunk.
 */
BOOL FastPopulateRegionCallback(
    const MEMORY_REGION_DESCRIPTOR* Descriptor,
    const MEMORY_REGION_FAST_SEGMENT* Segment,
    LPVOID ContextPtr) {
    UNUSED(Descriptor);

    if (Segment == NULL || ContextPtr == NULL) {
        return FALSE;
    }

    LPFAST_ALLOC_CONTEXT Context = (LPFAST_ALLOC_CONTEXT)ContextPtr;
    LINEAR SegmentBase = Segment->CanonicalBase;
    UINT Remaining = Segment->PageCount;
    UINT ChunkSize = (Segment->Level == MEMORY_REGION_FAST_LEVEL_PT)
        ? Segment->PageCount
        : FAST_REGION_PAGES_PER_PD;

    while (Remaining != 0u) {
        UINT ChunkPages = ChunkSize;
        if (ChunkPages > Remaining) {
            ChunkPages = Remaining;
        }

        if (FastPopulateChunk(SegmentBase, ChunkPages, Context) == FALSE) {
            Context->Success = FALSE;
            return FALSE;
        }

        SegmentBase += ((LINEAR)ChunkPages << PAGE_SIZE_MUL);
        Remaining -= ChunkPages;
    }

    return TRUE;
}

/************************************************************************/
/**
 * @brief Populate a region described by a descriptor using the fast walker.
 * @param Descriptor Region descriptor to populate.
 * @param Target Physical base for fixed mappings (0 for freshly allocated).
 * @param Flags Allocation flags.
 * @param FunctionName Caller label for diagnostics.
 * @param OutPagesProcessed Receives the number of pages processed (optional).
 * @return TRUE on success, FALSE on failure.
 */
BOOL FAST_WALKER_UNUSED FastPopulateRegionFromDescriptor(
    const MEMORY_REGION_DESCRIPTOR* Descriptor,
    PHYSICAL Target,
    U32 Flags,
    LPCSTR FunctionName,
    UINT* OutPagesProcessed) {
    if (Descriptor == NULL) {
        if (OutPagesProcessed != NULL) {
            *OutPagesProcessed = 0u;
        }
        return FALSE;
    }

    FAST_ALLOC_CONTEXT Context;
    Context.TargetBase = Target;
    Context.Flags = Flags;
    Context.ReadWrite = (Flags & ALLOC_PAGES_READWRITE) ? 1u : 0u;
    Context.PteCacheDisabled = (Flags & ALLOC_PAGES_UC) ? 1u : 0u;
    Context.PteWriteThrough = (Flags & ALLOC_PAGES_WC) ? 1u : 0u;
    if (Context.PteCacheDisabled != 0u) {
        Context.PteWriteThrough = 0u;
    }
    Context.FunctionName = FunctionName;
    Context.PagesProcessed = 0u;
    Context.Success = TRUE;

    if (ResolveRegionFast(Descriptor, FastPopulateRegionCallback, &Context) == FALSE) {
        Context.Success = FALSE;
    }

    if (OutPagesProcessed != NULL) {
        *OutPagesProcessed = Context.PagesProcessed;
    }

    return Context.Success;
}

/************************************************************************/
/**
 * @brief Release a contiguous chunk of pages using the fast walker context.
 * @param ChunkBase Canonical base of the chunk.
 * @param ChunkPages Number of pages in the chunk (<= 512).
 * @param Context Release context.
 * @return TRUE on success, FALSE on failure.
 */
BOOL FastReleaseChunk(LINEAR ChunkBase, UINT ChunkPages, LPFAST_RELEASE_CONTEXT Context) {
    if (ChunkPages == 0u || Context == NULL) {
        return FALSE;
    }

    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(ChunkBase);
    LPPAGE_TABLE Table = NULL;
    BOOL IsLargePage = FALSE;

    if (TryGetPageTableForIterator(&Iterator, &Table, &IsLargePage) == FALSE) {
        Context->PagesProcessed += ChunkPages;
        return TRUE;
    }

    UINT StartIndex = MemoryPageIteratorGetTableIndex(&Iterator);
    if (StartIndex + ChunkPages > PAGE_TABLE_NUM_ENTRIES) {
        ERROR(TEXT("[FastReleaseChunk] Chunk overruns table (base=%p start=%u pages=%u)"),
            (LPVOID)ChunkBase,
            StartIndex,
            ChunkPages);
        Context->Success = FALSE;
        return FALSE;
    }

    BOOL EntireTable = (ChunkPages == FAST_REGION_PAGES_PER_PD) && (StartIndex == 0u);
    LPPAGE_DIRECTORY Directory = NULL;
    UINT DirEntry = 0u;
    PHYSICAL TablePhysical = 0u;

    if (EntireTable) {
        Directory = GetPageDirectoryVAFor(ChunkBase);
        DirEntry = MemoryPageIteratorGetDirectoryIndex(&Iterator);
        U64 DirectoryEntryValue = ReadPageDirectoryEntryValue(Directory, DirEntry);

        if ((DirectoryEntryValue & PAGE_FLAG_PRESENT) != 0u &&
            (DirectoryEntryValue & PAGE_FLAG_PAGE_SIZE) == 0u) {
            TablePhysical = (PHYSICAL)(DirectoryEntryValue & PAGE_MASK);
        } else {
            EntireTable = FALSE;
        }
    }

    for (UINT LocalPage = 0u; LocalPage < ChunkPages; LocalPage++) {
        UINT TabEntry = StartIndex + LocalPage;
        if (TabEntry >= PAGE_TABLE_NUM_ENTRIES) {
            break;
        }

        if (PageTableEntryIsPresent(Table, TabEntry)) {
            PHYSICAL EntryPhysical = PageTableEntryGetPhysical(Table, TabEntry);
            BOOL Fixed = PageTableEntryIsFixed(Table, TabEntry);

            if (Fixed == FALSE) {
                SetPhysicalPageMark((UINT)(EntryPhysical >> PAGE_SIZE_MUL), 0u);
            }

            ClearPageTableEntry(Table, TabEntry);
        }

        Context->PagesProcessed++;
    }

    if (EntireTable && TablePhysical != 0u) {
        SetPhysicalPageMark((UINT)(TablePhysical >> PAGE_SIZE_MUL), 0u);
        ClearPageDirectoryEntry(Directory, DirEntry);
    }

    return TRUE;
}

/************************************************************************/
/**
 * @brief Resolve segments during release and free each chunk.
 */
BOOL FastReleaseRegionCallback(
    const MEMORY_REGION_DESCRIPTOR* Descriptor,
    const MEMORY_REGION_FAST_SEGMENT* Segment,
    LPVOID ContextPtr) {
    UNUSED(Descriptor);

    if (Segment == NULL || ContextPtr == NULL) {
        return FALSE;
    }

    LPFAST_RELEASE_CONTEXT Context = (LPFAST_RELEASE_CONTEXT)ContextPtr;
    LINEAR SegmentBase = Segment->CanonicalBase;
    UINT Remaining = Segment->PageCount;
    UINT ChunkSize = (Segment->Level == MEMORY_REGION_FAST_LEVEL_PT)
        ? Segment->PageCount
        : FAST_REGION_PAGES_PER_PD;

    while (Remaining != 0u) {
        UINT ChunkPages = ChunkSize;
        if (ChunkPages > Remaining) {
            ChunkPages = Remaining;
        }

        if (FastReleaseChunk(SegmentBase, ChunkPages, Context) == FALSE) {
            Context->Success = FALSE;
            return FALSE;
        }

        SegmentBase += ((LINEAR)ChunkPages << PAGE_SIZE_MUL);
        Remaining -= ChunkPages;
    }

    return TRUE;
}

/************************************************************************/
/**
 * @brief Release a region described by a descriptor using the fast walker.
 * @param Descriptor Region descriptor to release.
 * @param OutPagesProcessed Receives the number of pages processed (optional).
 * @return TRUE on success, FALSE on failure.
 */
BOOL FAST_WALKER_UNUSED FastReleaseRegionFromDescriptor(
    const MEMORY_REGION_DESCRIPTOR* Descriptor,
    UINT* OutPagesProcessed) {
    if (Descriptor == NULL) {
        if (OutPagesProcessed != NULL) {
            *OutPagesProcessed = 0u;
        }
        return FALSE;
    }

    FAST_RELEASE_CONTEXT Context;
    Context.PagesProcessed = 0u;
    Context.Success = TRUE;

    if (ResolveRegionFast(Descriptor, FastReleaseRegionCallback, &Context) == FALSE) {
        Context.Success = FALSE;
    }

    if (OutPagesProcessed != NULL) {
        *OutPagesProcessed = Context.PagesProcessed;
    }

    return Context.Success;
}

#if EXOS_X86_64_FAST_VMM
/************************************************************************/
/**
 * @brief Release a region span by walking descriptors in large aligned chunks.
 * @param CanonicalBase Canonical base address of the span.
 * @param NumPages Number of pages to release.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL ReleaseRegionWithFastWalker(LINEAR CanonicalBase, UINT NumPages) {
    if (NumPages == 0u) {
        return TRUE;
    }

    LPPROCESS Process = ResolveCurrentAddressSpaceOwner();
    LINEAR Cursor = CanonicalBase;
    LINEAR End = CanonicalBase + ((LINEAR)NumPages << PAGE_SIZE_MUL);

    while (Cursor < End) {
        LPMEMORY_REGION_DESCRIPTOR Descriptor = FindDescriptorCoveringAddress(Process, Cursor);
        if (Descriptor == NULL) {
            DEBUG(TEXT("[ReleaseRegionWithFastWalker] Missing descriptor for base=%p"),
                (LPVOID)Cursor);
            return FALSE;
        }

        LINEAR RegionStart = Descriptor->CanonicalBase;
        LINEAR RegionEnd = RegionStart + (LINEAR)Descriptor->Size;
        LINEAR SegmentEnd = End;
        if (SegmentEnd > RegionEnd) {
            SegmentEnd = RegionEnd;
        }

        if (SegmentEnd <= Cursor) {
            WARNING(TEXT("[ReleaseRegionWithFastWalker] Degenerate segment at base=%p"), (LPVOID)Cursor);
            return FALSE;
        }

        UINT SegmentPages = (UINT)((SegmentEnd - Cursor) >> PAGE_SIZE_MUL);
        if (SegmentPages == 0u) {
            WARNING(TEXT("[ReleaseRegionWithFastWalker] Zero-length segment at base=%p"), (LPVOID)Cursor);
            return FALSE;
        }

        PHYSICAL SegmentPhysical = Descriptor->PhysicalBase;
        if (SegmentPhysical != 0u) {
            SegmentPhysical += (PHYSICAL)(Cursor - RegionStart);
        }

        MEMORY_REGION_DESCRIPTOR SegmentDescriptor;
        InitializeTransientDescriptor(&SegmentDescriptor, Cursor, SegmentPages, SegmentPhysical, Descriptor->Flags);
        SegmentDescriptor.Attributes = Descriptor->Attributes;

        UINT ReleasedPages = 0u;
        if (FastReleaseRegionFromDescriptor(&SegmentDescriptor, &ReleasedPages) == FALSE ||
            ReleasedPages != SegmentPages) {
            WARNING(TEXT("[ReleaseRegionWithFastWalker] Fast walker released %u/%u pages at base=%p"),
                ReleasedPages,
                SegmentPages,
                (LPVOID)Cursor);
            return FALSE;
        }

        Cursor = SegmentEnd;
    }

    return TRUE;
}
#endif

/************************************************************************/
/**
 * @brief Legacy per-page region release path retained for fallback.
 * @param CanonicalBase Canonical base address.
 * @param NumPages Number of pages to release.
 * @param OriginalBase Original base requested by the caller.
 * @param Size Size in bytes requested by the caller.
 */
BOOL FreeRegionLegacyInternal(LINEAR CanonicalBase, UINT NumPages, LINEAR OriginalBase, UINT Size) {
    LPPAGE_TABLE Table = NULL;
    LINEAR Cursor = CanonicalBase;
    LINEAR CanonicalStart = CanonicalBase;
    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(CanonicalBase);

    UNUSED(OriginalBase);
    UNUSED(Size);

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);
        UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(&Iterator);
#if DEBUG_OUTPUT == 0
        UNUSED(DirEntry);
#endif

        BOOL IsLargePage = FALSE;

        if (TryGetPageTableForIterator(&Iterator, &Table, &IsLargePage) && PageTableEntryIsPresent(Table, TabEntry)) {
            PHYSICAL EntryPhysical = PageTableEntryGetPhysical(Table, TabEntry);
            BOOL Fixed = PageTableEntryIsFixed(Table, TabEntry);

            DEBUG(TEXT("[FreeRegion] Unmap Dir=%u Tab=%u Phys=%p Fixed=%u"), DirEntry, TabEntry,
                (LPVOID)EntryPhysical, (UINT)(Fixed ? 1u : 0u));

            if (Fixed == FALSE) {
                SetPhysicalPageMark((UINT)(EntryPhysical >> PAGE_SIZE_MUL), 0u);
            }

            ClearPageTableEntry(Table, TabEntry);
        } else if (IsLargePage == TRUE) {
            DEBUG(TEXT("[FreeRegion] Large mapping covers Dir=%u"),
                MemoryPageIteratorGetDirectoryIndex(&Iterator));
        } else {
            DEBUG(TEXT("[FreeRegion] Missing mapping Dir=%u Tab=%u IsLarge=%u"),
                DirEntry,
                TabEntry,
                (UINT)(IsLargePage ? 1u : 0u));
        }

        MemoryPageIteratorStepPage(&Iterator);
        Cursor += PAGE_SIZE;
    }

    RegionTrackFree(CanonicalStart, NumPages << PAGE_SIZE_MUL);

    FreeEmptyPageTables();
    FlushTLB();

    DEBUG(TEXT("[FreeRegion] Exit base=%p size=%u"), (LPVOID)OriginalBase, Size);

    return TRUE;
}
