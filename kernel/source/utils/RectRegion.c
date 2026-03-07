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


    RectRegion helper

\************************************************************************/

#include "utils/RectRegion.h"

/************************************************************************/
// Inline functions

static inline BOOL IsRectCanonical(LPRECT Rect) {
    if (Rect == NULL) return FALSE;
    if (Rect->X1 > Rect->X2) return FALSE;
    if (Rect->Y1 > Rect->Y2) return FALSE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Canonicalize one rectangle by ordering coordinates.
 * @param Rect Rectangle to canonicalize.
 */
static void NormalizeRect(LPRECT Rect) {
    I32 Value;

    if (Rect == NULL) return;

    if (Rect->X1 > Rect->X2) {
        Value = Rect->X1;
        Rect->X1 = Rect->X2;
        Rect->X2 = Value;
    }

    if (Rect->Y1 > Rect->Y2) {
        Value = Rect->Y1;
        Rect->Y1 = Rect->Y2;
        Rect->Y2 = Value;
    }
}

/************************************************************************/

/**
 * @brief Check whether two rectangles touch or overlap.
 * @param First First rectangle.
 * @param Second Second rectangle.
 * @return TRUE when rectangles overlap or are adjacent.
 */
static BOOL IsRectTouchingOrOverlapping(LPRECT First, LPRECT Second) {
    if (First == NULL || Second == NULL) return FALSE;
    if (!IsRectCanonical(First) || !IsRectCanonical(Second)) return FALSE;

    if (First->X2 < Second->X1) return FALSE;
    if (Second->X2 < First->X1) return FALSE;
    if (First->Y2 < Second->Y1) return FALSE;
    if (Second->Y2 < First->Y1) return FALSE;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Expand one destination rectangle to include a source rectangle.
 * @param Destination Destination rectangle updated in place.
 * @param Source Source rectangle to include.
 */
static void RectUnionInPlace(LPRECT Destination, LPRECT Source) {
    if (Destination == NULL || Source == NULL) return;

    if (Source->X1 < Destination->X1) Destination->X1 = Source->X1;
    if (Source->Y1 < Destination->Y1) Destination->Y1 = Source->Y1;
    if (Source->X2 > Destination->X2) Destination->X2 = Source->X2;
    if (Source->Y2 > Destination->Y2) Destination->Y2 = Source->Y2;
}

/************************************************************************/

/**
 * @brief Intersect one rectangle in place with another rectangle.
 * @param Destination Destination rectangle updated in place.
 * @param Clip Clip rectangle.
 * @return TRUE when the resulting intersection is not empty.
 */
static BOOL RectIntersectInPlace(LPRECT Destination, LPRECT Clip) {
    if (Destination == NULL || Clip == NULL) return FALSE;

    if (Clip->X1 > Destination->X1) Destination->X1 = Clip->X1;
    if (Clip->Y1 > Destination->Y1) Destination->Y1 = Clip->Y1;
    if (Clip->X2 < Destination->X2) Destination->X2 = Clip->X2;
    if (Clip->Y2 < Destination->Y2) Destination->Y2 = Clip->Y2;

    return Destination->X1 <= Destination->X2 && Destination->Y1 <= Destination->Y2;
}

/************************************************************************/

/**
 * @brief Merge one rectangle with any overlapping/adjacent rectangle in region.
 * @param Region Rectangle region.
 * @param Rect Rectangle to merge and store.
 * @return TRUE when rectangle is represented in region.
 */
static BOOL RectRegionMergeAndStore(LPRECT_REGION Region, LPRECT Rect) {
    RECT Candidate;
    BOOL RestartMerge = FALSE;
    UINT Index = 0;

    if (Region == NULL || Rect == NULL) return FALSE;

    Candidate = *Rect;
    NormalizeRect(&Candidate);
    if (IsRectCanonical(&Candidate) == FALSE) return FALSE;

    do {
        RestartMerge = FALSE;

        for (Index = 0; Index < Region->Count; Index++) {
            RECT Existing = Region->Storage[Index];

            if (IsRectTouchingOrOverlapping(&Candidate, &Existing) == FALSE) continue;

            RectUnionInPlace(&Candidate, &Existing);

            Region->Count--;
            if (Index < Region->Count) {
                Region->Storage[Index] = Region->Storage[Region->Count];
            }

            RestartMerge = TRUE;
            break;
        }
    } while (RestartMerge);

    if (Region->Count < Region->Capacity) {
        Region->Storage[Region->Count++] = Candidate;
        return TRUE;
    }

    Region->Overflowed = TRUE;

    if (Region->Capacity == 0) {
        Region->Count = 0;
        return FALSE;
    }

    if (Region->Count == 0) {
        Region->Storage[0] = Candidate;
        Region->Count = 1;
        return TRUE;
    }

    // Deterministic fallback when capacity is exceeded: collapse into one bounding rectangle.
    Region->Storage[0] = Candidate;
    for (Index = 1; Index < Region->Count; Index++) {
        RectUnionInPlace(&Region->Storage[0], &Region->Storage[Index]);
    }

    Region->Count = 1;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize a rectangle region with caller-provided storage.
 * @param Region Rectangle region descriptor.
 * @param Storage Rectangle storage buffer.
 * @param Capacity Maximum rectangles held in storage.
 * @return TRUE on success.
 */
BOOL RectRegionInit(LPRECT_REGION Region, LPRECT Storage, UINT Capacity) {
    if (Region == NULL) return FALSE;
    if (Capacity > 0 && Storage == NULL) return FALSE;

    Region->Storage = Storage;
    Region->Capacity = Capacity;
    Region->Count = 0;
    Region->Overflowed = FALSE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Reset one rectangle region while keeping its storage and capacity.
 * @param Region Rectangle region descriptor.
 */
void RectRegionReset(LPRECT_REGION Region) {
    if (Region == NULL) return;

    Region->Count = 0;
    Region->Overflowed = FALSE;
}

/************************************************************************/

/**
 * @brief Add one rectangle to a region with merge semantics.
 * @param Region Rectangle region descriptor.
 * @param Rect Rectangle to add.
 * @return TRUE when rectangle was added or merged.
 */
BOOL RectRegionAddRect(LPRECT_REGION Region, LPRECT Rect) {
    return RectRegionMergeAndStore(Region, Rect);
}

/************************************************************************/

/**
 * @brief Union one rectangle into a region.
 * @param Region Rectangle region descriptor.
 * @param Rect Rectangle to union.
 * @return TRUE when rectangle was added or merged.
 */
BOOL RectRegionUnionRect(LPRECT_REGION Region, LPRECT Rect) {
    return RectRegionMergeAndStore(Region, Rect);
}

/************************************************************************/

/**
 * @brief Intersect all region rectangles with one clip rectangle.
 * @param Region Rectangle region descriptor.
 * @param Rect Clip rectangle.
 * @return TRUE on success.
 */
BOOL RectRegionIntersectRect(LPRECT_REGION Region, LPRECT Rect) {
    RECT Clip;
    UINT Index;
    UINT WriteIndex;

    if (Region == NULL) return FALSE;
    if (Rect == NULL) return FALSE;
    if (Region->Storage == NULL && Region->Capacity > 0) return FALSE;

    Clip = *Rect;
    NormalizeRect(&Clip);
    if (IsRectCanonical(&Clip) == FALSE) {
        Region->Count = 0;
        return TRUE;
    }

    WriteIndex = 0;
    for (Index = 0; Index < Region->Count; Index++) {
        RECT Candidate = Region->Storage[Index];

        if (RectIntersectInPlace(&Candidate, &Clip) == FALSE) continue;
        Region->Storage[WriteIndex++] = Candidate;
    }

    Region->Count = WriteIndex;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Get the rectangle count from one region.
 * @param Region Rectangle region descriptor.
 * @return Number of rectangles in region.
 */
UINT RectRegionGetCount(LPRECT_REGION Region) {
    if (Region == NULL) return 0;
    return Region->Count;
}

/************************************************************************/

/**
 * @brief Retrieve one rectangle from region by index.
 * @param Region Rectangle region descriptor.
 * @param Index Rectangle index.
 * @param Rect Destination rectangle.
 * @return TRUE on success.
 */
BOOL RectRegionGetRect(LPRECT_REGION Region, UINT Index, LPRECT Rect) {
    if (Region == NULL) return FALSE;
    if (Rect == NULL) return FALSE;
    if (Index >= Region->Count) return FALSE;
    if (Region->Storage == NULL) return FALSE;

    *Rect = Region->Storage[Index];
    return TRUE;
}

/************************************************************************/

/**
 * @brief Query whether region overflow fallback has occurred.
 * @param Region Rectangle region descriptor.
 * @return TRUE when overflow fallback occurred.
 */
BOOL RectRegionIsOverflowed(LPRECT_REGION Region) {
    if (Region == NULL) return FALSE;
    return Region->Overflowed;
}

/************************************************************************/
