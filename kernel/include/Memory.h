
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


    Memory

\************************************************************************/
#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "I386.h"

/************************************************************************/

// Initializes the memory manager
void InitializeMemoryManager(void);

// Uses a temp page table to get access to a random physical page
LINEAR MapPhysicalPage(PHYSICAL Physical);

// Allocates physical space for a new page directory
PHYSICAL AllocPageDirectory(void);

// Allocates a physical page
PHYSICAL AllocPhysicalPage(void);

// Frees a physical page
void FreePhysicalPage(PHYSICAL Page);

// Returns TRUE if a pointer is an valid address (mapped in the calling process space)
BOOL IsValidMemory(LINEAR Pointer);

// Returns the physical address for a given virtual address
PHYSICAL MapLinearToPhysical(LINEAR Address);

// Allocates physical space for a new region of virtual memory
LINEAR AllocRegion(LINEAR Base, PHYSICAL Target, U32 Size, U32 Flags);

// Frees physical space of a region of virtual memory
BOOL FreeRegion(LINEAR Base, U32 Size);

// Map/unmap a physical MMIO region (BAR or Base Address Register) as Uncached Read/Write
LINEAR MmMapIo(PHYSICAL PhysicalBase, U32 Size);
BOOL MmUnmapIo(LINEAR LinearBase, U32 Size);

/************************************************************************/

#endif  // MEMORY_H_INCLUDED
