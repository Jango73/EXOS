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


    Desktop cursor ownership and rendering

\************************************************************************/

#include "Desktop-Cursor.h"

#include "Desktop.h"
#include "DisplaySession.h"
#include "GFX.h"
#include "Kernel.h"
#include "Log.h"
#include "input/MouseDispatcher.h"

/************************************************************************/

#define DESKTOP_CURSOR_WIDTH 10
#define DESKTOP_CURSOR_HEIGHT 10

/************************************************************************/

/**
 * @brief Clamp one value to inclusive bounds.
 * @param Value Input value.
 * @param Minimum Minimum bound.
 * @param Maximum Maximum bound.
 * @return Clamped value.
 */
static I32 ClampI32(I32 Value, I32 Minimum, I32 Maximum) {
    if (Value < Minimum) return Minimum;
    if (Value > Maximum) return Maximum;
    return Value;
}

/************************************************************************/

/**
 * @brief Build cursor rectangle in screen coordinates.
 * @param X Cursor X position.
 * @param Y Cursor Y position.
 * @param RectOut Output rectangle.
 */
static void DesktopCursorBuildRect(I32 X, I32 Y, LPRECT RectOut) {
    if (RectOut == NULL) return;

    RectOut->X1 = X;
    RectOut->Y1 = Y;
    RectOut->X2 = X + DESKTOP_CURSOR_WIDTH - 1;
    RectOut->Y2 = Y + DESKTOP_CURSOR_HEIGHT - 1;
}

/************************************************************************/

/**
 * @brief Check whether two rectangles overlap.
 * @param Left First rectangle.
 * @param Right Second rectangle.
 * @return TRUE when overlap exists.
 */
static BOOL DesktopCursorRectIntersects(LPRECT Left, LPRECT Right) {
    if (Left == NULL || Right == NULL) return FALSE;

    if (Left->X2 < Right->X1 || Right->X2 < Left->X1) return FALSE;
    if (Left->Y2 < Right->Y1 || Right->Y2 < Left->Y1) return FALSE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Intersect two rectangles.
 * @param Left First rectangle.
 * @param Right Second rectangle.
 * @param Result Output intersection.
 * @return TRUE on non-empty intersection.
 */
static BOOL DesktopCursorIntersectRect(LPRECT Left, LPRECT Right, LPRECT Result) {
    if (Left == NULL || Right == NULL || Result == NULL) return FALSE;

    Result->X1 = Left->X1 > Right->X1 ? Left->X1 : Right->X1;
    Result->Y1 = Left->Y1 > Right->Y1 ? Left->Y1 : Right->Y1;
    Result->X2 = Left->X2 < Right->X2 ? Left->X2 : Right->X2;
    Result->Y2 = Left->Y2 < Right->Y2 ? Left->Y2 : Right->Y2;

    return Result->X1 <= Result->X2 && Result->Y1 <= Result->Y2;
}

/************************************************************************/

/**
 * @brief Build one ABI header.
 * @param Header Output header.
 * @param Size Structure size.
 */
static void DesktopCursorInitializeHeader(ABI_HEADER* Header, U32 Size) {
    if (Header == NULL) return;

    Header->Size = Size;
    Header->Version = EXOS_ABI_VERSION;
    Header->Flags = 0;
}

/************************************************************************/

/**
 * @brief Convert cursor fallback reason to descriptive text.
 * @param Reason Fallback reason identifier.
 * @return Constant reason text.
 */
static LPCSTR DesktopCursorFallbackReasonToText(U32 Reason) {
    switch (Reason) {
        case DESKTOP_CURSOR_FALLBACK_NONE:
            return TEXT("none");
        case DESKTOP_CURSOR_FALLBACK_NOT_GRAPHICS:
            return TEXT("not_graphics");
        case DESKTOP_CURSOR_FALLBACK_NO_CAPABILITIES:
            return TEXT("no_capabilities");
        case DESKTOP_CURSOR_FALLBACK_NO_CURSOR_PLANE:
            return TEXT("no_cursor_plane");
        case DESKTOP_CURSOR_FALLBACK_SET_SHAPE_FAILED:
            return TEXT("set_shape_failed");
        case DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED:
            return TEXT("set_position_failed");
        case DESKTOP_CURSOR_FALLBACK_SET_VISIBLE_FAILED:
            return TEXT("set_visible_failed");
        default:
            return TEXT("unknown");
    }
}

/************************************************************************/

/**
 * @brief Update cursor diagnostics path and fallback reason.
 * @param Desktop Target desktop.
 * @param Path New cursor path value.
 * @param Reason New fallback reason value.
 * @param DriverStatus Driver status associated with fallback.
 */
static void DesktopCursorSetPathState(LPDESKTOP Desktop, U32 Path, U32 Reason, UINT DriverStatus) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;

    LockMutex(&(Desktop->Mutex), INFINITY);

    if (Desktop->CursorRenderPath == Path && Desktop->CursorFallbackReason == Reason) {
        UnlockMutex(&(Desktop->Mutex));
        return;
    }

    Desktop->CursorRenderPath = Path;
    Desktop->CursorFallbackReason = Reason;

    UnlockMutex(&(Desktop->Mutex));

    if (Path == DESKTOP_CURSOR_PATH_HARDWARE) {
        DEBUG(TEXT("[DesktopCursorSetPathState] Cursor path=hardware"));
    } else {
        WARNING(TEXT("[DesktopCursorSetPathState] Cursor path=software reason=%s"),
            DesktopCursorFallbackReasonToText(Reason));
        DEBUG(TEXT("[DesktopCursorSetPathState] Software fallback detail reason=%s status=%u"),
            DesktopCursorFallbackReasonToText(Reason),
            DriverStatus);
    }
}

/************************************************************************/

/**
 * @brief Convert one screen-space cursor rectangle to desktop-window coordinates and invalidate it.
 * @param Desktop Target desktop.
 * @param CursorRectScreen Cursor rectangle in screen coordinates.
 */
static void DesktopCursorInvalidateScreenRect(LPDESKTOP Desktop, LPRECT CursorRectScreen) {
    RECT WindowRect;
    RECT LocalRect;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;
    if (CursorRectScreen == NULL) return;

    if (Desktop->Window != NULL && Desktop->Window->TypeID == KOID_WINDOW) {
        LockMutex(&(Desktop->Window->Mutex), INFINITY);
        WindowRect = Desktop->Window->ScreenRect;
        UnlockMutex(&(Desktop->Window->Mutex));

        if (DesktopCursorRectIntersects(CursorRectScreen, &WindowRect) == FALSE) {
            return;
        }

        LocalRect.X1 = CursorRectScreen->X1 - WindowRect.X1;
        LocalRect.Y1 = CursorRectScreen->Y1 - WindowRect.Y1;
        LocalRect.X2 = CursorRectScreen->X2 - WindowRect.X1;
        LocalRect.Y2 = CursorRectScreen->Y2 - WindowRect.Y1;

        (void)InvalidateWindowRect((HANDLE)Desktop->Window, &LocalRect);
    }
}

/************************************************************************/

/**
 * @brief Invalidate old and new software cursor regions.
 * @param Desktop Target desktop.
 * @param OldX Old cursor X.
 * @param OldY Old cursor Y.
 * @param NewX New cursor X.
 * @param NewY New cursor Y.
 */
static void DesktopCursorInvalidateMove(LPDESKTOP Desktop, I32 OldX, I32 OldY, I32 NewX, I32 NewY) {
    RECT OldRect;
    RECT NewRect;

    DesktopCursorBuildRect(OldX, OldY, &OldRect);
    DesktopCursorBuildRect(NewX, NewY, &NewRect);

    DesktopCursorInvalidateScreenRect(Desktop, &OldRect);
    DesktopCursorInvalidateScreenRect(Desktop, &NewRect);
}

/************************************************************************/

/**
 * @brief Try to enable hardware cursor path on one graphics backend.
 * @param Desktop Target desktop.
 * @param GraphicsDriver Active graphics driver.
 * @return TRUE when hardware path is active.
 */
static BOOL DesktopCursorTryEnableHardware(LPDESKTOP Desktop, LPDRIVER GraphicsDriver) {
    GFX_CAPABILITIES Capabilities;
    GFX_CURSOR_SHAPE_INFO ShapeInfo;
    GFX_CURSOR_POSITION_INFO PositionInfo;
    GFX_CURSOR_VISIBLE_INFO VisibleInfo;
    UINT Status;
    U32 Pixels[DESKTOP_CURSOR_WIDTH * DESKTOP_CURSOR_HEIGHT];
    U32 X;
    U32 Y;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (GraphicsDriver == NULL || GraphicsDriver->Command == NULL) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_NO_CAPABILITIES, DF_RETURN_GENERIC);
        return FALSE;
    }

    MemorySet(&Capabilities, 0, sizeof(Capabilities));
    DesktopCursorInitializeHeader(&(Capabilities.Header), sizeof(Capabilities));

    Status = GraphicsDriver->Command(DF_GFX_GETCAPABILITIES, (UINT)(LPVOID)&Capabilities);
    if (Status != DF_RETURN_SUCCESS) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_NO_CAPABILITIES, Status);
        return FALSE;
    }

    if (Capabilities.HasCursorPlane == FALSE) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_NO_CURSOR_PLANE, DF_RETURN_SUCCESS);
        return FALSE;
    }

    for (Y = 0; Y < DESKTOP_CURSOR_HEIGHT; Y++) {
        for (X = 0; X < DESKTOP_CURSOR_WIDTH; X++) {
            BOOL IsBorder = (X == 0 || Y == 0 || X == DESKTOP_CURSOR_WIDTH - 1 || Y == DESKTOP_CURSOR_HEIGHT - 1);
            Pixels[(Y * DESKTOP_CURSOR_WIDTH) + X] = IsBorder ? 0xFF000000 : 0xFFFFFFFF;
        }
    }

    MemorySet(&ShapeInfo, 0, sizeof(ShapeInfo));
    DesktopCursorInitializeHeader(&(ShapeInfo.Header), sizeof(ShapeInfo));
    ShapeInfo.Width = DESKTOP_CURSOR_WIDTH;
    ShapeInfo.Height = DESKTOP_CURSOR_HEIGHT;
    ShapeInfo.HotspotX = 0;
    ShapeInfo.HotspotY = 0;
    ShapeInfo.Format = GFX_CURSOR_FORMAT_ARGB8888;
    ShapeInfo.Pitch = DESKTOP_CURSOR_WIDTH * sizeof(U32);
    ShapeInfo.Pixels = Pixels;

    Status = GraphicsDriver->Command(DF_GFX_CURSOR_SET_SHAPE, (UINT)(LPVOID)&ShapeInfo);
    if (Status != DF_RETURN_SUCCESS) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_SHAPE_FAILED, Status);
        return FALSE;
    }

    MemorySet(&PositionInfo, 0, sizeof(PositionInfo));
    DesktopCursorInitializeHeader(&(PositionInfo.Header), sizeof(PositionInfo));
    PositionInfo.X = Desktop->CursorX;
    PositionInfo.Y = Desktop->CursorY;

    Status = GraphicsDriver->Command(DF_GFX_CURSOR_SET_POSITION, (UINT)(LPVOID)&PositionInfo);
    if (Status != DF_RETURN_SUCCESS) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED, Status);
        return FALSE;
    }

    MemorySet(&VisibleInfo, 0, sizeof(VisibleInfo));
    DesktopCursorInitializeHeader(&(VisibleInfo.Header), sizeof(VisibleInfo));
    VisibleInfo.IsVisible = Desktop->CursorVisible;

    Status = GraphicsDriver->Command(DF_GFX_CURSOR_SET_VISIBLE, (UINT)(LPVOID)&VisibleInfo);
    if (Status != DF_RETURN_SUCCESS) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_VISIBLE_FAILED, Status);
        return FALSE;
    }

    DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_HARDWARE, DESKTOP_CURSOR_FALLBACK_NONE, DF_RETURN_SUCCESS);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Refresh cursor clipping against desktop bounds and clamp cursor position.
 * @param Desktop Target desktop.
 */
static void DesktopCursorRefreshClipAndPosition(LPDESKTOP Desktop) {
    RECT ScreenRect;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;
    if (GetDesktopScreenRect(Desktop, &ScreenRect) == FALSE) return;

    LockMutex(&(Desktop->Mutex), INFINITY);

    Desktop->CursorClipRect = ScreenRect;
    Desktop->CursorX = ClampI32(Desktop->CursorX, ScreenRect.X1, ScreenRect.X2);
    Desktop->CursorY = ClampI32(Desktop->CursorY, ScreenRect.Y1, ScreenRect.Y2);

    UnlockMutex(&(Desktop->Mutex));
}

/************************************************************************/

/**
 * @brief Draw one line in local window coordinates.
 * @param GC Graphics context.
 * @param X1 Start X.
 * @param Y1 Start Y.
 * @param X2 End X.
 * @param Y2 End Y.
 */
static void DesktopCursorDrawLine(HANDLE GC, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    LINEINFO LineInfo;

    MemorySet(&LineInfo, 0, sizeof(LineInfo));
    DesktopCursorInitializeHeader(&(LineInfo.Header), sizeof(LineInfo));
    LineInfo.GC = GC;
    LineInfo.X1 = X1;
    LineInfo.Y1 = Y1;
    LineInfo.X2 = X2;
    LineInfo.Y2 = Y2;

    (void)Line(&LineInfo);
}

/************************************************************************/

/**
 * @brief Initialize cursor ownership when one desktop enters graphics mode.
 * @param Desktop Target desktop.
 */
void DesktopCursorOnDesktopActivated(LPDESKTOP Desktop) {
    I32 CurrentX = 0;
    I32 CurrentY = 0;
    LPDRIVER GraphicsDriver;
    RECT CursorRect;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;

    LockMutex(&(Desktop->Mutex), INFINITY);

    Desktop->CursorVisible = TRUE;
    if (GetMousePosition(&CurrentX, &CurrentY) == TRUE) {
        Desktop->CursorX = CurrentX;
        Desktop->CursorY = CurrentY;
    }

    UnlockMutex(&(Desktop->Mutex));

    DesktopCursorRefreshClipAndPosition(Desktop);

    if (Desktop->Mode != DESKTOP_MODE_GRAPHICS) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_NOT_GRAPHICS, DF_RETURN_SUCCESS);
        return;
    }

    GraphicsDriver = DisplaySessionGetActiveGraphicsDriver();
    if (DesktopCursorTryEnableHardware(Desktop, GraphicsDriver) == FALSE) {
        LockMutex(&(Desktop->Mutex), INFINITY);
        CurrentX = Desktop->CursorX;
        CurrentY = Desktop->CursorY;
        UnlockMutex(&(Desktop->Mutex));

        DesktopCursorBuildRect(CurrentX, CurrentY, &CursorRect);
        DesktopCursorInvalidateScreenRect(Desktop, &CursorRect);
    }
}

/************************************************************************/

/**
 * @brief Apply one mouse position update to desktop cursor state.
 * @param Desktop Target desktop.
 * @param OldX Previous X position.
 * @param OldY Previous Y position.
 * @param NewX New X position.
 * @param NewY New Y position.
 */
void DesktopCursorOnMousePositionChanged(LPDESKTOP Desktop, I32 OldX, I32 OldY, I32 NewX, I32 NewY) {
    LPDRIVER GraphicsDriver;
    GFX_CURSOR_POSITION_INFO PositionInfo;
    UINT Status;
    RECT ClipRect;
    BOOL IsVisible;
    U32 CursorPath;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;

    DesktopCursorRefreshClipAndPosition(Desktop);

    LockMutex(&(Desktop->Mutex), INFINITY);

    ClipRect = Desktop->CursorClipRect;
    NewX = ClampI32(NewX, ClipRect.X1, ClipRect.X2);
    NewY = ClampI32(NewY, ClipRect.Y1, ClipRect.Y2);

    Desktop->CursorX = NewX;
    Desktop->CursorY = NewY;
    IsVisible = Desktop->CursorVisible;
    CursorPath = Desktop->CursorRenderPath;

    UnlockMutex(&(Desktop->Mutex));

    if (Desktop->Mode != DESKTOP_MODE_GRAPHICS || IsVisible == FALSE) {
        return;
    }

    if (CursorPath == DESKTOP_CURSOR_PATH_HARDWARE) {
        GraphicsDriver = DisplaySessionGetActiveGraphicsDriver();
        if (GraphicsDriver == NULL || GraphicsDriver->Command == NULL) {
            DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED, DF_RETURN_GENERIC);
            DesktopCursorInvalidateMove(Desktop, OldX, OldY, NewX, NewY);
            return;
        }

        MemorySet(&PositionInfo, 0, sizeof(PositionInfo));
        DesktopCursorInitializeHeader(&(PositionInfo.Header), sizeof(PositionInfo));
        PositionInfo.X = NewX;
        PositionInfo.Y = NewY;

        Status = GraphicsDriver->Command(DF_GFX_CURSOR_SET_POSITION, (UINT)(LPVOID)&PositionInfo);
        if (Status != DF_RETURN_SUCCESS) {
            DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED, Status);
            DesktopCursorInvalidateMove(Desktop, OldX, OldY, NewX, NewY);
        }

        return;
    }

    DesktopCursorInvalidateMove(Desktop, OldX, OldY, NewX, NewY);
}

/************************************************************************/

/**
 * @brief Render software cursor overlay on one window as final pass.
 * @param Window Target window.
 */
void DesktopCursorRenderSoftwareOverlayOnWindow(LPWINDOW Window) {
    LPDESKTOP Desktop;
    RECT WindowRect;
    RECT CursorRect;
    RECT ClipRect;
    RECT Intersection;
    I32 CursorX;
    I32 CursorY;
    BOOL IsVisible;
    U32 CursorPath;
    HANDLE GC;
    HANDLE OldPen;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;

    Desktop = GetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;

    LockMutex(&(Desktop->Mutex), INFINITY);

    CursorX = Desktop->CursorX;
    CursorY = Desktop->CursorY;
    ClipRect = Desktop->CursorClipRect;
    IsVisible = Desktop->CursorVisible;
    CursorPath = Desktop->CursorRenderPath;

    UnlockMutex(&(Desktop->Mutex));

    if (Desktop->Mode != DESKTOP_MODE_GRAPHICS) return;
    if (IsVisible == FALSE) return;
    if (CursorPath != DESKTOP_CURSOR_PATH_SOFTWARE) return;

    DesktopCursorBuildRect(CursorX, CursorY, &CursorRect);
    if (DesktopCursorIntersectRect(&CursorRect, &ClipRect, &CursorRect) == FALSE) {
        return;
    }

    LockMutex(&(Window->Mutex), INFINITY);
    WindowRect = Window->ScreenRect;
    UnlockMutex(&(Window->Mutex));

    if (DesktopCursorIntersectRect(&CursorRect, &WindowRect, &Intersection) == FALSE) {
        return;
    }

    GC = GetWindowGC((HANDLE)Window);
    if (GC == NULL) return;

    SAFE_USE_VALID_ID((LPGRAPHICSCONTEXT)GC, KOID_GRAPHICSCONTEXT) {
        LockMutex(&(((LPGRAPHICSCONTEXT)GC)->Mutex), INFINITY);
        ((LPGRAPHICSCONTEXT)GC)->LoClip.X = Intersection.X1;
        ((LPGRAPHICSCONTEXT)GC)->LoClip.Y = Intersection.Y1;
        ((LPGRAPHICSCONTEXT)GC)->HiClip.X = Intersection.X2;
        ((LPGRAPHICSCONTEXT)GC)->HiClip.Y = Intersection.Y2;
        UnlockMutex(&(((LPGRAPHICSCONTEXT)GC)->Mutex));
    }

    CursorX -= WindowRect.X1;
    CursorY -= WindowRect.Y1;

    OldPen = SelectPen(GC, GetSystemPen(SM_COLOR_TEXT_NORMAL));
    DesktopCursorDrawLine(GC, CursorX, CursorY, CursorX, CursorY + 9);
    DesktopCursorDrawLine(GC, CursorX, CursorY, CursorX + 6, CursorY);
    DesktopCursorDrawLine(GC, CursorX + 1, CursorY + 1, CursorX + 7, CursorY + 7);

    (void)SelectPen(GC, GetSystemPen(SM_COLOR_TEXT_SELECTED));
    DesktopCursorDrawLine(GC, CursorX + 1, CursorY + 2, CursorX + 1, CursorY + 8);
    DesktopCursorDrawLine(GC, CursorX + 2, CursorY + 1, CursorX + 5, CursorY + 1);

    (void)SelectPen(GC, OldPen);

    (void)ReleaseWindowGC(GC);
}

/************************************************************************/
