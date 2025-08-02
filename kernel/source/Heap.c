
// Heap.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\***************************************************************************/

#include "../include/Heap.h"

#include "../include/Kernel.h"
#include "../include/Process.h"

/***************************************************************************/

LPVOID HeapAlloc_HBHS(LINEAR HeapBase, U32 HeapSize, U32 Size) {
    LINEAR Pointer = NULL;
    LPHEAPCONTROLBLOCK Block = NULL;
    LPHEAPALLOCENTRY Entry = NULL;
    LINEAR HighBlock = NULL;
    U32 Index = 0;

    KernelLogText(LOG_DEBUG, TEXT("Entering HeapAllocEx"));

    //-------------------------------------
    // Check validity of parameters

    Block = (LPHEAPCONTROLBLOCK)HeapBase;

    if (Block == NULL) return NULL;
    if (Block->ID != ID_HEAP) return NULL;
    if (Size == 0) return NULL;

    HighBlock = HeapBase + sizeof(HEAPCONTROLBLOCK);

    // Look for a free memory block

    while (1) {
        for (Index = 0; Index < HEAP_NUM_ENTRIES; Index++) {
            Entry = Block->Entries + Index;

            if (Entry->Used == 0 && Entry->Base != NULL) {
                if (Entry->Size >= Size) {
                    Entry->Used = 1;
                    Entry->Size = Size;
                    Pointer = Entry->Base;
                    goto Out;
                }
            }

            if (Entry->Base + Entry->Size > HighBlock) {
                HighBlock = Entry->Base + Entry->Size;
            }
        }

        if (Block->Next == NULL) break;
        Block = Block->Next;

        if (((LINEAR)Block) + sizeof(HEAPCONTROLBLOCK) > HighBlock) {
            HighBlock = ((LINEAR)Block) + sizeof(HEAPCONTROLBLOCK);
        }
    }

    // First check if we have reached the top of the heap

    if (HighBlock + Size > HeapBase + HeapSize) {
        goto Out;
    }

    // Allocate a new memory block if possible

    Block = (LPHEAPCONTROLBLOCK)HeapBase;

    while (1) {
        for (Index = 0; Index < HEAP_NUM_ENTRIES; Index++) {
            Entry = Block->Entries + Index;

            if (Entry->Used == 0 && Entry->Base == NULL) {
                Entry->Used = 1;
                Entry->Base = HighBlock;
                Entry->Size = Size;
                Pointer = Entry->Base;
                goto Out;
            }
        }

        if (Block->Next == NULL) {
            // Allocate a new control block if possible
            // if (HighBlock + sizeof (HEAPCONTROLBLOCK) < 0x40000000)
            if (1) {
                Block->Next = (LPHEAPCONTROLBLOCK)HighBlock;
                MemorySet(Block->Next, 0, sizeof(HEAPCONTROLBLOCK));
                ((LPHEAPCONTROLBLOCK)Block->Next)->ID = ID_HEAP;
                HighBlock += sizeof(HEAPCONTROLBLOCK);
            } else {
                // Can't allocate any more control blocks
                goto Out;
            }
        }

        Block = Block->Next;
    }

Out:

    if (Pointer != NULL) {
        MemorySet((LPVOID)Pointer, 0, Size);
    }

    KernelLogText(LOG_DEBUG, TEXT("Exiting HeapAllocEx"));

    return (LPVOID)Pointer;
}

/***************************************************************************/

void HeapFree_HBHS(LINEAR HeapBase, U32 HeapSize, LPVOID Pointer) {
    UNUSED(HeapSize);

    LPHEAPCONTROLBLOCK Block = NULL;
    LPHEAPALLOCENTRY Entry = NULL;
    U32 Index = 0;

    KernelLogText(LOG_DEBUG, TEXT("Entering HeapFree_HBHS"));

    if (Pointer == NULL) return;

    Block = (LPHEAPCONTROLBLOCK)HeapBase;

    while (1) {
        for (Index = 0; Index < HEAP_NUM_ENTRIES; Index++) {
            Entry = Block->Entries + Index;

            if (Entry->Base == (LINEAR)Pointer && Entry->Used == 1) {
                Entry->Used = 0;
                goto Out;
            }
        }

        if (Block->Next == NULL) break;
        Block = Block->Next;
    }

Out:

    KernelLogText(LOG_DEBUG, TEXT("Exiting HeapAlloc_HBHS"));
}

/***************************************************************************/

LPVOID HeapAlloc_P(LPPROCESS Process, U32 Size) {
    LPVOID Pointer = NULL;
    LockMutex(&(Process->HeapMutex), INFINITY);
    Pointer = HeapAlloc_HBHS(Process->HeapBase, Process->HeapSize, Size);
    UnlockMutex(&(Process->HeapMutex));
    return Pointer;
}

/***************************************************************************/

void HeapFree_P(LPPROCESS Process, LPVOID Pointer) {
    LockMutex(&(Process->HeapMutex), INFINITY);
    HeapFree_HBHS(Process->HeapBase, Process->HeapSize, Pointer);
    UnlockMutex(&(Process->HeapMutex));
}

/***************************************************************************/

LPVOID HeapAlloc(U32 Size) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) return NULL;

    return HeapAlloc_P(Process, Size);
}

/***************************************************************************/

void HeapFree(LPVOID Pointer) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) return;

    HeapFree_P(Process, Pointer);
}

/***************************************************************************/
