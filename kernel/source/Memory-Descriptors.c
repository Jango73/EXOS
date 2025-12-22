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


    Memory region descriptors

\************************************************************************/

#include "Memory-Descriptors.h"

#include "Console.h"
#include "CoreString.h"
#include "Kernel.h"
#include "Log.h"
#include "process/Schedule.h"
#include "System.h"

/************************************************************************/
// Region descriptor tracking state

BOOL G_RegionDescriptorsEnabled = FALSE;
BOOL G_RegionDescriptorBootstrap = FALSE;
LPMEMORY_REGION_DESCRIPTOR G_FreeRegionDescriptors = NULL;
UINT G_FreeRegionDescriptorCount = 0;
UINT G_TotalRegionDescriptorCount = 0;
UINT G_RegionDescriptorPages = 0;

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
static BOOL EnsureDescriptorSlab(void) {
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
        ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER,
        TEXT("RegionDescriptorSlab"));
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
static LPMEMORY_REGION_DESCRIPTOR AcquireRegionDescriptor(void) {
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
static void ReleaseRegionDescriptor(LPMEMORY_REGION_DESCRIPTOR Descriptor) {
    if (Descriptor == NULL) {
        return;
    }

    Descriptor->TypeID = KOID_NONE;
    Descriptor->References = 0;
    Descriptor->ID = U64_Make(0, 0);
    Descriptor->OwnerProcess = NULL;
    Descriptor->Base = 0;
    Descriptor->CanonicalBase = 0;
    Descriptor->PhysicalBase = 0;
    Descriptor->Size = 0;
    Descriptor->PageCount = 0;
    Descriptor->Flags = 0;
    Descriptor->Attributes = 0;
    Descriptor->Granularity = MEMORY_REGION_GRANULARITY_4K;
    Descriptor->Tag[0] = STR_NULL;

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
static void InsertDescriptorOrdered(LPPROCESS Process, LPMEMORY_REGION_DESCRIPTOR Descriptor) {
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
static void RemoveDescriptor(LPPROCESS Process, LPMEMORY_REGION_DESCRIPTOR Descriptor) {
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
BOOL RegisterRegionDescriptor(LINEAR Base, UINT NumPages, PHYSICAL Target, U32 Flags, LPCSTR Tag) {
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
    Descriptor->ID = U64_Make(0, 0);
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
    if (Tag != NULL) {
        StringCopyLimit(Descriptor->Tag, Tag, MEMORY_REGION_TAG_MAX);
    } else {
        Descriptor->Tag[0] = STR_NULL;
    }

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
            Right->ID = U64_Make(0, 0);
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
            StringCopyLimit(Right->Tag, Descriptor->Tag, MEMORY_REGION_TAG_MAX);
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
 * @brief Track a successful region allocation in the descriptor list.
 * @param Base Base address for the allocation.
 * @param Target Physical base when fixed, 0 otherwise.
 * @param Size Size in bytes.
 * @param Flags Allocation flags.
 * @return TRUE on success or when tracking is disabled.
 */
BOOL RegionTrackAlloc(LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags, LPCSTR Tag) {
    if (G_RegionDescriptorsEnabled == FALSE || G_RegionDescriptorBootstrap == TRUE) {
        return TRUE;
    }

    if (Size == 0) {
        return FALSE;
    }

    UINT NumPages = (Size + PAGE_SIZE - 1) >> PAGE_SIZE_MUL;
    if (NumPages == 0) {
        return FALSE;
    }

    return RegisterRegionDescriptor(Base, NumPages, Target, Flags, Tag);
}

/************************************************************************/
/**
 * @brief Track a successful region release in the descriptor list.
 * @param Base Base address for the free.
 * @param Size Size in bytes.
 * @return TRUE on success or when tracking is disabled.
 */
BOOL RegionTrackFree(LINEAR Base, UINT Size) {
    if (G_RegionDescriptorsEnabled == FALSE || G_RegionDescriptorBootstrap == TRUE) {
        return TRUE;
    }

    if (Size == 0) {
        return FALSE;
    }

    UpdateDescriptorsForFree(Base, Size);
    return TRUE;
}

/************************************************************************/
/**
 * @brief Track a successful resize in the descriptor list.
 * @param Base Base address for the region.
 * @param OldSize Previous size in bytes.
 * @param NewSize New size in bytes.
 * @param Flags Allocation flags.
 * @return TRUE on success or when tracking is disabled.
 */
BOOL RegionTrackResize(LINEAR Base, UINT OldSize, UINT NewSize, U32 Flags) {
    if (G_RegionDescriptorsEnabled == FALSE || G_RegionDescriptorBootstrap == TRUE) {
        return TRUE;
    }

    if (OldSize == NewSize) {
        return TRUE;
    }

    if (NewSize < OldSize) {
        LINEAR FreeBase = Base + (LINEAR)NewSize;
        UINT FreeSize = OldSize - NewSize;
        UpdateDescriptorsForFree(FreeBase, FreeSize);
        return TRUE;
    }

    UINT AdditionalSize = NewSize - OldSize;
    UINT AdditionalPages = (AdditionalSize + PAGE_SIZE - 1) >> PAGE_SIZE_MUL;
    if (AdditionalPages == 0) {
        return TRUE;
    }

    LINEAR CanonicalBase = CanonicalizeLinearAddress(Base);
    LPMEMORY_REGION_DESCRIPTOR Descriptor = FindDescriptorForBase(ResolveCurrentAddressSpaceOwner(), CanonicalBase);
    if (Descriptor == NULL) {
        return RegisterRegionDescriptor(Base, (NewSize + PAGE_SIZE - 1) >> PAGE_SIZE_MUL, 0, Flags, NULL);
    }

    ExtendDescriptor(Descriptor, AdditionalPages);
    return TRUE;
}
