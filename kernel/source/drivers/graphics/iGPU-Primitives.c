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


    Intel graphics (drawing primitives)

\************************************************************************/

#include "iGPU-Internal.h"
#include "Profile.h"
#include "utils/Graphics-Utils.h"
#include "utils/LineRasterizer.h"

/************************************************************************/

static BOOL IntelGfxWritePixelInternal(LPGRAPHICSCONTEXT Context, I32 X, I32 Y, COLOR* Color) {
    U8* Pixel = NULL;
    U32 Offset = 0;
    COLOR Previous = 0;

    if (Context == NULL || Color == NULL || Context->MemoryBase == NULL) {
        return FALSE;
    }

    if (X < Context->LoClip.X || X > Context->HiClip.X || Y < Context->LoClip.Y || Y > Context->HiClip.Y) {
        return FALSE;
    }

    if (Context->BitsPerPixel != 32) {
        return FALSE;
    }

    Offset = (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)X << 2);
    Pixel = Context->MemoryBase + Offset;
    Previous = *((U32*)Pixel);
    *((U32*)Pixel) = *Color;
    *Color = Previous;

    return TRUE;
}

/************************************************************************/

static BOOL IntelGfxPlotLinePixel(LPVOID Context, I32 X, I32 Y, COLOR* Color) {
    return IntelGfxWritePixelInternal((LPGRAPHICSCONTEXT)Context, X, Y, Color);
}

/************************************************************************/

static void IntelGfxDrawLineInternal(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    if (Context == NULL || Context->Pen == NULL || Context->Pen->TypeID != KOID_PEN) {
        return;
    }

    LineRasterizerDraw(Context,
                       X1,
                       Y1,
                       X2,
                       Y2,
                       Context->Pen->Color,
                       Context->Pen->Pattern,
                       IntelGfxPlotLinePixel);
}

/************************************************************************/

static void IntelGfxDrawRectangleInternal(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 Temp = 0;
    COLOR FillColor = 0;
    PROFILE_SCOPE Scope;

    if (Context == NULL) {
        return;
    }

    if (X1 > X2) {
        Temp = X1;
        X1 = X2;
        X2 = Temp;
    }
    if (Y1 > Y2) {
        Temp = Y1;
        Y1 = Y2;
        Y2 = Temp;
    }

    if (Context->Brush != NULL && Context->Brush->TypeID == KOID_BRUSH && Context->MemoryBase != NULL &&
        Context->BitsPerPixel == 32) {
        ProfileStart(&Scope, TEXT("iGPU.RectangleFill"));
        FillColor = Context->Brush->Color;
        (void)GraphicsFillSolidRect(Context, X1, Y1, X2, Y2, FillColor);
        ProfileStop(&Scope);
    }

    if (Context->Pen != NULL && Context->Pen->TypeID == KOID_PEN) {
        IntelGfxDrawLineInternal(Context, X1, Y1, X2, Y1);
        IntelGfxDrawLineInternal(Context, X2, Y1, X2, Y2);
        IntelGfxDrawLineInternal(Context, X2, Y2, X1, Y2);
        IntelGfxDrawLineInternal(Context, X1, Y2, X1, Y1);
    }
}

/************************************************************************/

static I32 IntelGfxTriangleEdgeFunction(I32 Ax, I32 Ay, I32 Bx, I32 By, I32 Px, I32 Py) {
    return (Px - Ax) * (By - Ay) - (Py - Ay) * (Bx - Ax);
}

/************************************************************************/

static BOOL IntelGfxNormalizeFlushBounds(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, LPRECT BoundsOut) {
    I32 Temp = 0;

    if (Context == NULL || BoundsOut == NULL) {
        return FALSE;
    }

    if (X1 > X2) {
        Temp = X1;
        X1 = X2;
        X2 = Temp;
    }
    if (Y1 > Y2) {
        Temp = Y1;
        Y1 = Y2;
        Y2 = Temp;
    }

    if (X1 < 0) X1 = 0;
    if (Y1 < 0) Y1 = 0;
    if (X2 >= Context->Width) X2 = Context->Width - 1;
    if (Y2 >= Context->Height) Y2 = Context->Height - 1;
    if (X2 < X1 || Y2 < Y1) {
        return FALSE;
    }

    *BoundsOut = (RECT){.X1 = X1, .Y1 = Y1, .X2 = X2, .Y2 = Y2};
    return TRUE;
}

/************************************************************************/

static void IntelGfxFlushBoundsToScanout(LPGRAPHICSCONTEXT Context, LPRECT Bounds) {
    U32 Width = 0;
    U32 Height = 0;

    if (Context == NULL || Bounds == NULL) {
        return;
    }

    if (Bounds->X2 < Bounds->X1 || Bounds->Y2 < Bounds->Y1) {
        return;
    }

    Width = (U32)(Bounds->X2 - Bounds->X1 + 1);
    Height = (U32)(Bounds->Y2 - Bounds->Y1 + 1);
    if (Width == 0 || Height == 0) {
        return;
    }

    (void)IntelGfxFlushContextRegionToScanout(Context, Bounds->X1, Bounds->Y1, Width, Height);
}

/************************************************************************/

UINT IntelGfxSetPixel(LPPIXELINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    COLOR PixelColor = 0;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    PixelColor = Info->Color;

    LockMutex(&(Context->Mutex), INFINITY);
    if (!IntelGfxWritePixelInternal(Context, Info->X, Info->Y, &PixelColor)) {
        UnlockMutex(&(Context->Mutex));
        return 0;
    }
    (void)IntelGfxFlushContextRegionToScanout(Context, Info->X, Info->Y, 1, 1);
    UnlockMutex(&(Context->Mutex));
    Info->Color = PixelColor;
    return 1;
}

/************************************************************************/

UINT IntelGfxGetPixel(LPPIXELINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    U32 Offset = 0;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT || Context->MemoryBase == NULL) {
        return 0;
    }

    if (Context->BitsPerPixel != 32) {
        return 0;
    }

    if (Info->X < Context->LoClip.X || Info->X > Context->HiClip.X || Info->Y < Context->LoClip.Y || Info->Y > Context->HiClip.Y) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    Offset = (U32)(Info->Y * (I32)Context->BytesPerScanLine) + ((U32)Info->X << 2);
    Info->Color = *((U32*)(Context->MemoryBase + Offset));
    UnlockMutex(&(Context->Mutex));

    return 1;
}

/************************************************************************/

UINT IntelGfxLine(LPLINEINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    RECT Bounds = {0};

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxDrawLineInternal(Context, Info->X1, Info->Y1, Info->X2, Info->Y2);
    if (IntelGfxNormalizeFlushBounds(Context, Info->X1, Info->Y1, Info->X2, Info->Y2, &Bounds)) {
        IntelGfxFlushBoundsToScanout(Context, &Bounds);
    }
    UnlockMutex(&(Context->Mutex));

    return 1;
}

/************************************************************************/

UINT IntelGfxRectangle(LPRECTINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    RECT Bounds = {0};
    PROFILE_SCOPE Scope;
    PROFILE_SCOPE FlushScope;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    ProfileStart(&Scope, TEXT("iGPU.Rectangle"));
    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxDrawRectangleInternal(Context, Info->X1, Info->Y1, Info->X2, Info->Y2);
    if (IntelGfxNormalizeFlushBounds(Context, Info->X1, Info->Y1, Info->X2, Info->Y2, &Bounds)) {
        ProfileStart(&FlushScope, TEXT("iGPU.RectangleFlush"));
        IntelGfxFlushBoundsToScanout(Context, &Bounds);
        ProfileStop(&FlushScope);
    }
    UnlockMutex(&(Context->Mutex));
    ProfileStop(&Scope);

    return 1;
}

/************************************************************************/

UINT IntelGfxArc(LPARCINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    I32 X = 0;
    I32 Y = 0;
    I32 Error = 0;
    I32 Radius = 0;
    I32 CenterX = 0;
    I32 CenterY = 0;
    COLOR StrokeColor = 0;
    RECT Bounds = {0};

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    if (Context->Pen == NULL || Context->Pen->TypeID != KOID_PEN || Info->Radius <= 0) {
        UnlockMutex(&(Context->Mutex));
        return 1;
    }

    Radius = Info->Radius;
    CenterX = Info->CenterX;
    CenterY = Info->CenterY;
    StrokeColor = Context->Pen->Color;

    // Midpoint circle rasterization. Start/end angles are intentionally ignored.
    X = Radius;
    Y = 0;
    Error = 1 - Radius;

    while (X >= Y) {
        COLOR PixelColor = StrokeColor;
        (void)IntelGfxWritePixelInternal(Context, CenterX + X, CenterY + Y, &PixelColor);
        PixelColor = StrokeColor;
        (void)IntelGfxWritePixelInternal(Context, CenterX + Y, CenterY + X, &PixelColor);
        PixelColor = StrokeColor;
        (void)IntelGfxWritePixelInternal(Context, CenterX - Y, CenterY + X, &PixelColor);
        PixelColor = StrokeColor;
        (void)IntelGfxWritePixelInternal(Context, CenterX - X, CenterY + Y, &PixelColor);
        PixelColor = StrokeColor;
        (void)IntelGfxWritePixelInternal(Context, CenterX - X, CenterY - Y, &PixelColor);
        PixelColor = StrokeColor;
        (void)IntelGfxWritePixelInternal(Context, CenterX - Y, CenterY - X, &PixelColor);
        PixelColor = StrokeColor;
        (void)IntelGfxWritePixelInternal(Context, CenterX + Y, CenterY - X, &PixelColor);
        PixelColor = StrokeColor;
        (void)IntelGfxWritePixelInternal(Context, CenterX + X, CenterY - Y, &PixelColor);

        Y++;
        if (Error < 0) {
            Error += (2 * Y) + 1;
        } else {
            X--;
            Error += 2 * (Y - X) + 1;
        }
    }

    if (IntelGfxNormalizeFlushBounds(
            Context, CenterX - Radius, CenterY - Radius, CenterX + Radius, CenterY + Radius, &Bounds)) {
        IntelGfxFlushBoundsToScanout(Context, &Bounds);
    }
    UnlockMutex(&(Context->Mutex));

    return 1;
}

/************************************************************************/

UINT IntelGfxTriangle(LPTRIANGLEINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    I32 MinX = 0;
    I32 MaxX = 0;
    I32 MinY = 0;
    I32 MaxY = 0;
    I32 Area = 0;
    COLOR FillColor = 0;
    BOOL HasFill = FALSE;
    BOOL HasStroke = FALSE;
    RECT Bounds = {0};
    RECT FilledBounds = {0};

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    HasFill = (Context->Brush != NULL && Context->Brush->TypeID == KOID_BRUSH);
    HasStroke = (Context->Pen != NULL && Context->Pen->TypeID == KOID_PEN);
    if (HasFill == FALSE && HasStroke == FALSE) {
        UnlockMutex(&(Context->Mutex));
        return 1;
    }

    if (HasFill != FALSE) {
        FillColor = Context->Brush->Color;
    }

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

    Area = IntelGfxTriangleEdgeFunction(Info->P1.X, Info->P1.Y, Info->P2.X, Info->P2.Y, Info->P3.X, Info->P3.Y);
    if (Area == 0) {
        if (HasStroke != FALSE) {
            IntelGfxDrawLineInternal(Context, Info->P1.X, Info->P1.Y, Info->P2.X, Info->P2.Y);
            IntelGfxDrawLineInternal(Context, Info->P2.X, Info->P2.Y, Info->P3.X, Info->P3.Y);
            IntelGfxDrawLineInternal(Context, Info->P3.X, Info->P3.Y, Info->P1.X, Info->P1.Y);
        }
    } else {
        if (HasFill != FALSE) {
            (void)GraphicsFillTriangleSpans(Context, Info, FillColor, &FilledBounds);
        }

        if (HasStroke != FALSE) {
            IntelGfxDrawLineInternal(Context, Info->P1.X, Info->P1.Y, Info->P2.X, Info->P2.Y);
            IntelGfxDrawLineInternal(Context, Info->P2.X, Info->P2.Y, Info->P3.X, Info->P3.Y);
            IntelGfxDrawLineInternal(Context, Info->P3.X, Info->P3.Y, Info->P1.X, Info->P1.Y);
        }
    }

    if (IntelGfxNormalizeFlushBounds(Context, MinX, MinY, MaxX, MaxY, &Bounds)) {
        IntelGfxFlushBoundsToScanout(Context, &Bounds);
    }
    UnlockMutex(&(Context->Mutex));

    return 1;
}

/************************************************************************/
