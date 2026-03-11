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


    Desktop visible region helpers

\************************************************************************/

#include "Desktop-Private.h"
#include "Desktop.h"
#include "Kernel.h"
#include "utils/Graphics-Utils.h"

/************************************************************************/

/**
 * @brief Append one rectangle to one region when bounds are valid.
 * @param Region Destination region.
 * @param Rect Rectangle candidate.
 * @return TRUE on success.
 */
static BOOL DesktopVisibleRegionAppendRectIfValid(LPRECT_REGION Region, LPRECT Rect) {
    if (Region == NULL || Rect == NULL) return FALSE;
    if (Rect->X1 > Rect->X2 || Rect->Y1 > Rect->Y2) return TRUE;
    return RectRegionAddRect(Region, Rect);
}

/************************************************************************/

/**
 * @brief Subtract one occluder rectangle from one source rectangle.
 * @param Region Destination region receiving remaining parts.
 * @param Source Source rectangle.
 * @param Occluder Occluding rectangle.
 * @return TRUE on success.
 */
static BOOL DesktopVisibleRegionSubtractRectFromRect(LPRECT_REGION Region, LPRECT Source, LPRECT Occluder) {
    RECT Inter;
    RECT Piece;

    if (Region == NULL || Source == NULL || Occluder == NULL) return FALSE;

    if (IntersectRect(Source, Occluder, &Inter) == FALSE) {
        return DesktopVisibleRegionAppendRectIfValid(Region, Source);
    }

    Piece = (RECT){Source->X1, Source->Y1, Source->X2, Inter.Y1 - 1};
    if (DesktopVisibleRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece = (RECT){Source->X1, Inter.Y2 + 1, Source->X2, Source->Y2};
    if (DesktopVisibleRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece = (RECT){Source->X1, Inter.Y1, Inter.X1 - 1, Inter.Y2};
    if (DesktopVisibleRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece = (RECT){Inter.X2 + 1, Inter.Y1, Source->X2, Inter.Y2};
    if (DesktopVisibleRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Subtract one occluding rectangle from one clip region.
 * @param Region Region updated in place.
 * @param Occluder Occluding rectangle.
 * @param Capacity Temporary region storage capacity.
 * @return TRUE on success.
 */
BOOL DesktopVisibleRegionSubtractOccluder(LPRECT_REGION Region, LPRECT Occluder, UINT Capacity) {
    RECT ExistingStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT TempStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT_REGION TempRegion;
    RECT Existing;
    UINT Count;
    UINT Index;

    if (Region == NULL || Occluder == NULL) return FALSE;
    if (Capacity == 0 || Capacity > WINDOW_DIRTY_REGION_CAPACITY) Capacity = WINDOW_DIRTY_REGION_CAPACITY;
    if (RectRegionInit(&TempRegion, TempStorage, Capacity) == FALSE) return FALSE;
    RectRegionReset(&TempRegion);

    Count = RectRegionGetCount(Region);
    if (Count > Capacity) Count = Capacity;

    for (Index = 0; Index < Count; Index++) {
        if (RectRegionGetRect(Region, Index, &ExistingStorage[Index]) == FALSE) return FALSE;
    }

    for (Index = 0; Index < Count; Index++) {
        Existing = ExistingStorage[Index];
        if (DesktopVisibleRegionSubtractRectFromRect(&TempRegion, &Existing, Occluder) == FALSE) {
            RectRegionReset(Region);
            return FALSE;
        }
    }

    RectRegionReset(Region);
    Count = RectRegionGetCount(&TempRegion);
    for (Index = 0; Index < Count; Index++) {
        if (RectRegionGetRect(&TempRegion, Index, &Existing) == FALSE) return FALSE;
        if (RectRegionAddRect(Region, &Existing) == FALSE) return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Subtract one visible window subtree from one clip region.
 * @param Window Window subtree root.
 * @param Region Region to clip.
 * @param Capacity Temporary region storage capacity.
 */
void DesktopVisibleRegionSubtractVisibleWindowTree(LPWINDOW Window, LPRECT_REGION Region, UINT Capacity) {
    LPWINDOW* Children = NULL;
    LPWINDOW Child;
    RECT WindowRect;
    UINT ChildCount = 0;
    UINT ChildIndex;
    BOOL IsVisible = FALSE;
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (Window == NULL || Window->TypeID != KOID_WINDOW || Region == NULL) return;

    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return;
    IsVisible = ((Snapshot.Status & WINDOW_STATUS_VISIBLE) != 0);
    WindowRect = Snapshot.ScreenRect;
    (void)DesktopSnapshotWindowChildren(Window, &Children, &ChildCount);

    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        Child = Children[ChildIndex];
        if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;
        DesktopVisibleRegionSubtractVisibleWindowTree(Child, Region, Capacity);
    }

    if (Children != NULL) {
        KernelHeapFree(Children);
    }

    if (IsVisible != FALSE) {
        (void)DesktopVisibleRegionSubtractOccluder(Region, &WindowRect, Capacity);
    }
}

/************************************************************************/

/**
 * @brief Build one visible region for one window from one base screen rectangle.
 * @param Window Target window.
 * @param BaseRect Base screen rectangle.
 * @param ExcludeTargetChildren TRUE to subtract visible child subtrees of the target window.
 * @param Region Output region.
 * @param Storage Region storage.
 * @param Capacity Region storage capacity.
 * @return TRUE on success.
 */
BOOL DesktopBuildWindowVisibleRegion(
    LPWINDOW Window,
    LPRECT BaseRect,
    BOOL ExcludeTargetChildren,
    LPRECT_REGION Region,
    LPRECT Storage,
    UINT Capacity
) {
    LPWINDOW Parent;
    LPWINDOW* Windows = NULL;
    LPWINDOW Candidate;
    I32 WindowOrder;
    I32 CandidateOrder;
    UINT Count = 0;
    UINT Index;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (BaseRect == NULL || Region == NULL || Storage == NULL || Capacity == 0) return FALSE;
    if (GetWindowOrderSnapshot(Window, &WindowOrder) == FALSE) return FALSE;

    if (RectRegionInit(Region, Storage, Capacity) == FALSE) return FALSE;
    RectRegionReset(Region);
    if (RectRegionAddRect(Region, BaseRect) == FALSE) return FALSE;

    Parent = (LPWINDOW)GetWindowParent((HANDLE)Window);
    if (Parent != NULL && Parent->TypeID == KOID_WINDOW) {
        (void)DesktopSnapshotWindowChildren(Parent, &Windows, &Count);
        for (Index = 0; Index < Count; Index++) {
            Candidate = Windows[Index];
            if (Candidate == NULL || Candidate->TypeID != KOID_WINDOW) continue;
            if (Candidate == Window) continue;
            if (GetWindowOrderSnapshot(Candidate, &CandidateOrder) == FALSE) continue;
            if (CandidateOrder >= WindowOrder) continue;

            DesktopVisibleRegionSubtractVisibleWindowTree(Candidate, Region, Capacity);
            if (RectRegionGetCount(Region) == 0) {
                if (Windows != NULL) KernelHeapFree(Windows);
                return TRUE;
            }
        }

        if (Windows != NULL) {
            KernelHeapFree(Windows);
            Windows = NULL;
        }
    }

    if (ExcludeTargetChildren == FALSE) return TRUE;

    Count = 0;
    (void)DesktopSnapshotWindowChildren(Window, &Windows, &Count);
    for (Index = 0; Index < Count; Index++) {
        Candidate = Windows[Index];
        if (Candidate == NULL || Candidate->TypeID != KOID_WINDOW) continue;

        DesktopVisibleRegionSubtractVisibleWindowTree(Candidate, Region, Capacity);
        if (RectRegionGetCount(Region) == 0) {
            if (Windows != NULL) KernelHeapFree(Windows);
            return TRUE;
        }
    }

    if (Windows != NULL) {
        KernelHeapFree(Windows);
    }

    return TRUE;
}
