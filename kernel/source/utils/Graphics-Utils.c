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


    Graphics utility helpers

\************************************************************************/

#include "utils/Graphics-Utils.h"

/************************************************************************/

BOOL IntersectRect(LPRECT Left, LPRECT Right, LPRECT Result) {
    if (Left == NULL || Right == NULL || Result == NULL) return FALSE;

    Result->X1 = Left->X1 > Right->X1 ? Left->X1 : Right->X1;
    Result->Y1 = Left->Y1 > Right->Y1 ? Left->Y1 : Right->Y1;
    Result->X2 = Left->X2 < Right->X2 ? Left->X2 : Right->X2;
    Result->Y2 = Left->Y2 < Right->Y2 ? Left->Y2 : Right->Y2;

    return Result->X1 <= Result->X2 && Result->Y1 <= Result->Y2;
}

/************************************************************************/

/**
 * @brief Append one rectangle to a region when it is valid.
 * @param Region Destination region.
 * @param Rect Candidate rectangle.
 * @return TRUE on success.
 */
static BOOL GraphicsRegionAppendRectIfValid(LPRECT_REGION Region, LPRECT Rect) {
    if (Region == NULL || Rect == NULL) return FALSE;
    if (Rect->X1 > Rect->X2 || Rect->Y1 > Rect->Y2) return TRUE;
    return RectRegionAddRect(Region, Rect);
}

/************************************************************************/

BOOL SubtractRectFromRect(LPRECT Source, LPRECT Occluder, LPRECT_REGION Region) {
    RECT Intersection;
    RECT Piece;

    if (Source == NULL || Occluder == NULL || Region == NULL) return FALSE;

    if (IntersectRect(Source, Occluder, &Intersection) == FALSE) {
        return GraphicsRegionAppendRectIfValid(Region, Source);
    }

    Piece.X1 = Source->X1;
    Piece.Y1 = Source->Y1;
    Piece.X2 = Source->X2;
    Piece.Y2 = Intersection.Y1 - 1;
    if (GraphicsRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece.X1 = Source->X1;
    Piece.Y1 = Intersection.Y2 + 1;
    Piece.X2 = Source->X2;
    Piece.Y2 = Source->Y2;
    if (GraphicsRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece.X1 = Source->X1;
    Piece.Y1 = Intersection.Y1;
    Piece.X2 = Intersection.X1 - 1;
    Piece.Y2 = Intersection.Y2;
    if (GraphicsRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece.X1 = Intersection.X2 + 1;
    Piece.Y1 = Intersection.Y1;
    Piece.X2 = Source->X2;
    Piece.Y2 = Intersection.Y2;
    if (GraphicsRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    return TRUE;
}

/************************************************************************/

BOOL SubtractRectFromRegion(LPRECT_REGION Region, LPRECT Occluder, LPRECT TempStorage, UINT TempCapacity) {
    RECT_REGION TempRegion;
    RECT Current;
    UINT Count;
    UINT Index;

    if (Region == NULL || Occluder == NULL) return FALSE;
    if (RectRegionInit(&TempRegion, TempStorage, TempCapacity) == FALSE) return FALSE;
    RectRegionReset(&TempRegion);

    Count = RectRegionGetCount(Region);
    for (Index = 0; Index < Count; Index++) {
        if (RectRegionGetRect(Region, Index, &Current) == FALSE) return FALSE;
        if (SubtractRectFromRect(&Current, Occluder, &TempRegion) == FALSE) return FALSE;
    }

    RectRegionReset(Region);
    Count = RectRegionGetCount(&TempRegion);
    for (Index = 0; Index < Count; Index++) {
        if (RectRegionGetRect(&TempRegion, Index, &Current) == FALSE) return FALSE;
        if (RectRegionAddRect(Region, &Current) == FALSE) return FALSE;
    }

    return TRUE;
}

/************************************************************************/

void GraphicsScreenRectToWindowRect(LPRECT WindowScreenRect, LPRECT ScreenRect, LPRECT WindowRect) {
    if (WindowScreenRect == NULL || ScreenRect == NULL || WindowRect == NULL) return;

    WindowRect->X1 = ScreenRect->X1 - WindowScreenRect->X1;
    WindowRect->Y1 = ScreenRect->Y1 - WindowScreenRect->Y1;
    WindowRect->X2 = ScreenRect->X2 - WindowScreenRect->X1;
    WindowRect->Y2 = ScreenRect->Y2 - WindowScreenRect->Y1;
}

/************************************************************************/

void GraphicsWindowRectToScreenRect(LPRECT WindowScreenRect, LPRECT WindowRect, LPRECT ScreenRect) {
    if (WindowScreenRect == NULL || WindowRect == NULL || ScreenRect == NULL) return;

    ScreenRect->X1 = WindowScreenRect->X1 + WindowRect->X1;
    ScreenRect->Y1 = WindowScreenRect->Y1 + WindowRect->Y1;
    ScreenRect->X2 = WindowScreenRect->X1 + WindowRect->X2;
    ScreenRect->Y2 = WindowScreenRect->Y1 + WindowRect->Y2;
}

/************************************************************************/

void GraphicsScreenPointToWindowPoint(LPRECT WindowScreenRect, LPPOINT ScreenPoint, LPPOINT WindowPoint) {
    if (WindowScreenRect == NULL || ScreenPoint == NULL || WindowPoint == NULL) return;

    WindowPoint->X = ScreenPoint->X - WindowScreenRect->X1;
    WindowPoint->Y = ScreenPoint->Y - WindowScreenRect->Y1;
}

/************************************************************************/

void GraphicsWindowPointToScreenPoint(LPRECT WindowScreenRect, LPPOINT WindowPoint, LPPOINT ScreenPoint) {
    if (WindowScreenRect == NULL || WindowPoint == NULL || ScreenPoint == NULL) return;

    ScreenPoint->X = WindowScreenRect->X1 + WindowPoint->X;
    ScreenPoint->Y = WindowScreenRect->Y1 + WindowPoint->Y;
}

/************************************************************************/
