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

#include "Desktop.h"
#include "utils/Graphics-Utils.h"

/************************************************************************/

/**
 * @brief Invalidate one screen rectangle across one visible window subtree.
 * @param Window Root window.
 * @param ScreenRect Rectangle in screen coordinates.
 * @param SkipCurrent TRUE to skip invalidating @p Window itself.
 */
static void DesktopOverlayInvalidateWindowTreeRectInternal(LPWINDOW Window, LPRECT ScreenRect, BOOL SkipCurrent) {
    LPLISTNODE ChildNode;
    LPWINDOW Child;
    RECT WindowScreenRect;
    RECT Intersection;
    RECT WindowRect;
    BOOL IsVisible = FALSE;
    BOOL HasIntersection = FALSE;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;
    if (ScreenRect == NULL) return;

    LockMutex(&(Window->Mutex), INFINITY);

    IsVisible = ((Window->Status & WINDOW_STATUS_VISIBLE) != 0);
    WindowScreenRect = Window->ScreenRect;

    for (ChildNode = Window->Children != NULL ? Window->Children->First : NULL; ChildNode != NULL; ChildNode = ChildNode->Next) {
        Child = (LPWINDOW)ChildNode;
        if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;
        DesktopOverlayInvalidateWindowTreeRectInternal(Child, ScreenRect, FALSE);
    }

    if (SkipCurrent == FALSE && IsVisible != FALSE) {
        if (IntersectRect(&WindowScreenRect, ScreenRect, &Intersection) != FALSE) {
            ScreenRectToWindowLocalRect(&WindowScreenRect, &Intersection, &WindowRect);
            HasIntersection = TRUE;
        }
    }

    UnlockMutex(&(Window->Mutex));

    if (HasIntersection != FALSE) {
        (void)InvalidateWindowRect((HANDLE)Window, &WindowRect);
    }
}

/************************************************************************/

void DesktopOverlayInvalidateWindowTreeRect(LPWINDOW Window, LPRECT ScreenRect, BOOL SkipCurrent) {
    DesktopOverlayInvalidateWindowTreeRectInternal(Window, ScreenRect, SkipCurrent);
}

/************************************************************************/

BOOL DesktopOverlayInvalidateRootRect(LPWINDOW RootWindow, LPRECT ScreenRect) {
    RECT RootScreenRect;
    RECT Intersection;
    RECT RootWindowRect;
    BOOL IsVisible = FALSE;
    BOOL HasIntersection = FALSE;

    if (RootWindow == NULL || RootWindow->TypeID != KOID_WINDOW) return FALSE;
    if (ScreenRect == NULL) return FALSE;

    LockMutex(&(RootWindow->Mutex), INFINITY);

    IsVisible = ((RootWindow->Status & WINDOW_STATUS_VISIBLE) != 0);
    RootScreenRect = RootWindow->ScreenRect;

    if (IsVisible != FALSE) {
        if (IntersectRect(&RootScreenRect, ScreenRect, &Intersection) != FALSE) {
            ScreenRectToWindowLocalRect(&RootScreenRect, &Intersection, &RootWindowRect);
            HasIntersection = TRUE;
        }
    }

    UnlockMutex(&(RootWindow->Mutex));

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
    (void)DesktopOverlayInvalidateRootRect(RootWindow, ScreenRect);
}

/************************************************************************/
