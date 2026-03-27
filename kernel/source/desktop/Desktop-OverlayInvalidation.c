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


    Desktop overlay invalidation helpers

\************************************************************************/

#include "Desktop-OverlayInvalidation.h"

#include "Desktop-Private.h"
#include "Desktop.h"
#include "Kernel.h"
#include "utils/Graphics-Utils.h"

/************************************************************************/

/**
 * @brief Invalidate one screen rectangle across one visible window subtree.
 * @param Window Root window.
 * @param ScreenRect Rectangle in screen coordinates.
 * @param SkipCurrent TRUE to skip invalidating @p Window itself.
 * @param FullWindow TRUE to invalidate the whole window when intersected.
 */
static void DesktopOverlayInvalidateWindowTreeRectInternal(LPWINDOW Window, LPRECT ScreenRect, BOOL SkipCurrent, BOOL FullWindow) {
    LPWINDOW* Children = NULL;
    LPWINDOW Child;
    RECT WindowScreenRect;
    RECT Intersection;
    RECT WindowRect;
    UINT ChildCount = 0;
    UINT ChildIndex;
    BOOL IsVisible = FALSE;
    BOOL HasIntersection = FALSE;
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;
    if (ScreenRect == NULL) return;

    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return;
    IsVisible = ((Snapshot.Status & WINDOW_STATUS_VISIBLE) != 0);
    WindowScreenRect = Snapshot.ScreenRect;

    if (SkipCurrent == FALSE && IsVisible != FALSE) {
        if (IntersectRect(&WindowScreenRect, ScreenRect, &Intersection) != FALSE) {
            if (FullWindow != FALSE) {
                WindowRect.X1 = 0;
                WindowRect.Y1 = 0;
                WindowRect.X2 = WindowScreenRect.X2 - WindowScreenRect.X1;
                WindowRect.Y2 = WindowScreenRect.Y2 - WindowScreenRect.Y1;
            } else {
                GraphicsScreenRectToWindowRect(&WindowScreenRect, &Intersection, &WindowRect);
            }
            HasIntersection = TRUE;
        }
    }

    if (HasIntersection != FALSE) {
        (void)InvalidateWindowRect((HANDLE)Window, &WindowRect);
    }

    (void)DesktopSnapshotWindowChildren(Window, &Children, &ChildCount);

    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        Child = Children[ChildIndex];
        if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;
        DesktopOverlayInvalidateWindowTreeRectInternal(Child, ScreenRect, FALSE, FullWindow);
    }

    if (Children != NULL) {
        KernelHeapFree(Children);
    }
}

/************************************************************************/

/**
 * @brief Invalidate root window only on parts visible from desktop background.
 * @param RootWindow Desktop root window.
 * @param ScreenRect Damage rectangle in screen coordinates.
 * @return TRUE when at least one root rectangle was invalidated.
 */
static BOOL DesktopOverlayInvalidateRootVisibleRemainderRect(LPWINDOW RootWindow, LPRECT ScreenRect) {
    RECT RootScreenRect;
    RECT RootIntersection;
    RECT RemainderStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT TempStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT_REGION RemainderRegion;
    RECT ChildRect;
    RECT RemainingRect;
    RECT RootWindowRect;
    LPWINDOW* Children = NULL;
    LPWINDOW Child;
    UINT RemainingIndex;
    UINT ChildCount = 0;
    UINT ChildIndex;
    BOOL IsVisible = FALSE;
    BOOL HasIntersection = FALSE;
    BOOL InvalidatedAny = FALSE;
    WINDOW_STATE_SNAPSHOT RootSnapshot;
    WINDOW_STATE_SNAPSHOT ChildSnapshot;

    if (RootWindow == NULL || RootWindow->TypeID != KOID_WINDOW) return FALSE;
    if (ScreenRect == NULL) return FALSE;
    if (RectRegionInit(&RemainderRegion, RemainderStorage, WINDOW_DIRTY_REGION_CAPACITY) == FALSE) return FALSE;
    RectRegionReset(&RemainderRegion);

    if (GetWindowStateSnapshot(RootWindow, &RootSnapshot) == FALSE) return FALSE;
    IsVisible = ((RootSnapshot.Status & WINDOW_STATUS_VISIBLE) != 0);
    RootScreenRect = RootSnapshot.ScreenRect;
    if (IsVisible != FALSE) {
        if (IntersectRect(&RootScreenRect, ScreenRect, &RootIntersection) != FALSE) {
            HasIntersection = TRUE;
            (void)RectRegionAddRect(&RemainderRegion, &RootIntersection);
        }
    }

    if (HasIntersection != FALSE) {
        (void)DesktopSnapshotWindowChildren(RootWindow, &Children, &ChildCount);
        for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
            Child = Children[ChildIndex];
            if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;
            if (GetWindowStateSnapshot(Child, &ChildSnapshot) == FALSE) continue;
            if ((ChildSnapshot.Status & WINDOW_STATUS_VISIBLE) == 0) continue;
            if ((ChildSnapshot.Status & WINDOW_STATUS_CONTENT_TRANSPARENT) != 0) continue;

            ChildRect = ChildSnapshot.ScreenRect;
            if (SubtractRectFromRegion(&RemainderRegion, &ChildRect, TempStorage, WINDOW_DIRTY_REGION_CAPACITY) == FALSE) {
                RectRegionReset(&RemainderRegion);
                break;
            }

            if (RectRegionGetCount(&RemainderRegion) == 0) {
                break;
            }
        }
    }
    if (Children != NULL) {
        KernelHeapFree(Children);
    }

    if (HasIntersection == FALSE) return FALSE;

    for (RemainingIndex = 0; RemainingIndex < RectRegionGetCount(&RemainderRegion); RemainingIndex++) {
        if (RectRegionGetRect(&RemainderRegion, RemainingIndex, &RemainingRect) == FALSE) continue;
        GraphicsScreenRectToWindowRect(&RootScreenRect, &RemainingRect, &RootWindowRect);
        if (InvalidateWindowRect((HANDLE)RootWindow, &RootWindowRect) != FALSE) {
            InvalidatedAny = TRUE;
        }
    }

    return InvalidatedAny;
}

/************************************************************************/

void DesktopOverlayInvalidateWindowTreeRect(LPWINDOW Window, LPRECT ScreenRect, BOOL SkipCurrent) {
    DesktopOverlayInvalidateWindowTreeRectInternal(Window, ScreenRect, SkipCurrent, FALSE);
}

BOOL DesktopOverlayInvalidateRootRect(LPWINDOW RootWindow, LPRECT ScreenRect) {
    RECT RootScreenRect;
    RECT Intersection;
    RECT RootWindowRect;
    BOOL IsVisible = FALSE;
    BOOL HasIntersection = FALSE;
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (RootWindow == NULL || RootWindow->TypeID != KOID_WINDOW) return FALSE;
    if (ScreenRect == NULL) return FALSE;

    if (GetWindowStateSnapshot(RootWindow, &Snapshot) == FALSE) return FALSE;
    IsVisible = ((Snapshot.Status & WINDOW_STATUS_VISIBLE) != 0);
    RootScreenRect = Snapshot.ScreenRect;

    if (IsVisible != FALSE) {
        if (IntersectRect(&RootScreenRect, ScreenRect, &Intersection) != FALSE) {
            GraphicsScreenRectToWindowRect(&RootScreenRect, &Intersection, &RootWindowRect);
            HasIntersection = TRUE;
        }
    }

    if (HasIntersection != FALSE) {
        return InvalidateWindowRect((HANDLE)RootWindow, &RootWindowRect);
    }

    return FALSE;
}

/************************************************************************/

void DesktopOverlayInvalidateWindowTreeThenRootRect(LPWINDOW RootWindow, LPRECT ScreenRect) {
    if (RootWindow == NULL || RootWindow->TypeID != KOID_WINDOW) return;
    if (ScreenRect == NULL) return;

    DesktopOverlayInvalidateWindowTreeRect(RootWindow, ScreenRect, TRUE);
    (void)DesktopOverlayInvalidateRootVisibleRemainderRect(RootWindow, ScreenRect);
}

/************************************************************************/
