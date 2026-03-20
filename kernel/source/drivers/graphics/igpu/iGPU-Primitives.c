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
                       Context->Pen->Width != 0 ? Context->Pen->Width : 1,
                       IntelGfxPlotLinePixel);
}

/************************************************************************/

static void IntelGfxDrawRectangleInternal(LPGRAPHICSCONTEXT Context, LPRECT_INFO Info) {
    PROFILE_SCOPE Scope;

    if (Context == NULL || Info == NULL) {
        return;
    }

    if (Context->MemoryBase != NULL && Context->BitsPerPixel == 32) {
        ProfileStart(&Scope, TEXT("iGPU.RectangleFill"));
        (void)GraphicsDrawRectangleFromDescriptor(Context, Info);
        ProfileStop(&Scope);
    }
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

UINT IntelGfxSetPixel(LPPIXEL_INFO Info) {
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

UINT IntelGfxGetPixel(LPPIXEL_INFO Info) {
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

UINT IntelGfxLine(LPLINE_INFO Info) {
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

UINT IntelGfxRectangle(LPRECT_INFO Info) {
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
    IntelGfxDrawRectangleInternal(Context, Info);
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

UINT IntelGfxArc(LPARC_INFO Info) {
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
    (void)GraphicsDrawArcFromDescriptor(Context, Info);

    if (IntelGfxNormalizeFlushBounds(
            Context,
            Info->CenterX - Info->Radius,
            Info->CenterY - Info->Radius,
            Info->CenterX + Info->Radius,
            Info->CenterY + Info->Radius,
            &Bounds)) {
        IntelGfxFlushBoundsToScanout(Context, &Bounds);
    }
    UnlockMutex(&(Context->Mutex));

    return 1;
}

/************************************************************************/

UINT IntelGfxTriangle(LPTRIANGLE_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    I32 MinX = 0;
    I32 MaxX = 0;
    I32 MinY = 0;
    I32 MaxY = 0;
    RECT Bounds = {0};

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
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

    (void)GraphicsDrawTriangleFromDescriptor(Context, Info);

    if (IntelGfxNormalizeFlushBounds(Context, MinX, MinY, MaxX, MaxY, &Bounds)) {
        IntelGfxFlushBoundsToScanout(Context, &Bounds);
    }
    UnlockMutex(&(Context->Mutex));

    return 1;
}

/************************************************************************/
