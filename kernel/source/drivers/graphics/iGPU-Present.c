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


    Intel graphics (drawing, surfaces and present)

\************************************************************************/

#include "iGPU-Internal.h"

#include "Clock.h"
#include "CoreString.h"
#include "DeferredWork.h"
#include "Log.h"
#include "Memory.h"
#include "drivers/graphics/Graphics-TextRenderer.h"
#include "utils/Cooldown.h"

/************************************************************************/

static INTEL_GFX_SURFACE IntelGfxSurfaces[INTEL_GFX_MAX_SURFACES] = {0};

/************************************************************************/

#define INTEL_GFX_CURSOR_SHOW_DELAY_MS 250

/************************************************************************/

typedef struct tag_INTEL_GFX_CURSOR_RUNTIME {
    COOLDOWN ShowCooldown;
    U32 DeferredHandle;
    BOOL PendingShow;
} INTEL_GFX_CURSOR_RUNTIME, *LPINTEL_GFX_CURSOR_RUNTIME;

/************************************************************************/

static INTEL_GFX_CURSOR_RUNTIME IntelGfxCursorRuntime = {.DeferredHandle = DEFERRED_WORK_INVALID_HANDLE};

/************************************************************************/

static void IntelGfxCursorDeferredPoll(LPVOID Context) {
    GFX_TEXT_CURSOR_VISIBLE_INFO CursorVisibleInfo;
    U32 Now = 0;

    UNUSED(Context);

    if ((IntelGfxDriver.Flags & DRIVER_FLAG_READY) == 0 || IntelGfxState.HasActiveMode == FALSE) {
        return;
    }

    if (IntelGfxCursorRuntime.PendingShow == FALSE) {
        return;
    }

    if (IntelGfxCursorRuntime.ShowCooldown.Initialized == FALSE) {
        return;
    }

    Now = GetSystemTime();
    if (!CooldownReady(&IntelGfxCursorRuntime.ShowCooldown, Now)) {
        return;
    }

    CursorVisibleInfo = (GFX_TEXT_CURSOR_VISIBLE_INFO){.IsVisible = TRUE};

    LockMutex(&(IntelGfxState.Context.Mutex), INFINITY);
    if (GfxTextSetCursorVisible(&IntelGfxState.Context, &CursorVisibleInfo)) {
        IntelGfxCursorRuntime.PendingShow = FALSE;
    }
    UnlockMutex(&(IntelGfxState.Context.Mutex));
}

/************************************************************************/

static void IntelGfxEnsureCursorDeferredRegistration(void) {
    if (IntelGfxCursorRuntime.DeferredHandle != DEFERRED_WORK_INVALID_HANDLE) {
        return;
    }

    IntelGfxCursorRuntime.DeferredHandle =
        DeferredWorkRegisterPollOnly(IntelGfxCursorDeferredPoll, NULL, TEXT("IntelGfxCursor"));

    if (IntelGfxCursorRuntime.DeferredHandle == DEFERRED_WORK_INVALID_HANDLE) {
        WARNING(TEXT("[IntelGfxEnsureCursorDeferredRegistration] DeferredWork registration failed"));
    }
}

/************************************************************************/

static void IntelGfxArmCursorShowCooldown(void) {
    U32 Now = 0;

    if (IntelGfxCursorRuntime.ShowCooldown.Initialized == FALSE) {
        if (!CooldownInit(&IntelGfxCursorRuntime.ShowCooldown, INTEL_GFX_CURSOR_SHOW_DELAY_MS)) {
            return;
        }
    } else {
        CooldownSetInterval(&IntelGfxCursorRuntime.ShowCooldown, INTEL_GFX_CURSOR_SHOW_DELAY_MS);
    }

    Now = GetSystemTime();
    IntelGfxCursorRuntime.ShowCooldown.NextAllowedTick = Now + INTEL_GFX_CURSOR_SHOW_DELAY_MS;
    IntelGfxCursorRuntime.PendingShow = TRUE;

    if (IntelGfxCursorRuntime.DeferredHandle != DEFERRED_WORK_INVALID_HANDLE) {
        DeferredWorkSignal(IntelGfxCursorRuntime.DeferredHandle);
    }
}

/************************************************************************/

static void IntelGfxHandleTextWriteActivity(LPGRAPHICSCONTEXT Context) {
    GFX_TEXT_CURSOR_VISIBLE_INFO CursorVisibleInfo;

    if (Context == NULL) {
        return;
    }

    IntelGfxEnsureCursorDeferredRegistration();
    CursorVisibleInfo = (GFX_TEXT_CURSOR_VISIBLE_INFO){.IsVisible = FALSE};
    (void)GfxTextSetCursorVisible(Context, &CursorVisibleInfo);
    IntelGfxArmCursorShowCooldown();
}

/************************************************************************/

static UINT IntelGfxFlushContextRegionToScanout(LPGRAPHICSCONTEXT Context, I32 X, I32 Y, U32 Width, U32 Height) {
    U32 BytesPerPixel = 0;
    U32 CopyBytes = 0;
    U32 Row = 0;

    if (Context == NULL || Context->MemoryBase == NULL || Width == 0 || Height == 0) {
        return DF_RETURN_GENERIC;
    }

    if (IntelGfxState.FrameBufferLinear == 0 || IntelGfxState.FrameBufferSize == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    if (Context->MemoryBase == (U8*)(LINEAR)IntelGfxState.FrameBufferLinear) {
        return DF_RETURN_SUCCESS;
    }

    if (X < 0 || Y < 0) {
        return DF_RETURN_GENERIC;
    }

    if ((U32)X + Width > IntelGfxState.ActiveWidth || (U32)Y + Height > IntelGfxState.ActiveHeight) {
        return DF_RETURN_GENERIC;
    }

    BytesPerPixel = Context->BitsPerPixel / 8;
    if (BytesPerPixel == 0) {
        return DF_RETURN_GENERIC;
    }

    CopyBytes = Width * BytesPerPixel;
    for (Row = 0; Row < Height; Row++) {
        U32 SourceOffset = ((U32)Y + Row) * (U32)Context->BytesPerScanLine + ((U32)X * BytesPerPixel);
        U32 DestinationOffset = ((U32)Y + Row) * IntelGfxState.ActiveStride + ((U32)X * BytesPerPixel);
        U8* Source = Context->MemoryBase + SourceOffset;
        U8* Destination = (U8*)(LINEAR)IntelGfxState.FrameBufferLinear + DestinationOffset;
        MemoryCopy(Destination, Source, CopyBytes);
    }

    IntelGfxState.PresentBlitCount++;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

static BOOL IntelGfxEnsureShadowFrameBufferSize(UINT RequiredSize) {
    if (RequiredSize == 0) {
        return FALSE;
    }

    if (IntelGfxState.ShadowFrameBufferLinear != 0 && IntelGfxState.ShadowFrameBufferSize != RequiredSize) {
        FreeRegion(IntelGfxState.ShadowFrameBufferLinear, IntelGfxState.ShadowFrameBufferSize);
        IntelGfxState.ShadowFrameBufferLinear = 0;
        IntelGfxState.ShadowFrameBufferSize = 0;
    }

    if (IntelGfxState.ShadowFrameBufferLinear == 0) {
        IntelGfxState.ShadowFrameBufferLinear = AllocRegion(VMA_KERNEL,
                                                            0,
                                                            RequiredSize,
                                                            ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER,
                                                            TEXT("IntelGfxShadowFrameBuffer"));
        if (IntelGfxState.ShadowFrameBufferLinear == 0) {
            ERROR(TEXT("[IntelGfxEnsureShadowFrameBufferSize] AllocRegion failed size=%u"), RequiredSize);
            return FALSE;
        }

        IntelGfxState.ShadowFrameBufferSize = RequiredSize;
    }

    return TRUE;
}

/************************************************************************/

static UINT IntelGfxScrollRegionViaShadow(LPGRAPHICSCONTEXT Context, LPGFX_TEXT_REGION_INFO Info) {
    GRAPHICSCONTEXT ShadowContext;
    I32 PixelX = 0;
    I32 PixelY = 0;
    U32 PixelWidth = 0;
    U32 PixelHeight = 0;
    U32 BytesPerPixel = 0;
    U32 Row = 0;
    U32 RowBytes = 0;
    BOOL Result = FALSE;
    U8* FrameBuffer = NULL;
    U8* ShadowBuffer = NULL;
    UINT FrameSize = 0;

    if (Context == NULL || Info == NULL || Context->MemoryBase == NULL) {
        return DF_RETURN_GENERIC;
    }

    if (IntelGfxState.FrameBufferLinear == 0 || IntelGfxState.FrameBufferSize == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    PixelX = (I32)(Info->CellX * Info->GlyphCellWidth);
    PixelY = (I32)(Info->CellY * Info->GlyphCellHeight);
    PixelWidth = Info->RegionCellWidth * Info->GlyphCellWidth;
    PixelHeight = Info->RegionCellHeight * Info->GlyphCellHeight;

    if (PixelWidth == 0 || PixelHeight == 0 || PixelX < 0 || PixelY < 0) {
        return DF_RETURN_GENERIC;
    }

    if ((U32)PixelX + PixelWidth > IntelGfxState.ActiveWidth || (U32)PixelY + PixelHeight > IntelGfxState.ActiveHeight) {
        return DF_RETURN_GENERIC;
    }

    FrameSize = IntelGfxState.FrameBufferSize;
    if (!IntelGfxEnsureShadowFrameBufferSize(FrameSize)) {
        return DF_RETURN_UNEXPECTED;
    }

    BytesPerPixel = Context->BitsPerPixel / 8;
    if (BytesPerPixel == 0) {
        return DF_RETURN_GENERIC;
    }

    RowBytes = PixelWidth * BytesPerPixel;
    FrameBuffer = (U8*)(LINEAR)IntelGfxState.FrameBufferLinear;
    ShadowBuffer = (U8*)(LINEAR)IntelGfxState.ShadowFrameBufferLinear;

    for (Row = 0; Row < PixelHeight; Row++) {
        U32 Offset = ((U32)PixelY + Row) * IntelGfxState.ActiveStride + ((U32)PixelX * BytesPerPixel);
        MemoryCopy(ShadowBuffer + Offset, FrameBuffer + Offset, RowBytes);
    }

    ShadowContext = *Context;
    ShadowContext.MemoryBase = ShadowBuffer;

    Result = GfxTextScrollRegion(&ShadowContext, Info);
    if (!Result) {
        return DF_RETURN_GENERIC;
    }

    for (Row = 0; Row < PixelHeight; Row++) {
        U32 Offset = ((U32)PixelY + Row) * IntelGfxState.ActiveStride + ((U32)PixelX * BytesPerPixel);
        MemoryCopy(FrameBuffer + Offset, ShadowBuffer + Offset, RowBytes);
    }

    IntelGfxState.PresentBlitCount++;
    return DF_RETURN_SUCCESS;
}

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

static void IntelGfxDrawLineInternal(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 Dx = 0;
    I32 Sx = 0;
    I32 Dy = 0;
    I32 Sy = 0;
    I32 Error = 0;
    COLOR Color = 0;
    U32 Pattern = 0;
    U32 PatternBit = 0;

    if (Context == NULL || Context->Pen == NULL || Context->Pen->TypeID != KOID_PEN) {
        return;
    }

    Color = Context->Pen->Color;
    Pattern = Context->Pen->Pattern;
    if (Pattern == 0) {
        Pattern = MAX_U32;
    }

    Dx = (X2 >= X1) ? (X2 - X1) : (X1 - X2);
    Sx = X1 < X2 ? 1 : -1;
    Dy = -((Y2 >= Y1) ? (Y2 - Y1) : (Y1 - Y2));
    Sy = Y1 < Y2 ? 1 : -1;
    Error = Dx + Dy;

    for (;;) {
        if (((Pattern >> (PatternBit & 31)) & 1) != 0) {
            COLOR PixelColor = Color;
            (void)IntelGfxWritePixelInternal(Context, X1, Y1, &PixelColor);
        }
        PatternBit++;

        if (X1 == X2 && Y1 == Y2) break;

        I32 DoubleError = Error << 1;
        if (DoubleError >= Dy) {
            Error += Dy;
            X1 += Sx;
        }
        if (DoubleError <= Dx) {
            Error += Dx;
            Y1 += Sy;
        }
    }
}

/************************************************************************/

static void IntelGfxDrawRectangleInternal(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 X = 0;
    I32 Y = 0;
    I32 Temp = 0;

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

    if (Context->Brush != NULL && Context->Brush->TypeID == KOID_BRUSH) {
        for (Y = Y1; Y <= Y2; Y++) {
            for (X = X1; X <= X2; X++) {
                COLOR FillColor = Context->Brush->Color;
                (void)IntelGfxWritePixelInternal(Context, X, Y, &FillColor);
            }
        }
    }

    if (Context->Pen != NULL && Context->Pen->TypeID == KOID_PEN) {
        IntelGfxDrawLineInternal(Context, X1, Y1, X2, Y1);
        IntelGfxDrawLineInternal(Context, X2, Y1, X2, Y2);
        IntelGfxDrawLineInternal(Context, X2, Y2, X1, Y2);
        IntelGfxDrawLineInternal(Context, X1, Y2, X1, Y1);
    }
}

/************************************************************************/

static U32 IntelGfxGetSurfaceBytesPerPixel(U32 Format) {
    switch (Format) {
        case GFX_FORMAT_UNKNOWN:
        case GFX_FORMAT_XRGB8888:
        case GFX_FORMAT_ARGB8888:
            return 4;
    }

    return 0;
}

/************************************************************************/

static LPINTEL_GFX_SURFACE IntelGfxFindSurface(U32 SurfaceId) {
    UINT Index = 0;

    if (SurfaceId == 0) {
        return NULL;
    }

    for (Index = 0; Index < INTEL_GFX_MAX_SURFACES; Index++) {
        if (IntelGfxSurfaces[Index].InUse && IntelGfxSurfaces[Index].SurfaceId == SurfaceId) {
            return &IntelGfxSurfaces[Index];
        }
    }

    return NULL;
}

/************************************************************************/

static LPINTEL_GFX_SURFACE IntelGfxAllocateSurfaceSlot(void) {
    UINT Index = 0;

    for (Index = 0; Index < INTEL_GFX_MAX_SURFACES; Index++) {
        if (!IntelGfxSurfaces[Index].InUse) {
            return &IntelGfxSurfaces[Index];
        }
    }

    return NULL;
}

/************************************************************************/

static U32 IntelGfxGenerateSurfaceId(void) {
    U32 Candidate = 0;
    UINT Attempt = 0;

    if (IntelGfxState.NextSurfaceId < INTEL_GFX_SURFACE_FIRST_ID) {
        IntelGfxState.NextSurfaceId = INTEL_GFX_SURFACE_FIRST_ID;
    }

    for (Attempt = 0; Attempt < MAX_U32; Attempt++) {
        Candidate = IntelGfxState.NextSurfaceId++;
        if (Candidate < INTEL_GFX_SURFACE_FIRST_ID) {
            IntelGfxState.NextSurfaceId = INTEL_GFX_SURFACE_FIRST_ID;
            Candidate = IntelGfxState.NextSurfaceId++;
        }

        if (IntelGfxFindSurface(Candidate) == NULL) {
            return Candidate;
        }
    }

    return 0;
}

/************************************************************************/

static void IntelGfxReleaseSurface(LPINTEL_GFX_SURFACE Surface) {
    if (Surface == NULL || !Surface->InUse) {
        return;
    }

    if (Surface->MemoryBase != NULL) {
        FreeRegion((LINEAR)Surface->MemoryBase, Surface->SizeBytes);
    }

    *Surface = (INTEL_GFX_SURFACE){0};
}

/************************************************************************/

void IntelGfxReleaseAllSurfaces(void) {
    UINT Index = 0;

    for (Index = 0; Index < INTEL_GFX_MAX_SURFACES; Index++) {
        IntelGfxReleaseSurface(&IntelGfxSurfaces[Index]);
    }

    IntelGfxState.ScanoutSurfaceId = 0;
    IntelGfxState.NextSurfaceId = INTEL_GFX_SURFACE_FIRST_ID;

    if (IntelGfxCursorRuntime.DeferredHandle != DEFERRED_WORK_INVALID_HANDLE) {
        DeferredWorkUnregister(IntelGfxCursorRuntime.DeferredHandle);
    }
    IntelGfxCursorRuntime = (INTEL_GFX_CURSOR_RUNTIME){.DeferredHandle = DEFERRED_WORK_INVALID_HANDLE};
}

/************************************************************************/

static BOOL IntelGfxResolveDirtyRegion(LPRECT DirtyRect, LPINTEL_GFX_SURFACE Surface, U32* X, U32* Y, U32* Width, U32* Height) {
    I32 X1 = 0;
    I32 Y1 = 0;
    I32 X2 = 0;
    I32 Y2 = 0;

    if (Surface == NULL || X == NULL || Y == NULL || Width == NULL || Height == NULL) {
        return FALSE;
    }

    if (DirtyRect != NULL) {
        X1 = DirtyRect->X1;
        Y1 = DirtyRect->Y1;
        X2 = DirtyRect->X2;
        Y2 = DirtyRect->Y2;
    }

    if (DirtyRect == NULL || X2 < X1 || Y2 < Y1) {
        X1 = 0;
        Y1 = 0;
        X2 = (I32)Surface->Width - 1;
        Y2 = (I32)Surface->Height - 1;
    }

    if (X1 < 0) X1 = 0;
    if (Y1 < 0) Y1 = 0;
    if (X2 >= (I32)Surface->Width) X2 = (I32)Surface->Width - 1;
    if (Y2 >= (I32)Surface->Height) Y2 = (I32)Surface->Height - 1;
    if (X2 < X1 || Y2 < Y1) {
        return FALSE;
    }

    *X = (U32)X1;
    *Y = (U32)Y1;
    *Width = (U32)(X2 - X1 + 1);
    *Height = (U32)(Y2 - Y1 + 1);
    return TRUE;
}

/************************************************************************/

static UINT IntelGfxBlitSurfaceRegionToScanout(LPINTEL_GFX_SURFACE Surface, U32 X, U32 Y, U32 Width, U32 Height) {
    U32 Row = 0;
    U32 CopyBytes = 0;

    if (Surface == NULL || Surface->MemoryBase == NULL || Width == 0 || Height == 0) {
        return DF_RETURN_GENERIC;
    }

    if (IntelGfxState.FrameBufferLinear == 0 || IntelGfxState.FrameBufferSize == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    if (X + Width > IntelGfxState.ActiveWidth || Y + Height > IntelGfxState.ActiveHeight || X + Width > Surface->Width ||
        Y + Height > Surface->Height) {
        return DF_RETURN_GENERIC;
    }

    CopyBytes = Width << 2;
    for (Row = 0; Row < Height; Row++) {
        U32 SourceOffset = (Y + Row) * Surface->Pitch + (X << 2);
        U32 DestinationOffset = (Y + Row) * IntelGfxState.ActiveStride + (X << 2);
        U8* Source = Surface->MemoryBase + SourceOffset;
        U8* Destination = (U8*)(LINEAR)IntelGfxState.FrameBufferLinear + DestinationOffset;
        MemoryCopy(Destination, Source, CopyBytes);
    }

    IntelGfxState.PresentBlitCount++;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

UINT IntelGfxAllocateSurface(LPGFX_SURFACE_INFO Info) {
    LPINTEL_GFX_SURFACE Surface = NULL;
    U32 BytesPerPixel = 0;
    U32 Format = 0;
    U32 Width = 0;
    U32 Height = 0;
    U32 Pitch = 0;
    U32 SizeBytes = 0;
    U32 Flags = 0;
    LINEAR MemoryLinear = 0;
    U8* Memory = NULL;
    U32 SurfaceId = 0;

    if ((IntelGfxDriver.Flags & DRIVER_FLAG_READY) == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    SAFE_USE(Info) {
        Width = Info->Width;
        Height = Info->Height;
        Format = (Info->Format == GFX_FORMAT_UNKNOWN) ? GFX_FORMAT_XRGB8888 : Info->Format;
        Flags = Info->Flags;
    }

    if (Width == 0 || Height == 0) {
        return DF_RETURN_GENERIC;
    }

    if (Width > IntelGfxState.Capabilities.MaxWidth || Height > IntelGfxState.Capabilities.MaxHeight || Width > IntelGfxState.ActiveWidth ||
        Height > IntelGfxState.ActiveHeight) {
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    BytesPerPixel = IntelGfxGetSurfaceBytesPerPixel(Format);
    if (BytesPerPixel == 0) {
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    if (Width > MAX_U32 / BytesPerPixel) {
        return DF_RETURN_GENERIC;
    }

    Pitch = Width * BytesPerPixel;
    if (Height > MAX_U32 / Pitch) {
        return DF_RETURN_GENERIC;
    }
    SizeBytes = Pitch * Height;

    Surface = IntelGfxAllocateSurfaceSlot();
    if (Surface == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    SurfaceId = IntelGfxGenerateSurfaceId();
    if (SurfaceId == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    MemoryLinear = AllocRegion(VMA_KERNEL,
                               0,
                               SizeBytes,
                               ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER,
                               TEXT("IntelGfxSurface"));
    if (MemoryLinear == 0) {
        return DF_RETURN_UNEXPECTED;
    }
    Memory = (U8*)(LINEAR)MemoryLinear;
    MemorySet(Memory, 0, SizeBytes);

    *Surface = (INTEL_GFX_SURFACE){
        .InUse = TRUE,
        .SurfaceId = SurfaceId,
        .Width = Width,
        .Height = Height,
        .Format = Format,
        .Pitch = Pitch,
        .Flags = Flags | GFX_SURFACE_FLAG_CPU_VISIBLE,
        .SizeBytes = SizeBytes,
        .MemoryBase = Memory
    };

    SAFE_USE(Info) {
        Info->SurfaceId = Surface->SurfaceId;
        Info->Format = Surface->Format;
        Info->Pitch = Surface->Pitch;
        Info->MemoryBase = Surface->MemoryBase;
        Info->Flags = Surface->Flags;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

UINT IntelGfxFreeSurface(LPGFX_SURFACE_INFO Info) {
    U32 SurfaceId = 0;
    LPINTEL_GFX_SURFACE Surface = NULL;

    SAFE_USE(Info) { SurfaceId = Info->SurfaceId; }
    if (SurfaceId == 0) {
        return DF_RETURN_GENERIC;
    }

    Surface = IntelGfxFindSurface(SurfaceId);
    if (Surface == NULL) {
        return DF_RETURN_UNEXPECTED;
    }

    if (IntelGfxState.ScanoutSurfaceId == SurfaceId) {
        IntelGfxState.ScanoutSurfaceId = 0;
    }

    IntelGfxReleaseSurface(Surface);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

UINT IntelGfxSetScanout(LPGFX_SCANOUT_INFO Info) {
    LPINTEL_GFX_SURFACE Surface = NULL;

    SAFE_USE(Info) { Surface = IntelGfxFindSurface(Info->SurfaceId); }
    if (Surface == NULL) {
        return DF_RETURN_GENERIC;
    }

    if (Surface->Width != IntelGfxState.ActiveWidth || Surface->Height != IntelGfxState.ActiveHeight) {
        WARNING(TEXT("[IntelGfxSetScanout] Surface dimensions mismatch (%ux%u expected=%ux%u)"),
            Surface->Width,
            Surface->Height,
            IntelGfxState.ActiveWidth,
            IntelGfxState.ActiveHeight);
        return DF_GFX_ERROR_MODEUNAVAIL;
    }

    IntelGfxState.ScanoutSurfaceId = Surface->SurfaceId;

    SAFE_USE(Info) {
        Info->Width = Surface->Width;
        Info->Height = Surface->Height;
        Info->Format = Surface->Format;
    }

    return DF_RETURN_SUCCESS;
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
    I32 X1 = 0;
    I32 Y1 = 0;
    I32 X2 = 0;
    I32 Y2 = 0;
    I32 Temp = 0;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxDrawLineInternal(Context, Info->X1, Info->Y1, Info->X2, Info->Y2);
    X1 = Info->X1;
    Y1 = Info->Y1;
    X2 = Info->X2;
    Y2 = Info->Y2;
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
    if (X2 >= X1 && Y2 >= Y1) {
        (void)IntelGfxFlushContextRegionToScanout(Context, X1, Y1, (U32)(X2 - X1 + 1), (U32)(Y2 - Y1 + 1));
    }
    UnlockMutex(&(Context->Mutex));

    return 1;
}

/************************************************************************/

UINT IntelGfxRectangle(LPRECTINFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    I32 X1 = 0;
    I32 Y1 = 0;
    I32 X2 = 0;
    I32 Y2 = 0;
    I32 Temp = 0;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxDrawRectangleInternal(Context, Info->X1, Info->Y1, Info->X2, Info->Y2);
    X1 = Info->X1;
    Y1 = Info->Y1;
    X2 = Info->X2;
    Y2 = Info->Y2;
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
    if (X2 >= X1 && Y2 >= Y1) {
        (void)IntelGfxFlushContextRegionToScanout(Context, X1, Y1, (U32)(X2 - X1 + 1), (U32)(Y2 - Y1 + 1));
    }
    UnlockMutex(&(Context->Mutex));

    return 1;
}

/************************************************************************/

UINT IntelGfxTextPutCell(LPGFX_TEXT_CELL_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxHandleTextWriteActivity(Context);
    Result = GfxTextPutCell(Context, Info);
    UnlockMutex(&(Context->Mutex));
    return Result ? 1 : 0;
}

/************************************************************************/

UINT IntelGfxTextClearRegion(LPGFX_TEXT_REGION_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxHandleTextWriteActivity(Context);
    Result = GfxTextClearRegion(Context, Info);
    UnlockMutex(&(Context->Mutex));
    return Result ? 1 : 0;
}

/************************************************************************/

UINT IntelGfxTextScrollRegion(LPGFX_TEXT_REGION_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    UINT Result = DF_RETURN_GENERIC;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxHandleTextWriteActivity(Context);
    Result = IntelGfxScrollRegionViaShadow(Context, Info);
    UnlockMutex(&(Context->Mutex));
    return (Result == DF_RETURN_SUCCESS) ? 1 : 0;
}

/************************************************************************/

UINT IntelGfxTextSetCursor(LPGFX_TEXT_CURSOR_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    BOOL Result = FALSE;
    I32 PixelX = 0;
    I32 PixelY = 0;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    Result = GfxTextSetCursor(Context, Info);
    if (Result) {
        U32 CursorHeight = (Info->CellHeight >= 4) ? 2 : 1;
        PixelX = (I32)(Info->CellX * Info->CellWidth);
        PixelY = (I32)(Info->CellY * Info->CellHeight) + (I32)Info->CellHeight - (I32)CursorHeight;
        (void)IntelGfxFlushContextRegionToScanout(Context, PixelX, PixelY, Info->CellWidth, CursorHeight);
    }
    UnlockMutex(&(Context->Mutex));
    return Result ? 1 : 0;
}

/************************************************************************/

UINT IntelGfxTextSetCursorVisible(LPGFX_TEXT_CURSOR_VISIBLE_INFO Info) {
    LPGRAPHICSCONTEXT Context = NULL;
    GFX_TEXT_CURSOR_VISIBLE_INFO CursorVisibleInfo;
    BOOL Result = FALSE;

    if (Info == NULL) {
        return 0;
    }

    Context = (LPGRAPHICSCONTEXT)Info->GC;
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return 0;
    }

    LockMutex(&(Context->Mutex), INFINITY);
    IntelGfxEnsureCursorDeferredRegistration();
    if (IntelGfxCursorRuntime.DeferredHandle == DEFERRED_WORK_INVALID_HANDLE) {
        Result = GfxTextSetCursorVisible(Context, Info);
        UnlockMutex(&(Context->Mutex));
        return Result ? 1 : 0;
    }

    if (Info->IsVisible == FALSE) {
        CursorVisibleInfo = (GFX_TEXT_CURSOR_VISIBLE_INFO){.IsVisible = FALSE};
        (void)GfxTextSetCursorVisible(Context, &CursorVisibleInfo);
        IntelGfxArmCursorShowCooldown();
        UnlockMutex(&(Context->Mutex));
        return 1;
    }

    IntelGfxArmCursorShowCooldown();
    UnlockMutex(&(Context->Mutex));
    return 1;
}

/************************************************************************/

UINT IntelGfxPresent(LPGFX_PRESENT_INFO Info) {
    LPINTEL_GFX_SURFACE Surface = NULL;
    RECT DirtyRect = {0};
    U32 SourceSurfaceId = 0;
    U32 X = 0;
    U32 Y = 0;
    U32 Width = 0;
    U32 Height = 0;
    UINT Result = DF_RETURN_SUCCESS;

    if (IntelGfxState.FrameBufferLinear == 0 || IntelGfxState.FrameBufferSize == 0) {
        return DF_RETURN_UNEXPECTED;
    }

    if (Info == NULL) {
        return DF_RETURN_GENERIC;
    }

    SourceSurfaceId = Info->SurfaceId;
    DirtyRect = Info->DirtyRect;

    if (SourceSurfaceId == 0) {
        SourceSurfaceId = IntelGfxState.ScanoutSurfaceId;
    }

    if (SourceSurfaceId == 0) {
        return DF_RETURN_SUCCESS;
    }

    Surface = IntelGfxFindSurface(SourceSurfaceId);
    if (Surface == NULL || Surface->MemoryBase == NULL) {
        return DF_RETURN_GENERIC;
    }

    if (!IntelGfxResolveDirtyRegion(&DirtyRect, Surface, &X, &Y, &Width, &Height)) {
        return DF_RETURN_SUCCESS;
    }

    LockMutex(&(IntelGfxState.Context.Mutex), INFINITY);
    Result = IntelGfxBlitSurfaceRegionToScanout(Surface, X, Y, Width, Height);
    UnlockMutex(&(IntelGfxState.Context.Mutex));
    return Result;
}

/************************************************************************/
