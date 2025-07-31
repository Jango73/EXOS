
// Heap.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#include "Heap.h"

#include "Kernel.h"
#include "Process.h"

/***************************************************************************/

#define HEAP_NUM_ENTRIES 127

/***************************************************************************/

typedef struct tag_HEAPALLOCENTRY {
    LINEAR Base;
    U32 Size : 31;
    U32 Used : 1;
} HEAPALLOCENTRY, *LPHEAPALLOCENTRY;

/***************************************************************************/

typedef struct tag_HEAPCONTROLBLOCK {
    U32 ID;
    LPVOID Next;
    HEAPALLOCENTRY Entries[HEAP_NUM_ENTRIES];
} HEAPCONTROLBLOCK, *LPHEAPCONTROLBLOCK;

/***************************************************************************/

LPVOID HeapAlloc_HBHS(LINEAR HeapBase, U32 HeapSize, U32 Size) {
    LINEAR Pointer = NULL;
    LPHEAPCONTROLBLOCK Block = NULL;
    LPHEAPALLOCENTRY Entry = NULL;
    LINEAR HighBlock = NULL;
    U32 Index = 0;
    STR Text[16];

    /*
    #ifdef __DEBUG__
      KernelPrint("Entering HeapAllocEx\n");
      U32ToHexString(HeapBase, Text);
      KernelPrint("Heap base : ");
      KernelPrint(Text);
      KernelPrint(Text_NewLine);
      U32ToHexString(HeapSize, Text);
      KernelPrint("Heap size : ");
      KernelPrint(Text);
      KernelPrint(Text_NewLine);
    #endif
    */

    KernelLogText(LOG_DEBUG, "Entering HeapAllocEx");

    //-------------------------------------
    // Check validity of parameters

    Block = (LPHEAPCONTROLBLOCK)HeapBase;

    if (Block == NULL) return NULL;
    if (Block->ID != ID_HEAP) return NULL;
    if (Size == 0) return NULL;

    // LockSemaphore(Process->HeapSemaphore, INFINITY);

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

    // UnlockSemaphore(KernelData->Process[Prc].HeapSemaphore);

    /*
    #ifdef __DEBUG__
      KernelPrint("Exiting HeapAllocEx\n");
      U32ToHexString(Pointer, Text);
      KernelPrint("Return value : ");
      KernelPrint(Text);
      KernelPrint(Text_NewLine);
    #endif
    */

    KernelLogText(LOG_DEBUG, "Exiting HeapAllocEx");

    return (LPVOID)Pointer;
}

/***************************************************************************/

void HeapFree_HBHS(LINEAR HeapBase, U32 HeapSize, LPVOID Pointer) {
    LPHEAPCONTROLBLOCK Block = NULL;
    LPHEAPALLOCENTRY Entry = NULL;
    U32 Index = 0;

    /*
    #ifdef __DEBUG__
      KernelPrint("Entering HeapFree_HBHS\n");
    #endif
    */

    KernelLogText(LOG_DEBUG, "Entering HeapFree_HBHS");

    if (Pointer == NULL) return;

    // LockSemaphore(Process->HeapSemaphore, INFINITY);

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

    /*
    #ifdef __DEBUG__
      KernelPrint("Exiting HeapAlloc_HBHS\n");
    #endif
    */

    KernelLogText(LOG_DEBUG, "Exiting HeapAlloc_HBHS");

    // UnlockSemaphore(KernelData->Process[Prc].HeapSemaphore);
}

/***************************************************************************/

LPVOID HeapAlloc_P(LPPROCESS Process, U32 Size) {
    return HeapAlloc_HBHS(Process->HeapBase, Process->HeapSize, Size);
}

/***************************************************************************/

void HeapFree_P(LPPROCESS Process, LPVOID Pointer) {
    HeapFree_HBHS(Process->HeapBase, Process->HeapSize, Pointer);
}

/***************************************************************************/

LPVOID HeapAlloc(U32 Size) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) return NULL;

    return HeapAlloc_HBHS(Process->HeapBase, Process->HeapSize, Size);
}

/***************************************************************************/

void HeapFree(LPVOID Pointer) {
    LPPROCESS Process = GetCurrentProcess();
    if (Process == NULL) return;

    HeapFree_HBHS(Process->HeapBase, Process->HeapSize, Pointer);
}

/***************************************************************************/
