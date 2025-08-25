
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\************************************************************************/

#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "I386.h"

/************************************************************************/

// Initializes the memory manager
void InitializeMemoryManager(void);

// Sets up task state segments and per-task descriptors
void InitializeTaskSegments(void);

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
