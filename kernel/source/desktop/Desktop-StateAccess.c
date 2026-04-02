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
#include "text/CoreString.h"
#include "Kernel.h"

/***************************************************************************/

/**
 * @brief Refresh one subtree effective visibility from one ancestor visibility.
 * @param Window Subtree root.
 * @param AncestorVisible TRUE when all ancestors are effectively visible.
 * @return TRUE on success.
 */
static BOOL DesktopRefreshWindowEffectiveVisibilityTreeInternal(LPWINDOW Window, BOOL AncestorVisible) {
    LPWINDOW* Children;
    LPWINDOW ChildWindow;
    UINT ChildCount;
    UINT ChildIndex;
    BOOL RequestedVisible;
    BOOL EffectiveVisible;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    RequestedVisible = ((Window->Style & EWS_VISIBLE) != 0);
    EffectiveVisible = (AncestorVisible != FALSE && RequestedVisible != FALSE);
    if (EffectiveVisible != FALSE) {
        Window->Status |= WINDOW_STATUS_VISIBLE;
    } else {
        Window->Status &= ~WINDOW_STATUS_VISIBLE;
    }
    UnlockMutex(&(Window->Mutex));

    Children = NULL;
    ChildCount = 0;
    if (DesktopSnapshotWindowChildren(Window, &Children, &ChildCount) == FALSE) return FALSE;

    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        ChildWindow = Children[ChildIndex];
        if (ChildWindow == NULL || ChildWindow->TypeID != KOID_WINDOW) continue;
        (void)DesktopRefreshWindowEffectiveVisibilityTreeInternal(ChildWindow, EffectiveVisible);
    }

    if (Children != NULL) {
        KernelHeapFree(Children);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Recalculate one parent child order list from sibling z-order styles.
 * @param Parent Parent window, already locked.
 */
static void DesktopRecalculateParentChildOrdersLocked(LPWINDOW Parent) {
    LPLISTNODE Node;
    LPWINDOW Child;
    I32 NextOrder;
    U32 Pass;

    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return;
    if (Parent->Children == NULL) return;

    NextOrder = 0;
    for (Pass = 0; Pass < 3; Pass++) {
        for (Node = Parent->Children->First; Node != NULL; Node = Node->Next) {
            Child = (LPWINDOW)Node;
            if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;

            LockMutex(&(Child->Mutex), INFINITY);
            if (Pass == 0 && (Child->Style & EWS_ALWAYS_IN_FRONT) != 0) {
                Child->Order = NextOrder++;
            } else if (Pass == 1 &&
                       (Child->Style & (EWS_ALWAYS_IN_FRONT | EWS_ALWAYS_AT_BOTTOM)) == 0) {
                Child->Order = NextOrder++;
            } else if (Pass == 2 && (Child->Style & EWS_ALWAYS_AT_BOTTOM) != 0) {
                Child->Order = NextOrder++;
            }
            UnlockMutex(&(Child->Mutex));
        }
    }

    ListSort(Parent->Children, SortWindows_Order);
}

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
    Snapshot->ContentTransparencyHint = Window->ContentTransparencyHint;
    Snapshot->Level = Window->Level;
    Snapshot->Order = Window->Order;
    Snapshot->ParentWindow = Window->ParentWindow;
    Snapshot->Task = Window->Task;
    Snapshot->Class = Window->Class;
    Snapshot->HasWorkRect = ((Window->Status & WINDOW_STATUS_HAS_WORK_RECT) != 0);
    StringCopyLimit(Snapshot->Caption, Window->Caption, MAX_WINDOW_CAPTION);
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
    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return FALSE;
    if (Child == NULL || Child->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Parent->Mutex), INFINITY);
    ListAddHead(Parent->Children, Child);
    DesktopRecalculateParentChildOrdersLocked(Parent);

    UnlockMutex(&(Parent->Mutex));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Re-sort one parent child list after one z-order style change.
 * @param Window Window whose parent child list must be re-sorted.
 * @return TRUE on success.
 */
BOOL DesktopRefreshWindowZOrder(LPWINDOW Window) {
    LPWINDOW Parent;
    LPWINDOW* Children;
    UINT ChildCount;
    UINT ChildIndex;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    Parent = Window->ParentWindow;
    UnlockMutex(&(Window->Mutex));

    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return TRUE;

    LockMutex(&(Parent->Mutex), INFINITY);
    DesktopRecalculateParentChildOrdersLocked(Parent);
    UnlockMutex(&(Parent->Mutex));

    (void)RequestWindowDraw((HANDLE)Parent);

    Children = NULL;
    ChildCount = 0;
    if (DesktopSnapshotWindowChildren(Parent, &Children, &ChildCount) == FALSE) return FALSE;

    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        if (Children[ChildIndex] == NULL || Children[ChildIndex]->TypeID != KOID_WINDOW) continue;
        (void)RequestWindowDraw((HANDLE)Children[ChildIndex]);
    }

    if (Children != NULL) {
        KernelHeapFree(Children);
    }

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
    } else {
        Window->Style &= ~EWS_VISIBLE;
    }
    UnlockMutex(&(Window->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Refresh one window subtree effective visibility from style and ancestry.
 * @param Window Subtree root.
 * @return TRUE on success.
 */
BOOL DesktopRefreshWindowEffectiveVisibilityTree(LPWINDOW Window) {
    WINDOW_STATE_SNAPSHOT Snapshot;
    WINDOW_STATE_SNAPSHOT ParentSnapshot;
    BOOL AncestorVisible = TRUE;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return FALSE;

    if (Snapshot.ParentWindow != NULL && Snapshot.ParentWindow->TypeID == KOID_WINDOW) {
        if (GetWindowStateSnapshot(Snapshot.ParentWindow, &ParentSnapshot) == FALSE) return FALSE;
        AncestorVisible = ((ParentSnapshot.Status & WINDOW_STATUS_VISIBLE) != 0);
    }

    return DesktopRefreshWindowEffectiveVisibilityTreeInternal(Window, AncestorVisible);
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

/**
 * @brief Set one content transparency hint under the owner mutex.
 * @param Window Target window.
 * @param Hint One WINDOW_CONTENT_TRANSPARENCY_HINT_* value.
 * @return TRUE on success.
 */
BOOL DesktopSetWindowContentTransparencyHint(LPWINDOW Window, U32 Hint) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Hint > WINDOW_CONTENT_TRANSPARENCY_HINT_TRANSPARENT) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    Window->ContentTransparencyHint = Hint;
    UnlockMutex(&(Window->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Set one resolved transparency state under the owner mutex.
 * @param Window Target window.
 * @param Enabled TRUE when the resolved clear path left content transparent.
 * @return TRUE on success.
 */
BOOL DesktopSetWindowResolvedTransparencyState(LPWINDOW Window, BOOL Enabled) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    if (Enabled != FALSE) {
        Window->Status |= WINDOW_STATUS_CONTENT_TRANSPARENT;
    } else {
        Window->Status &= ~WINDOW_STATUS_CONTENT_TRANSPARENT;
    }
    UnlockMutex(&(Window->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Update one window caption under the window owner mutex.
 * @param Window Target window.
 * @param Caption New caption text, or NULL for an empty caption.
 * @return TRUE on success.
 */
BOOL DesktopSetWindowCaption(LPWINDOW Window, LPCSTR Caption) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    StringCopyLimit(Window->Caption, Caption != NULL ? Caption : TEXT(""), MAX_WINDOW_CAPTION);
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
 * @brief Snapshot the focused window of one desktop under desktop owner mutex.
 * @param Desktop Target desktop.
 * @param FocusWindow Receives the focused window pointer, or NULL.
 * @return TRUE on success.
 */
BOOL DesktopGetFocusWindow(LPDESKTOP Desktop, LPWINDOW* FocusWindow) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (FocusWindow == NULL) return FALSE;

    LockMutex(&(Desktop->Mutex), INFINITY);
    *FocusWindow = Desktop->Focus;
    UnlockMutex(&(Desktop->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve the focusable ancestor for one clicked window.
 * @param Window Click target.
 * @return Focusable window, promoted to the direct desktop child when applicable.
 */
static LPWINDOW DesktopResolveFocusableWindow(LPWINDOW Window) {
    LPDESKTOP Desktop;
    LPWINDOW RootWindow = NULL;
    LPWINDOW Current;
    LPWINDOW Parent;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return NULL;

    Desktop = DesktopGetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return Window;
    if (DesktopGetRootWindow(Desktop, &RootWindow) == FALSE) return Window;
    if (RootWindow == NULL || RootWindow->TypeID != KOID_WINDOW) return Window;

    Current = Window;
    FOREVER {
        Parent = (LPWINDOW)GetWindowParent((HANDLE)Current);
        if (Parent == NULL || Parent->TypeID != KOID_WINDOW) break;
        if (Parent == RootWindow) break;
        Current = Parent;
    }

    return Current;
}

/***************************************************************************/

/**
 * @brief Focus one window on its desktop and synchronize focused process.
 * @param Window Window that should receive focus.
 * @return TRUE on success.
 */
BOOL DesktopSetFocusWindow(LPWINDOW Window) {
    LPDESKTOP Desktop;
    LPWINDOW FocusWindow;
    LPWINDOW PreviousFocus = NULL;
    LPPROCESS Process = NULL;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;

    Desktop = DesktopGetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    FocusWindow = DesktopResolveFocusableWindow(Window);
    if (FocusWindow == NULL || FocusWindow->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Desktop->Mutex), INFINITY);
    PreviousFocus = Desktop->Focus;
    Desktop->Focus = FocusWindow;
    UnlockMutex(&(Desktop->Mutex));

    SAFE_USE_VALID_ID(FocusWindow->Task, KOID_TASK) {
        SAFE_USE_VALID_ID(FocusWindow->Task->OwnerProcess, KOID_PROCESS) {
            Process = FocusWindow->Task->OwnerProcess;
        }
    }

    if (Process != NULL) {
        SetFocusedProcess(Process);
    }

    if (PreviousFocus != FocusWindow) {
        SAFE_USE_VALID_ID(PreviousFocus, KOID_WINDOW) {
            (void)RequestWindowDraw((HANDLE)PreviousFocus);
        }
        (void)RequestWindowDraw((HANDLE)FocusWindow);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Tell whether one window is the focused window of its desktop.
 * @param Window Target window.
 * @return TRUE when the desktop focus points to that window.
 */
BOOL IsDesktopWindowFocused(LPWINDOW Window) {
    LPDESKTOP Desktop;
    LPWINDOW FocusWindow = NULL;
    BOOL IsFocused = FALSE;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;

    Desktop = DesktopGetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    if (DesktopGetFocusWindow(Desktop, &FocusWindow) == FALSE) return FALSE;
    IsFocused = (DesktopResolveFocusableWindow(Window) == FocusWindow);

    return IsFocused;
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
    if (Desktop->LastMouseMoveTarget == Window) Desktop->LastMouseMoveTarget = NULL;
    if (Desktop->Focus == Window) Desktop->Focus = NULL;
    UnlockMutex(&(Desktop->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Get desktop last mouse move target for one window desktop.
 * @param Window Any window on the target desktop.
 * @param TargetWindow Receives last mouse move target (optional).
 * @return TRUE on success.
 */
BOOL GetDesktopLastMouseMoveTarget(LPWINDOW Window, LPWINDOW* TargetWindow) {
    LPDESKTOP Desktop;

    if (TargetWindow != NULL) *TargetWindow = NULL;

    Desktop = DesktopGetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    LockMutex(&(Desktop->Mutex), INFINITY);
    if (TargetWindow != NULL) *TargetWindow = Desktop->LastMouseMoveTarget;
    UnlockMutex(&(Desktop->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Set desktop last mouse move target for one window desktop.
 * @param Window Any window on the target desktop.
 * @param TargetWindow Target window or NULL.
 * @return TRUE on success.
 */
BOOL SetDesktopLastMouseMoveTarget(LPWINDOW Window, LPWINDOW TargetWindow) {
    LPDESKTOP Desktop;

    Desktop = DesktopGetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    LockMutex(&(Desktop->Mutex), INFINITY);
    Desktop->LastMouseMoveTarget = TargetWindow;
    UnlockMutex(&(Desktop->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve one window target under desktop owner mutex.
 * @param Desktop Desktop owning the tree.
 * @param Target Target handle to resolve.
 * @param Window Receives resolved window or NULL.
 * @return TRUE when the resolve path completed.
 */
BOOL DesktopResolveWindowTarget(LPDESKTOP Desktop, HANDLE Target, LPWINDOW* Window) {
    if (Window == NULL) return FALSE;
    *Window = NULL;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (Target == NULL) return FALSE;

    LockMutex(&(Desktop->Mutex), INFINITY);
    *Window = (LPWINDOW)DesktopContainsWindow(Desktop->Window, (LPWINDOW)Target);
    UnlockMutex(&(Desktop->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Mark one window dispatch begin state under owner mutex.
 * @param Window Target window.
 * @param Message Dispatched message.
 * @return TRUE on success.
 */
BOOL DesktopMarkWindowDispatchBegin(LPWINDOW Window, U32 Message) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    if (Message == EWM_DRAW) {
        Window->Status &= ~WINDOW_STATUS_NEED_DRAW;
        Window->Status |= WINDOW_STATUS_DRAWING;
    }
    UnlockMutex(&(Window->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Mark one window dispatch end state under owner mutex.
 * @param Window Target window.
 * @param Message Dispatched message.
 * @return TRUE on success.
 */
BOOL DesktopMarkWindowDispatchEnd(LPWINDOW Window, U32 Message) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    if (Message == EWM_DRAW) {
        Window->Status &= ~WINDOW_STATUS_DRAWING;
    }
    UnlockMutex(&(Window->Mutex));

    return TRUE;
}
