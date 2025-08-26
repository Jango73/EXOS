
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

// Allocates memory space in the calling process' heap
// Must provide the heap's limits.
LPVOID HeapAlloc_HBHS(LINEAR HeapBase, U32 HeapSize, U32 Size);

// Frees memory space in the calling process' heap
// Must provide the heap's limits.
void HeapFree_HBHS(LINEAR HeapBase, U32 HeapSize, LPVOID Pointer);

// LPVOID HeapAlloc_P    (LPPROCESS, U32);
// void   HeapFree_P     (LPPROCESS, LPVOID);

// Allocates memory space in the calling process' heap
LPVOID HeapAlloc(U32 Size);

// Frees memory space in the calling process' heap
void HeapFree(LPVOID Pointer);

/***************************************************************************/

#endif
