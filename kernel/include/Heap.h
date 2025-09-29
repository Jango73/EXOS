
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

#ifndef HEAP_H_INCLUDED
#define HEAP_H_INCLUDED

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

#include "Base.h"
#include "Process.h"

/***************************************************************************/

#define HEAP_NUM_SIZE_CLASSES 8
#define HEAP_MIN_BLOCK_SIZE 16
#define HEAP_MAX_SMALL_BLOCK_SIZE 2048

/***************************************************************************/

typedef struct tag_HEAPBLOCKHEADER {
    U32 ID;
    U32 Size;
    struct tag_HEAPBLOCKHEADER* Next;
    struct tag_HEAPBLOCKHEADER* Prev;
} HEAPBLOCKHEADER, *LPHEAPBLOCKHEADER;

/***************************************************************************/

typedef struct tag_HEAPCONTROLBLOCK {
    U32 ID;
    LINEAR HeapBase;
    U32 HeapSize;
    LPHEAPBLOCKHEADER FreeLists[HEAP_NUM_SIZE_CLASSES];
    LPHEAPBLOCKHEADER LargeFreeList;
    LPVOID FirstUnallocated;
} HEAPCONTROLBLOCK, *LPHEAPCONTROLBLOCK;

/***************************************************************************/

void HeapInit(LINEAR HeapBase, U32 HeapSize);

// Allocates memory space in the calling process' heap
// Must provide the heap's limits.
LPVOID HeapAlloc_HBHS(LINEAR HeapBase, U32 HeapSize, U32 Size);

// Reallocates memory space in the calling process' heap
// Must provide the heap's limits.
LPVOID HeapRealloc_HBHS(LINEAR HeapBase, U32 HeapSize, LPVOID Pointer, U32 Size);

// Frees memory space in the calling process' heap
// Must provide the heap's limits.
void HeapFree_HBHS(LINEAR HeapBase, U32 HeapSize, LPVOID Pointer);

// Allocates memory space in the specified process' heap
LPVOID HeapAlloc_P(LPPROCESS Process, U32 Size);

// Reallocates memory space in the specified process' heap
LPVOID HeapRealloc_P(LPPROCESS Process, LPVOID Pointer, U32 Size);

// Frees memory space in the specified process' heap
void HeapFree_P(LPPROCESS Process, LPVOID Pointer);

// Allocates memory space in the kernel's heap
LPVOID KernelHeapAlloc(U32 Size);

// Reallocates memory space in the kernel's heap
LPVOID KernelHeapRealloc(LPVOID Pointer, U32 Size);

// Frees memory space in the kernel's heap
void KernelHeapFree(LPVOID Pointer);

// Allocates memory space in the calling process' heap
LPVOID HeapAlloc(U32 Size);

// Reallocates memory space in the calling process' heap
LPVOID HeapRealloc(LPVOID Pointer, U32 Size);

// Frees memory space in the calling process' heap
void HeapFree(LPVOID Pointer);

/***************************************************************************/

#pragma pack(pop)

#endif  // HEAP_H_INCLUDED
