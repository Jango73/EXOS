
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


    x86-64-specific memory helpers

\************************************************************************/

#include "Memory.h"

#include "Console.h"
#include "CoreString.h"
#include "Kernel.h"
#include "process/Process.h"
#include "process/Schedule.h"
#include "Log.h"
#include "Stack.h"
#include "System.h"
#include "Text.h"
#include "arch/x86-64/x86-64.h"
#include "arch/x86-64/x86-64-Log.h"

/************************************************************************/
// Temporary mapping slots state

static LINEAR G_TempLinear1 = TEMP_LINEAR_PAGE_1;
static LINEAR G_TempLinear2 = TEMP_LINEAR_PAGE_2;
static LINEAR G_TempLinear3 = TEMP_LINEAR_PAGE_3;
static PHYSICAL G_TempPhysical1 = 0;
static PHYSICAL G_TempPhysical2 = 0;
static PHYSICAL G_TempPhysical3 = 0;

/************************************************************************/
/**
 * @brief Build a page table entry with the supplied access flags.
 * @param Physical Physical base of the page.
 * @param ReadWrite 1 when the mapping permits writes.
 * @param Privilege Privilege level (kernel/user).
 * @param WriteThrough 1 to enable write-through caching.
 * @param CacheDisabled 1 to disable CPU caches.
 * @param Global 1 when the mapping is global.
 * @param Fixed 1 when the entry must survive reclamation.
 * @return Encoded 64-bit PTE value.
 */
U64 MakePageTableEntryValue(
    PHYSICAL Physical,
    U32 ReadWrite,
    U32 Privilege,
    U32 WriteThrough,
    U32 CacheDisabled,
    U32 Global,
    U32 Fixed) {
    U64 Flags = BuildPageFlags(ReadWrite, Privilege, WriteThrough, CacheDisabled, Global, Fixed);
    return ((U64)Physical & PAGE_MASK) | Flags;
}

/************************************************************************/
/**
 * @brief Build a raw paging entry value without recomputing the flags.
 * @param Physical Physical base of the page.
 * @param Flags Pre-built flag mask.
 * @return Encoded paging entry value.
 */
U64 MakePageEntryRaw(PHYSICAL Physical, U64 Flags) {
    return ((U64)Physical & PAGE_MASK) | (Flags & 0xFFFu);
}

/************************************************************************/
/**
 * @brief Store a value inside a page-directory level entry.
 * @param Directory Directory pointer.
 * @param Index Entry index within the directory.
 * @param Value Encoded PDE value.
 */
void WritePageDirectoryEntryValue(LPPAGE_DIRECTORY Directory, UINT Index, U64 Value) {
    ((volatile U64*)Directory)[Index] = Value;
}

/************************************************************************/
/**
 * @brief Store a value inside a page-table entry.
 * @param Table Page table pointer.
 * @param Index Entry index within the table.
 * @param Value Encoded PTE value.
 */
void WritePageTableEntryValue(LPPAGE_TABLE Table, UINT Index, U64 Value) {
    ((volatile U64*)Table)[Index] = Value;
}

/************************************************************************/
/**
 * @brief Read a value from a page-directory level entry.
 * @param Directory Directory pointer.
 * @param Index Entry index.
 * @return Encoded PDE value.
 */
U64 ReadPageDirectoryEntryValue(const LPPAGE_DIRECTORY Directory, UINT Index) {
    if (Directory == NULL) {
        ERROR(TEXT("[ReadPageDirectoryEntryValue] NULL directory pointer (Index=%u)"),
            Index);
        return 0;
    }

    return ((volatile const U64*)Directory)[Index];
}

/************************************************************************/
/**
 * @brief Read a value from a page-table entry.
 * @param Table Page table pointer.
 * @param Index Entry index.
 * @return Encoded PTE value.
 */
U64 ReadPageTableEntryValue(const LPPAGE_TABLE Table, UINT Index) {
    return ((volatile const U64*)Table)[Index];
}

/************************************************************************/
/**
 * @brief Test whether a page-directory entry is marked present.
 * @param Directory Directory pointer.
 * @param Index Entry index.
 * @return TRUE when the entry is present.
 */
BOOL PageDirectoryEntryIsPresent(const LPPAGE_DIRECTORY Directory, UINT Index) {
    return (ReadPageDirectoryEntryValue(Directory, Index) & PAGE_FLAG_PRESENT) != 0;
}

/************************************************************************/
/**
 * @brief Test whether a page-table entry is marked present.
 * @param Table Page table pointer.
 * @param Index Entry index.
 * @return TRUE when the entry is present.
 */
BOOL PageTableEntryIsPresent(const LPPAGE_TABLE Table, UINT Index) {
    return (ReadPageTableEntryValue(Table, Index) & PAGE_FLAG_PRESENT) != 0;
}

/************************************************************************/
/**
 * @brief Extract the physical address encoded in a PDE.
 * @param Directory Directory pointer.
 * @param Index Entry index.
 * @return Physical base address.
 */
PHYSICAL PageDirectoryEntryGetPhysical(const LPPAGE_DIRECTORY Directory, UINT Index) {
    return (PHYSICAL)(ReadPageDirectoryEntryValue(Directory, Index) & PAGE_MASK);
}

/************************************************************************/
/**
 * @brief Extract the physical address encoded in a PTE.
 * @param Table Page table pointer.
 * @param Index Entry index.
 * @return Physical base address.
 */
PHYSICAL PageTableEntryGetPhysical(const LPPAGE_TABLE Table, UINT Index) {
    return (PHYSICAL)(ReadPageTableEntryValue(Table, Index) & PAGE_MASK);
}

/************************************************************************/
/**
 * @brief Test whether a page-table entry is marked fixed.
 * @param Table Page table pointer.
 * @param Index Entry index.
 * @return TRUE when the entry is fixed.
 */
BOOL PageTableEntryIsFixed(const LPPAGE_TABLE Table, UINT Index) {
    return (ReadPageTableEntryValue(Table, Index) & PAGE_FLAG_FIXED) != 0;
}

/************************************************************************/
/**
 * @brief Clear a page-directory entry.
 * @param Directory Directory pointer.
 * @param Index Entry index.
 */
void ClearPageDirectoryEntry(LPPAGE_DIRECTORY Directory, UINT Index) {
    WritePageDirectoryEntryValue(Directory, Index, (U64)0);
}

/************************************************************************/
/**
 * @brief Clear a page-table entry.
 * @param Table Page table pointer.
 * @param Index Entry index.
 */
void ClearPageTableEntry(LPPAGE_TABLE Table, UINT Index) {
    WritePageTableEntryValue(Table, Index, (U64)0);
}

/************************************************************************/
/**
 * @brief Return the first non-canonical linear address.
 * @return Maximum linear address plus one.
 */
U64 GetMaxLinearAddressPlusOne(void) {
    return (U64)1 << 48;
}

/************************************************************************/
/**
 * @brief Return the first non-addressable physical address.
 * @return Maximum physical address plus one.
 */
U64 GetMaxPhysicalAddressPlusOne(void) {
    return (U64)1 << 52;
}

/************************************************************************/
// Feature toggles

#ifndef EXOS_X86_64_FAST_VMM
#define EXOS_X86_64_FAST_VMM 1
#endif

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

typedef enum tag_MEMORY_REGION_FAST_LEVEL {
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

/************************************************************************/
// Region descriptor tracking state

BOOL G_RegionDescriptorsEnabled = FALSE;
BOOL G_RegionDescriptorBootstrap = FALSE;
LPMEMORY_REGION_DESCRIPTOR G_FreeRegionDescriptors = NULL;
UINT G_FreeRegionDescriptorCount = 0;
UINT G_TotalRegionDescriptorCount = 0;
UINT G_RegionDescriptorPages = 0;

/************************************************************************/
// Forward declarations for descriptor helpers

BOOL EnsureDescriptorSlab(void);
LPMEMORY_REGION_DESCRIPTOR AcquireRegionDescriptor(void);
void ReleaseRegionDescriptor(LPMEMORY_REGION_DESCRIPTOR Descriptor);
void InsertDescriptorOrdered(LPPROCESS Process, LPMEMORY_REGION_DESCRIPTOR Descriptor);
void RemoveDescriptor(LPPROCESS Process, LPMEMORY_REGION_DESCRIPTOR Descriptor);
LPMEMORY_REGION_DESCRIPTOR FindDescriptorForBase(LPPROCESS Process, LINEAR CanonicalBase);
LPMEMORY_REGION_DESCRIPTOR FindDescriptorCoveringAddress(LPPROCESS Process, LINEAR CanonicalBase);
BOOL RegisterRegionDescriptor(LINEAR Base, UINT NumPages, PHYSICAL Target, U32 Flags);
void UpdateDescriptorsForFree(LINEAR Base, UINT SizeBytes);
void ExtendDescriptor(LPMEMORY_REGION_DESCRIPTOR Descriptor, UINT AdditionalPages);
LPPROCESS ResolveCurrentAddressSpaceOwner(void);
void InitializeRegionDescriptorTracking(void);
MEMORY_REGION_GRANULARITY ComputeDescriptorGranularity(LINEAR Base, UINT PageCount);
void RefreshDescriptorGranularity(LPMEMORY_REGION_DESCRIPTOR Descriptor);
#if EXOS_X86_64_FAST_VMM
void InitializeTransientDescriptor(LPMEMORY_REGION_DESCRIPTOR Descriptor,
                                          LINEAR Base,
                                          UINT PageCount,
                                          PHYSICAL PhysicalBase,
                                          U32 Flags);
#endif
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
BOOL ReleaseRegionWithFastWalker(LINEAR CanonicalBase, UINT NumPages);
#endif
BOOL FreeRegionLegacyInternal(LINEAR CanonicalBase, UINT NumPages, LINEAR OriginalBase, UINT Size);

/************************************************************************/
/**
 * @brief Resolve the process associated with the active address space.
 * @return Current process or KernelProcess when unavailable.
 */
LPPROCESS ResolveCurrentAddressSpaceOwner(void) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) {
        Process = &KernelProcess;
    }
    return Process;
}

/************************************************************************/
/**
 * @brief Allocate a new descriptor slab when the free list runs empty.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL EnsureDescriptorSlab(void) {
    if (G_FreeRegionDescriptors != NULL) {
        return TRUE;
    }

    PHYSICAL Physical = AllocPhysicalPage();
    if (Physical == NULL) {
        ERROR(TEXT("[EnsureDescriptorSlab] No physical page available"));
        return FALSE;
    }

    G_RegionDescriptorBootstrap = TRUE;
    LINEAR Linear = AllocKernelRegion(
        Physical,
        PAGE_SIZE,
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER);
    G_RegionDescriptorBootstrap = FALSE;

    if (Linear == NULL) {
        ERROR(TEXT("[EnsureDescriptorSlab] Failed to map descriptor slab"));
        FreePhysicalPage(Physical);
        return FALSE;
    }

    MemorySet((LPVOID)Linear, 0, PAGE_SIZE);

    UINT Capacity = (UINT)(PAGE_SIZE / (UINT)sizeof(MEMORY_REGION_DESCRIPTOR));
    LPMEMORY_REGION_DESCRIPTOR DescriptorArray = (LPMEMORY_REGION_DESCRIPTOR)(LINEAR)Linear;

    for (UINT Index = 0; Index < Capacity; Index++) {
        LPMEMORY_REGION_DESCRIPTOR Descriptor = DescriptorArray + Index;
        Descriptor->Next = (LPLISTNODE)G_FreeRegionDescriptors;
        Descriptor->Prev = NULL;
        G_FreeRegionDescriptors = Descriptor;
        G_FreeRegionDescriptorCount++;
        G_TotalRegionDescriptorCount++;
    }

    G_RegionDescriptorPages++;

    DEBUG(TEXT("[EnsureDescriptorSlab] Added slab #%u (capacity=%u free=%u total=%u)"),
        G_RegionDescriptorPages,
        Capacity,
        G_FreeRegionDescriptorCount,
        G_TotalRegionDescriptorCount);

    return TRUE;
}

/************************************************************************/
/**
 * @brief Obtain an initialized descriptor from the free list.
 * @return Descriptor pointer or NULL when exhausted.
 */
LPMEMORY_REGION_DESCRIPTOR AcquireRegionDescriptor(void) {
    if (G_FreeRegionDescriptors == NULL) {
        if (EnsureDescriptorSlab() == FALSE) {
            return NULL;
        }
    }

    LPMEMORY_REGION_DESCRIPTOR Descriptor = G_FreeRegionDescriptors;
    if (Descriptor != NULL) {
        G_FreeRegionDescriptors = (LPMEMORY_REGION_DESCRIPTOR)Descriptor->Next;
        if (G_FreeRegionDescriptors != NULL) {
            G_FreeRegionDescriptors->Prev = NULL;
        }
        Descriptor->Next = NULL;
        Descriptor->Prev = NULL;
        if (G_FreeRegionDescriptorCount != 0) {
            G_FreeRegionDescriptorCount--;
        }
    }

    return Descriptor;
}

/************************************************************************/
/**
 * @brief Return a descriptor to the free list.
 * @param Descriptor Descriptor to recycle.
 */
void ReleaseRegionDescriptor(LPMEMORY_REGION_DESCRIPTOR Descriptor) {
    if (Descriptor == NULL) {
        return;
    }

    Descriptor->TypeID = KOID_NONE;
    Descriptor->References = 0;
    Descriptor->ID = 0;
    Descriptor->OwnerProcess = NULL;
    Descriptor->Base = 0;
    Descriptor->CanonicalBase = 0;
    Descriptor->PhysicalBase = 0;
    Descriptor->Size = 0;
    Descriptor->PageCount = 0;
    Descriptor->Flags = 0;
    Descriptor->Attributes = 0;
    Descriptor->Granularity = MEMORY_REGION_GRANULARITY_4K;

    Descriptor->Next = (LPLISTNODE)G_FreeRegionDescriptors;
    Descriptor->Prev = NULL;
    G_FreeRegionDescriptors = Descriptor;
    G_FreeRegionDescriptorCount++;
}

/************************************************************************/
/**
 * @brief Insert a descriptor into the process list sorted by base address.
 * @param Process Target process.
 * @param Descriptor Descriptor to link.
 */
void InsertDescriptorOrdered(LPPROCESS Process, LPMEMORY_REGION_DESCRIPTOR Descriptor) {
    LPMEMORY_REGION_DESCRIPTOR Current = Process->RegionListHead;
    LPMEMORY_REGION_DESCRIPTOR Previous = NULL;

    while (Current != NULL && Current->CanonicalBase < Descriptor->CanonicalBase) {
        Previous = Current;
        Current = (LPMEMORY_REGION_DESCRIPTOR)Current->Next;
    }

    Descriptor->Next = (LPLISTNODE)Current;
    Descriptor->Prev = (LPLISTNODE)Previous;

    if (Current != NULL) {
        Current->Prev = (LPLISTNODE)Descriptor;
    } else {
        Process->RegionListTail = Descriptor;
    }

    if (Previous != NULL) {
        Previous->Next = (LPLISTNODE)Descriptor;
    } else {
        Process->RegionListHead = Descriptor;
    }

    Process->RegionCount++;
}

/************************************************************************/
/**
 * @brief Remove a descriptor from the process list.
 * @param Process Target process.
 * @param Descriptor Descriptor to unlink.
 */
void RemoveDescriptor(LPPROCESS Process, LPMEMORY_REGION_DESCRIPTOR Descriptor) {
    LPMEMORY_REGION_DESCRIPTOR Prev = (LPMEMORY_REGION_DESCRIPTOR)Descriptor->Prev;
    LPMEMORY_REGION_DESCRIPTOR Next = (LPMEMORY_REGION_DESCRIPTOR)Descriptor->Next;

    if (Prev != NULL) {
        Prev->Next = (LPLISTNODE)Next;
    } else {
        Process->RegionListHead = Next;
    }

    if (Next != NULL) {
        Next->Prev = (LPLISTNODE)Prev;
    } else {
        Process->RegionListTail = Prev;
    }

    Descriptor->Next = NULL;
    Descriptor->Prev = NULL;

    if (Process->RegionCount != 0) {
        Process->RegionCount--;
    }
}

/************************************************************************/
/**
 * @brief Find the descriptor that starts at the specified canonical base.
 * @param Process Target process.
 * @param CanonicalBase Canonical linear base.
 * @return Descriptor pointer or NULL when absent.
 */
LPMEMORY_REGION_DESCRIPTOR FindDescriptorForBase(LPPROCESS Process, LINEAR CanonicalBase) {
    LPMEMORY_REGION_DESCRIPTOR Current = Process->RegionListHead;

    while (Current != NULL) {
        if (Current->CanonicalBase == CanonicalBase) {
            return Current;
        }
        if (Current->CanonicalBase > CanonicalBase) {
            break;
        }
        Current = (LPMEMORY_REGION_DESCRIPTOR)Current->Next;
    }

    return NULL;
}

/************************************************************************/
/**
 * @brief Find the descriptor covering a given canonical address.
 * @param Process Target process.
 * @param CanonicalBase Address to resolve.
 * @return Descriptor pointer or NULL when no descriptor covers the address.
 */
LPMEMORY_REGION_DESCRIPTOR FindDescriptorCoveringAddress(
    LPPROCESS Process,
    LINEAR CanonicalBase) {
    LPMEMORY_REGION_DESCRIPTOR Current = Process->RegionListHead;

    while (Current != NULL) {
        LINEAR RegionStart = Current->CanonicalBase;
        LINEAR RegionEnd = RegionStart + (LINEAR)Current->Size;

        if (CanonicalBase >= RegionStart && CanonicalBase < RegionEnd) {
            return Current;
        }

        if (RegionStart > CanonicalBase) {
            break;
        }

        Current = (LPMEMORY_REGION_DESCRIPTOR)Current->Next;
    }

    return NULL;
}

/************************************************************************/
/**
 * @brief Extend an existing descriptor when pages are appended.
 * @param Descriptor Descriptor to update.
 * @param AdditionalPages Number of new pages.
 */
void ExtendDescriptor(LPMEMORY_REGION_DESCRIPTOR Descriptor, UINT AdditionalPages) {
    if (Descriptor == NULL || AdditionalPages == 0) {
        return;
    }

    UINT AdditionalBytes = AdditionalPages << PAGE_SIZE_MUL;
    Descriptor->Size += AdditionalBytes;
    Descriptor->PageCount += AdditionalPages;
    RefreshDescriptorGranularity(Descriptor);

    DEBUG(TEXT("[ExtendDescriptor] Base=%p addedPages=%u newSize=%u newPages=%u"),
        (LPVOID)Descriptor->CanonicalBase,
        AdditionalPages,
        Descriptor->Size,
        Descriptor->PageCount);
}

/************************************************************************/
/**
 * @brief Register a freshly allocated region descriptor.
 * @param Base Canonical base address.
 * @param NumPages Number of pages covered.
 * @param Target Physical base when fixed, 0 otherwise.
 * @param Flags Allocation flags.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL RegisterRegionDescriptor(LINEAR Base, UINT NumPages, PHYSICAL Target, U32 Flags) {
    LPPROCESS Process = ResolveCurrentAddressSpaceOwner();
    LPMEMORY_REGION_DESCRIPTOR Descriptor = AcquireRegionDescriptor();

    if (Descriptor == NULL) {
        ERROR(TEXT("[RegisterRegionDescriptor] Descriptor pool exhausted (base=%p sizePages=%u)"),
            (LPVOID)Base,
            NumPages);
        return FALSE;
    }

    Descriptor->TypeID = KOID_MEMORY_REGION_DESCRIPTOR;
    Descriptor->References = 1;
    Descriptor->ID = 0;
    Descriptor->OwnerProcess = Process;
    Descriptor->CanonicalBase = CanonicalizeLinearAddress(Base);
    Descriptor->Base = Descriptor->CanonicalBase;
    Descriptor->PhysicalBase = Target;
    Descriptor->PageCount = NumPages;
    Descriptor->Size = NumPages << PAGE_SIZE_MUL;
    Descriptor->Flags = Flags;
    RefreshDescriptorGranularity(Descriptor);

    U32 Attributes = 0;
    if ((Flags & ALLOC_PAGES_COMMIT) != 0) {
        Attributes |= MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_COMMIT;
    }
    if ((Flags & ALLOC_PAGES_IO) != 0) {
        Attributes |= MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_IO;
        Attributes |= MEMORY_REGION_DESCRIPTOR_ATTRIBUTE_FIXED;
    }
    Descriptor->Attributes = Attributes;

    InsertDescriptorOrdered(Process, Descriptor);

    DEBUG(TEXT("[RegisterRegionDescriptor] Process=%p base=%p pages=%u flags=%x count=%u free=%u"),
        (LPVOID)Process,
        (LPVOID)Descriptor->CanonicalBase,
        Descriptor->PageCount,
        Flags,
        Process->RegionCount,
        G_FreeRegionDescriptorCount);

    return TRUE;
}

/************************************************************************/
/**
 * @brief Update descriptors to account for a freed virtual span.
 * @param Base Canonical base of the range being freed.
 * @param SizeBytes Size of the freed range in bytes.
 */
void UpdateDescriptorsForFree(LINEAR Base, UINT SizeBytes) {
    if (SizeBytes == 0) {
        return;
    }

    LPPROCESS Process = ResolveCurrentAddressSpaceOwner();
    LINEAR CanonicalBase = CanonicalizeLinearAddress(Base);
    LINEAR Cursor = CanonicalBase;
    UINT RemainingBytes = SizeBytes;

    while (RemainingBytes != 0) {
        LPMEMORY_REGION_DESCRIPTOR Descriptor = FindDescriptorCoveringAddress(Process, Cursor);
        if (Descriptor == NULL) {
            WARNING(TEXT("[UpdateDescriptorsForFree] Missing descriptor for base=%p size=%u"),
                (LPVOID)Cursor,
                RemainingBytes);
            break;
        }

        LINEAR RegionStart = Descriptor->CanonicalBase;
        LINEAR RegionEnd = RegionStart + (LINEAR)Descriptor->Size;
        LINEAR FreeStart = Cursor;
        LINEAR FreeEnd = Cursor + (LINEAR)RemainingBytes;
        if (FreeEnd > RegionEnd) {
            FreeEnd = RegionEnd;
        }

        UINT SegmentBytes = (UINT)(FreeEnd - FreeStart);
        if (SegmentBytes == 0) {
            break;
        }

        BOOL EntireRegion = (FreeStart == RegionStart && FreeEnd == RegionEnd);
        BOOL TrimHead = (FreeStart == RegionStart && FreeEnd < RegionEnd);
        BOOL TrimTail = (FreeStart > RegionStart && FreeEnd == RegionEnd);

        if (EntireRegion) {
            RemoveDescriptor(Process, Descriptor);
            DEBUG(TEXT("[UpdateDescriptorsForFree] Removed region base=%p size=%u remaining=%u"),
                (LPVOID)RegionStart,
                (UINT)Descriptor->Size,
                Process->RegionCount);
            ReleaseRegionDescriptor(Descriptor);
        } else if (TrimTail) {
            UINT Remaining = (UINT)(FreeStart - RegionStart);
            if (Remaining == 0) {
                RemoveDescriptor(Process, Descriptor);
                ReleaseRegionDescriptor(Descriptor);
            } else {
                Descriptor->Size = Remaining;
                Descriptor->PageCount = Remaining >> PAGE_SIZE_MUL;
                RefreshDescriptorGranularity(Descriptor);
                DEBUG(TEXT("[UpdateDescriptorsForFree] Shrunk tail base=%p newSize=%u"),
                    (LPVOID)RegionStart,
                    Descriptor->Size);
            }
        } else if (TrimHead) {
            UINT Remaining = (UINT)(RegionEnd - FreeEnd);
            RemoveDescriptor(Process, Descriptor);
            Descriptor->Base = FreeEnd;
            Descriptor->CanonicalBase = FreeEnd;
            if (Descriptor->PhysicalBase != 0) {
                Descriptor->PhysicalBase += (PHYSICAL)(FreeEnd - RegionStart);
            }
            Descriptor->Size = Remaining;
            Descriptor->PageCount = Remaining >> PAGE_SIZE_MUL;
            RefreshDescriptorGranularity(Descriptor);
            if (Descriptor->Size == 0) {
                ReleaseRegionDescriptor(Descriptor);
            } else {
                InsertDescriptorOrdered(Process, Descriptor);
                DEBUG(TEXT("[UpdateDescriptorsForFree] Trimmed head newBase=%p newSize=%u"),
                    (LPVOID)Descriptor->CanonicalBase,
                    Descriptor->Size);
            }
        } else {
            UINT LeftBytes = (UINT)(FreeStart - RegionStart);
            UINT RightBytes = (UINT)(RegionEnd - FreeEnd);
            LPMEMORY_REGION_DESCRIPTOR Right = AcquireRegionDescriptor();

            if (Right == NULL) {
                ERROR(TEXT("[UpdateDescriptorsForFree] Unable to split descriptor at %p"),
                    (LPVOID)FreeStart);
                ConsolePanic(TEXT("Descriptor split allocation failed"));
            }

            RemoveDescriptor(Process, Descriptor);

            Descriptor->Size = LeftBytes;
            Descriptor->PageCount = LeftBytes >> PAGE_SIZE_MUL;
            PHYSICAL DescriptorPhysicalBase = Descriptor->PhysicalBase;
            RefreshDescriptorGranularity(Descriptor);

            if (Descriptor->Size == 0) {
                ReleaseRegionDescriptor(Descriptor);
            } else {
                InsertDescriptorOrdered(Process, Descriptor);
            }

            Right->TypeID = KOID_MEMORY_REGION_DESCRIPTOR;
            Right->References = 1;
            Right->ID = 0;
            Right->OwnerProcess = Process;
            Right->Base = FreeEnd;
            Right->CanonicalBase = FreeEnd;
            Right->PhysicalBase = DescriptorPhysicalBase;
            if (Right->PhysicalBase != 0) {
                Right->PhysicalBase += (PHYSICAL)(FreeEnd - RegionStart);
            }
            Right->Size = RightBytes;
            Right->PageCount = RightBytes >> PAGE_SIZE_MUL;
            Right->Flags = Descriptor->Flags;
            Right->Attributes = Descriptor->Attributes;
            Right->Granularity = Descriptor->Granularity;
            RefreshDescriptorGranularity(Right);

            if (Right->Size == 0) {
                ReleaseRegionDescriptor(Right);
            } else {
                InsertDescriptorOrdered(Process, Right);
            }

            DEBUG(TEXT("[UpdateDescriptorsForFree] Split region %p -> left=%u right=%u count=%u"),
                (LPVOID)RegionStart,
                LeftBytes,
                RightBytes,
                Process->RegionCount);
        }

        if (RemainingBytes >= SegmentBytes) {
            RemainingBytes -= SegmentBytes;
        } else {
            RemainingBytes = 0;
        }

        Cursor = FreeEnd;
    }
}

/************************************************************************/
/**
 * @brief Initialize the descriptor tracking subsystem.
 */
void InitializeRegionDescriptorTracking(void) {
    if (G_RegionDescriptorsEnabled == TRUE) {
        return;
    }

    if (EnsureDescriptorSlab() == FALSE) {
        ERROR(TEXT("[InitializeRegionDescriptorTracking] Initial slab allocation failed"));
        return;
    }

    G_RegionDescriptorsEnabled = TRUE;

    DEBUG(TEXT("[InitializeRegionDescriptorTracking] Enabled (free=%u total=%u)"),
        G_FreeRegionDescriptorCount,
        G_TotalRegionDescriptorCount);
}

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
/**
 * @brief Refresh the granularity metadata stored in a descriptor.
 * @param Descriptor Descriptor to update.
 */
void RefreshDescriptorGranularity(LPMEMORY_REGION_DESCRIPTOR Descriptor) {
    if (Descriptor == NULL) {
        return;
    }

    Descriptor->Granularity = ComputeDescriptorGranularity(
        Descriptor->CanonicalBase,
        Descriptor->PageCount);
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
 * @brief Compute the number of 4 KiB pages required to reach an alignment.
 * @param Base Canonical base.
 * @param SpanSize Alignment span expressed in bytes.
 * @return Number of pages until alignment (0 when already aligned).
 */
UINT ComputePagesUntilAlignment(LINEAR Base, U64 SpanSize) {
    if (SpanSize == 0u) {
        return 0u;
    }

    U64 Mask = SpanSize - (U64)1;
    U64 Offset = (U64)Base & Mask;

    if (Offset == 0u) {
        return 0u;
    }

    U64 RemainingBytes = SpanSize - Offset;
    return (UINT)(RemainingBytes >> PAGE_SIZE_MUL);
}

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

/**
 * @brief Validate that a physical range remains intact after clipping.
 *
 * The routine checks whether the provided base and length in pages survive
 * the ClipPhysicalRange() constraints without alteration.
 *
 * @param Base Physical base page frame to validate.
 * @param NumPages Number of pages requested in the range.
 * @return TRUE when the range is valid or degenerate, FALSE otherwise.
 */
BOOL ValidatePhysicalTargetRange(PHYSICAL Base, UINT NumPages) {
    if (Base == 0 || NumPages == 0) return TRUE;

    UINT RequestedLength = NumPages << PAGE_SIZE_MUL;

    PHYSICAL ClippedBase = 0;
    UINT ClippedLength = 0;

    if (ClipPhysicalRange((U64)Base, (U64)RequestedLength, &ClippedBase, &ClippedLength) == FALSE) return FALSE;

    return (ClippedBase == Base && ClippedLength == RequestedLength);
}

/************************************************************************/
// Map or remap a single virtual page by directly editing its PTE via the self-map.

void MapOnePage(
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
inline void UnmapOnePage(LINEAR Linear) {
    LPPAGE_TABLE Table = GetPageTableVAFor(Linear);
    UINT tab = GetTableEntry(Linear);
    ClearPageTableEntry(Table, tab);
    InvalidatePage(Linear);
}


/************************************************************************/
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

    G_TempPhysical1 = Physical;

    MapOnePage(
        G_TempLinear1, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

    // Ensure the CPU stops using the previous translation before callers touch the
    // new physical page through the shared temporary slot.
    FlushTLB();

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

    G_TempPhysical2 = Physical;

    MapOnePage(
        G_TempLinear2, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

    // Ensure the CPU stops using the previous translation before callers touch the
    // new physical page through the shared temporary slot.
    FlushTLB();

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

    G_TempPhysical3 = Physical;

    MapOnePage(
        G_TempLinear3, Physical,
        /*RW*/ 1, PAGE_PRIVILEGE_KERNEL, /*WT*/ 0, /*UC*/ 0, /*Global*/ 0, /*Fixed*/ 1);

    // Ensure the CPU stops using the previous translation before callers touch the
    // new physical page through the shared temporary slot.
    FlushTLB();

    return G_TempLinear3;
}

/************************************************************************/

/**
 * @brief Allocate and link a page table for the provided linear address.
 *
 * The helper walks the paging hierarchy, checks that upper levels are present,
 * allocates a new table and installs it in the page directory.
 *
 * @param Base Linear address whose table should be allocated.
 * @return Canonical virtual address of the mapped table, or NULL on failure.
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

/************************************************************************/

/**
 * @brief Retrieve the page table referenced by an iterator when present.
 *
 * The iterator supplies the paging indexes and the function verifies the
 * presence of intermediate levels. Large pages are reported through
 * @p OutLargePage when requested.
 *
 * @param Iterator Pointer describing the directory entry to inspect.
 * @param OutTable Receives the resulting page table pointer when available.
 * @param OutLargePage Optionally receives TRUE when the entry maps a large page.
 * @return TRUE if a table is available, FALSE otherwise.
 */
BOOL TryGetPageTableForIterator(
    const ARCH_PAGE_ITERATOR* Iterator,
    LPPAGE_TABLE* OutTable,
    BOOL* OutLargePage) {
    if (Iterator == NULL || OutTable == NULL) return FALSE;

    if (OutLargePage != NULL) {
        *OutLargePage = FALSE;
    }

    LINEAR Linear = (LINEAR)MemoryPageIteratorGetLinear(Iterator);
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

/************************************************************************/

typedef enum _PAGE_TABLE_POPULATE_MODE {
    PAGE_TABLE_POPULATE_IDENTITY,
    PAGE_TABLE_POPULATE_SINGLE_ENTRY,
    PAGE_TABLE_POPULATE_EMPTY
} PAGE_TABLE_POPULATE_MODE;

#define USERLAND_SEEDED_TABLES 1u

typedef struct _PAGE_TABLE_SETUP {
    UINT DirectoryIndex;
    U32 ReadWrite;
    U32 Privilege;
    U32 Global;
    PAGE_TABLE_POPULATE_MODE Mode;
    PHYSICAL Physical;
    union {
        struct {
            PHYSICAL PhysicalBase;
            BOOL ProtectBios;
        } Identity;
        struct {
            UINT TableIndex;
            PHYSICAL Physical;
            U32 ReadWrite;
            U32 Privilege;
            U32 Global;
        } Single;
    } Data;
} PAGE_TABLE_SETUP;

typedef struct _REGION_SETUP {
    LPCSTR Label;
    UINT PdptIndex;
    U32 ReadWrite;
    U32 Privilege;
    U32 Global;
    PHYSICAL PdptPhysical;
    PHYSICAL DirectoryPhysical;
    PAGE_TABLE_SETUP Tables[64];
    UINT TableCount;
} REGION_SETUP;

/************************************************************************/

typedef struct _LOW_REGION_SHARED_TABLES {
    PHYSICAL BiosTablePhysical;
    PHYSICAL IdentityTablePhysical;
} LOW_REGION_SHARED_TABLES;

LOW_REGION_SHARED_TABLES LowRegionSharedTables = {
    .BiosTablePhysical = NULL,
    .IdentityTablePhysical = NULL,
};

/************************************************************************/

/**
 * @brief Obtain or create a shared identity table used by the low region.
 *
 * The function lazily allocates the table, initializes its entries according
 * to the requested physical base and BIOS protection flag, and records the
 * physical address for future reuse.
 *
 * @param TablePhysical Receives the physical address of the shared table.
 * @param PhysicalBase Physical base used to populate identity mappings.
 * @param ProtectBios TRUE when BIOS ranges must be cleared from the table.
 * @param Label Debug label describing the shared table.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL EnsureSharedLowTable(
    PHYSICAL* TablePhysical,
    PHYSICAL PhysicalBase,
    BOOL ProtectBios,
    LPCSTR Label) {

    if (TablePhysical == NULL || Label == NULL) {
        ERROR(TEXT("[SetupLowRegion] Invalid shared table parameters"));
        return FALSE;
    }

    if (*TablePhysical != NULL) {
        DEBUG(TEXT("[SetupLowRegion] Reusing shared %s table at %p"), Label, *TablePhysical);
        return TRUE;
    }

    PHYSICAL Physical = AllocPhysicalPage();

    if (Physical == NULL) {
        ERROR(TEXT("[SetupLowRegion] Out of physical pages for shared %s table"), Label);
        return FALSE;
    }

    LINEAR Linear = MapTemporaryPhysicalPage3(Physical);

    if (Linear == NULL) {
        ERROR(TEXT("[SetupLowRegion] MapTemporaryPhysicalPage3 failed for shared %s table"), Label);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    LPPAGE_TABLE Table = (LPPAGE_TABLE)Linear;
    MemorySet(Table, 0, PAGE_SIZE);

#if !defined(PROTECT_BIOS)
    UNUSED(ProtectBios);
#endif

    for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
        PHYSICAL EntryPhysical = PhysicalBase + ((PHYSICAL)Index << PAGE_SIZE_MUL);

#ifdef PROTECT_BIOS
        if (ProtectBios) {
            BOOL Protected =
                (EntryPhysical == 0) || (EntryPhysical > PROTECTED_ZONE_START && EntryPhysical <= PROTECTED_ZONE_END);

            if (Protected) {
                ClearPageTableEntry(Table, Index);
                continue;
            }
        }
#endif

        WritePageTableEntryValue(
            Table,
            Index,
            MakePageTableEntryValue(
                EntryPhysical,
                /*ReadWrite*/ 1,
                PAGE_PRIVILEGE_KERNEL,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                /*Global*/ 0,
                /*Fixed*/ 1));
    }

    *TablePhysical = Physical;

    DEBUG(TEXT("[SetupLowRegion] Shared %s table prepared at %p (base %p)"), Label, Physical, PhysicalBase);

    return TRUE;
}

/************************************************************************/
/**
 * @brief Clear a REGION_SETUP structure to its default state.
 * @param Region Structure to reset.
 */
void ResetRegionSetup(REGION_SETUP* Region) {
    MemorySet(Region, 0, sizeof(REGION_SETUP));
}

/************************************************************************/

/**
 * @brief Release the physical resources owned by a REGION_SETUP.
 * @param Region Structure that tracks the allocated tables.
 */
void ReleaseRegionSetup(REGION_SETUP* Region) {
    if (Region->PdptPhysical != NULL) {
        FreePhysicalPage(Region->PdptPhysical);
        Region->PdptPhysical = NULL;
    }

    if (Region->DirectoryPhysical != NULL) {
        FreePhysicalPage(Region->DirectoryPhysical);
        Region->DirectoryPhysical = NULL;
    }

    for (UINT Index = 0; Index < Region->TableCount; Index++) {
        if (Region->Tables[Index].Physical != NULL) {
            FreePhysicalPage(Region->Tables[Index].Physical);
            Region->Tables[Index].Physical = NULL;
        }
    }

    Region->TableCount = 0;
}

/************************************************************************/

/**
 * @brief Allocate a page table and populate it according to the setup entry.
 * @param Region Parent region that will own the table.
 * @param Table Table description containing allocation parameters.
 * @param Directory Page-directory view used to link the table.
 * @return TRUE on success, FALSE when allocation or mapping fails.
 */
BOOL AllocateTableAndPopulate(
    REGION_SETUP* Region,
    PAGE_TABLE_SETUP* Table,
    LPPAGE_DIRECTORY Directory) {

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] begin"), Region->Label, Table->DirectoryIndex);

    Table->Physical = AllocPhysicalPage();

    if (Table->Physical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] %s region out of physical pages"), Region->Label);
        return FALSE;
    }

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] physical %p mode %u"),
        Region->Label,
        Table->DirectoryIndex,
        Table->Physical,
        (UINT)Table->Mode);

    LINEAR TableLinear = MapTemporaryPhysicalPage3(Table->Physical);

    if (TableLinear == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage3 failed for %s table"), Region->Label);
        FreePhysicalPage(Table->Physical);
        Table->Physical = NULL;
        return FALSE;
    }

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] mapped at %p"),
        Region->Label,
        Table->DirectoryIndex,
        TableLinear);

    LPPAGE_TABLE TableVA = (LPPAGE_TABLE)TableLinear;
    MemorySet(TableVA, 0, PAGE_SIZE);

    switch (Table->Mode) {
    case PAGE_TABLE_POPULATE_IDENTITY:
        for (UINT Index = 0; Index < PAGE_TABLE_NUM_ENTRIES; Index++) {
            PHYSICAL Physical = Table->Data.Identity.PhysicalBase + ((PHYSICAL)Index << PAGE_SIZE_MUL);

#ifdef PROTECT_BIOS
            if (Table->Data.Identity.ProtectBios) {
                BOOL Protected =
                    (Physical == 0) || (Physical > PROTECTED_ZONE_START && Physical <= PROTECTED_ZONE_END);

                if (Protected) {
                    ClearPageTableEntry(TableVA, Index);
                    continue;
                }
            }
#endif

            WritePageTableEntryValue(
                TableVA,
                Index,
                MakePageTableEntryValue(
                    Physical,
                    Table->ReadWrite,
                    Table->Privilege,
                    /*WriteThrough*/ 0,
                    /*CacheDisabled*/ 0,
                    Table->Global,
                    /*Fixed*/ 1));
        }
        break;

    case PAGE_TABLE_POPULATE_SINGLE_ENTRY:
        WritePageTableEntryValue(
            TableVA,
            Table->Data.Single.TableIndex,
            MakePageTableEntryValue(
                Table->Data.Single.Physical,
                Table->Data.Single.ReadWrite,
                Table->Data.Single.Privilege,
                /*WriteThrough*/ 0,
                /*CacheDisabled*/ 0,
                Table->Data.Single.Global,
                /*Fixed*/ 1));
        break;

    case PAGE_TABLE_POPULATE_EMPTY:
    default:
        break;
    }

    WritePageDirectoryEntryValue(
        Directory,
        Table->DirectoryIndex,
        MakePageDirectoryEntryValue(
            Table->Physical,
            Table->ReadWrite,
            Table->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Table->Global,
            /*Fixed*/ 1));

    U64 DirectoryEntryValue = ReadPageDirectoryEntryValue(Directory, Table->DirectoryIndex);
    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] entry value=%p"),
        Region->Label,
        Table->DirectoryIndex,
        (LINEAR)DirectoryEntryValue);

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] table ready at %p"),
        Region->Label,
        Table->DirectoryIndex,
        Table->Physical);

    DEBUG(TEXT("[AllocateTableAndPopulate] %s directory[%u] complete"), Region->Label, Table->DirectoryIndex);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Build identity-mapped tables for the low virtual address space.
 * @param Region Region descriptor to populate.
 * @param UserSeedTables Number of empty user tables to pre-allocate.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL SetupLowRegion(REGION_SETUP* Region, UINT UserSeedTables) {
    ResetRegionSetup(Region);

    Region->Label = TEXT("Low");
    Region->PdptIndex = GetPdptEntry(0);
    Region->ReadWrite = 1;
    Region->Privilege = (UserSeedTables != 0u) ? PAGE_PRIVILEGE_USER : PAGE_PRIVILEGE_KERNEL;
    Region->Global = 0;

    DEBUG(TEXT("[SetupLowRegion] Config PdptIndex=%u Privilege=%u UserSeedTables=%u"),
        Region->PdptIndex,
        Region->Privilege,
        UserSeedTables);

    if (EnsureSharedLowTable(&LowRegionSharedTables.BiosTablePhysical, 0, TRUE, TEXT("BIOS")) == FALSE) {
        return FALSE;
    }

    if (EnsureSharedLowTable(
            &LowRegionSharedTables.IdentityTablePhysical,
            ((PHYSICAL)PAGE_TABLE_NUM_ENTRIES << PAGE_SIZE_MUL),
            FALSE,
            TEXT("low identity")) == FALSE) {
        return FALSE;
    }

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();

    DEBUG(TEXT("[SetupLowRegion] PDPT %p, directory %p"), Region->PdptPhysical, Region->DirectoryPhysical);

    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Low region out of physical pages"));
        if (Region->PdptPhysical != NULL) {
            FreePhysicalPage(Region->PdptPhysical);
            Region->PdptPhysical = NULL;
        }
        if (Region->DirectoryPhysical != NULL) {
            FreePhysicalPage(Region->DirectoryPhysical);
            Region->DirectoryPhysical = NULL;
        }
        return FALSE;
    }

    LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(Region->PdptPhysical);

    if (Pdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed for low PDPT"));
        return FALSE;
    }

    DEBUG(TEXT("[SetupLowRegion] PDPT mapped at %p"), Pdpt);

    MemorySet(Pdpt, 0, PAGE_SIZE);

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(Region->DirectoryPhysical);

    if (Directory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed for low directory"));
        return FALSE;
    }

    DEBUG(TEXT("[SetupLowRegion] Directory mapped at %p"), Directory);

    MemorySet(Directory, 0, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        Pdpt,
        Region->PdptIndex,
        MakePageDirectoryEntryValue(
            Region->DirectoryPhysical,
            Region->ReadWrite,
            Region->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Region->Global,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupLowRegion] PDPT[%u] -> %p"), Region->PdptIndex, Region->DirectoryPhysical);

    UINT LowDirectoryIndex = GetDirectoryEntry(0);

    WritePageDirectoryEntryValue(
        Directory,
        LowDirectoryIndex,
        MakePageDirectoryEntryValue(
            LowRegionSharedTables.BiosTablePhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupLowRegion] Directory[%u] -> shared BIOS table %p"),
        LowDirectoryIndex,
        LowRegionSharedTables.BiosTablePhysical);

    WritePageDirectoryEntryValue(
        Directory,
        LowDirectoryIndex + 1u,
        MakePageDirectoryEntryValue(
            LowRegionSharedTables.IdentityTablePhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_KERNEL,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupLowRegion] Directory[%u] -> shared identity table %p"),
        LowDirectoryIndex + 1u,
        LowRegionSharedTables.IdentityTablePhysical);

    if (UserSeedTables != 0u) {
        UINT TableCapacity = (UINT)(sizeof(Region->Tables) / sizeof(Region->Tables[0]));
        DEBUG(TEXT("[SetupLowRegion] User seed request=%u current=%u capacity=%u region=%p tables=%p"),
            UserSeedTables,
            Region->TableCount,
            TableCapacity,
            Region,
            Region->Tables);

        UINT BaseDirectory = GetDirectoryEntry((U64)VMA_USER);

        for (UINT Index = 0; Index < UserSeedTables; Index++) {
            if (Region->TableCount >= TableCapacity) {
                ERROR(TEXT("[SetupLowRegion] User seed table overflow index=%u count=%u capacity=%u"),
                    Index,
                    Region->TableCount,
                    TableCapacity);
                return FALSE;
            }

            PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];
            DEBUG(TEXT("[SetupLowRegion] Seeding idx=%u count=%u table=%p base=%u"),
                Index,
                Region->TableCount,
                Table,
                BaseDirectory);

            Table->DirectoryIndex = BaseDirectory + Index;
            Table->ReadWrite = 1;
            Table->Privilege = PAGE_PRIVILEGE_USER;
            Table->Global = 0;
            Table->Mode = PAGE_TABLE_POPULATE_EMPTY;
            DEBUG(TEXT("[SetupLowRegion] Preparing user seed table slot=%u"), Table->DirectoryIndex);
            if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) return FALSE;
            DEBUG(TEXT("[SetupLowRegion] Seed slot=%u populated physical=%p"),
                Table->DirectoryIndex,
                Table->Physical);
            Region->TableCount++;
            DEBUG(TEXT("[SetupLowRegion] Table count advanced to %u"), Region->TableCount);
        }
    }

    DEBUG(TEXT("[SetupLowRegion] Completed table count %u (shared bios=%p identity=%p)"),
        Region->TableCount,
        LowRegionSharedTables.BiosTablePhysical,
        LowRegionSharedTables.IdentityTablePhysical);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Compute the number of bytes of kernel memory that must be mapped.
 * @return Size in bytes covered by kernel tables.
 */
UINT ComputeKernelCoverageBytes(void) {
    PHYSICAL PhysBaseKernel = KernelStartup.KernelPhysicalBase;
    PHYSICAL CoverageEnd = PhysBaseKernel + (PHYSICAL)KernelStartup.KernelSize;

    if (KernelStartup.StackTop > CoverageEnd) {
        CoverageEnd = KernelStartup.StackTop;
    }

    if (CoverageEnd <= PhysBaseKernel) {
        return PAGE_TABLE_CAPACITY;
    }

    PHYSICAL Coverage = CoverageEnd - PhysBaseKernel;
    UINT CoverageBytes = (UINT)PAGE_ALIGN((UINT)Coverage);

    if (CoverageBytes < PAGE_TABLE_CAPACITY) {
        CoverageBytes = PAGE_TABLE_CAPACITY;
    }

    return CoverageBytes;
}

/************************************************************************/

/**
 * @brief Create identity mappings for the kernel virtual address space.
 * @param Region Region descriptor to populate.
 * @param TableCountRequired Number of tables that must be allocated.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL SetupKernelRegion(REGION_SETUP* Region, UINT TableCountRequired) {
    ResetRegionSetup(Region);

    Region->Label = TEXT("Kernel");
    Region->PdptIndex = GetPdptEntry((U64)VMA_KERNEL);
    Region->ReadWrite = 1;
    Region->Privilege = PAGE_PRIVILEGE_KERNEL;
    Region->Global = 0;

    if (TableCountRequired > ARRAY_COUNT(Region->Tables)) {
        ERROR(TEXT("[AllocPageDirectory] Kernel region requires too many tables"));
        return FALSE;
    }

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();

    DEBUG(TEXT("[SetupKernelRegion] PDPT %p, directory %p"), Region->PdptPhysical, Region->DirectoryPhysical);

    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Kernel region out of physical pages"));
        return FALSE;
    }

    LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(Region->PdptPhysical);

    if (Pdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed for kernel PDPT"));
        return FALSE;
    }

    MemorySet(Pdpt, 0, PAGE_SIZE);

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(Region->DirectoryPhysical);

    if (Directory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed for kernel directory"));
        return FALSE;
    }

    MemorySet(Directory, 0, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        Pdpt,
        Region->PdptIndex,
        MakePageDirectoryEntryValue(
            Region->DirectoryPhysical,
            Region->ReadWrite,
            Region->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Region->Global,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupKernelRegion] PDPT[%u] -> %p"), Region->PdptIndex, Region->DirectoryPhysical);

    UINT DirectoryIndex = GetDirectoryEntry((U64)VMA_KERNEL);
    PHYSICAL PhysBaseKernel = KernelStartup.KernelPhysicalBase;

    for (UINT TableIndex = 0; TableIndex < TableCountRequired; TableIndex++) {
        PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];
        Table->DirectoryIndex = DirectoryIndex + TableIndex;
        Table->ReadWrite = 1;
        Table->Privilege = PAGE_PRIVILEGE_KERNEL;
        Table->Global = 0;
        Table->Mode = PAGE_TABLE_POPULATE_IDENTITY;
        Table->Data.Identity.PhysicalBase = PhysBaseKernel + ((PHYSICAL)TableIndex << PAGE_TABLE_CAPACITY_MUL);
        Table->Data.Identity.ProtectBios = FALSE;

        if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) {
            return FALSE;
        }
        Region->TableCount++;
    }

    DEBUG(TEXT("[SetupKernelRegion] Completed table count %u"), Region->TableCount);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Map the user-mode task runner trampoline into the new address space.
 * @param Region Region descriptor to populate.
 * @param TaskRunnerPhysical Physical address of the task runner code.
 * @param TaskRunnerTableIndex Page table index that contains the trampoline.
 * @return TRUE on success, FALSE otherwise.
 */
BOOL SetupTaskRunnerRegion(
    REGION_SETUP* Region,
    PHYSICAL TaskRunnerPhysical,
    UINT TaskRunnerTableIndex) {
    ResetRegionSetup(Region);

    Region->Label = TEXT("TaskRunner");
    Region->PdptIndex = GetPdptEntry((U64)VMA_TASK_RUNNER);
    Region->ReadWrite = 1;
    Region->Privilege = PAGE_PRIVILEGE_USER;
    Region->Global = 0;

    Region->PdptPhysical = AllocPhysicalPage();
    Region->DirectoryPhysical = AllocPhysicalPage();

    DEBUG(TEXT("[SetupTaskRunnerRegion] PDPT %p, directory %p"), Region->PdptPhysical, Region->DirectoryPhysical);

    if (Region->PdptPhysical == NULL || Region->DirectoryPhysical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] TaskRunner region out of physical pages"));
        return FALSE;
    }

    LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(Region->PdptPhysical);

    if (Pdpt == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed for TaskRunner PDPT"));
        return FALSE;
    }

    MemorySet(Pdpt, 0, PAGE_SIZE);

    LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(Region->DirectoryPhysical);

    if (Directory == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage2 failed for TaskRunner directory"));
        return FALSE;
    }

    MemorySet(Directory, 0, PAGE_SIZE);

    WritePageDirectoryEntryValue(
        Pdpt,
        Region->PdptIndex,
        MakePageDirectoryEntryValue(
            Region->DirectoryPhysical,
            Region->ReadWrite,
            Region->Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            Region->Global,
            /*Fixed*/ 1));
    DEBUG(TEXT("[SetupTaskRunnerRegion] PDPT[%u] -> %p"), Region->PdptIndex, Region->DirectoryPhysical);

    PAGE_TABLE_SETUP* Table = &Region->Tables[Region->TableCount];
    Table->DirectoryIndex = GetDirectoryEntry((U64)VMA_TASK_RUNNER);
    Table->ReadWrite = 1;
    Table->Privilege = PAGE_PRIVILEGE_USER;
    Table->Global = 0;
    Table->Mode = PAGE_TABLE_POPULATE_SINGLE_ENTRY;
    Table->Data.Single.TableIndex = TaskRunnerTableIndex;
    Table->Data.Single.Physical = TaskRunnerPhysical;
    Table->Data.Single.ReadWrite = 0;
    Table->Data.Single.Privilege = PAGE_PRIVILEGE_USER;
    Table->Data.Single.Global = 0;

    if (AllocateTableAndPopulate(Region, Table, Directory) == FALSE) {
        return FALSE;
    }

    Region->TableCount++;
    DEBUG(TEXT("[SetupTaskRunnerRegion] Completed table count %u"), Region->TableCount);
    return TRUE;
}

/************************************************************************/

/*
U64 ReadTableEntrySnapshot(PHYSICAL TablePhysical, UINT Index) {
    if (TablePhysical == NULL) {
        return 0;
    }

    LINEAR Linear = MapTemporaryPhysicalPage3(TablePhysical);

    if (Linear == NULL) {
        return 0;
    }

    return ReadPageTableEntryValue((LPPAGE_TABLE)Linear, Index);
}
*/

/************************************************************************/

/**
 * @brief Build the kernel-mode long mode paging hierarchy.
 *
 * Low, kernel and task runner regions are prepared, connected to a newly
 * allocated PML4 and the recursive slot is configured before returning the
 * physical address.
 *
 * @return Physical address of the allocated PML4, or NULL on failure.
 */
PHYSICAL AllocPageDirectory(void) {
    REGION_SETUP LowRegion;
    REGION_SETUP KernelRegion;
    REGION_SETUP TaskRunnerRegion;
    PHYSICAL Pml4Physical = NULL;
    BOOL Success = FALSE;

    DEBUG(TEXT("[AllocPageDirectory] Enter"));

    if (EnsureCurrentStackSpace(N_32KB) == FALSE) {
        ERROR(TEXT("[AllocPageDirectory] Unable to ensure stack availability"));
        return NULL;
    }

    ResetRegionSetup(&LowRegion);
    ResetRegionSetup(&KernelRegion);
    ResetRegionSetup(&TaskRunnerRegion);

    UINT LowPml4Index = GetPml4Entry(0);
    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);
    UINT TaskRunnerPml4Index = GetPml4Entry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerTableIndex = GetTableEntry((U64)VMA_TASK_RUNNER);

    UINT KernelCoverageBytes = ComputeKernelCoverageBytes();
    UINT KernelTableCount = KernelCoverageBytes >> PAGE_TABLE_CAPACITY_MUL;
    if (KernelTableCount == 0u) KernelTableCount = 1u;

    if (SetupLowRegion(&LowRegion, 0u) == FALSE) goto Out;
    DEBUG(TEXT("[AllocPageDirectory] Low region tables=%u"), LowRegion.TableCount);

    if (SetupKernelRegion(&KernelRegion, KernelTableCount) == FALSE) goto Out;
    DEBUG(TEXT("[AllocPageDirectory] Kernel region tables=%u"), KernelRegion.TableCount);

    LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
    PHYSICAL TaskRunnerPhysical = KernelToPhysical(TaskRunnerLinear);

    DEBUG(TEXT("[AllocPageDirectory] TaskRunnerPhysical = %p + (%p - %p) = %p"),
        KernelStartup.KernelPhysicalBase,
        TaskRunnerLinear,
        VMA_KERNEL,
        TaskRunnerPhysical);

    if (SetupTaskRunnerRegion(&TaskRunnerRegion, TaskRunnerPhysical, TaskRunnerTableIndex) == FALSE) goto Out;
    DEBUG(TEXT("[AllocPageDirectory] TaskRunner tables=%u"), TaskRunnerRegion.TableCount);

    Pml4Physical = AllocPhysicalPage();

    if (Pml4Physical == NULL) {
        ERROR(TEXT("[AllocPageDirectory] Out of physical pages"));
        goto Out;
    }

    LINEAR Pml4Linear = MapTemporaryPhysicalPage1(Pml4Physical);

    if (Pml4Linear == NULL) {
        ERROR(TEXT("[AllocPageDirectory] MapTemporaryPhysicalPage1 failed on PML4"));
        goto Out;
    }

    LPPAGE_DIRECTORY Pml4 = (LPPAGE_DIRECTORY)Pml4Linear;
    MemorySet(Pml4, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocPageDirectory] PML4 mapped at %p"), Pml4);

    WritePageDirectoryEntryValue(
        Pml4,
        LowPml4Index,
        MakePageDirectoryEntryValue(
            LowRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            LowRegion.Privilege,
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

    U64 LowEntry = ReadPageDirectoryEntryValue(Pml4, LowPml4Index);
    U64 KernelEntry = ReadPageDirectoryEntryValue(Pml4, KernelPml4Index);
    U64 TaskEntry = ReadPageDirectoryEntryValue(Pml4, TaskRunnerPml4Index);
    U64 RecursiveEntry = ReadPageDirectoryEntryValue(Pml4, PML4_RECURSIVE_SLOT);

    DEBUG(TEXT("[AllocPageDirectory] PML4 entries set (low=%p, kernel=%p, task=%p, recursive=%p)"),
        (LINEAR)LowEntry,
        (LINEAR)KernelEntry,
        (LINEAR)TaskEntry,
        (LINEAR)RecursiveEntry);

    FlushTLB();

    Success = TRUE;

Out:
    if (!Success) {
        if (Pml4Physical != NULL) {
            FreePhysicalPage(Pml4Physical);
        }
        ReleaseRegionSetup(&LowRegion);
        ReleaseRegionSetup(&KernelRegion);
        ReleaseRegionSetup(&TaskRunnerRegion);
        return NULL;
    }

    DEBUG(TEXT("[AllocPageDirectory] Exit"));
    return Pml4Physical;
}

/************************************************************************/

/**
 * @brief Create a user-mode page directory derived from the current context.
 *
 * Kernel mappings are cloned from the active CR3 while the low region is
 * seeded with identity tables and the task runner trampoline is prepared as
 * needed.
 *
 * @return Physical address of the allocated PML4, or NULL on failure.
 */
PHYSICAL AllocUserPageDirectory(void) {
    REGION_SETUP LowRegion;
    REGION_SETUP KernelRegion;
    REGION_SETUP TaskRunnerRegion;
    PHYSICAL Pml4Physical = NULL;
    BOOL Success = FALSE;
    BOOL TaskRunnerReused = FALSE;

    DEBUG(TEXT("[AllocUserPageDirectory] Enter"));

    if (EnsureCurrentStackSpace(N_32KB) == FALSE) {
        ERROR(TEXT("[AllocUserPageDirectory] Unable to ensure stack availability"));
        return NULL;
    }

    ResetRegionSetup(&LowRegion);
    ResetRegionSetup(&KernelRegion);
    ResetRegionSetup(&TaskRunnerRegion);

    UINT LowPml4Index = GetPml4Entry(0);
    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);
    UINT TaskRunnerPml4Index = GetPml4Entry((U64)VMA_TASK_RUNNER);
    UINT TaskRunnerTableIndex = GetTableEntry((U64)VMA_TASK_RUNNER);

    if (SetupLowRegion(&LowRegion, USERLAND_SEEDED_TABLES) == FALSE) goto Out;
    DEBUG(TEXT("[AllocUserPageDirectory] Low region tables=%u"), LowRegion.TableCount);

    Pml4Physical = AllocPhysicalPage();

    if (Pml4Physical == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] Out of physical pages"));
        goto Out;
    }

    LINEAR Pml4Linear = MapTemporaryPhysicalPage1(Pml4Physical);

    if (Pml4Linear == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] MapTemporaryPhysicalPage1 failed on PML4"));
        goto Out;
    }

    LPPAGE_DIRECTORY Pml4 = (LPPAGE_DIRECTORY)Pml4Linear;
    MemorySet(Pml4, 0, PAGE_SIZE);
    DEBUG(TEXT("[AllocUserPageDirectory] PML4 mapped at %p"), Pml4);

    LPPML4 CurrentPml4 = GetCurrentPml4VA();
    if (CurrentPml4 == NULL) {
        ERROR(TEXT("[AllocUserPageDirectory] Current PML4 pointer is NULL"));
        goto Out;
    }

    WritePageDirectoryEntryValue(
        Pml4,
        LowPml4Index,
        MakePageDirectoryEntryValue(
            LowRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            LowRegion.Privilege,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1));

    UINT KernelBaseIndex = PML4_ENTRY_COUNT / 2u;
    UINT ClonedKernelEntries = 0u;
    for (UINT Index = KernelBaseIndex; Index < PML4_ENTRY_COUNT; Index++) {
        if (Index == PML4_RECURSIVE_SLOT) continue;

        U64 EntryValue = ReadPageDirectoryEntryValue(CurrentPml4, Index);
        if ((EntryValue & PAGE_FLAG_PRESENT) == 0) continue;

        WritePageDirectoryEntryValue(Pml4, Index, EntryValue);
        ClonedKernelEntries++;
    }

    if (ClonedKernelEntries == 0u) {
        ERROR(TEXT("[AllocUserPageDirectory] No kernel PML4 entries copied from current directory"));
        goto Out;
    }

    DEBUG(TEXT("[AllocUserPageDirectory] Cloned %u kernel PML4 entries from index %u"),
        ClonedKernelEntries,
        KernelBaseIndex);

    U64 TaskRunnerEntryValue = ReadPageDirectoryEntryValue(CurrentPml4, TaskRunnerPml4Index);
    if ((TaskRunnerEntryValue & PAGE_FLAG_PRESENT) != 0 && (TaskRunnerEntryValue & PAGE_FLAG_USER) != 0) {
        TaskRunnerReused = TRUE;
        DEBUG(TEXT("[AllocUserPageDirectory] Reusing existing task runner entry %p from current CR3"),
            (LINEAR)TaskRunnerEntryValue);
    } else {
        LINEAR TaskRunnerLinear = (LINEAR)&__task_runner_start;
        PHYSICAL TaskRunnerPhysical = KernelToPhysical(TaskRunnerLinear);

        DEBUG(TEXT("[AllocUserPageDirectory] TaskRunnerPhysical = %p + (%p - %p) = %p"),
            KernelStartup.KernelPhysicalBase,
            TaskRunnerLinear,
            VMA_KERNEL,
            TaskRunnerPhysical);

        if (SetupTaskRunnerRegion(&TaskRunnerRegion, TaskRunnerPhysical, TaskRunnerTableIndex) == FALSE) goto Out;
        DEBUG(TEXT("[AllocUserPageDirectory] TaskRunner tables=%u"), TaskRunnerRegion.TableCount);
        DEBUG(TEXT("[AllocUserPageDirectory] Regions low(pdpt=%p dir=%p priv=%u tables=%u) kernel(reuse existing) task(pdpt=%p dir=%p)"),
            LowRegion.PdptPhysical,
            LowRegion.DirectoryPhysical,
            LowRegion.Privilege,
            LowRegion.TableCount,
            TaskRunnerRegion.PdptPhysical,
            TaskRunnerRegion.DirectoryPhysical);

        TaskRunnerEntryValue = MakePageDirectoryEntryValue(
            TaskRunnerRegion.PdptPhysical,
            /*ReadWrite*/ 1,
            PAGE_PRIVILEGE_USER,
            /*WriteThrough*/ 0,
            /*CacheDisabled*/ 0,
            /*Global*/ 0,
            /*Fixed*/ 1);
    }

    WritePageDirectoryEntryValue(Pml4, TaskRunnerPml4Index, TaskRunnerEntryValue);

    if (!TaskRunnerReused) {
        LINEAR TaskRunnerDirectoryLinear = MapTemporaryPhysicalPage2(TaskRunnerRegion.DirectoryPhysical);
        LINEAR TaskRunnerTableLinear = MapTemporaryPhysicalPage3(TaskRunnerRegion.Tables[0].Physical);

        if (TaskRunnerDirectoryLinear != NULL && TaskRunnerTableLinear != NULL) {
            UINT TaskRunnerDirectoryIndex = GetDirectoryEntry((U64)VMA_TASK_RUNNER);
            U64 TaskDirectoryEntry =
                ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)TaskRunnerDirectoryLinear, TaskRunnerDirectoryIndex);
            U64 TaskTableEntry = ReadPageTableEntryValue((LPPAGE_TABLE)TaskRunnerTableLinear, TaskRunnerTableIndex);

            DEBUG(TEXT("[AllocUserPageDirectory] TaskRunner PDE[%u]=%p PTE[%u]=%p"),
                TaskRunnerDirectoryIndex,
                (LINEAR)TaskDirectoryEntry,
                TaskRunnerTableIndex,
                (LINEAR)TaskTableEntry);
        } else {
            ERROR(TEXT("[AllocUserPageDirectory] Unable to map TaskRunner directory/table snapshot"));
        }
    } else {
        DEBUG(TEXT("[AllocUserPageDirectory] Task runner entry reused without rebuilding tables"));
    }

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

    U64 LowEntry = ReadPageDirectoryEntryValue(Pml4, LowPml4Index);
    U64 KernelEntry = ReadPageDirectoryEntryValue(Pml4, KernelPml4Index);
    U64 TaskEntry = ReadPageDirectoryEntryValue(Pml4, TaskRunnerPml4Index);
    U64 RecursiveEntry = ReadPageDirectoryEntryValue(Pml4, PML4_RECURSIVE_SLOT);

    DEBUG(TEXT("[AllocUserPageDirectory] PML4 entries set (low=%p, kernel=%p, task=%p, recursive=%p)"),
        (LINEAR)LowEntry,
        (LINEAR)KernelEntry,
        (LINEAR)TaskEntry,
        (LINEAR)RecursiveEntry);

    LogPageDirectory64(Pml4Physical);

    FlushTLB();

    Success = TRUE;

Out:
    if (!Success) {
        if (Pml4Physical != NULL) {
            FreePhysicalPage(Pml4Physical);
        }
        ReleaseRegionSetup(&LowRegion);
        ReleaseRegionSetup(&KernelRegion);
        ReleaseRegionSetup(&TaskRunnerRegion);
        return NULL;
    }

    DEBUG(TEXT("[AllocUserPageDirectory] Exit"));
    return Pml4Physical;
}

/************************************************************************/

/**
 * @brief Initialize the x86-64 memory manager and install the kernel mappings.
 *
 * The routine prepares the physical page bitmap, constructs a new kernel page
 * directory, loads it and finalizes the descriptor tables required for long
 * mode execution.
 */
void InitializeMemoryManager(void) {
    DEBUG(TEXT("[InitializeMemoryManager] Enter"));

    DEBUG(TEXT("[InitializeMemoryManager] Temp pages reserved: %p, %p, %p"),
        (LPVOID)(LINEAR)X86_64_TEMP_LINEAR_PAGE_1,
        (LPVOID)(LINEAR)X86_64_TEMP_LINEAR_PAGE_2,
        (LPVOID)(LINEAR)X86_64_TEMP_LINEAR_PAGE_3);

    UpdateKernelMemoryMetricsFromMultibootMap();

    if (KernelStartup.PageCount == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    UINT BitmapBytes = (KernelStartup.PageCount + 7) >> MUL_8;
    UINT BitmapBytesAligned = (UINT)PAGE_ALIGN(BitmapBytes);

    U64 KernelSpan = (U64)KernelStartup.KernelSize + (U64)N_512KB;
    PHYSICAL MapSize = (PHYSICAL)PAGE_ALIGN(KernelSpan);
    U64 TotalPages = (MapSize + PAGE_SIZE - 1) >> PAGE_SIZE_MUL;
    U64 TablesRequired = (TotalPages + (U64)PAGE_TABLE_NUM_ENTRIES - 1) / (U64)PAGE_TABLE_NUM_ENTRIES;
    PHYSICAL TablesSize = (PHYSICAL)(TablesRequired * (U64)PAGE_TABLE_SIZE);
    PHYSICAL LoaderReservedEnd = KernelStartup.KernelPhysicalBase + MapSize + TablesSize;
    PHYSICAL PpbPhysical = PAGE_ALIGN(LoaderReservedEnd);

    Kernel.PPB = (LPPAGEBITMAP)(UINT)PpbPhysical;
    Kernel.PPBSize = BitmapBytesAligned;

    DEBUG(TEXT("[InitializeMemoryManager] Kernel.PPB physical base: %p"), (LINEAR)Kernel.PPB);
    DEBUG(TEXT("[InitializeMemoryManager] Kernel.PPB size: %x"), Kernel.PPBSize);

    MemorySet(Kernel.PPB, 0, Kernel.PPBSize);

    MarkUsedPhysicalMemory();

    if (KernelStartup.MemorySize == 0) {
        ConsolePanic(TEXT("Detected memory = 0"));
    }

    PHYSICAL NewPageDirectory = AllocPageDirectory();

    DEBUG(TEXT("[InitializeMemoryManager] New page directory: %p"), (LINEAR)NewPageDirectory);

    if (NewPageDirectory == NULL) {
        ERROR(TEXT("[InitializeMemoryManager] AllocPageDirectory failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    LoadPageDirectory(NewPageDirectory);

    FlushTLB();

    LogPageDirectory64(NewPageDirectory);

    DEBUG(TEXT("[InitializeMemoryManager] TLB flushed"));

    Kernel_i386.GDT = (LPVOID)AllocKernelRegion(0, GDT_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);

    if (Kernel_i386.GDT == NULL) {
        ERROR(TEXT("[InitializeMemoryManager] AllocRegion for GDT failed"));
        ConsolePanic(TEXT("Could not allocate critical memory management tool"));
        DO_THE_SLEEPING_BEAUTY;
    }

    InitializeGlobalDescriptorTable((LPSEGMENT_DESCRIPTOR)Kernel_i386.GDT);

    LogGlobalDescriptorTable((LPSEGMENT_DESCRIPTOR)Kernel_i386.GDT, 10);

    DEBUG(TEXT("[InitializeMemoryManager] Loading GDT"));

    LoadGlobalDescriptorTable((PHYSICAL)Kernel_i386.GDT, GDT_SIZE - 1);

    InitializeRegionDescriptorTracking();

    DEBUG(TEXT("[InitializeMemoryManager] Exit"));
}

/************************************************************************/

/**
 * @brief Resolve a canonical linear address to its physical counterpart.
 *
 * The lookup walks the paging hierarchy, accounting for large pages, and
 * returns the physical address when the mapping exists.
 *
 * @param Address Linear address to translate.
 * @return Physical address of the resolved page, or 0 when unmapped.
 */
PHYSICAL MapLinearToPhysical(LINEAR Address) {
    Address = CanonicalizeLinearAddress(Address);

    ARCH_PAGE_ITERATOR Iterator = MemoryPageIteratorFromLinear(Address);
    UINT Pml4Index = MemoryPageIteratorGetPml4Index(&Iterator);
    UINT PdptIndex = MemoryPageIteratorGetPdptIndex(&Iterator);
    UINT DirIndex = MemoryPageIteratorGetDirectoryIndex(&Iterator);
    UINT TabIndex = MemoryPageIteratorGetTableIndex(&Iterator);

    LPPML4 Pml4 = GetCurrentPml4VA();
    U64 Pml4EntryValue = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)Pml4, Pml4Index);
    if ((Pml4EntryValue & PAGE_FLAG_PRESENT) == 0) return 0;

    PHYSICAL PdptPhysical = (PHYSICAL)(Pml4EntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY PdptLinear = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(PdptPhysical);
    U64 PdptEntryValue = ReadPageDirectoryEntryValue(PdptLinear, PdptIndex);
    if ((PdptEntryValue & PAGE_FLAG_PRESENT) == 0) return 0;

    if ((PdptEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
        PHYSICAL LargeBase = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
        return (PHYSICAL)(LargeBase | (Address & (N_1GB - 1)));
    }

    PHYSICAL DirectoryPhysical = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
    LPPAGE_DIRECTORY DirectoryLinear = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(DirectoryPhysical);
    U64 DirectoryEntryValue = ReadPageDirectoryEntryValue(DirectoryLinear, DirIndex);
    if ((DirectoryEntryValue & PAGE_FLAG_PRESENT) == 0) return 0;

    if ((DirectoryEntryValue & PAGE_FLAG_PAGE_SIZE) != 0) {
        PHYSICAL LargeBase = (PHYSICAL)(DirectoryEntryValue & PAGE_MASK);
        return (PHYSICAL)(LargeBase | (Address & (N_2MB - 1)));
    }

    LPPAGE_TABLE Table = MemoryPageIteratorGetTable(&Iterator);
    if (!PageTableEntryIsPresent(Table, TabIndex)) return 0;

    PHYSICAL PagePhysical = PageTableEntryGetPhysical(Table, TabIndex);
    if (PagePhysical == 0) return 0;

    return (PHYSICAL)(PagePhysical | (Address & (PAGE_SIZE - 1)));
}

/************************************************************************/

/**
 * @brief Check if a linear address is mapped and accessible.
 * @param Address Linear address to test.
 * @return TRUE if the address resolves to a present page table entry.
 */
BOOL IsValidMemory(LINEAR Address) {
    LINEAR Canonical = CanonicalizeLinearAddress(Address);

    if (Canonical != Address) {
        return FALSE;
    }

    return MapLinearToPhysical(Canonical) != 0;
}

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
LINEAR FindFreeRegion(LINEAR StartBase, UINT Size) {
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
void FreeEmptyPageTables(void) {
    LPPML4 Pml4 = GetCurrentPml4VA();
    UINT KernelPml4Index = GetPml4Entry((U64)VMA_KERNEL);

    for (UINT Pml4Index = 0u; Pml4Index < KernelPml4Index; Pml4Index++) {
        if (Pml4Index == PML4_RECURSIVE_SLOT) {
            continue;
        }

        U64 Pml4EntryValue = ReadPageDirectoryEntryValue((LPPAGE_DIRECTORY)Pml4, Pml4Index);
        if ((Pml4EntryValue & PAGE_FLAG_PRESENT) == 0u) {
            continue;
        }
        if ((Pml4EntryValue & PAGE_FLAG_PAGE_SIZE) != 0u) {
            continue;
        }

        PHYSICAL PdptPhysical = (PHYSICAL)(Pml4EntryValue & PAGE_MASK);
        LPPAGE_DIRECTORY Pdpt = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage1(PdptPhysical);

        for (UINT PdptIndex = 0u; PdptIndex < PAGE_TABLE_NUM_ENTRIES; PdptIndex++) {
            U64 PdptEntryValue = ReadPageDirectoryEntryValue(Pdpt, PdptIndex);
            if ((PdptEntryValue & PAGE_FLAG_PRESENT) == 0u) {
                continue;
            }
            if ((PdptEntryValue & PAGE_FLAG_PAGE_SIZE) != 0u) {
                continue;
            }

            PHYSICAL DirectoryPhysical = (PHYSICAL)(PdptEntryValue & PAGE_MASK);
            LPPAGE_DIRECTORY Directory = (LPPAGE_DIRECTORY)MapTemporaryPhysicalPage2(DirectoryPhysical);

            for (UINT DirIndex = 0u; DirIndex < PAGE_TABLE_NUM_ENTRIES; DirIndex++) {
                U64 DirectoryEntryValue = ReadPageDirectoryEntryValue(Directory, DirIndex);
                if ((DirectoryEntryValue & PAGE_FLAG_PRESENT) == 0u) {
                    continue;
                }
                if ((DirectoryEntryValue & PAGE_FLAG_PAGE_SIZE) != 0u) {
                    continue;
                }

                PHYSICAL TablePhysical = (PHYSICAL)(DirectoryEntryValue & PAGE_MASK);
                if (TablePhysical == 0u) {
                    continue;
                }

                LPPAGE_TABLE Table = (LPPAGE_TABLE)MapTemporaryPhysicalPage3(TablePhysical);
                if (Table == NULL) {
                    ERROR(TEXT("[FreeEmptyPageTables] Failed to map table PML4=%u PDPT=%u Dir=%u phys=%p"),
                        Pml4Index, PdptIndex, DirIndex, (LPVOID)TablePhysical);
                    continue;
                }

                if (PageTableIsEmpty(Table)) {
                    DEBUG(TEXT("[FreeEmptyPageTables] Clearing PML4=%u PDPT=%u Dir=%u tablePhys=%p"),
                        Pml4Index, PdptIndex, DirIndex, (LPVOID)TablePhysical);
                    SetPhysicalPageMark((UINT)(TablePhysical >> PAGE_SIZE_MUL), 0u);
                    ClearPageDirectoryEntry(Directory, DirIndex);
                }
            }
        }
    }
}

/************************************************************************/

BOOL PopulateRegionPagesLegacy(LINEAR Base,
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
                BOOL PreviousBootstrap = G_RegionDescriptorBootstrap;
                G_RegionDescriptorBootstrap = TRUE;
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                G_RegionDescriptorBootstrap = PreviousBootstrap;
                return FALSE;
            }

            if (AllocPageTable(CurrentLinear) == NULL) {
                BOOL PreviousBootstrap = G_RegionDescriptorBootstrap;
                G_RegionDescriptorBootstrap = TRUE;
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                G_RegionDescriptorBootstrap = PreviousBootstrap;
                return FALSE;
            }

            if (!TryGetPageTableForIterator(&Iterator, &Table, NULL)) {
                BOOL PreviousBootstrap = G_RegionDescriptorBootstrap;
                G_RegionDescriptorBootstrap = TRUE;
                FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                G_RegionDescriptorBootstrap = PreviousBootstrap;
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
                    BOOL PreviousBootstrap = G_RegionDescriptorBootstrap;
                    G_RegionDescriptorBootstrap = TRUE;
                    FreeRegion(RollbackBase, (UINT)(Index << PAGE_SIZE_MUL));
                    G_RegionDescriptorBootstrap = PreviousBootstrap;
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

#if EXOS_X86_64_FAST_VMM
    BOOL FastPathUsed = FALSE;
    if (G_RegionDescriptorsEnabled && G_RegionDescriptorBootstrap == FALSE) {
        MEMORY_REGION_DESCRIPTOR TempDescriptor;
        InitializeTransientDescriptor(&TempDescriptor, Pointer, NumPages, Target, Flags);

        UINT PagesProcessed = 0u;
        if (FastPopulateRegionFromDescriptor(&TempDescriptor,
                                             Target,
                                             Flags,
                                             TEXT("AllocRegion"),
                                             &PagesProcessed) == TRUE &&
            PagesProcessed == NumPages) {
            FastPathUsed = TRUE;
        } else {
            if (PagesProcessed != 0u) {
                MEMORY_REGION_DESCRIPTOR RollbackDescriptor;
                InitializeTransientDescriptor(&RollbackDescriptor, Pointer, PagesProcessed, Target, Flags);
                if (FastReleaseRegionFromDescriptor(&RollbackDescriptor, NULL) == FALSE) {
                    WARNING(TEXT("[AllocRegion] Fast rollback failed for base=%p pages=%u"),
                        (LPVOID)Pointer,
                        PagesProcessed);
                }
            }

            DEBUG(TEXT("[AllocRegion] Falling back to legacy population (processed=%u targetPages=%u)"),
                PagesProcessed,
                NumPages);
        }
    }
#else
    BOOL FastPathUsed = FALSE;
#endif

    if (FastPathUsed == FALSE) {
        if (PopulateRegionPagesLegacy(Base, Target, NumPages, Flags, Pointer, TEXT("AllocRegion")) == FALSE) {
            return NULL;
        }
    }

    if (G_RegionDescriptorsEnabled && G_RegionDescriptorBootstrap == FALSE) {
        if (RegisterRegionDescriptor(Pointer, NumPages, Target, Flags) == FALSE) {
            G_RegionDescriptorBootstrap = TRUE;
            FreeRegion(Pointer, NumPages << PAGE_SIZE_MUL);
            G_RegionDescriptorBootstrap = FALSE;
            return NULL;
        }
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

    Base = CanonicalizeLinearAddress(Base);

    if (NewSize > KernelStartup.MemorySize / 4) {
        ERROR(TEXT("[ResizeRegion] New size %x exceeds 25%% of memory (%lX)"),
              NewSize,
              KernelStartup.MemorySize / 4);
        return FALSE;
    }

    LPMEMORY_REGION_DESCRIPTOR Descriptor = NULL;
    if (G_RegionDescriptorsEnabled && G_RegionDescriptorBootstrap == FALSE) {
        Descriptor = FindDescriptorForBase(ResolveCurrentAddressSpaceOwner(), Base);
        if (Descriptor == NULL) {
            WARNING(TEXT("[ResizeRegion] Missing descriptor for base=%p"),
                (LPVOID)Base);
        }
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

#if EXOS_X86_64_FAST_VMM
        BOOL ExpansionFastPathUsed = FALSE;
        if (Descriptor != NULL && G_RegionDescriptorBootstrap == FALSE) {
            MEMORY_REGION_DESCRIPTOR TempDescriptor;
            InitializeTransientDescriptor(&TempDescriptor, NewBase, AdditionalPages, AdditionalTarget, Flags);

            UINT PagesProcessed = 0u;
            if (FastPopulateRegionFromDescriptor(&TempDescriptor,
                                                 AdditionalTarget,
                                                 Flags,
                                                 TEXT("ResizeRegion"),
                                                 &PagesProcessed) == TRUE &&
                PagesProcessed == AdditionalPages) {
                ExpansionFastPathUsed = TRUE;
            } else {
                if (PagesProcessed != 0u) {
                    MEMORY_REGION_DESCRIPTOR RollbackDescriptor;
                    InitializeTransientDescriptor(&RollbackDescriptor,
                                                  NewBase,
                                                  PagesProcessed,
                                                  AdditionalTarget,
                                                  Flags);
                    if (FastReleaseRegionFromDescriptor(&RollbackDescriptor, NULL) == FALSE) {
                        WARNING(TEXT("[ResizeRegion] Fast rollback failed for base=%p pages=%u"),
                            (LPVOID)NewBase,
                            PagesProcessed);
                    }
                }

                DEBUG(TEXT("[ResizeRegion] Falling back to legacy population (processed=%u targetPages=%u)"),
                    PagesProcessed,
                    AdditionalPages);
            }
        }
#else
        BOOL ExpansionFastPathUsed = FALSE;
#endif

        if (ExpansionFastPathUsed == FALSE) {
            if (PopulateRegionPagesLegacy(NewBase,
                                          AdditionalTarget,
                                          AdditionalPages,
                                          Flags,
                                          NewBase,
                                          TEXT("ResizeRegion")) == FALSE) {
                return FALSE;
            }
        }

        if (Descriptor != NULL) {
            ExtendDescriptor(Descriptor, AdditionalPages);
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
#if EXOS_X86_64_FAST_VMM
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

    for (UINT Index = 0; Index < NumPages; Index++) {
        UINT TabEntry = MemoryPageIteratorGetTableIndex(&Iterator);
        UINT DirEntry = MemoryPageIteratorGetDirectoryIndex(&Iterator);

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

    if (G_RegionDescriptorsEnabled && G_RegionDescriptorBootstrap == FALSE) {
        UpdateDescriptorsForFree(CanonicalStart, NumPages << PAGE_SIZE_MUL);
    }

    FreeEmptyPageTables();
    FlushTLB();

    DEBUG(TEXT("[FreeRegion] Exit base=%p size=%u"), (LPVOID)OriginalBase, Size);

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
    LINEAR OriginalBase = Base;
    UINT NumPages = (Size + (PAGE_SIZE - 1u)) >> PAGE_SIZE_MUL;
    if (NumPages == 0u) {
        NumPages = 1u;
    }

    DEBUG(TEXT("[FreeRegion] Enter base=%p size=%u pages=%u"),
        (LPVOID)OriginalBase,
        Size,
        NumPages);

    LINEAR CanonicalBase = CanonicalizeLinearAddress(Base);
    DEBUG(TEXT("[FreeRegion] Canonical base=%p"), (LPVOID)CanonicalBase);

#if EXOS_X86_64_FAST_VMM
    if (G_RegionDescriptorsEnabled && G_RegionDescriptorBootstrap == FALSE) {
        if (ReleaseRegionWithFastWalker(CanonicalBase, NumPages) == TRUE) {
            UpdateDescriptorsForFree(CanonicalBase, NumPages << PAGE_SIZE_MUL);
            FreeEmptyPageTables();
            FlushTLB();
            DEBUG(TEXT("[FreeRegion] Exit base=%p size=%u"), (LPVOID)OriginalBase, Size);
            return TRUE;
        }

        DEBUG(TEXT("[FreeRegion] Fast walker fallback engaged for base=%p size=%u"),
            (LPVOID)CanonicalBase,
            Size);
    }
#endif

    return FreeRegionLegacyInternal(CanonicalBase, NumPages, OriginalBase, Size);
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
