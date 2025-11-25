
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
#include "process/Process.h"
#include "Memory.h"
#if defined(__EXOS_ARCH_I386__)
    #include "arch/i386/i386-Log.h"
#elif defined(__EXOS_ARCH_X86_64__)
    #include "arch/x86-64/x86-64-Log.h"
#endif

/************************************************************************/

/**
 * @brief Determines the size class for a given allocation size
 * @param Size Size in bytes to categorize
 * @return Size class index (0-7) or 0xFF for large blocks (>2048 bytes)
 *
 * Maps allocation sizes to predefined size classes for efficient freelist management.
 * Size classes: 16, 32, 64, 128, 256, 512, 1024, 2048 bytes.
 */
static UINT GetSizeClass(UINT Size) {
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
static UINT GetSizeForClass(UINT SizeClass) {
    UINT Sizes[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
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
static void AddToFreeList(LPHEAPCONTROLBLOCK ControlBlock, LPHEAPBLOCKHEADER Block, UINT SizeClass) {
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
static void RemoveFromFreeList(LPHEAPCONTROLBLOCK ControlBlock, LPHEAPBLOCKHEADER Block, UINT SizeClass) {
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
 * @brief Dump the current task frame to ease heap allocation debugging.
 */
static void DumpCurrentTaskFrame(void) {
    LPTASK Task = GetCurrentTask();

    SAFE_USE(Task) {
        LogCPUState(&(Task->Arch.Context));
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
    MemorySet((LPVOID)HeapBase, 0, HeapSize);

    LPHEAPCONTROLBLOCK ControlBlock = (LPHEAPCONTROLBLOCK)HeapBase;

    ControlBlock->TypeID = KOID_HEAP;
    ControlBlock->HeapBase = HeapBase;
    ControlBlock->HeapSize = HeapSize;
    ControlBlock->Owner = Process;

    // Initialize all freelists to NULL
    for (UINT i = 0; i < HEAP_NUM_SIZE_CLASSES; i++) {
        ControlBlock->FreeLists[i] = NULL;
    }
    ControlBlock->LargeFreeList = NULL;

    // Set first unallocated to after control block, aligned to 16 bytes
    ControlBlock->FirstUnallocated = (LPVOID)((HeapBase + sizeof(HEAPCONTROLBLOCK) + 15) & ~15);
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

    if (ResizeRegion(Process->HeapBase, 0, CurrentSize, DesiredSize, Flags) == FALSE) {
        ERROR("[TryExpandHeap] ResizeRegion failed for heap at %x (from %x to %x)", Process->HeapBase, CurrentSize,
            DesiredSize);
        return FALSE;
    }

    ControlBlock->HeapSize = DesiredSize;
    Process->HeapSize = DesiredSize;

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
    if (Process != NULL) {
        ControlBlock->Owner = Process;
    }
    LPHEAPBLOCKHEADER Block = NULL;
    UINT SizeClass = 0;
    UINT ActualSize = 0;
    UINT TotalSize = 0;

    // Check validity of parameters
    if (ControlBlock == NULL) return NULL;
    if (ControlBlock->TypeID != KOID_HEAP) return NULL;
    if (Size == 0) return NULL;

    // DEBUG("[HeapAlloc_HBHS] Allocating size %x", Size);

    // Determine size class and actual allocation size
    SizeClass = GetSizeClass(Size);
    if (SizeClass != 0xFF) {
        ActualSize = GetSizeForClass(SizeClass);
    } else {
        // Large block - align to 16 bytes
        ActualSize = (Size + 15) & ~15;
    }

    TotalSize = ActualSize + sizeof(HEAPBLOCKHEADER);

    // DEBUG("[HeapAlloc_HBHS] Size class: %x, actual size: %x, total size: %x", SizeClass, ActualSize, TotalSize);

    // Try to find a block in the appropriate freelist
    if (SizeClass != 0xFF) {
        // Small block - check exact size class first
        Block = ControlBlock->FreeLists[SizeClass];
        if (Block != NULL && Block->TypeID == KOID_HEAP && Block->Size >= TotalSize) {
            RemoveFromFreeList(ControlBlock, Block, SizeClass);
            // DEBUG("[HeapAlloc_HBHS] Found block in size class %x at %x", SizeClass, Block);
            return (LPVOID)((LINEAR)Block + sizeof(HEAPBLOCKHEADER));
        }

        // Try larger size classes
        for (UINT i = SizeClass + 1; i < HEAP_NUM_SIZE_CLASSES; i++) {
            Block = ControlBlock->FreeLists[i];
            if (Block != NULL && Block->TypeID == KOID_HEAP && Block->Size >= TotalSize) {
                RemoveFromFreeList(ControlBlock, Block, i);

                // Split the block if it's significantly larger
                if (Block->Size > TotalSize) {
                    UINT RemainingSize = Block->Size - TotalSize;
                    if (RemainingSize >= sizeof(HEAPBLOCKHEADER) + HEAP_MIN_BLOCK_SIZE) {
                        LPHEAPBLOCKHEADER SplitBlock = (LPHEAPBLOCKHEADER)((LINEAR)Block + TotalSize);
                        SplitBlock->TypeID = KOID_HEAP;
                        SplitBlock->Size = RemainingSize;
                        SplitBlock->Next = NULL;
                        SplitBlock->Prev = NULL;

                        UINT SplitSizeClass = GetSizeClass(RemainingSize - sizeof(HEAPBLOCKHEADER));
                        AddToFreeList(ControlBlock, SplitBlock, SplitSizeClass);

                        Block->Size = TotalSize;
                    }
                }

                // DEBUG("[HeapAlloc_HBHS] Found larger block in size class %x at %x", i, Block);
                return (LPVOID)((LINEAR)Block + sizeof(HEAPBLOCKHEADER));
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
                    UINT RemainingSize = Block->Size - TotalSize;

                    if (RemainingSize >= sizeof(HEAPBLOCKHEADER) + HEAP_MIN_BLOCK_SIZE) {
                        LPHEAPBLOCKHEADER SplitBlock = (LPHEAPBLOCKHEADER)((LINEAR)Block + TotalSize);
                        SplitBlock->TypeID = KOID_HEAP;
                        SplitBlock->Size = RemainingSize;
                        SplitBlock->Next = NULL;
                        SplitBlock->Prev = NULL;

                        AddToFreeList(ControlBlock, SplitBlock, 0xFF);

                        Block->Size = TotalSize;
                    }
                }

                // DEBUG("[HeapAlloc_HBHS] Found large block at %x", Block);
                return (LPVOID)((LINEAR)Block + sizeof(HEAPBLOCKHEADER));
            }
            Block = Block->Next;
        }
    }

    // No suitable free block found, allocate from unallocated space
    LINEAR NewBlockAddr = (LINEAR)ControlBlock->FirstUnallocated;
    if (NewBlockAddr + TotalSize > ControlBlock->HeapBase + ControlBlock->HeapSize) {
        if (TryExpandHeap(ControlBlock, TotalSize) == FALSE) {
            return NULL;
        }

        NewBlockAddr = (LINEAR)ControlBlock->FirstUnallocated;

        if (NewBlockAddr + TotalSize > ControlBlock->HeapBase + ControlBlock->HeapSize) {
            return NULL;
        }
    }

    Block = (LPHEAPBLOCKHEADER)NewBlockAddr;
    Block->TypeID = KOID_HEAP;
    Block->Size = TotalSize;
    Block->Next = NULL;
    Block->Prev = NULL;

    ControlBlock->FirstUnallocated = (LPVOID)(NewBlockAddr + TotalSize);

    // DEBUG("[HeapAlloc_HBHS] Allocated new block at %x, next unallocated: %x", Block, ControlBlock->FirstUnallocated);

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

    // Get the block header
    LPHEAPBLOCKHEADER Block = (LPHEAPBLOCKHEADER)((LINEAR)Pointer - sizeof(HEAPBLOCKHEADER));
    if (Block->TypeID != KOID_HEAP) {
        ERROR("[HeapRealloc_HBHS] Invalid block header ID");
        return NULL;
    }

    UINT OldDataSize = Block->Size - sizeof(HEAPBLOCKHEADER);
    UINT NewSizeClass = GetSizeClass(Size);
    UINT NewActualSize = (NewSizeClass != 0xFF) ? GetSizeForClass(NewSizeClass) : ((Size + 15) & ~15);
    UINT NewTotalSize = NewActualSize + sizeof(HEAPBLOCKHEADER);

    // If new size fits in current block, just return the same pointer
    if (NewTotalSize <= Block->Size) {
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
    UINT SizeClass = 0;

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

    UINT DataSize = Block->Size - sizeof(HEAPBLOCKHEADER);
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

    if (Process == NULL) {
        ERROR(TEXT("[HeapAlloc_P] Process pointer is NULL"));
        return NULL;
    }

    LockMutex(&(Process->HeapMutex), INFINITY);
    Pointer = HeapAlloc_HBHS(Process, Process->HeapBase, Process->HeapSize, Size);
    UnlockMutex(&(Process->HeapMutex));

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
    LPVOID Pointer = HeapAlloc_P(&KernelProcess, Size);

    if (Pointer == NULL) {
        ERROR(TEXT("[KernelHeapAlloc] Allocation failed"));
        DumpCurrentTaskFrame();
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
LPVOID KernelHeapRealloc(LPVOID Pointer, UINT Size) {
    return HeapRealloc_P(&KernelProcess, Pointer, Size);
}

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
LPVOID HeapRealloc(LPVOID Pointer, UINT Size) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) return NULL;
    return HeapRealloc_P(Process, Pointer, Size);
}
