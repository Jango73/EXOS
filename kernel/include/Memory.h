
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


    Memory manager

\************************************************************************/

#ifndef MEMORY_H_INCLUDED
#define MEMORY_H_INCLUDED

#include "Base.h"
#include "arch/Memory.h"

/************************************************************************/

// Inlines
#if defined(__EXOS_ARCH_X86_64__)
static inline LINEAR CanonicalizeLinearAddress(LINEAR Address) {
    return (LINEAR)ArchCanonicalizeAddress((U64)Address);
}
#else
static inline LINEAR CanonicalizeLinearAddress(LINEAR Address) {
    return Address;
}
#endif

// External symbols
// Initializes the memory manager
void InitializeMemoryManager(void);

// Architecture helpers
void MemorySetTemporaryLinearPages(LINEAR Linear1, LINEAR Linear2, LINEAR Linear3);
void UpdateKernelMemoryMetricsFromMultibootMap(void);
void MarkUsedPhysicalMemory(void);

// Uses temp page tables to get access to random physical pages
LINEAR MapTemporaryPhysicalPage1(PHYSICAL Physical);
LINEAR MapTemporaryPhysicalPage2(PHYSICAL Physical);
LINEAR MapTemporaryPhysicalPage3(PHYSICAL Physical);

// Allocates physical space for a new page directory
PHYSICAL AllocPageDirectory(void);

// Allocates physical space for a new page directory for userland processes
PHYSICAL AllocUserPageDirectory(void);

// Allocates a physical page
PHYSICAL AllocPhysicalPage(void);

// Frees a physical page
void FreePhysicalPage(PHYSICAL Page);

// Returns TRUE if a pointer is an valid address (mapped in the calling process space)
BOOL IsValidMemory(LINEAR Pointer);

extern BOOL KernelSafeValidationAvailable;

// Returns the physical address for a given virtual address
PHYSICAL MapLinearToPhysical(LINEAR Address);

// Allocates physical space for a new region of virtual memory
LINEAR AllocRegion(LINEAR Base, PHYSICAL Target, UINT Size, U32 Flags);

// Resizes an existing region of virtual memory
BOOL ResizeRegion(LINEAR Base, PHYSICAL Target, UINT Size, UINT NewSize, U32 Flags);

// Frees physical space of a region of virtual memory
BOOL FreeRegion(LINEAR Base, UINT Size);

// Map/unmap a physical MMIO region (BAR or Base Address Register) as Uncached Read/Write
LINEAR MapIOMemory(PHYSICAL PhysicalBase, UINT Size);
BOOL UnMapIOMemory(LINEAR LinearBase, UINT Size);

// Kernel region allocation wrapper - automatically uses VMA_KERNEL and AT_OR_OVER
LINEAR AllocKernelRegion(PHYSICAL Target, UINT Size, U32 Flags);

/************************************************************************/

#endif  // MEMORY_H_INCLUDED
