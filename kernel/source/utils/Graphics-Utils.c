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

BOOL GraphicsFillSolidRect(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR FillColor) {
    I32 DrawX1 = 0;
    I32 DrawY1 = 0;
    I32 DrawX2 = 0;
    I32 DrawY2 = 0;
    I32 Y = 0;
    U32 Width = 0;
    U32 X = 0;

    if (Context == NULL || Context->MemoryBase == NULL) return FALSE;

    DrawX1 = X1;
    DrawY1 = Y1;
    DrawX2 = X2;
    DrawY2 = Y2;

    if (DrawX1 < Context->LoClip.X) DrawX1 = Context->LoClip.X;
    if (DrawY1 < Context->LoClip.Y) DrawY1 = Context->LoClip.Y;
    if (DrawX2 > Context->HiClip.X) DrawX2 = Context->HiClip.X;
    if (DrawY2 > Context->HiClip.Y) DrawY2 = Context->HiClip.Y;

    if (DrawX1 < 0) DrawX1 = 0;
    if (DrawY1 < 0) DrawY1 = 0;
    if (DrawX2 >= Context->Width) DrawX2 = Context->Width - 1;
    if (DrawY2 >= Context->Height) DrawY2 = Context->Height - 1;

    if (DrawX2 < DrawX1 || DrawY2 < DrawY1) return TRUE;

    Width = (U32)(DrawX2 - DrawX1 + 1);

    switch (Context->BitsPerPixel) {
        case 32:
            for (Y = DrawY1; Y <= DrawY2; Y++) {
                U32* Pixel = (U32*)(Context->MemoryBase + (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)DrawX1 << 2));

                switch (Context->RasterOperation) {
                    case ROP_SET:
                        for (X = 0; X < Width; X++) Pixel[X] = FillColor;
                        break;
                    case ROP_XOR:
                        for (X = 0; X < Width; X++) Pixel[X] ^= FillColor;
                        break;
                    case ROP_OR:
                        for (X = 0; X < Width; X++) Pixel[X] |= FillColor;
                        break;
                    case ROP_AND:
                        for (X = 0; X < Width; X++) Pixel[X] &= FillColor;
                        break;
                    default:
                        return FALSE;
                }
            }
            return TRUE;

        case 24: {
            U32 Converted = 0;
            U8 Red = 0;
            U8 Green = 0;
            U8 Blue = 0;

            Converted = (((FillColor >> 0) & 0xFF) << 16) | (((FillColor >> 8) & 0xFF) << 8) | (((FillColor >> 16) & 0xFF) << 0);
            Red = (U8)((Converted >> 0) & 0xFF);
            Green = (U8)((Converted >> 8) & 0xFF);
            Blue = (U8)((Converted >> 16) & 0xFF);

            for (Y = DrawY1; Y <= DrawY2; Y++) {
                U8* Pixel = Context->MemoryBase + (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)DrawX1 * 3);

                switch (Context->RasterOperation) {
                    case ROP_SET:
                        for (X = 0; X < Width; X++) {
                            Pixel[0] = Red;
                            Pixel[1] = Green;
                            Pixel[2] = Blue;
                            Pixel += 3;
                        }
                        break;
                    case ROP_XOR:
                        for (X = 0; X < Width; X++) {
                            Pixel[0] ^= Red;
                            Pixel[1] ^= Green;
                            Pixel[2] ^= Blue;
                            Pixel += 3;
                        }
                        break;
                    case ROP_OR:
                        for (X = 0; X < Width; X++) {
                            Pixel[0] |= Red;
                            Pixel[1] |= Green;
                            Pixel[2] |= Blue;
                            Pixel += 3;
                        }
                        break;
                    case ROP_AND:
                        for (X = 0; X < Width; X++) {
                            Pixel[0] &= Red;
                            Pixel[1] &= Green;
                            Pixel[2] &= Blue;
                            Pixel += 3;
                        }
                        break;
                    default:
                        return FALSE;
                }
            }
            return TRUE;
        }

        case 16: {
            U16 Fill16 = (U16)FillColor;

            for (Y = DrawY1; Y <= DrawY2; Y++) {
                U16* Pixel = (U16*)(Context->MemoryBase + (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)DrawX1 << 1));

                switch (Context->RasterOperation) {
                    case ROP_SET:
                        for (X = 0; X < Width; X++) Pixel[X] = Fill16;
                        break;
                    case ROP_XOR:
                        for (X = 0; X < Width; X++) Pixel[X] ^= Fill16;
                        break;
                    case ROP_OR:
                        for (X = 0; X < Width; X++) Pixel[X] |= Fill16;
                        break;
                    case ROP_AND:
                        for (X = 0; X < Width; X++) Pixel[X] &= Fill16;
                        break;
                    default:
                        return FALSE;
                }
            }
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

static I32 GraphicsTriangleEdgeFunction(I32 Ax, I32 Ay, I32 Bx, I32 By, I32 Px, I32 Py) {
    return (Px - Ax) * (By - Ay) - (Py - Ay) * (Bx - Ax);
}

/************************************************************************/

BOOL GraphicsFillTriangleSpans(LPGRAPHICSCONTEXT Context, LPTRIANGLEINFO Info, COLOR FillColor, LPRECT FilledBounds) {
    I32 MinX = 0;
    I32 MaxX = 0;
    I32 MinY = 0;
    I32 MaxY = 0;
    I32 Area = 0;
    I32 Y = 0;
    BOOL FilledAny = FALSE;

    if (FilledBounds != NULL) {
        *FilledBounds = (RECT){0};
    }

    if (Context == NULL || Info == NULL || Context->MemoryBase == NULL) return FALSE;

    MinX = Info->P1.X;
    if (Info->P2.X < MinX) MinX = Info->P2.X;
    if (Info->P3.X < MinX) MinX = Info->P3.X;
    MaxX = Info->P1.X;
    if (Info->P2.X > MaxX) MaxX = Info->P2.X;
    if (Info->P3.X > MaxX) MaxX = Info->P3.X;

    MinY = Info->P1.Y;
    if (Info->P2.Y < MinY) MinY = Info->P2.Y;
    if (Info->P3.Y < MinY) MinY = Info->P3.Y;
    MaxY = Info->P1.Y;
    if (Info->P2.Y > MaxY) MaxY = Info->P2.Y;
    if (Info->P3.Y > MaxY) MaxY = Info->P3.Y;

    if (MinX < Context->LoClip.X) MinX = Context->LoClip.X;
    if (MinY < Context->LoClip.Y) MinY = Context->LoClip.Y;
    if (MaxX > Context->HiClip.X) MaxX = Context->HiClip.X;
    if (MaxY > Context->HiClip.Y) MaxY = Context->HiClip.Y;
    if (MinX > MaxX || MinY > MaxY) return FALSE;

    Area = GraphicsTriangleEdgeFunction(Info->P1.X, Info->P1.Y, Info->P2.X, Info->P2.Y, Info->P3.X, Info->P3.Y);
    if (Area == 0) return FALSE;

    for (Y = MinY; Y <= MaxY; Y++) {
        I32 SpanStart = 0;
        I32 SpanEnd = 0;
        I32 X = 0;
        BOOL SpanActive = FALSE;

        for (X = MinX; X <= MaxX; X++) {
            I32 W0 = GraphicsTriangleEdgeFunction(Info->P2.X, Info->P2.Y, Info->P3.X, Info->P3.Y, X, Y);
            I32 W1 = GraphicsTriangleEdgeFunction(Info->P3.X, Info->P3.Y, Info->P1.X, Info->P1.Y, X, Y);
            I32 W2 = GraphicsTriangleEdgeFunction(Info->P1.X, Info->P1.Y, Info->P2.X, Info->P2.Y, X, Y);

            if (Area > 0) {
                if (W0 < 0 || W1 < 0 || W2 < 0) continue;
            } else {
                if (W0 > 0 || W1 > 0 || W2 > 0) continue;
            }

            if (SpanActive == FALSE) {
                SpanStart = X;
                SpanActive = TRUE;
            }
            SpanEnd = X;
        }

        if (SpanActive == FALSE) continue;
        if (GraphicsFillSolidRect(Context, SpanStart, Y, SpanEnd, Y, FillColor) == FALSE) return FALSE;

        if (FilledAny == FALSE) {
            if (FilledBounds != NULL) {
                FilledBounds->X1 = SpanStart;
                FilledBounds->Y1 = Y;
                FilledBounds->X2 = SpanEnd;
                FilledBounds->Y2 = Y;
            }
            FilledAny = TRUE;
        } else if (FilledBounds != NULL) {
            if (SpanStart < FilledBounds->X1) FilledBounds->X1 = SpanStart;
            if (SpanEnd > FilledBounds->X2) FilledBounds->X2 = SpanEnd;
            FilledBounds->Y2 = Y;
        }
    }

    return FilledAny;
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
