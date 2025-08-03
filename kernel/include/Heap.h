
/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\***************************************************************************/

#ifndef HEAP_H_INCLUDED
#define HEAP_H_INCLUDED

/***************************************************************************/

#include "Base.h"

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

// Functions in Heap.c

LPVOID HeapAlloc_HBHS(LINEAR, U32, U32);
void HeapFree_HBHS(LINEAR, U32, LPVOID);
// LPVOID HeapAlloc_P    (LPPROCESS, U32);
// void   HeapFree_P     (LPPROCESS, LPVOID);
LPVOID HeapAlloc(U32);
void HeapFree(LPVOID);

/***************************************************************************/

#endif
