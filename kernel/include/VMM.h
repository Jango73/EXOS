
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef VMM_H_INCLUDED
#define VMM_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "I386.h"

/***************************************************************************/

// VMM data

typedef struct tag_VMMDATA {
    U32 Memory;
    U32 Pages;
} VMMDATA, *LPVMMDATA;

/***************************************************************************/

extern U32 Memory;
extern U32 Pages;

/***************************************************************************/

void InitializeVirtualMemoryManager();
void InitPageTable(LPPAGETABLE, PHYSICAL);
void SetPhysicalPageMark(U32, U32);
U32 GetPhysicalPageMark(U32);
LINEAR MapPhysicalPage(PHYSICAL Physical);
PHYSICAL AllocPageDirectory();
PHYSICAL AllocPhysicalPage();
LINEAR VirtualAlloc(LINEAR Base, PHYSICAL Target, U32 Size, U32 Flags);
BOOL VirtualFree(LINEAR Base, U32 Size);

// Map/unmap a physical MMIO region (BAR or Base Address Register) as Uncached Read/Write
LINEAR MmMapIo(PHYSICAL PhysicalBase, U32 Size);
BOOL   MmUnmapIo(LINEAR LinearBase, U32 Size);

/***************************************************************************/

#endif
