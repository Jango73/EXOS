
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/List.h"

#include "../include/Heap.h"
#include "../include/String.h"

/***************************************************************************/

static void RecursiveSort(U8* Base, I32 Left, I32 Rite, U32 ItemSize,
                          COMPAREFUNC Func, U8* Buffer) {
    I32 i = Left;
    I32 j = Rite;

    U8* x = (U8*)HeapAlloc(ItemSize);
    if (x == NULL) return;

    MemoryCopy(x, Base + (((Left + Rite) / 2) * ItemSize), ItemSize);

    while (i <= j) {
        while (Func((LPCVOID)x, (LPCVOID)(Base + (i * ItemSize))) > 0) i++;

        while (Func((LPCVOID)(Base + (j * ItemSize)), (LPCVOID)x) > 0) j--;

        if (i <= j) {
            if (i != j) {
                MemoryCopy(Buffer, Base + (i * ItemSize), ItemSize);
                MemoryCopy(Base + (i * ItemSize), Base + (j * ItemSize),
                           ItemSize);
                MemoryCopy(Base + (j * ItemSize), Buffer, ItemSize);
            }
            i++;
            j--;
        }
    }

    HeapFree(x);

    if (Left < j) RecursiveSort(Base, Left, j, ItemSize, Func, Buffer);
    if (i < Rite) RecursiveSort(Base, i, Rite, ItemSize, Func, Buffer);
}

/***************************************************************************/

void QuickSort(LPVOID Base, U32 NumItems, U32 ItemSize, COMPAREFUNC Func) {
    U8* Buffer;

    if (Base == NULL) return;
    if (NumItems == 0) return;
    if (ItemSize == 0) return;
    if (Func == NULL) return;

    Buffer = (U8*)HeapAlloc(ItemSize);

    RecursiveSort((U8*)Base, 0, NumItems - 1, ItemSize, Func, Buffer);

    HeapFree(Buffer);
}

/***************************************************************************/

LPLIST NewList(LISTITEMDESTRUCTOR ItemDestructor, MEMALLOCFUNC MemAlloc,
               MEMFREEFUNC MemFree) {
    LPLIST This = NULL;

    if (MemAlloc == NULL) MemAlloc = (MEMALLOCFUNC)HeapAlloc;
    if (MemFree == NULL) MemFree = (MEMFREEFUNC)HeapFree;

    This = (LPLIST)MemAlloc(sizeof(LIST));

    if (This == NULL) return NULL;

    This->First = NULL;
    This->Last = NULL;
    This->Current = This->First;
    This->NumItems = 0;
    This->MemAllocFunc = MemAlloc;
    This->MemFreeFunc = MemFree;
    This->Destructor = ItemDestructor;

    return This;
}

/*************************************************************************************************/

U32 DeleteList(LPLIST This) {
    ListReset(This);
    This->MemFreeFunc(This);

    return TRUE;
}

/*************************************************************************************************/

U32 ListGetSize(LPLIST This) { return This->NumItems; }

/*************************************************************************************************/

U32 ListAddItem(LPLIST This, LPVOID Item) {
    LPLISTNODE NewNode = (LPLISTNODE)Item;

    if (This == NULL) return FALSE;

    if (NewNode) {
        if (This->First == NULL) {
            This->First = NewNode;
        } else {
            This->Last->Next = NewNode;
            NewNode->Prev = This->Last;
        }

        This->Last = NewNode;
        NewNode->Next = NULL;

        This->NumItems++;

        return TRUE;
    }

    return FALSE;
}

/*************************************************************************************************/

U32 ListAddBefore(LPLIST This, LPVOID RefItem, LPVOID NewItem) {
    LPLISTNODE CurNode = NULL;
    LPLISTNODE PrevNode = NULL;
    LPLISTNODE NewNode = (LPLISTNODE)NewItem;
    LPLISTNODE RefNode = (LPLISTNODE)RefItem;

    if (This->First == NULL) return ListAddItem(This, NewItem);

    CurNode = This->First;
    PrevNode = This->First;

    while (CurNode) {
        if (CurNode == RefNode) {
            if (CurNode == This->First) {
                This->First = NewNode;
                NewNode->Next = CurNode;
                NewNode->Prev = NULL;
                CurNode->Prev = NewNode;

                This->NumItems++;

                return TRUE;
            } else {
                NewNode->Next = CurNode;
                NewNode->Prev = PrevNode;
                PrevNode->Next = NewNode;
                CurNode->Prev = NewNode;

                This->NumItems++;

                return TRUE;
            }
        }
        PrevNode = CurNode;
        CurNode = CurNode->Next;
    }

    return ListAddItem(This, NewItem);
}

/*************************************************************************************************/

U32 ListAddAfter(LPLIST This, LPVOID RefItem, LPVOID NewItem) {
    LPLISTNODE PrevNode = NULL;
    LPLISTNODE NextNode = NULL;
    LPLISTNODE NewNode = (LPLISTNODE)NewItem;
    LPLISTNODE RefNode = (LPLISTNODE)RefItem;

    if (This->First == NULL) return ListAddItem(This, NewItem);

    PrevNode = This->First;

    while (PrevNode) {
        if (PrevNode == RefNode) {
            NextNode = PrevNode->Next;

            if (NextNode) {
                PrevNode->Next = NewNode;
                NextNode->Prev = NewNode;
                NewNode->Prev = PrevNode;
                NewNode->Next = NextNode;

                This->NumItems++;

                return TRUE;
            } else {
                return ListAddItem(This, NewItem);
            }
        }
        PrevNode = PrevNode->Next;
    }

    return ListAddItem(This, NewItem);
}

/*************************************************************************************************/

U32 ListAddHead(LPLIST This, LPVOID Item) {
    return ListAddBefore(This, This->First, Item);
}

/*************************************************************************************************/

U32 ListAddTail(LPLIST This, LPVOID Item) {
    return ListAddAfter(This, This->Last, Item);
}

/*************************************************************************************************/

LPVOID ListRemove(LPLIST This, LPVOID Item) {
    LPLISTNODE Temp;
    LPLISTNODE Node = (LPLISTNODE)Item;

    if (Node == NULL) return NULL;

    if (This->Current == Node) {
        Temp = Node;
        This->Current = NULL;

        if (Temp->Prev) {
            Temp->Prev->Next = Temp->Next;
            This->Current = Temp->Prev;
        }

        if (Temp->Next) {
            Temp->Next->Prev = Temp->Prev;
            This->Current = Temp->Next;
        }

        This->NumItems--;

        if (This->First == Temp) This->First = Temp->Next;
        if (This->Last == Temp) This->Last = Temp->Prev;

        return Temp;
    }

    Temp = This->First;

    while (Temp) {
        if (Temp == Node) {
            if (Temp->Prev) Temp->Prev->Next = Temp->Next;
            if (Temp->Next) Temp->Next->Prev = Temp->Prev;

            This->NumItems--;

            if (This->First == Temp) This->First = Temp->Next;
            if (This->Last == Temp) This->Last = Temp->Prev;

            return Temp;
        } else {
            Temp = Temp->Next;
        }
    }

    return NULL;
}

/*************************************************************************************************/

U32 ListErase(LPLIST This, LPVOID Item) {
    Item = ListRemove(This, Item);

    // if (Item && This->MemFreeFunc) This->MemFreeFunc(Item);

    if (Item && This->Destructor) {
        This->Destructor(Item);
    }

    return TRUE;
}

/*************************************************************************************************/

U32 ListEraseLast(LPLIST This) {
    LPLISTNODE Node = NULL;

    for (Node = This->First; Node != NULL; Node = Node->Next) {
        if (Node->Next == NULL) {
            ListErase(This, Node);
            return TRUE;
        }
    }

    return FALSE;
}

/*************************************************************************************************/

U32 ListEraseItem(LPLIST This, LPVOID Item) {
    LPLISTNODE Node = NULL;

    for (Node = This->First; Node != NULL; Node = Node->Next) {
        if (Node == (LPLISTNODE)Item) {
            ListErase(This, Node);
            return TRUE;
        }
    }

    return FALSE;
}

/*************************************************************************************************/

U32 ListReset(LPLIST This) {
    LPLISTNODE Node = This->First;

    while (Node) {
        This->Current = Node;
        Node = This->Current->Next;

        if (This->Destructor != NULL) {
            This->Destructor(This->Current);
        } else {
            // This->MemFreeFunc(This->Current);
        }
    }

    This->First = NULL;
    This->Current = NULL;
    This->Last = NULL;
    This->NumItems = 0;

    return 1;
}

/*************************************************************************************************/

LPVOID ListGetItem(LPLIST This, U32 Index) {
    LPLISTNODE Node = This->First;
    U32 Counter = 0;

    if (This->NumItems == 0) return NULL;

    if (Index >= This->NumItems) return NULL;

    while (Node) {
        if (Counter == Index) break;
        Counter++;
        Node = Node->Next;
    }

    return Node;
}

/*************************************************************************************************/

U32 ListGetItemIndex(LPLIST This, LPVOID Item) {
    LPLISTNODE Node = NULL;
    U32 Index = MAX_U32;

    for (Node = This->First; Node; Node = Node->Next) {
        Index++;
        if (Node == (LPLISTNODE)Item) break;
    }

    return Index;
}

/*************************************************************************************************/

LPLIST ListMergeList(LPLIST This, LPLIST That) {
    LPLISTNODE Node = NULL;

    for (Node = That->First; Node != NULL; Node = Node->Next) {
        ListAddItem(This, Node);
    }

    DeleteList(That);

    return This;
}

/*************************************************************************************************/

U32 ListSort(LPLIST This, COMPAREFUNC Func) {
    LPLISTNODE Node = NULL;
    LPVOID* Data = NULL;
    U32 NumItems = 0;
    U32 Index = 0;

    if (This->NumItems == 0) return TRUE;

    NumItems = This->NumItems;
    Data = (LPVOID*)This->MemAllocFunc(sizeof(LPVOID) * NumItems);

    if (Data == NULL) return 0;

    // Record all items in the list and clear nodes

    for (Node = This->First, Index = 0; Node; Node = Node->Next, Index++) {
        Data[Index] = Node;
    }

    // Clear the list

    This->First = NULL;
    This->Last = NULL;
    This->Current = NULL;
    This->NumItems = 0;

    // Do the sort

    QuickSort(Data, NumItems, sizeof(LPVOID), Func);

    // Rebuild the list with the sorted items

    for (Index = 0; Index < NumItems; Index++) {
        ListAddItem(This, Data[Index]);
    }

    This->MemFreeFunc(Data);

    return 1;
}

/*************************************************************************************************/
