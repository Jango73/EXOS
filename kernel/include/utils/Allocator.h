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

#ifndef ALLOCATOR_H_INCLUDED
#define ALLOCATOR_H_INCLUDED

/**************************************************************************/

#include "Base.h"
#include "process/Process.h"

/**************************************************************************/

typedef LPVOID (*ALLOCATOR_ALLOC_FUNCTION)(LPVOID Context, UINT Size);
typedef LPVOID (*ALLOCATOR_REALLOC_FUNCTION)(LPVOID Context, LPVOID Pointer, UINT Size);
typedef void (*ALLOCATOR_FREE_FUNCTION)(LPVOID Context, LPVOID Pointer);

typedef struct tag_ALLOCATOR {
    LPVOID Context;
    ALLOCATOR_ALLOC_FUNCTION Alloc;
    ALLOCATOR_REALLOC_FUNCTION Realloc;
    ALLOCATOR_FREE_FUNCTION Free;
} ALLOCATOR, *LPALLOCATOR;
typedef const ALLOCATOR* LPCALLOCATOR;

/**************************************************************************/

void AllocatorInitKernel(LPALLOCATOR Allocator);
void AllocatorInitProcess(LPALLOCATOR Allocator, LPPROCESS Process);
void AllocatorInitFunctions(
    LPALLOCATOR Allocator,
    LPVOID Context,
    ALLOCATOR_ALLOC_FUNCTION Alloc,
    ALLOCATOR_REALLOC_FUNCTION Realloc,
    ALLOCATOR_FREE_FUNCTION Free);

LPVOID AllocatorAlloc(LPCALLOCATOR Allocator, UINT Size);
LPVOID AllocatorRealloc(LPCALLOCATOR Allocator, LPVOID Pointer, UINT Size);
void AllocatorFree(LPCALLOCATOR Allocator, LPVOID Pointer);

LPVOID AllocatorListAlloc(LPVOID Context, UINT Size);
void AllocatorListFree(LPVOID Context, LPVOID Pointer);

/**************************************************************************/

#endif  // ALLOCATOR_H_INCLUDED
