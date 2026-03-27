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


    Desktop pipeline trace

\************************************************************************/

#include "Desktop-Private.h"
#include "Desktop-PipelineTrace.h"
#include "Kernel.h"
#include "KernelData.h"
#include "utils/Graphics-Utils.h"

/************************************************************************/

#define DESKTOP_PIPELINE_TRACE_PAUSE_MS 1000
#define DESKTOP_PIPELINE_TRACE_BORDER_THICKNESS 1
#define DESKTOP_PIPELINE_TRACE_RED 0x00FF0000
#define DESKTOP_PIPELINE_TRACE_GREEN 0x0000FF00

/************************************************************************/

typedef struct tag_DESKTOP_PIPELINE_TRACE_BUFFER {
    RECT Bounds;
    U8* Pixels;
    UINT RowBytes;
    UINT Height;
    UINT Size;
} DESKTOP_PIPELINE_TRACE_BUFFER, *LPDESKTOP_PIPELINE_TRACE_BUFFER;

/************************************************************************/

/**
 * @brief Return the bytes used by one framebuffer pixel.
 * @param Context Target graphics context.
 * @return Pixel size in bytes.
 */
static UINT DesktopPipelineTraceGetBytesPerPixel(LPGRAPHICSCONTEXT Context) {
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) return 0;
    return (Context->BitsPerPixel + 7) / 8;
}

/************************************************************************/

/**
 * @brief Clamp one screen rectangle to one graphics context bounds.
 * @param Context Target graphics context.
 * @param Rect Rectangle updated in place.
 * @return TRUE when a non-empty clamped rectangle remains.
 */
static BOOL DesktopPipelineTraceClampRectToContext(LPGRAPHICSCONTEXT Context, LPRECT Rect) {
    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;
    if (Rect == NULL) return FALSE;

    if (Rect->X1 < 0) Rect->X1 = 0;
    if (Rect->Y1 < 0) Rect->Y1 = 0;
    if (Rect->X2 >= Context->Width) Rect->X2 = Context->Width - 1;
    if (Rect->Y2 >= Context->Height) Rect->Y2 = Context->Height - 1;

    return (Rect->X1 <= Rect->X2 && Rect->Y1 <= Rect->Y2) ? TRUE : FALSE;
}

/************************************************************************/

/**
 * @brief Build the bounding rectangle covering one region and one optional rectangle.
 * @param Region Region to bound.
 * @param ExtraRect Optional extra rectangle.
 * @param Bounds Receives the resulting bounds.
 * @return TRUE when bounds were produced.
 */
static BOOL DesktopPipelineTraceBuildBounds(LPRECT_REGION Region, LPRECT ExtraRect, LPRECT Bounds) {
    RECT CurrentRect;
    UINT RectCount;
    UINT RectIndex;
    BOOL HasBounds = FALSE;

    if (Region == NULL || Bounds == NULL) return FALSE;

    RectCount = RectRegionGetCount(Region);
    for (RectIndex = 0; RectIndex < RectCount; RectIndex++) {
        if (RectRegionGetRect(Region, RectIndex, &CurrentRect) == FALSE) continue;

        if (HasBounds == FALSE) {
            *Bounds = CurrentRect;
            HasBounds = TRUE;
            continue;
        }

        if (CurrentRect.X1 < Bounds->X1) Bounds->X1 = CurrentRect.X1;
        if (CurrentRect.Y1 < Bounds->Y1) Bounds->Y1 = CurrentRect.Y1;
        if (CurrentRect.X2 > Bounds->X2) Bounds->X2 = CurrentRect.X2;
        if (CurrentRect.Y2 > Bounds->Y2) Bounds->Y2 = CurrentRect.Y2;
    }

    if (ExtraRect != NULL) {
        if (HasBounds == FALSE) {
            *Bounds = *ExtraRect;
            HasBounds = TRUE;
        } else {
            if (ExtraRect->X1 < Bounds->X1) Bounds->X1 = ExtraRect->X1;
            if (ExtraRect->Y1 < Bounds->Y1) Bounds->Y1 = ExtraRect->Y1;
            if (ExtraRect->X2 > Bounds->X2) Bounds->X2 = ExtraRect->X2;
            if (ExtraRect->Y2 > Bounds->Y2) Bounds->Y2 = ExtraRect->Y2;
        }
    }

    return HasBounds;
}

/************************************************************************/

/**
 * @brief Acquire the desktop root window and one direct scanout context.
 * @param Window Any window on the target desktop.
 * @param RootWindow Receives the root window.
 * @param ContextOut Receives the scanout graphics context.
 * @return TRUE when the context is available.
 */
static BOOL DesktopPipelineTraceAcquireContext(LPWINDOW Window, LPWINDOW* RootWindow, LPGRAPHICSCONTEXT* ContextOut) {
    LPDESKTOP Desktop;

    if (RootWindow == NULL || ContextOut == NULL) return FALSE;
    *RootWindow = NULL;
    *ContextOut = NULL;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;

    Desktop = DesktopGetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (Desktop->Mode != DESKTOP_MODE_GRAPHICS) return FALSE;
    if (GetWindowPipelineTraceEnabled() == FALSE) return FALSE;
    if (DesktopGetRootWindow(Desktop, RootWindow) == FALSE) return FALSE;
    if (*RootWindow == NULL || (*RootWindow)->TypeID != KOID_WINDOW) return FALSE;

    if (DesktopGetWindowGraphicsContext(*RootWindow, TRUE, ContextOut) == FALSE) return FALSE;
    if (*ContextOut == NULL || (*ContextOut)->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    ResetGraphicsContext(*ContextOut);

    LockMutex(&((*ContextOut)->Mutex), INFINITY);
    (*ContextOut)->Origin.X = 0;
    (*ContextOut)->Origin.Y = 0;
    UnlockMutex(&((*ContextOut)->Mutex));

    return TRUE;
}

/************************************************************************/

/**
 * @brief Save one screen rectangle from the live framebuffer.
 * @param Context Direct scanout graphics context.
 * @param Bounds Screen rectangle to save.
 * @param Buffer Receives the saved pixels.
 * @return TRUE on success.
 */
static BOOL DesktopPipelineTraceCaptureBuffer(
    LPGRAPHICSCONTEXT Context,
    LPRECT Bounds,
    LPDESKTOP_PIPELINE_TRACE_BUFFER Buffer) {
    U8* SourceRow;
    U8* DestinationRow;
    UINT BytesPerPixel;
    UINT Width;
    UINT Height;
    UINT RowBytes;
    UINT Y;

    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;
    if (Bounds == NULL || Buffer == NULL) return FALSE;

    BytesPerPixel = DesktopPipelineTraceGetBytesPerPixel(Context);
    if (BytesPerPixel == 0) return FALSE;

    Width = (UINT)(Bounds->X2 - Bounds->X1 + 1);
    Height = (UINT)(Bounds->Y2 - Bounds->Y1 + 1);
    RowBytes = Width * BytesPerPixel;

    Buffer->Pixels = (U8*)KernelHeapAlloc(RowBytes * Height);
    if (Buffer->Pixels == NULL) return FALSE;

    Buffer->Bounds = *Bounds;
    Buffer->RowBytes = RowBytes;
    Buffer->Height = Height;
    Buffer->Size = RowBytes * Height;

    LockMutex(&(Context->Mutex), INFINITY);

    for (Y = 0; Y < Height; Y++) {
        SourceRow = Context->MemoryBase + ((Bounds->Y1 + (I32)Y) * Context->BytesPerScanLine) + (Bounds->X1 * BytesPerPixel);
        DestinationRow = Buffer->Pixels + (Y * RowBytes);
        MemoryCopy(DestinationRow, SourceRow, RowBytes);
    }

    UnlockMutex(&(Context->Mutex));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Restore one previously saved screen rectangle to the live framebuffer.
 * @param Context Direct scanout graphics context.
 * @param Buffer Saved pixel buffer.
 */
static void DesktopPipelineTraceRestoreBuffer(LPGRAPHICSCONTEXT Context, LPDESKTOP_PIPELINE_TRACE_BUFFER Buffer) {
    U8* SourceRow;
    U8* DestinationRow;
    UINT BytesPerPixel;
    UINT Width;
    UINT Y;

    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) return;
    if (Buffer == NULL || Buffer->Pixels == NULL) return;

    BytesPerPixel = DesktopPipelineTraceGetBytesPerPixel(Context);
    if (BytesPerPixel == 0) return;

    Width = (UINT)(Buffer->Bounds.X2 - Buffer->Bounds.X1 + 1);
    if (Buffer->RowBytes != Width * BytesPerPixel) return;

    LockMutex(&(Context->Mutex), INFINITY);

    for (Y = 0; Y < Buffer->Height; Y++) {
        SourceRow = Buffer->Pixels + (Y * Buffer->RowBytes);
        DestinationRow =
            Context->MemoryBase + ((Buffer->Bounds.Y1 + (I32)Y) * Context->BytesPerScanLine) + (Buffer->Bounds.X1 * BytesPerPixel);
        MemoryCopy(DestinationRow, SourceRow, Buffer->RowBytes);
    }

    UnlockMutex(&(Context->Mutex));
}

/************************************************************************/

/**
 * @brief Release one saved screen buffer.
 * @param Buffer Buffer released in place.
 */
static void DesktopPipelineTraceReleaseBuffer(LPDESKTOP_PIPELINE_TRACE_BUFFER Buffer) {
    if (Buffer == NULL) return;

    if (Buffer->Pixels != NULL) {
        KernelHeapFree(Buffer->Pixels);
    }

    MemorySet(Buffer, 0, sizeof(*Buffer));
}

/************************************************************************/

/**
 * @brief Draw one rectangle border in screen coordinates.
 * @param Context Direct scanout graphics context.
 * @param Rect Screen rectangle to draw.
 * @param Color Border color.
 */
static void DesktopPipelineTraceDrawBorder(LPGRAPHICSCONTEXT Context, LPRECT Rect, COLOR Color) {
    RECT ClampedRect;
    I32 Thickness = DESKTOP_PIPELINE_TRACE_BORDER_THICKNESS;

    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) return;
    if (Rect == NULL) return;

    ClampedRect = *Rect;
    if (DesktopPipelineTraceClampRectToContext(Context, &ClampedRect) == FALSE) return;

    (void)GraphicsFillSolidRect(Context, ClampedRect.X1, ClampedRect.Y1, ClampedRect.X2, ClampedRect.Y1 + Thickness - 1, Color);
    (void)GraphicsFillSolidRect(Context, ClampedRect.X1, ClampedRect.Y2 - Thickness + 1, ClampedRect.X2, ClampedRect.Y2, Color);
    (void)GraphicsFillSolidRect(Context, ClampedRect.X1, ClampedRect.Y1, ClampedRect.X1 + Thickness - 1, ClampedRect.Y2, Color);
    (void)GraphicsFillSolidRect(Context, ClampedRect.X2 - Thickness + 1, ClampedRect.Y1, ClampedRect.X2, ClampedRect.Y2, Color);
}

/************************************************************************/

/**
 * @brief Draw one region as screen-space borders.
 * @param Context Direct scanout graphics context.
 * @param Region Region to draw.
 * @param Color Border color.
 */
static void DesktopPipelineTraceDrawRegion(LPGRAPHICSCONTEXT Context, LPRECT_REGION Region, COLOR Color) {
    RECT CurrentRect;
    UINT RectCount;
    UINT RectIndex;

    if (Context == NULL || Context->TypeID != KOID_GRAPHICSCONTEXT) return;
    if (Region == NULL) return;

    RectCount = RectRegionGetCount(Region);
    for (RectIndex = 0; RectIndex < RectCount; RectIndex++) {
        if (RectRegionGetRect(Region, RectIndex, &CurrentRect) == FALSE) continue;
        DesktopPipelineTraceDrawBorder(Context, &CurrentRect, Color);
    }
}

/************************************************************************/

/**
 * @brief Trace one region and one optional extra rectangle on screen.
 * @param Window Any window on the target desktop.
 * @param Region Main region traced in red.
 * @param ExtraRect Optional rectangle traced in green.
 */
static void DesktopPipelineTraceShow(LPWINDOW Window, LPRECT_REGION Region, LPRECT ExtraRect) {
    DESKTOP_PIPELINE_TRACE_BUFFER Buffer;
    LPGRAPHICSCONTEXT Context = NULL;
    LPWINDOW RootWindow = NULL;
    RECT Bounds;

    MemorySet(&Buffer, 0, sizeof(Buffer));

    if (Region == NULL) return;
    if (DesktopPipelineTraceAcquireContext(Window, &RootWindow, &Context) == FALSE) return;
    if (DesktopPipelineTraceBuildBounds(Region, ExtraRect, &Bounds) == FALSE) return;
    if (DesktopPipelineTraceClampRectToContext(Context, &Bounds) == FALSE) return;
    if (DesktopPipelineTraceCaptureBuffer(Context, &Bounds, &Buffer) == FALSE) return;

    DesktopPipelineTraceDrawRegion(Context, Region, DESKTOP_PIPELINE_TRACE_RED);
    if (ExtraRect != NULL) {
        DesktopPipelineTraceDrawBorder(Context, ExtraRect, DESKTOP_PIPELINE_TRACE_GREEN);
    }

    Sleep(DESKTOP_PIPELINE_TRACE_PAUSE_MS);
    DesktopPipelineTraceRestoreBuffer(Context, &Buffer);
    DesktopPipelineTraceReleaseBuffer(&Buffer);

    UNUSED(RootWindow);
}

/************************************************************************/

/**
 * @brief Trace one computed region before the pipeline continues.
 * @param Window Any window on the target desktop.
 * @param Region Computed screen-space region.
 */
void DesktopPipelineTraceRegion(LPWINDOW Window, LPRECT_REGION Region) {
    if (GetWindowPipelineTraceEnabled() == FALSE) return;
    DesktopPipelineTraceShow(Window, Region, NULL);
}

/************************************************************************/

/**
 * @brief Trace one draw dispatch region and the target client rectangle.
 * @param Window Target window receiving the draw callback.
 * @param ClipRect Screen-space draw clip rectangle.
 * @param ClientScreenRect Screen-space client rectangle.
 */
void DesktopPipelineTraceWindowDrawDispatch(LPWINDOW Window, LPRECT ClipRect, LPRECT ClientScreenRect) {
    RECT RegionStorage[1];
    RECT_REGION Region;

    if (GetWindowPipelineTraceEnabled() == FALSE) return;
    if (ClipRect == NULL) return;
    if (RectRegionInit(&Region, RegionStorage, 1) == FALSE) return;
    RectRegionReset(&Region);
    if (RectRegionAddRect(&Region, ClipRect) == FALSE) return;

    DesktopPipelineTraceShow(Window, &Region, ClientScreenRect);
}
