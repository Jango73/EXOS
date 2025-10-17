
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


    Heap

\************************************************************************/

#include "Heap.h"

#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "Process.h"
#include "Console.h"

/************************************************************************/

/**
 * @brief Determines the size class for a given allocation size
 * @param Size Size in bytes to categorize
 * @return Size class index (0-7) or 0xFF for large blocks (>2048 bytes)
 *
 * Maps allocation sizes to predefined size classes for efficient freelist management.
 * Size classes: 16, 32, 64, 128, 256, 512, 1024, 2048 bytes.
 */
static U32 GetSizeClass(U32 Size) {
    if (Size <= 16) return 0;
    if (Size <= 32) return 1;
    if (Size <= 64) return 2;
    if (Size <= 128) return 3;
    if (Size <= 256) return 4;
    if (Size <= 512) return 5;
    if (Size <= 1024) return 6;
    if (Size <= 2048) return 7;
    return 0xFF; // Large block
}

/************************************************************************/

/**
 * @brief Returns the actual allocation size for a given size class
 * @param SizeClass Size class index (0-7)
 * @return Size in bytes for the given class, or 0 if invalid class
 *
 * Converts size class indices back to their corresponding byte sizes.
 * Used to determine the actual allocation size for small blocks.
 */
static U32 GetSizeForClass(U32 SizeClass) {
    U32 Sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
    if (SizeClass < HEAP_NUM_SIZE_CLASSES) {
        return Sizes[SizeClass];
    }
    return 0;
}

/************************************************************************/

/**
 * @brief Adds a free block to the appropriate freelist
 * @param ControlBlock Pointer to the heap control block
 * @param Block Pointer to the block header to add
 * @param SizeClass Size class of the block (0-7 for small blocks, 0xFF for large blocks)
 *
 * Inserts the block at the head of the corresponding freelist as a doubly-linked list.
 * Large blocks (>2048 bytes) are added to the separate large block freelist.
 */
static void AddToFreeList(LPHEAPCONTROLBLOCK ControlBlock, LPHEAPBLOCKHEADER Block, U32 SizeClass) {
    if (SizeClass == 0xFF) {
        // Large block
        Block->Next = ControlBlock->LargeFreeList;
        Block->Prev = NULL;
        if (ControlBlock->LargeFreeList) {
            ControlBlock->LargeFreeList->Prev = Block;
        }
        ControlBlock->LargeFreeList = Block;
    } else {
        // Small block
        Block->Next = ControlBlock->FreeLists[SizeClass];
        Block->Prev = NULL;
        if (ControlBlock->FreeLists[SizeClass]) {
            ControlBlock->FreeLists[SizeClass]->Prev = Block;
        }
        ControlBlock->FreeLists[SizeClass] = Block;
    }
}

/************************************************************************/

/**
 * @brief Removes a block from its freelist
 * @param ControlBlock Pointer to the heap control block
 * @param Block Pointer to the block header to remove
 * @param SizeClass Size class of the block (0-7 for small blocks, 0xFF for large blocks)
 *
 * Removes the block from its doubly-linked freelist by updating the previous and next
 * block pointers. Updates the freelist head if removing the first block.
 */
static void RemoveFromFreeList(LPHEAPCONTROLBLOCK ControlBlock, LPHEAPBLOCKHEADER Block, U32 SizeClass) {
    if (Block == NULL) return;

    if (Block->Prev) {
        Block->Prev->Next = Block->Next;
    } else {
        if (SizeClass == 0xFF) {
            ControlBlock->LargeFreeList = Block->Next;
        } else {
            ControlBlock->FreeLists[SizeClass] = Block->Next;
        }
    }
    if (Block->Next) {
        Block->Next->Prev = Block->Prev;
    }
}

/************************************************************************/

/**
 * @brief Initializes a heap with freelist-based allocation
 * @param HeapBase Linear address of the heap base
 * @param HeapSize Size of the heap in bytes
 *
 * Sets up the heap control block and initializes all freelists to empty.
 * The control block contains metadata for managing the heap, including
 * size class freelists and a pointer to the first unallocated space.
 * Memory is 16-byte aligned for optimal performance.
 */
void HeapInit(LPPROCESS Process, LINEAR HeapBase, UINT HeapSize) {
    DEBUG("[HeapInit] Initializing heap at %x, size %x", HeapBase, HeapSize);

    MemorySet((LPVOID)HeapBase, 0, HeapSize);

    LPHEAPCONTROLBLOCK ControlBlock = (LPHEAPCONTROLBLOCK)HeapBase;

    ControlBlock->TypeID = KOID_HEAP;
    ControlBlock->HeapBase = HeapBase;
    ControlBlock->HeapSize = HeapSize;
    ControlBlock->Owner = Process;

    DEBUG("[HeapInit] Owner=%p HeapEnd=%p", Process, (LPVOID)(HeapBase + HeapSize));

    // Initialize all freelists to NULL
    for (U32 i = 0; i < HEAP_NUM_SIZE_CLASSES; i++) {
        ControlBlock->FreeLists[i] = NULL;
    }
    ControlBlock->LargeFreeList = NULL;

    // Set first unallocated to after control block, aligned to 16 bytes
    ControlBlock->FirstUnallocated = (LPVOID)((HeapBase + sizeof(HEAPCONTROLBLOCK) + 15) & ~15);

    DEBUG("[HeapInit] Control block size: %x, first unallocated: %x", sizeof(HEAPCONTROLBLOCK), ControlBlock->FirstUnallocated);
}

/************************************************************************/

/**
 * @brief Attempt to expand the heap when additional memory is required.
 * @param ControlBlock Pointer to the heap control block.
 * @param RequiredSize Allocation size (including header) that triggered the expansion.
 * @return TRUE if the heap was expanded, FALSE otherwise.
 */
static BOOL TryExpandHeap(LPHEAPCONTROLBLOCK ControlBlock, UINT RequiredSize) {
    if (ControlBlock == NULL || ControlBlock->Owner == NULL) {
        ERROR("[TryExpandHeap] Heap owner is undefined");
        return FALSE;
    }

    LPPROCESS Process = ControlBlock->Owner;
    UINT CurrentSize = ControlBlock->HeapSize;
    UINT Limit = Process->MaximumAllocatedMemory;
    UINT AdditionalRequired = (UINT)RequiredSize;
    UINT DesiredSize = CurrentSize << 1;

    DEBUG("[TryExpandHeap] Enter ControlBlock=%p Owner=%p CurrentSize=%u Required=%u Limit=%u", ControlBlock,
        Process, CurrentSize, RequiredSize, Limit);
    DEBUG("[TryExpandHeap] FirstUnallocated=%p HeapEnd=%p", ControlBlock->FirstUnallocated,
        (LPVOID)(ControlBlock->HeapBase + ControlBlock->HeapSize));

    if (DesiredSize < CurrentSize) {
        DesiredSize = Limit;
    }

    UINT MinimumRequired = CurrentSize + AdditionalRequired;
    if (MinimumRequired < CurrentSize) {
        MinimumRequired = Limit;
    }

    if (DesiredSize < MinimumRequired) {
        DesiredSize = MinimumRequired;
    }

    if (DesiredSize > Limit) {
        DesiredSize = Limit;
    }

    if (DesiredSize <= CurrentSize) {
        ERROR("[TryExpandHeap] Heap limit reached (Current=%x Limit=%x)", CurrentSize, Limit);
        return FALSE;
    }

    U32 Flags = ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE;
    if (Process->Privilege == PRIVILEGE_KERNEL) {
        Flags |= ALLOC_PAGES_AT_OR_OVER;
    }

    if (ResizeRegion(Process->HeapBase, 0, (U32)CurrentSize, (U32)DesiredSize, Flags) == FALSE) {
        ERROR("[TryExpandHeap] ResizeRegion failed for heap at %x (from %x to %x)", Process->HeapBase, CurrentSize,
            DesiredSize);
        return FALSE;
    }

    ControlBlock->HeapSize = DesiredSize;
    Process->HeapSize = DesiredSize;

    DEBUG("[TryExpandHeap] Success NewSize=%u", DesiredSize);
    DEBUG("[TryExpandHeap] Updated HeapEnd=%p", (LPVOID)(ControlBlock->HeapBase + ControlBlock->HeapSize));

    return TRUE;
}

/************************************************************************/

/**
 * @brief Allocates memory from a heap using heap base, heap size, and size parameters
 * @param HeapBase Linear address of the heap base
 * @param HeapSize Size of the heap in bytes
 * @param Size Size of memory to allocate in bytes
 * @return Pointer to allocated memory, or NULL if allocation failed
 *
 * This function implements a freelist-based heap allocation algorithm with size classes.
 * Memory is 16-byte aligned for optimal performance.
 */
LPVOID HeapAlloc_HBHS(LPPROCESS Process, LINEAR HeapBase, UINT HeapSize, UINT Size) {
    UNUSED(HeapSize);

    LPHEAPCONTROLBLOCK ControlBlock = (LPHEAPCONTROLBLOCK)HeapBase;
    BOOL ControlBlockValid = FALSE;

    if (ControlBlock != NULL) {
        ControlBlockValid = IsValidMemory((LINEAR)ControlBlock);
    }

    if (ControlBlockValid == FALSE) {
        ERROR("[HeapAlloc_HBHS] ControlBlock %p is invalid (HeapBase=%p Size=%u)", ControlBlock, (LPVOID)HeapBase, Size);
        return NULL;
    }

    if (Process != NULL) {
        ControlBlock->Owner = Process;
    }
    LPHEAPBLOCKHEADER Block = NULL;
    U32 SizeClass = 0;
    U32 ActualSize = 0;
    U32 TotalSize = 0;

    // Check validity of parameters
    if (ControlBlock == NULL) {
        DEBUG("[HeapAlloc_HBHS] ControlBlock is NULL (HeapBase=%p)", (LPVOID)HeapBase);
        return NULL;
    }

    DEBUG("[HeapAlloc_HBHS] ControlBlock=%p TypeID=%x HeapBase=%p HeapSize=%u FirstUnallocated=%p Owner=%p", ControlBlock,
        ControlBlock->TypeID, (LPVOID)ControlBlock->HeapBase, ControlBlock->HeapSize, ControlBlock->FirstUnallocated,
        ControlBlock->Owner);

    if (ControlBlock->TypeID != KOID_HEAP) {
        DEBUG("[HeapAlloc_HBHS] Invalid control block type %x (expected %x)", ControlBlock->TypeID, KOID_HEAP);
        return NULL;
    }

    if (Size == 0) {
        DEBUG("[HeapAlloc_HBHS] Requested size is zero");
        return NULL;
    }

    DEBUG("[HeapAlloc_HBHS] Request Process=%p HeapBase=%p Size=%u FirstUnallocated=%p HeapEnd=%p", Process,
        (LPVOID)HeapBase, Size, ControlBlock->FirstUnallocated,
        (LPVOID)(ControlBlock->HeapBase + ControlBlock->HeapSize));

    // Determine size class and actual allocation size
    SizeClass = GetSizeClass(Size);
    if (SizeClass != 0xFF) {
        ActualSize = GetSizeForClass(SizeClass);
    } else {
        // Large block - align to 16 bytes
        ActualSize = (Size + 15) & ~15;
    }

    TotalSize = ActualSize + sizeof(HEAPBLOCKHEADER);

    // Ensure block, including header, is 16-byte aligned so returned pointer
    // keeps the required alignment for 64-bit data and SIMD state.
    TotalSize = (TotalSize + 0x0F) & ~0x0F;

    DEBUG("[HeapAlloc_HBHS] SizeClass=%x ActualSize=%u TotalSize=%u", SizeClass, ActualSize, TotalSize);

    // Try to find a block in the appropriate freelist
    if (SizeClass != 0xFF) {
        // Small block - check exact size class first
        Block = ControlBlock->FreeLists[SizeClass];
        if (Block != NULL && Block->TypeID == KOID_HEAP && Block->Size >= TotalSize) {
            RemoveFromFreeList(ControlBlock, Block, SizeClass);
            LPVOID UserPointer = (LPVOID)((LINEAR)Block + sizeof(HEAPBLOCKHEADER));
            DEBUG("[HeapAlloc_HBHS] Using freelist block=%p SizeClass=%x BlockSize=%u UserPointer=%p Next=%p Prev=%p",
                Block, SizeClass, Block->Size, UserPointer, Block->Next, Block->Prev);
            return UserPointer;
        }

        // Try larger size classes
        for (U32 i = SizeClass + 1; i < HEAP_NUM_SIZE_CLASSES; i++) {
            Block = ControlBlock->FreeLists[i];
            if (Block != NULL && Block->TypeID == KOID_HEAP && Block->Size >= TotalSize) {
                RemoveFromFreeList(ControlBlock, Block, i);

                // Split the block if it's significantly larger
                if (Block->Size > TotalSize) {
                    U32 RemainingSize = Block->Size - TotalSize;
                    if (RemainingSize >= sizeof(HEAPBLOCKHEADER) + HEAP_MIN_BLOCK_SIZE) {
                        LPHEAPBLOCKHEADER SplitBlock = (LPHEAPBLOCKHEADER)((LINEAR)Block + TotalSize);
                        SplitBlock->TypeID = KOID_HEAP;
                        SplitBlock->Size = RemainingSize;
                        SplitBlock->Next = NULL;
                        SplitBlock->Prev = NULL;
#ifdef __EXOS_64__
                        SplitBlock->AlignmentPadding = 0;
#endif

                        U32 SplitSizeClass = GetSizeClass(RemainingSize - sizeof(HEAPBLOCKHEADER));
                        AddToFreeList(ControlBlock, SplitBlock, SplitSizeClass);

                        Block->Size = TotalSize;
                    }
                }

                LPVOID SplitUserPointer = (LPVOID)((LINEAR)Block + sizeof(HEAPBLOCKHEADER));
                DEBUG("[HeapAlloc_HBHS] Using larger freelist block=%p SizeClass=%x BlockSize=%u UserPointer=%p Next=%p Prev=%p",
                    Block, i, Block->Size, SplitUserPointer, Block->Next, Block->Prev);
                return SplitUserPointer;
            }
        }
    } else {
        // Large block - search large freelist
        Block = ControlBlock->LargeFreeList;
        while (Block != NULL) {
            if (Block->TypeID == KOID_HEAP && Block->Size >= TotalSize) {
                RemoveFromFreeList(ControlBlock, Block, 0xFF);

                // Split if significantly larger
                if (Block->Size > TotalSize) {
                    U32 RemainingSize = Block->Size - TotalSize;
                    if (RemainingSize >= sizeof(HEAPBLOCKHEADER) + HEAP_MIN_BLOCK_SIZE) {
                        LPHEAPBLOCKHEADER SplitBlock = (LPHEAPBLOCKHEADER)((LINEAR)Block + TotalSize);
                        SplitBlock->TypeID = KOID_HEAP;
                        SplitBlock->Size = RemainingSize;
                        SplitBlock->Next = NULL;
                        SplitBlock->Prev = NULL;
#ifdef __EXOS_64__
                        SplitBlock->AlignmentPadding = 0;
#endif

                        AddToFreeList(ControlBlock, SplitBlock, 0xFF);

                        Block->Size = TotalSize;
                    }
                }

                LPVOID LargeUserPointer = (LPVOID)((LINEAR)Block + sizeof(HEAPBLOCKHEADER));
                DEBUG("[HeapAlloc_HBHS] Using large freelist block=%p BlockSize=%u UserPointer=%p Next=%p Prev=%p",
                    Block, Block->Size, LargeUserPointer, Block->Next, Block->Prev);
                return LargeUserPointer;
            }
            Block = Block->Next;
        }
    }

    // No suitable free block found, allocate from unallocated space
    LINEAR NewBlockAddr = (LINEAR)ControlBlock->FirstUnallocated;
    DEBUG("[HeapAlloc_HBHS] Allocate from unallocated NewBlockAddr=%p TotalSizeNeeded=%u HeapEnd=%p",
        (LPVOID)NewBlockAddr, TotalSize, (LPVOID)(ControlBlock->HeapBase + ControlBlock->HeapSize));
    if (NewBlockAddr + TotalSize > ControlBlock->HeapBase + ControlBlock->HeapSize) {
        if (TryExpandHeap(ControlBlock, TotalSize) == FALSE) {
            DEBUG("[HeapAlloc_HBHS] TryExpandHeap failed for Required=%u", TotalSize);
            return NULL;
        }

        NewBlockAddr = (LINEAR)ControlBlock->FirstUnallocated;

        DEBUG("[HeapAlloc_HBHS] Post-expand NewBlockAddr=%p HeapEnd=%p", (LPVOID)NewBlockAddr,
            (LPVOID)(ControlBlock->HeapBase + ControlBlock->HeapSize));

        if (NewBlockAddr + TotalSize > ControlBlock->HeapBase + ControlBlock->HeapSize) {
            DEBUG("[HeapAlloc_HBHS] Heap still too small after expansion NewBlockAddr=%p TotalSize=%u", (LPVOID)NewBlockAddr,
                TotalSize);
            return NULL;
        }
    }

    Block = (LPHEAPBLOCKHEADER)NewBlockAddr;
    Block->TypeID = KOID_HEAP;
    Block->Size = TotalSize;
    Block->Next = NULL;
    Block->Prev = NULL;
#ifdef __EXOS_64__
    Block->AlignmentPadding = 0;
#endif

    ControlBlock->FirstUnallocated = (LPVOID)(NewBlockAddr + TotalSize);

    DEBUG("[HeapAlloc_HBHS] Allocated new block=%p UserPointer=%p NextUnallocated=%p", Block,
        (LPVOID)(NewBlockAddr + sizeof(HEAPBLOCKHEADER)), ControlBlock->FirstUnallocated);

    return (LPVOID)(NewBlockAddr + sizeof(HEAPBLOCKHEADER));
}

/************************************************************************/

/**
 * @brief Reallocates memory from a heap using heap base, heap size, and size parameters
 * @param HeapBase Linear address of the heap base
 * @param HeapSize Size of the heap in bytes
 * @param Pointer Pointer to existing memory block, or NULL
 * @param Size New size of memory block in bytes
 * @return Pointer to reallocated memory, or NULL if reallocation failed
 *
 * This function behaves like standard realloc():
 * - If Pointer is NULL, behaves like HeapAlloc_HBHS()
 * - If Size is 0, frees the memory and returns NULL
 * - Otherwise, changes the size of the memory block, potentially moving it
 */
LPVOID HeapRealloc_HBHS(LPPROCESS Process, LINEAR HeapBase, UINT HeapSize, LPVOID Pointer, UINT Size) {
    if (Pointer == NULL) {
        return HeapAlloc_HBHS(Process, HeapBase, HeapSize, Size);
    }

    if (Size == 0) {
        HeapFree_HBHS(HeapBase, HeapSize, Pointer);
        return NULL;
    }

    LPHEAPCONTROLBLOCK ControlBlock = (LPHEAPCONTROLBLOCK)HeapBase;
    if (Process != NULL) {
        ControlBlock->Owner = Process;
    }
    if (ControlBlock == NULL || ControlBlock->TypeID != KOID_HEAP) return NULL;

    DEBUG("[HeapRealloc_HBHS] Reallocating pointer %x to size %x", Pointer, Size);

    // Get the block header
    LPHEAPBLOCKHEADER Block = (LPHEAPBLOCKHEADER)((LINEAR)Pointer - sizeof(HEAPBLOCKHEADER));
    if (Block->TypeID != KOID_HEAP) {
        ERROR("[HeapRealloc_HBHS] Invalid block header ID");
        return NULL;
    }

    U32 OldDataSize = Block->Size - sizeof(HEAPBLOCKHEADER);
    U32 NewSizeClass = GetSizeClass(Size);
    U32 NewActualSize = (NewSizeClass != 0xFF) ? GetSizeForClass(NewSizeClass) : ((Size + 15) & ~15);
    U32 NewTotalSize = NewActualSize + sizeof(HEAPBLOCKHEADER);

    // If new size fits in current block, just return the same pointer
    if (NewTotalSize <= Block->Size) {
        DEBUG("[HeapRealloc_HBHS] Reusing existing block");
        return Pointer;
    }

    // Need to allocate new block
    LPVOID NewPointer = HeapAlloc_HBHS(Process, HeapBase, HeapSize, Size);
    SAFE_USE(NewPointer) {
        // Copy old data to new location
        MemoryCopy(NewPointer, Pointer, OldDataSize < Size ? OldDataSize : Size);
        // Free old block
        HeapFree_HBHS(HeapBase, HeapSize, Pointer);
    }

    DEBUG("[HeapRealloc_HBHS] Allocated new block at %x", NewPointer);
    return NewPointer;
}

/************************************************************************/

/**
 * @brief Frees memory allocated from a heap using heap base and heap size parameters
 * @param HeapBase Linear address of the heap base
 * @param HeapSize Size of the heap in bytes (unused but kept for consistency)
 * @param Pointer Pointer to memory to free
 *
 * This function frees a block and attempts to coalesce with adjacent free blocks.
 */
void HeapFree_HBHS(LINEAR HeapBase, UINT HeapSize, LPVOID Pointer) {
    UNUSED(HeapSize);

    LPHEAPCONTROLBLOCK ControlBlock = (LPHEAPCONTROLBLOCK)HeapBase;
    LPHEAPBLOCKHEADER Block = NULL;
    U32 SizeClass = 0;

    if (Pointer == NULL) return;
    if (ControlBlock == NULL || ControlBlock->TypeID != KOID_HEAP) return;

    // DEBUG("[HeapFree_HBHS] Freeing pointer %x", Pointer);

    // Get the block header
    Block = (LPHEAPBLOCKHEADER)((LINEAR)Pointer - sizeof(HEAPBLOCKHEADER));
    if (Block->TypeID != KOID_HEAP) {
        ERROR("[HeapFree_HBHS] Invalid block header ID");
        return;
    }

    // DEBUG("[HeapFree_HBHS] Freeing block at %x, size %x", Block, Block->Size);

    // TODO: Implement coalescing with adjacent blocks
    // For now, just add to appropriate freelist

    U32 DataSize = Block->Size - sizeof(HEAPBLOCKHEADER);
    SizeClass = GetSizeClass(DataSize);

    Block->Next = NULL;
    Block->Prev = NULL;

    AddToFreeList(ControlBlock, Block, SizeClass);

    // DEBUG("[HeapFree_HBHS] Added block to freelist, size class %x", SizeClass);
}

/************************************************************************/

/**
 * @brief Allocates memory from a process's heap with mutex protection
 * @param Process Pointer to the process structure
 * @param Size Size of memory to allocate in bytes
 * @return Pointer to allocated memory, or NULL if allocation failed
 *
 * This function provides thread-safe memory allocation by acquiring the
 * process's heap mutex before calling the core allocation function.
 */
LPVOID HeapAlloc_P(LPPROCESS Process, UINT Size) {
    LPVOID Pointer = NULL;
    LPHEAPCONTROLBLOCK ControlBlock = NULL;
    BOOL ProcessValid = FALSE;
    BOOL ControlBlockValid = FALSE;

    if (Process != NULL) {
        ProcessValid = IsValidMemory((LINEAR)Process);
        DEBUG("[HeapAlloc_P] Process pointer=%p Valid=%u", Process, ProcessValid);

        if (ProcessValid != FALSE) {
            ControlBlock = (LPHEAPCONTROLBLOCK)Process->HeapBase;
            ControlBlockValid = IsValidMemory((LINEAR)ControlBlock);
            DEBUG("[HeapAlloc_P] ControlBlock pointer=%p Valid=%u", ControlBlock, ControlBlockValid);

            if (ControlBlockValid != FALSE) {
                DEBUG("[HeapAlloc_P] HeapBase=%p HeapSize=%u FirstUnallocated=%p MaximumAllocated=%u HeapMutex=%p Lock=%u",
                    (LPVOID)Process->HeapBase, Process->HeapSize, ControlBlock->FirstUnallocated,
                    Process->MaximumAllocatedMemory, &(Process->HeapMutex), Process->HeapMutex.Lock);
            }
        }
    }

    if (Process == NULL) {
        DEBUG("[HeapAlloc_P] Process is NULL for allocation of size %u", Size);
        return NULL;
    }

    DEBUG("[HeapAlloc_P] Lock mutex Process=%p Mutex=%p", Process, &(Process->HeapMutex));
    LockMutex(&(Process->HeapMutex), INFINITY);
    DEBUG("[HeapAlloc_P] Locked mutex Process=%p Mutex=%p Lock=%u", Process, &(Process->HeapMutex), Process->HeapMutex.Lock);

    Pointer = HeapAlloc_HBHS(Process, Process->HeapBase, Process->HeapSize, Size);

    DEBUG("[HeapAlloc_P] Unlock mutex Process=%p Mutex=%p", Process, &(Process->HeapMutex));
    UnlockMutex(&(Process->HeapMutex));
    DEBUG("[HeapAlloc_P] Result Process=%p Size=%u Pointer=%p", Process, Size, Pointer);

    if (ControlBlock != NULL && ControlBlockValid != FALSE) {
        DEBUG("[HeapAlloc_P] Post-alloc FirstUnallocated=%p HeapSize=%u", ControlBlock->FirstUnallocated,
            ControlBlock->HeapSize);
    }

    return Pointer;
}

/************************************************************************/

/**
 * @brief Reallocates memory from a process's heap with mutex protection
 * @param Process Pointer to the process structure
 * @param Pointer Pointer to existing memory block, or NULL
 * @param Size New size of memory block in bytes
 * @return Pointer to reallocated memory, or NULL if reallocation failed
 *
 * This function provides thread-safe memory reallocation by acquiring the
 * process's heap mutex before calling the core reallocation function.
 */
LPVOID HeapRealloc_P(LPPROCESS Process, LPVOID Pointer, UINT Size) {
    LPVOID NewPointer = NULL;
    LockMutex(&(Process->HeapMutex), INFINITY);
    NewPointer = HeapRealloc_HBHS(Process, Process->HeapBase, Process->HeapSize, Pointer, Size);
    UnlockMutex(&(Process->HeapMutex));
    return NewPointer;
}

/************************************************************************/

/**
 * @brief Frees memory from a process's heap with mutex protection
 * @param Process Pointer to the process structure
 * @param Pointer Pointer to memory to free
 *
 * This function provides thread-safe memory deallocation by acquiring the
 * process's heap mutex before calling the core deallocation function.
 */
void HeapFree_P(LPPROCESS Process, LPVOID Pointer) {
    LockMutex(&(Process->HeapMutex), INFINITY);
    HeapFree_HBHS(Process->HeapBase, Process->HeapSize, Pointer);
    UnlockMutex(&(Process->HeapMutex));
}

/************************************************************************/

/**
 * @brief Allocates memory from the kernel heap
 * @param Size Size of memory to allocate in bytes
 * @return Pointer to allocated memory, or NULL if allocation failed
 *
 * Convenience function for allocating memory from the kernel process heap.
 */
LPVOID KernelHeapAlloc(UINT Size) {
    LPVOID Pointer = NULL;
    LPHEAPCONTROLBLOCK ControlBlock = NULL;
    BOOL ControlBlockValid = FALSE;
    static LINEAR ExpectedKernelHeapBase = 0;
    static UINT ExpectedKernelHeapSize = 0;

    LINEAR HeapBaseValue = KernelProcess.HeapBase;
    UINT HeapSizeValue = KernelProcess.HeapSize;
    UINT HeapBaseOffset = 0;
    BOOL RecoveredFromSwap = FALSE;

    if (HeapBaseValue >= VMA_KERNEL) {
        HeapBaseOffset = (UINT)(HeapBaseValue - VMA_KERNEL);
    }

    DEBUG(TEXT("[KernelHeapAlloc] Enter Size=%u"), Size);
    DEBUG(TEXT("[KernelHeapAlloc] KernelProcess=%p HeapBase=%p HeapSize=%u HeapMutex=%p Lock=%u"), &KernelProcess,
        (LPVOID)HeapBaseValue, HeapSizeValue, &(KernelProcess.HeapMutex), KernelProcess.HeapMutex.Lock);
    DEBUG(TEXT("[KernelHeapAlloc] HeapBase offset from VMA_KERNEL=%x HeapSizeHex=%x"), HeapBaseOffset, HeapSizeValue);
    DEBUG(TEXT("[KernelHeapAlloc] Heap field addresses HeapBase@%p HeapSize@%p"), &(KernelProcess.HeapBase),
        &(KernelProcess.HeapSize));

    UNUSED(HeapBaseOffset);

    if (HeapBaseValue >= VMA_KERNEL) {
        ControlBlock = (LPHEAPCONTROLBLOCK)HeapBaseValue;
        ControlBlockValid = IsValidMemory((LINEAR)ControlBlock);
    }

    if (ControlBlockValid == FALSE && KernelProcess.HeapSize >= VMA_KERNEL) {
        LPHEAPCONTROLBLOCK Candidate = (LPHEAPCONTROLBLOCK)KernelProcess.HeapSize;
        if (IsValidMemory((LINEAR)Candidate) != FALSE && Candidate->TypeID == KOID_HEAP) {
            ERROR(TEXT("[KernelHeapAlloc] Kernel heap base/size mismatch detected (Base=%p Size=%p). Using control block %p"),
                (LPVOID)KernelProcess.HeapBase, (LPVOID)KernelProcess.HeapSize, Candidate);
            KernelProcess.HeapBase = (LINEAR)Candidate;
            KernelProcess.HeapSize = Candidate->HeapSize;
            HeapBaseValue = KernelProcess.HeapBase;
            HeapSizeValue = KernelProcess.HeapSize;
            ControlBlock = Candidate;
            ControlBlockValid = TRUE;
            RecoveredFromSwap = TRUE;
        }
    }

    if (ControlBlockValid == FALSE && ExpectedKernelHeapBase >= VMA_KERNEL) {
        ERROR(TEXT("[KernelHeapAlloc] Restoring kernel heap base from expected snapshot Base=%p Size=%u"),
            (LPVOID)ExpectedKernelHeapBase, ExpectedKernelHeapSize);
        KernelProcess.HeapBase = ExpectedKernelHeapBase;
        HeapBaseValue = KernelProcess.HeapBase;
        ControlBlock = (LPHEAPCONTROLBLOCK)HeapBaseValue;
        ControlBlockValid = IsValidMemory((LINEAR)ControlBlock);
        if (ExpectedKernelHeapSize != 0) {
            KernelProcess.HeapSize = ExpectedKernelHeapSize;
            HeapSizeValue = KernelProcess.HeapSize;
        }
    }

    if (ControlBlockValid == FALSE) {
        ConsolePanic(TEXT("[KernelHeapAlloc] Kernel heap control block is unmapped"));
    }

    if (KernelProcess.HeapSize != ControlBlock->HeapSize) {
        DEBUG(TEXT("[KernelHeapAlloc] Synchronizing kernel heap size from control block (Process=%u Control=%u)"),
            KernelProcess.HeapSize, ControlBlock->HeapSize);
        KernelProcess.HeapSize = ControlBlock->HeapSize;
        HeapSizeValue = KernelProcess.HeapSize;
    }

    if (ExpectedKernelHeapBase == 0 && HeapBaseValue >= VMA_KERNEL) {
        ExpectedKernelHeapBase = HeapBaseValue;
        ExpectedKernelHeapSize = HeapSizeValue;
        DEBUG(TEXT("[KernelHeapAlloc] Recorded initial kernel heap base %p size %u"), (LPVOID)ExpectedKernelHeapBase,
            ExpectedKernelHeapSize);
    } else if (ExpectedKernelHeapBase != 0) {
        if (HeapBaseValue != ExpectedKernelHeapBase) {
            ERROR(TEXT("[KernelHeapAlloc] KernelProcess.HeapBase changed! Expected=%p Current=%p"),
                (LPVOID)ExpectedKernelHeapBase, (LPVOID)HeapBaseValue);
            ExpectedKernelHeapBase = HeapBaseValue;
        }

        if (HeapSizeValue != ExpectedKernelHeapSize) {
            DEBUG(TEXT("[KernelHeapAlloc] KernelProcess.HeapSize changed Expected=%u Current=%u"), ExpectedKernelHeapSize,
                HeapSizeValue);
            ExpectedKernelHeapSize = HeapSizeValue;
        }
    }

    DEBUG(TEXT("[KernelHeapAlloc] ControlBlock pointer=%p Valid=%u"), ControlBlock, ControlBlockValid);

    if (RecoveredFromSwap != FALSE) {
        DEBUG(TEXT("[KernelHeapAlloc] Recovered kernel heap base=%p size=%u"), (LPVOID)KernelProcess.HeapBase,
            KernelProcess.HeapSize);
    }

    DEBUG(TEXT("[KernelHeapAlloc] ControlBlock TypeID=%x FirstUnallocated=%p Owner=%p HeapSize=%u"), ControlBlock->TypeID,
        ControlBlock->FirstUnallocated, ControlBlock->Owner, ControlBlock->HeapSize);

    Pointer = HeapAlloc_P(&KernelProcess, Size);

    if (Pointer != NULL) {
        DEBUG("[KernelHeapAlloc] Allocation succeeded Pointer=%p", Pointer);

        if (ControlBlock != NULL && ControlBlockValid != FALSE) {
            LINEAR HeaderAddress = ((LINEAR)Pointer) - sizeof(HEAPBLOCKHEADER);
            LPHEAPBLOCKHEADER Header = (LPHEAPBLOCKHEADER)HeaderAddress;
            BOOL HeaderValid = IsValidMemory((LINEAR)Header);
            DEBUG("[KernelHeapAlloc] Header=%p Valid=%u", Header, HeaderValid);

            if (HeaderValid != FALSE) {
                DEBUG("[KernelHeapAlloc] HeaderSize=%u TypeID=%x Next=%p Prev=%p NextUnallocated=%p", Header->Size,
                    Header->TypeID, Header->Next, Header->Prev, ControlBlock->FirstUnallocated);
            }
        }
    } else {
        DEBUG("[KernelHeapAlloc] Allocation failed Pointer=NULL ControlBlockValid=%u", ControlBlockValid);
    }

    return Pointer;
}

/************************************************************************/

/**
 * @brief Reallocates memory from the kernel heap
 * @param Pointer Pointer to existing memory block, or NULL
 * @param Size New size of memory block in bytes
 * @return Pointer to reallocated memory, or NULL if reallocation failed
 *
 * Convenience function for reallocating memory from the kernel process heap.
 */
LPVOID KernelHeapRealloc(LPVOID Pointer, UINT Size) { return HeapRealloc_P(&KernelProcess, Pointer, Size); }

/***************************************************************************/

/**
 * @brief Frees memory from the kernel heap
 * @param Pointer Pointer to memory to free
 *
 * Convenience function for freeing memory from the kernel process heap.
 */
void KernelHeapFree(LPVOID Pointer) { HeapFree_P(&KernelProcess, Pointer); }

/***************************************************************************/

/**
 * @brief Allocates memory from the current process's heap
 * @param Size Size of memory to allocate in bytes
 * @return Pointer to allocated memory, or NULL if allocation failed
 *
 * Convenience function that automatically determines the current process
 * and allocates memory from its heap.
 */
LPVOID HeapAlloc(UINT Size) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) return NULL;

    return HeapAlloc_P(Process, Size);
}

/***************************************************************************/

/**
 * @brief Frees memory from the current process's heap
 * @param Pointer Pointer to memory to free
 *
 * Convenience function that automatically determines the current process
 * and frees memory from its heap.
 */
void HeapFree(LPVOID Pointer) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) return;

    HeapFree_P(Process, Pointer);
}

/***************************************************************************/

/**
 * @brief Reallocates memory from the current process's heap
 * @param Pointer Pointer to existing memory block, or NULL
 * @param Size New size of memory block in bytes
 * @return Pointer to reallocated memory, or NULL if reallocation failed
 *
 * Convenience function that automatically determines the current process
 * and reallocates memory from its heap.
 */
LPVOID HeapRealloc(LPVOID Pointer, U32 Size) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) return NULL;

    return HeapRealloc_P(Process, Pointer, Size);
}
