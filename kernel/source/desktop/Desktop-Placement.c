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


    Desktop window placement resolver

\************************************************************************/

#include "Desktop-Private.h"
#include "Desktop.h"
#include "Kernel.h"
#include "utils/Graphics-Utils.h"

/***************************************************************************/

/**
 * @brief Resolve whether one sibling reserves placement against its siblings.
 * @param WindowSibling Candidate sibling window.
 * @return TRUE when sibling publishes one reserved placement rectangle.
 */
static BOOL WindowReservesSiblingPlacement(LPWINDOW WindowSibling) {
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (WindowSibling == NULL || WindowSibling->TypeID != KOID_WINDOW) return FALSE;
    if (GetWindowStateSnapshot(WindowSibling, &Snapshot) == FALSE) return FALSE;
    if ((Snapshot.Style & EWS_VISIBLE) == 0) return FALSE;

    return ((Snapshot.Style & EWS_EXCLUDE_SIBLING_PLACEMENT) != 0);
}

/***************************************************************************/

/**
 * @brief Resolve whether one rectangle spans the full width of another one.
 * @param Rect Candidate rectangle.
 * @param Container Container rectangle.
 * @return TRUE when width is fully spanned.
 */
static BOOL RectSpansHorizontalRange(LPRECT Rect, LPRECT Container) {
    if (Rect == NULL || Container == NULL) return FALSE;

    return (Rect->X1 <= Container->X1 && Rect->X2 >= Container->X2);
}

/***************************************************************************/

/**
 * @brief Resolve whether one rectangle spans the full height of another one.
 * @param Rect Candidate rectangle.
 * @param Container Container rectangle.
 * @return TRUE when height is fully spanned.
 */
static BOOL RectSpansVerticalRange(LPRECT Rect, LPRECT Container) {
    if (Rect == NULL || Container == NULL) return FALSE;

    return (Rect->Y1 <= Container->Y1 && Rect->Y2 >= Container->Y2);
}

/***************************************************************************/

/**
 * @brief Shrink one placement rectangle around one reserved sibling edge band.
 * @param PlacementRect In-out placement rectangle.
 * @param ReservedRect Reserved sibling rectangle.
 * @return TRUE when the placement rectangle changed.
 */
static BOOL ShrinkPlacementRectFromReservedSibling(LPRECT PlacementRect, LPRECT ReservedRect) {
    BOOL Changed = FALSE;

    if (PlacementRect == NULL || ReservedRect == NULL) return FALSE;

    if (RectSpansHorizontalRange(ReservedRect, PlacementRect) != FALSE) {
        if (ReservedRect->Y1 <= PlacementRect->Y1 && ReservedRect->Y2 >= PlacementRect->Y1) {
            PlacementRect->Y1 = ReservedRect->Y2 + 1;
            Changed = TRUE;
        } else if (ReservedRect->Y2 >= PlacementRect->Y2 && ReservedRect->Y1 <= PlacementRect->Y2) {
            PlacementRect->Y2 = ReservedRect->Y1 - 1;
            Changed = TRUE;
        }
    }

    if (RectSpansVerticalRange(ReservedRect, PlacementRect) != FALSE) {
        if (ReservedRect->X1 <= PlacementRect->X1 && ReservedRect->X2 >= PlacementRect->X1) {
            PlacementRect->X1 = ReservedRect->X2 + 1;
            Changed = TRUE;
        } else if (ReservedRect->X2 >= PlacementRect->X2 && ReservedRect->X1 <= PlacementRect->X2) {
            PlacementRect->X2 = ReservedRect->X1 - 1;
            Changed = TRUE;
        }
    }

    return Changed;
}

/***************************************************************************/

/**
 * @brief Resolve one base placement rectangle inside one parent.
 * @param Window Target window.
 * @param Parent Parent window.
 * @param PlacementRect Receives base placement rectangle.
 * @return TRUE on success.
 */
static BOOL ResolveBasePlacementRect(LPWINDOW Window, LPWINDOW Parent, LPRECT PlacementRect) {
    WINDOW_STATE_SNAPSHOT WindowSnapshot;
    WINDOW_STATE_SNAPSHOT ParentSnapshot;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return FALSE;
    if (PlacementRect == NULL) return FALSE;
    if (GetWindowStateSnapshot(Window, &WindowSnapshot) == FALSE) return FALSE;
    if (GetWindowStateSnapshot(Parent, &ParentSnapshot) == FALSE) return FALSE;

    if ((WindowSnapshot.Status & WINDOW_STATUS_BYPASS_PARENT_WORK_RECT) != 0) {
        PlacementRect->X1 = 0;
        PlacementRect->Y1 = 0;
        PlacementRect->X2 = ParentSnapshot.Rect.X2 - ParentSnapshot.Rect.X1;
        PlacementRect->Y2 = ParentSnapshot.Rect.Y2 - ParentSnapshot.Rect.Y1;
        return TRUE;
    }

    return GetWindowEffectiveWorkRectSnapshot(Parent, PlacementRect);
}

/***************************************************************************/

/**
 * @brief Resolve one effective placement rectangle against reserved siblings.
 * @param Window Target window.
 * @param Parent Parent window.
 * @param PlacementRect Receives effective placement rectangle.
 * @return TRUE on success.
 */
static BOOL ResolveEffectivePlacementRect(LPWINDOW Window, LPWINDOW Parent, LPRECT PlacementRect) {
    LPWINDOW* Siblings;
    WINDOW_STATE_SNAPSHOT SiblingSnapshot;
    LPWINDOW Sibling;
    UINT Count;
    UINT Index;
    UINT Pass;
    BOOL Changed;

    if (ResolveBasePlacementRect(Window, Parent, PlacementRect) == FALSE) return FALSE;

    Count = 0;
    Siblings = NULL;
    if (DesktopSnapshotWindowChildren(Parent, &Siblings, &Count) == FALSE) return FALSE;

    for (Pass = 0; Pass < Count + 1; Pass++) {
        Changed = FALSE;

        for (Index = 0; Index < Count; Index++) {
            Sibling = Siblings[Index];

            if (Sibling == NULL || Sibling->TypeID != KOID_WINDOW || Sibling == Window) continue;
            if (WindowReservesSiblingPlacement(Sibling) == FALSE) continue;
            if (GetWindowStateSnapshot(Sibling, &SiblingSnapshot) == FALSE) continue;

            if (ShrinkPlacementRectFromReservedSibling(PlacementRect, &(SiblingSnapshot.Rect)) != FALSE) {
                Changed = TRUE;
            }
        }

        if (Changed == FALSE) break;
    }

    if (Siblings != NULL) {
        KernelHeapFree(Siblings);
    }

    return (PlacementRect->X1 <= PlacementRect->X2 && PlacementRect->Y1 <= PlacementRect->Y2);
}

/***************************************************************************/

/**
 * @brief Resolve whether one candidate rectangle intersects reserved siblings.
 * @param Window Target window.
 * @param Parent Parent window.
 * @param CandidateRect Candidate window rectangle.
 * @return TRUE when candidate is clear of reserved sibling rectangles.
 */
static BOOL ValidateReservedSiblingIntersections(LPWINDOW Window, LPWINDOW Parent, LPRECT CandidateRect) {
    LPWINDOW* Siblings;
    WINDOW_STATE_SNAPSHOT SiblingSnapshot;
    RECT Intersection;
    LPWINDOW Sibling;
    UINT Count;
    UINT Index;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return FALSE;
    if (CandidateRect == NULL) return FALSE;

    Count = 0;
    Siblings = NULL;
    if (DesktopSnapshotWindowChildren(Parent, &Siblings, &Count) == FALSE) return FALSE;

    for (Index = 0; Index < Count; Index++) {
        Sibling = Siblings[Index];

        if (Sibling == NULL || Sibling->TypeID != KOID_WINDOW || Sibling == Window) continue;
        if (WindowReservesSiblingPlacement(Sibling) == FALSE) continue;
        if (GetWindowStateSnapshot(Sibling, &SiblingSnapshot) == FALSE) continue;
        if (IntersectRect(CandidateRect, &(SiblingSnapshot.Rect), &Intersection) != FALSE) {
            if (Siblings != NULL) {
                KernelHeapFree(Siblings);
            }
            return FALSE;
        }
    }

    if (Siblings != NULL) {
        KernelHeapFree(Siblings);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve one candidate window rectangle through the shared placement path.
 * @param Window Target window.
 * @param WindowRect In-out candidate window rectangle in parent coordinates.
 * @return TRUE on success.
 */
BOOL DesktopResolveWindowPlacementRect(LPWINDOW Window, LPRECT WindowRect) {
    WINDOW_STATE_SNAPSHOT Snapshot;
    RECT PlacementRect;
    I32 Width;
    I32 Height;
    LPWINDOW Parent;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (WindowRect == NULL) return FALSE;
    if (WindowRect->X1 > WindowRect->X2 || WindowRect->Y1 > WindowRect->Y2) return FALSE;
    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return FALSE;

    Parent = Snapshot.ParentWindow;
    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return TRUE;
    if (ResolveEffectivePlacementRect(Window, Parent, &PlacementRect) == FALSE) return FALSE;

    Width = WindowRect->X2 - WindowRect->X1 + 1;
    Height = WindowRect->Y2 - WindowRect->Y1 + 1;
    if (Width <= 0 || Height <= 0) return FALSE;

    if (Width > (PlacementRect.X2 - PlacementRect.X1 + 1)) {
        WindowRect->X1 = PlacementRect.X1;
        WindowRect->X2 = PlacementRect.X2;
    } else {
        if (WindowRect->X1 < PlacementRect.X1) {
            WindowRect->X1 = PlacementRect.X1;
            WindowRect->X2 = WindowRect->X1 + Width - 1;
        }
        if (WindowRect->X2 > PlacementRect.X2) {
            WindowRect->X2 = PlacementRect.X2;
            WindowRect->X1 = WindowRect->X2 - Width + 1;
        }
    }

    if (Height > (PlacementRect.Y2 - PlacementRect.Y1 + 1)) {
        WindowRect->Y1 = PlacementRect.Y1;
        WindowRect->Y2 = PlacementRect.Y2;
    } else {
        if (WindowRect->Y1 < PlacementRect.Y1) {
            WindowRect->Y1 = PlacementRect.Y1;
            WindowRect->Y2 = WindowRect->Y1 + Height - 1;
        }
        if (WindowRect->Y2 > PlacementRect.Y2) {
            WindowRect->Y2 = PlacementRect.Y2;
            WindowRect->Y1 = WindowRect->Y2 - Height + 1;
        }
    }

    return ValidateReservedSiblingIntersections(Window, Parent, WindowRect);
}
