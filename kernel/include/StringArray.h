/***************************************************************************\\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\\***************************************************************************/

#ifndef STRINGARRAY_H_INCLUDED
#define STRINGARRAY_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

typedef struct tag_STRINGARRAY {
    U32 Capacity;
    U32 Count;
    LPSTR *Items;
} STRINGARRAY, *LPSTRINGARRAY;

/***************************************************************************/

BOOL StringArrayInit(LPSTRINGARRAY Array, U32 Capacity);
void StringArrayDeinit(LPSTRINGARRAY Array);
BOOL StringArrayAddUnique(LPSTRINGARRAY Array, LPCSTR String);
LPCSTR StringArrayGet(LPSTRINGARRAY Array, U32 Index);

/***************************************************************************/

#endif
