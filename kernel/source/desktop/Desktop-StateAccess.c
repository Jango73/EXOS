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


    Desktop state access helpers

\************************************************************************/

#include "Desktop-Private.h"
#include "Desktop.h"
#include "Kernel.h"

/***************************************************************************/

/**
 * @brief Snapshot one window state under its owner mutex.
 * @param Window Source window.
 * @param Snapshot Receives state snapshot.
 * @return TRUE on success.
 */
BOOL GetWindowStateSnapshot(LPWINDOW Window, LPWINDOW_STATE_SNAPSHOT Snapshot) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Snapshot == NULL) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    Snapshot->Rect = Window->Rect;
    Snapshot->ScreenRect = Window->ScreenRect;
    Snapshot->WorkRect = Window->WorkRect;
    Snapshot->Style = Window->Style;
    Snapshot->Status = Window->Status;
    Snapshot->Level = Window->Level;
    Snapshot->Order = Window->Order;
    Snapshot->ParentWindow = Window->ParentWindow;
    Snapshot->Task = Window->Task;
    Snapshot->Class = Window->Class;
    Snapshot->HasWorkRect = ((Window->Status & WINDOW_STATUS_HAS_WORK_RECT) != 0);
    UnlockMutex(&(Window->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Snapshot one window z-order value under owner mutex.
 * @param Window Source window.
 * @param Order Receives window order.
 * @return TRUE on success.
 */
BOOL GetWindowOrderSnapshot(LPWINDOW Window, I32* Order) {
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (Order == NULL) return FALSE;
    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return FALSE;

    *Order = Snapshot.Order;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Snapshot one window level under owner mutex.
 * @param Window Source window.
 * @param Level Receives window level.
 * @return TRUE on success.
 */
BOOL GetWindowLevelSnapshot(LPWINDOW Window, U32* Level) {
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (Level == NULL) return FALSE;
    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return FALSE;

    *Level = Snapshot.Level;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Snapshot one effective work rectangle under owner mutex.
 * @param Window Source window.
 * @param WorkRect Receives effective work rectangle.
 * @return TRUE on success.
 */
BOOL GetWindowEffectiveWorkRectSnapshot(LPWINDOW Window, LPRECT WorkRect) {
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (WorkRect == NULL) return FALSE;
    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return FALSE;

    if (Snapshot.HasWorkRect != FALSE) {
        *WorkRect = Snapshot.WorkRect;
    } else {
        WorkRect->X1 = 0;
        WorkRect->Y1 = 0;
        WorkRect->X2 = Snapshot.Rect.X2 - Snapshot.Rect.X1;
        WorkRect->Y2 = Snapshot.Rect.Y2 - Snapshot.Rect.Y1;
    }

    return TRUE;
}

BOOL GetWindowDrawContextSnapshot(LPWINDOW Window, LPWINDOW_DRAW_CONTEXT_SNAPSHOT Snapshot) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Snapshot == NULL) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    Snapshot->SurfaceRect = Window->DrawSurfaceRect;
    Snapshot->ClipRect = Window->DrawClipRect;
    Snapshot->Origin = Window->DrawOrigin;
    Snapshot->Flags = Window->DrawContextFlags;
    UnlockMutex(&(Window->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Snapshot direct children of one window under owner mutex.
 * @param Parent Parent window.
 * @param Children Receives allocated child pointer array.
 * @param ChildCount Receives number of children.
 * @return TRUE on success.
 */
BOOL DesktopSnapshotWindowChildren(LPWINDOW Parent, LPWINDOW** Children, UINT* ChildCount) {
    LPLISTNODE Node;
    LPWINDOW* Snapshot;
    UINT Index = 0;
    UINT Count = 0;

    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return FALSE;
    if (Children == NULL || ChildCount == NULL) return FALSE;

    *Children = NULL;
    *ChildCount = 0;

    LockMutex(&(Parent->Mutex), INFINITY);

    if (Parent->Children != NULL) {
        Count = Parent->Children->NumItems;
    }

    if (Count == 0) {
        UnlockMutex(&(Parent->Mutex));
        return TRUE;
    }

    Snapshot = (LPWINDOW*)KernelHeapAlloc(sizeof(LPWINDOW) * Count);
    if (Snapshot == NULL) {
        UnlockMutex(&(Parent->Mutex));
        return FALSE;
    }

    for (Node = Parent->Children->First; Node != NULL && Index < Count; Node = Node->Next) {
        Snapshot[Index++] = (LPWINDOW)Node;
    }

    UnlockMutex(&(Parent->Mutex));

    *Children = Snapshot;
    *ChildCount = Index;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Consume one window dirty region under owner mutex.
 * @param Window Source window.
 * @param ClipRegion Destination clip region.
 * @param ClipStorage Backing storage for the region.
 * @param ClipCapacity Storage capacity.
 * @param ScreenRect Receives window screen rectangle.
 * @param Order Receives window order.
 * @param ParentWindow Receives parent window.
 * @return TRUE on success.
 */
BOOL DesktopConsumeWindowDirtyRegionSnapshot(
    LPWINDOW Window,
    LPRECT_REGION ClipRegion,
    LPRECT ClipStorage,
    UINT ClipCapacity,
    LPRECT ScreenRect,
    I32* Order,
    LPWINDOW* ParentWindow) {
    RECT DirtyRect;
    UINT DirtyCount;
    UINT DirtyIndex;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ClipRegion == NULL || ScreenRect == NULL || Order == NULL || ParentWindow == NULL) return FALSE;
    if (RectRegionInit(ClipRegion, ClipStorage, ClipCapacity) == FALSE) return FALSE;

    RectRegionReset(ClipRegion);

    LockMutex(&(Window->Mutex), INFINITY);

    *ScreenRect = Window->ScreenRect;
    *Order = Window->Order;
    *ParentWindow = Window->ParentWindow;

    if (Window->DirtyRegion.Storage != Window->DirtyRects ||
        Window->DirtyRegion.Capacity != WINDOW_DIRTY_REGION_CAPACITY) {
        (void)RectRegionInit(&Window->DirtyRegion, Window->DirtyRects, WINDOW_DIRTY_REGION_CAPACITY);
    }

    DirtyCount = RectRegionGetCount(&Window->DirtyRegion);
    if (DirtyCount == 0) {
        (void)RectRegionAddRect(ClipRegion, ScreenRect);
    } else {
        for (DirtyIndex = 0; DirtyIndex < DirtyCount; DirtyIndex++) {
            if (RectRegionGetRect(&Window->DirtyRegion, DirtyIndex, &DirtyRect) == FALSE) continue;

            if (DirtyRect.X1 < ScreenRect->X1) DirtyRect.X1 = ScreenRect->X1;
            if (DirtyRect.Y1 < ScreenRect->Y1) DirtyRect.Y1 = ScreenRect->Y1;
            if (DirtyRect.X2 > ScreenRect->X2) DirtyRect.X2 = ScreenRect->X2;
            if (DirtyRect.Y2 > ScreenRect->Y2) DirtyRect.Y2 = ScreenRect->Y2;
            if (DirtyRect.X1 > DirtyRect.X2 || DirtyRect.Y1 > DirtyRect.Y2) continue;

            (void)RectRegionAddRect(ClipRegion, &DirtyRect);
        }
    }

    if (RectRegionIsOverflowed(&Window->DirtyRegion) || RectRegionIsOverflowed(ClipRegion)) {
        RectRegionReset(ClipRegion);
        (void)RectRegionAddRect(ClipRegion, ScreenRect);
    }

    if (RectRegionGetCount(ClipRegion) == 0) {
        (void)RectRegionAddRect(ClipRegion, ScreenRect);
    }

    RectRegionReset(&Window->DirtyRegion);

    UnlockMutex(&(Window->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Attach one child to one parent under the parent owner mutex.
 * @param Parent Parent window.
 * @param Child Child window.
 * @return TRUE on success.
 */
BOOL DesktopAttachWindowChild(LPWINDOW Parent, LPWINDOW Child) {
    LPLISTNODE Node;
    LPWINDOW Sibling;

    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return FALSE;
    if (Child == NULL || Child->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Parent->Mutex), INFINITY);

    for (Node = Parent->Children != NULL ? Parent->Children->First : NULL; Node != NULL; Node = Node->Next) {
        Sibling = (LPWINDOW)Node;
        if (Sibling == NULL || Sibling->TypeID != KOID_WINDOW) continue;

        LockMutex(&(Sibling->Mutex), INFINITY);
        Sibling->Order++;
        UnlockMutex(&(Sibling->Mutex));
    }

    LockMutex(&(Child->Mutex), INFINITY);
    Child->Order = 0;
    UnlockMutex(&(Child->Mutex));

    ListAddHead(Parent->Children, Child);

    UnlockMutex(&(Parent->Mutex));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Detach one child from one parent under the parent owner mutex.
 * @param Parent Parent window.
 * @param Child Child window.
 * @return TRUE on success.
 */
BOOL DesktopDetachWindowChild(LPWINDOW Parent, LPWINDOW Child) {
    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return FALSE;
    if (Child == NULL || Child->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Parent->Mutex), INFINITY);
    ListRemove(Parent->Children, Child);
    UnlockMutex(&(Parent->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Set one owner task on one window under the window owner mutex.
 * @param Window Target window.
 * @param Task New owner task.
 * @return TRUE on success.
 */
BOOL DesktopSetWindowTask(LPWINDOW Window, LPTASK Task) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Task == NULL || Task->TypeID != KOID_TASK) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    Window->Task = Task;
    UnlockMutex(&(Window->Mutex));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Set one window visible state under the window owner mutex.
 * @param Window Target window.
 * @param ShowHide TRUE to show, FALSE to hide.
 * @return TRUE on success.
 */
BOOL DesktopSetWindowVisibleState(LPWINDOW Window, BOOL ShowHide) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    if (ShowHide != FALSE) {
        Window->Style |= EWS_VISIBLE;
        Window->Status |= WINDOW_STATUS_VISIBLE;
    } else {
        Window->Style &= ~EWS_VISIBLE;
        Window->Status &= ~WINDOW_STATUS_VISIBLE;
    }
    UnlockMutex(&(Window->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Set one masked window style state under the window owner mutex.
 * @param Window Target window.
 * @param StyleMask Style bits to update.
 * @param Enabled TRUE to set bits, FALSE to clear bits.
 * @return TRUE on success.
 */
BOOL DesktopSetWindowStyleState(LPWINDOW Window, U32 StyleMask, BOOL Enabled) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (StyleMask == 0) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    if (Enabled != FALSE) {
        Window->Style |= StyleMask;
    } else {
        Window->Style &= ~StyleMask;
    }
    UnlockMutex(&(Window->Mutex));

    return TRUE;
}

/***************************************************************************/

BOOL DesktopGetRootWindow(LPDESKTOP Desktop, LPWINDOW* RootWindow) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (RootWindow == NULL) return FALSE;

    LockMutex(&(Desktop->Mutex), INFINITY);
    *RootWindow = Desktop->Window;
    UnlockMutex(&(Desktop->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Clear desktop references targeting one window under desktop owner mutex.
 * @param Desktop Target desktop.
 * @param Window Window being released.
 * @return TRUE on success.
 */
BOOL DesktopClearWindowReferences(LPDESKTOP Desktop, LPWINDOW Window) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Desktop->Mutex), INFINITY);
    if (Desktop->Capture == Window) Desktop->Capture = NULL;
    if (Desktop->Focus == Window) Desktop->Focus = NULL;
    UnlockMutex(&(Desktop->Mutex));

    return TRUE;
}
