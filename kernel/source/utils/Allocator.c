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


    Allocator

\************************************************************************/

#include "utils/Allocator.h"

#include "Heap.h"

/************************************************************************/

static LPVOID AllocatorKernelAlloc(LPVOID Context, UINT Size) {
    UNUSED(Context);
    return KernelHeapAlloc(Size);
}

/************************************************************************/

static LPVOID AllocatorKernelRealloc(LPVOID Context, LPVOID Pointer, UINT Size) {
    UNUSED(Context);
    return KernelHeapRealloc(Pointer, Size);
}

/************************************************************************/

static void AllocatorKernelFree(LPVOID Context, LPVOID Pointer) {
    UNUSED(Context);
    KernelHeapFree(Pointer);
}

/************************************************************************/

static LPVOID AllocatorProcessAlloc(LPVOID Context, UINT Size) {
    return HeapAlloc_P((LPPROCESS)Context, Size);
}

/************************************************************************/

static LPVOID AllocatorProcessRealloc(LPVOID Context, LPVOID Pointer, UINT Size) {
    return HeapRealloc_P((LPPROCESS)Context, Pointer, Size);
}

/************************************************************************/

static void AllocatorProcessFree(LPVOID Context, LPVOID Pointer) {
    HeapFree_P((LPPROCESS)Context, Pointer);
}

/************************************************************************/

void AllocatorInitKernel(LPALLOCATOR Allocator) {
    AllocatorInitFunctions(Allocator, NULL, AllocatorKernelAlloc, AllocatorKernelRealloc, AllocatorKernelFree);
}

/************************************************************************/

void AllocatorInitProcess(LPALLOCATOR Allocator, LPPROCESS Process) {
    AllocatorInitFunctions(Allocator, Process, AllocatorProcessAlloc, AllocatorProcessRealloc, AllocatorProcessFree);
}

/************************************************************************/

void AllocatorInitFunctions(
    LPALLOCATOR Allocator,
    LPVOID Context,
    ALLOCATOR_ALLOC_FUNCTION Alloc,
    ALLOCATOR_REALLOC_FUNCTION Realloc,
    ALLOCATOR_FREE_FUNCTION Free) {
    if (Allocator == NULL) {
        return;
    }

    Allocator->Context = Context;
    Allocator->Alloc = Alloc;
    Allocator->Realloc = Realloc;
    Allocator->Free = Free;
}

/************************************************************************/

LPVOID AllocatorAlloc(LPCALLOCATOR Allocator, UINT Size) {
    if (Allocator == NULL || Allocator->Alloc == NULL) {
        return NULL;
    }

    return Allocator->Alloc(Allocator->Context, Size);
}

/************************************************************************/

LPVOID AllocatorRealloc(LPCALLOCATOR Allocator, LPVOID Pointer, UINT Size) {
    if (Allocator == NULL || Allocator->Realloc == NULL) {
        return NULL;
    }

    return Allocator->Realloc(Allocator->Context, Pointer, Size);
}

/************************************************************************/

void AllocatorFree(LPCALLOCATOR Allocator, LPVOID Pointer) {
    if (Allocator == NULL || Allocator->Free == NULL) {
        return;
    }

    Allocator->Free(Allocator->Context, Pointer);
}

/************************************************************************/

LPVOID AllocatorListAlloc(LPVOID Context, UINT Size) {
    return AllocatorAlloc((LPCALLOCATOR)Context, Size);
}

/************************************************************************/

void AllocatorListFree(LPVOID Context, LPVOID Pointer) {
    AllocatorFree((LPCALLOCATOR)Context, Pointer);
}

/************************************************************************/
