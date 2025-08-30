
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
