
// Heap.h

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#ifndef HEAP_H_INCLUDED
#define HEAP_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

// Functions in Heap.c

LPVOID HeapAlloc_HBHS (LINEAR, U32, U32);
void   HeapFree_HBHS  (LINEAR, U32, LPVOID);
// LPVOID HeapAlloc_P    (LPPROCESS, U32);
// void   HeapFree_P     (LPPROCESS, LPVOID);
LPVOID HeapAlloc      (U32);
void   HeapFree       (LPVOID);

/***************************************************************************/

#endif
