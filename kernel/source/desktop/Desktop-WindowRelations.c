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


    Desktop window relation helpers

\************************************************************************/

#include "Desktop-Private.h"

/***************************************************************************/

/**
 * @brief Retrieve the parent of one window.
 * @param Handle Window handle.
 * @return Parent handle or NULL.
 */
HANDLE GetWindowParent(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPWINDOW Parent;

    if (This == NULL || This->TypeID != KOID_WINDOW) return NULL;

    LockMutex(&(This->Mutex), INFINITY);
    Parent = This->ParentWindow;
    UnlockMutex(&(This->Mutex));

    return (HANDLE)Parent;
}

/***************************************************************************/

/**
 * @brief Return the number of direct children owned by one window.
 * @param Handle Window handle.
 * @return Number of direct children.
 */
U32 GetWindowChildCount(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    U32 Count = 0;

    if (This == NULL || This->TypeID != KOID_WINDOW) return 0;

    LockMutex(&(This->Mutex), INFINITY);
    if (This->Children != NULL) Count = This->Children->NumItems;
    UnlockMutex(&(This->Mutex));

    return Count;
}

/***************************************************************************/

/**
 * @brief Retrieve one direct child by zero-based index.
 * @param Handle Parent window handle.
 * @param ChildIndex Zero-based child index.
 * @return Child handle or NULL when unavailable.
 */
HANDLE GetWindowChild(HANDLE Handle, U32 ChildIndex) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPLISTNODE Node;
    U32 Index;

    if (This == NULL || This->TypeID != KOID_WINDOW) return NULL;

    LockMutex(&(This->Mutex), INFINITY);

    if (This->Children == NULL || ChildIndex >= This->Children->NumItems) {
        UnlockMutex(&(This->Mutex));
        return NULL;
    }

    Index = 0;
    for (Node = This->Children->First; Node != NULL; Node = Node->Next) {
        if (Index == ChildIndex) {
            UnlockMutex(&(This->Mutex));
            return (HANDLE)Node;
        }
        Index++;
    }

    UnlockMutex(&(This->Mutex));
    return NULL;
}

/***************************************************************************/

/**
 * @brief Retrieve the next sibling of one window.
 * @param Handle Window handle.
 * @return Next sibling handle or NULL.
 */
HANDLE GetNextWindowSibling(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPWINDOW Parent;
    LPLISTNODE Node;

    if (This == NULL || This->TypeID != KOID_WINDOW) return NULL;

    Parent = (LPWINDOW)GetWindowParent(Handle);
    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return NULL;

    LockMutex(&(Parent->Mutex), INFINITY);

    for (Node = Parent->Children != NULL ? Parent->Children->First : NULL; Node != NULL; Node = Node->Next) {
        if ((LPWINDOW)Node != This) continue;
        Node = Node->Next;
        while (Node != NULL) {
            if (((LPWINDOW)Node)->TypeID == KOID_WINDOW) {
                UnlockMutex(&(Parent->Mutex));
                return (HANDLE)Node;
            }
            Node = Node->Next;
        }
        break;
    }

    UnlockMutex(&(Parent->Mutex));
    return NULL;
}

/***************************************************************************/

/**
 * @brief Retrieve the previous sibling of one window.
 * @param Handle Window handle.
 * @return Previous sibling handle or NULL.
 */
HANDLE GetPreviousWindowSibling(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPWINDOW Parent;
    LPLISTNODE Node;
    LPWINDOW PreviousWindow = NULL;

    if (This == NULL || This->TypeID != KOID_WINDOW) return NULL;

    Parent = (LPWINDOW)GetWindowParent(Handle);
    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return NULL;

    LockMutex(&(Parent->Mutex), INFINITY);

    for (Node = Parent->Children != NULL ? Parent->Children->First : NULL; Node != NULL; Node = Node->Next) {
        if ((LPWINDOW)Node == This) {
            UnlockMutex(&(Parent->Mutex));
            return (HANDLE)PreviousWindow;
        }
        if (((LPWINDOW)Node)->TypeID == KOID_WINDOW) {
            PreviousWindow = (LPWINDOW)Node;
        }
    }

    UnlockMutex(&(Parent->Mutex));
    return NULL;
}

/***************************************************************************/
