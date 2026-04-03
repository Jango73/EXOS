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


    ReservedHeap

\************************************************************************/

#ifndef RESERVED_HEAP_H_INCLUDED
#define RESERVED_HEAP_H_INCLUDED

/**************************************************************************/

#include "memory/Memory.h"
#include "memory/Heap.h"
#include "sync/Mutex.h"
#include "utils/Allocator.h"

/**************************************************************************/

typedef struct tag_RESERVED_HEAP {
    MUTEX Mutex;
    LPPROCESS Process;
    LINEAR HeapBase;
    UINT HeapSize;
    UINT MaximumSize;
    U32 RegionFlags;
    STR Tag[MEMORY_REGION_TAG_MAX];
} RESERVED_HEAP, *LPRESERVED_HEAP;

/**************************************************************************/

BOOL ReservedHeapInit(
    LPRESERVED_HEAP Heap,
    LPPROCESS Process,
    UINT InitialSize,
    UINT MaximumSize,
    U32 RegionFlags,
    LPCSTR Tag);
void ReservedHeapDeinit(LPRESERVED_HEAP Heap);
LPVOID ReservedHeapAlloc(LPRESERVED_HEAP Heap, UINT Size);
LPVOID ReservedHeapRealloc(LPRESERVED_HEAP Heap, LPVOID Pointer, UINT Size);
void ReservedHeapFree(LPRESERVED_HEAP Heap, LPVOID Pointer);
void ReservedHeapInitAllocator(LPRESERVED_HEAP Heap, LPALLOCATOR Allocator);

/**************************************************************************/

#endif  // RESERVED_HEAP_H_INCLUDED
