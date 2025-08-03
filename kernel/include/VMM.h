
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
PHYSICAL AllocPageDirectory();
PHYSICAL AllocPhysicalPage();
LINEAR VirtualAlloc(LINEAR, U32, U32);
BOOL VirtualFree(LINEAR, U32);

/***************************************************************************/

#endif
