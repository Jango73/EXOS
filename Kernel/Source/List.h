
// LIST.H

/*************************************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\*************************************************************************************************/

#ifndef LIST_H_INCLUDED
#define LIST_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

#pragma pack (1)

/***************************************************************************/

#define LISTNODE_FIELDS  \
  U32        ID;         \
  U32        References; \
  LPLISTNODE Next;       \
  LPLISTNODE Prev;

typedef struct LISTNODESTRUCT LISTNODE, *LPLISTNODE;

struct LISTNODESTRUCT
{
  LISTNODE_FIELDS
};

/*************************************************************************************************/

typedef void   (*LISTITEMDESTRUCTOR) (LPVOID);
typedef LPVOID (*MEMALLOCFUNC)       (U32);
typedef void   (*MEMFREEFUNC)        (LPVOID);

typedef struct LISTSTRUCT
{
  LPLISTNODE         First;
  LPLISTNODE         Last;
  LPLISTNODE         Current;
  U32                NumItems;
  MEMALLOCFUNC       MemAllocFunc;
  MEMFREEFUNC        MemFreeFunc;
  LISTITEMDESTRUCTOR Destructor;
} LIST, *LPLIST;

/*************************************************************************************************/

typedef I32 (*COMPAREFUNC) (LPCVOID, LPCVOID);

/*************************************************************************************************/

void       QuickSort        (LPVOID, U32, U32, COMPAREFUNC);
LPLIST     NewList          (LISTITEMDESTRUCTOR, MEMALLOCFUNC, MEMFREEFUNC);
U32        DeleteList       (LPLIST);
U32        ListGetSize      (LPLIST);
U32        ListAddItem      (LPLIST, LPVOID);
U32        ListAddBefore    (LPLIST, LPVOID, LPVOID);
U32        ListAddAfter     (LPLIST, LPVOID, LPVOID);
U32        ListAddHead      (LPLIST, LPVOID);
U32        ListAddTail      (LPLIST, LPVOID);
LPVOID     ListRemove       (LPLIST, LPVOID);
U32        ListErase        (LPLIST, LPVOID);
U32        ListEraseLast    (LPLIST);
U32        ListReset        (LPLIST);
LPVOID     ListGetItem      (LPLIST, U32);
U32        ListGetItemIndex (LPLIST, LPVOID);
LPLIST     ListMergeList    (LPLIST, LPLIST);
U32        ListSort         (LPLIST, COMPAREFUNC);

/*************************************************************************************************/

#endif
