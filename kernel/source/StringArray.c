
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\************************************************************************/

#include "../include/Heap.h"
#include "../include/String.h"
#include "../include/StringArray.h"

/************************************************************************/

BOOL StringArrayInit(LPSTRINGARRAY Array, U32 Capacity) {
    Array->Capacity = Capacity;
    Array->Count = 0;
    Array->Items = (LPSTR *)HeapAlloc(sizeof(LPSTR) * Capacity);
    if (Array->Items == NULL) return FALSE;
    return TRUE;
}

/************************************************************************/

void StringArrayDeinit(LPSTRINGARRAY Array) {
    U32 Index;
    if (Array->Items) {
        for (Index = 0; Index < Array->Count; Index++) {
            if (Array->Items[Index]) HeapFree(Array->Items[Index]);
        }
        HeapFree(Array->Items);
    }
    Array->Items = NULL;
    Array->Count = 0;
    Array->Capacity = 0;
}

/************************************************************************/

static void StringArrayShiftLeft(LPSTRINGARRAY Array) {
    U32 Index;
    if (Array->Count == 0) return;
    if (Array->Items[0]) HeapFree(Array->Items[0]);
    for (Index = 1; Index < Array->Count; Index++) {
        Array->Items[Index - 1] = Array->Items[Index];
    }
    Array->Count--;
}

/************************************************************************/

BOOL StringArrayAddUnique(LPSTRINGARRAY Array, LPCSTR String) {
    U32 Index;
    if (Array->Items == NULL) return FALSE;

    for (Index = 0; Index < Array->Count; Index++) {
        if (StringCompare(Array->Items[Index], String) == 0) {
            return FALSE;
        }
    }

    if (Array->Count == Array->Capacity) {
        StringArrayShiftLeft(Array);
    }

    Array->Items[Array->Count] = (LPSTR)HeapAlloc(StringLength(String) + 1);
    if (Array->Items[Array->Count] == NULL) return FALSE;
    StringCopy(Array->Items[Array->Count], String);
    Array->Count++;
    return TRUE;
}

/************************************************************************/

LPCSTR StringArrayGet(LPSTRINGARRAY Array, U32 Index) {
    if (Index >= Array->Count) return NULL;
    return Array->Items[Index];
}
