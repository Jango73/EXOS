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
#include "Desktop-OverlayInvalidation.h"
#include "CoreString.h"
#include "DisplaySession.h"
#include "GFX.h"
#include "Kernel.h"
#include "Log.h"
#include "Clock.h"
#include "input/MouseDispatcher.h"
#include "utils/Helpers.h"
#include "utils/Graphics-Utils.h"
#include "utils/RateLimiter.h"

/************************************************************************/

#define DESKTOP_CURSOR_MIN_SIZE 4
#define DESKTOP_CURSOR_DEFAULT_WIDTH 10
#define DESKTOP_CURSOR_DEFAULT_HEIGHT 10
#define DESKTOP_CURSOR_MAX_SIZE 64

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
static void DesktopCursorBuildRect(LPDESKTOP Desktop, I32 X, I32 Y, LPRECT RectOut) {
    U32 CursorWidth = DESKTOP_CURSOR_DEFAULT_WIDTH;
    U32 CursorHeight = DESKTOP_CURSOR_DEFAULT_HEIGHT;

    if (Desktop != NULL && Desktop->TypeID == KOID_DESKTOP) {
        if (Desktop->Cursor.Width >= DESKTOP_CURSOR_MIN_SIZE) CursorWidth = Desktop->Cursor.Width;
        if (Desktop->Cursor.Height >= DESKTOP_CURSOR_MIN_SIZE) CursorHeight = Desktop->Cursor.Height;
    }

    if (RectOut == NULL) return;

    RectOut->X1 = X;
    RectOut->Y1 = Y;
    RectOut->X2 = X + (I32)CursorWidth - 1;
    RectOut->Y2 = Y + (I32)CursorHeight - 1;
}

/************************************************************************/

/**
 * @brief Build the bounding rectangle of two rectangles.
 * @param Left First rectangle.
 * @param Right Second rectangle.
 * @param Result Output bounding rectangle.
 */
static void DesktopCursorUnionRect(LPRECT Left, LPRECT Right, LPRECT Result) {
    if (Left == NULL || Right == NULL || Result == NULL) return;

    Result->X1 = Left->X1 < Right->X1 ? Left->X1 : Right->X1;
    Result->Y1 = Left->Y1 < Right->Y1 ? Left->Y1 : Right->Y1;
    Result->X2 = Left->X2 > Right->X2 ? Left->X2 : Right->X2;
    Result->Y2 = Left->Y2 > Right->Y2 ? Left->Y2 : Right->Y2;
}

/************************************************************************/

/**
 * @brief Append one rectangle into one region when valid.
 * @param Region Target region.
 * @param Rect Rectangle to append.
 * @return TRUE on success.
 */
static BOOL DesktopCursorRegionAppendRectIfValid(LPRECT_REGION Region, LPRECT Rect) {
    if (Region == NULL || Rect == NULL) return FALSE;
    if (Rect->X1 > Rect->X2 || Rect->Y1 > Rect->Y2) return TRUE;
    return RectRegionAddRect(Region, Rect);
}

/************************************************************************/

/**
 * @brief Subtract one occluder rectangle from one source rectangle.
 * @param Region Destination region receiving remaining rectangles.
 * @param Source Source rectangle.
 * @param Occluder Occluding rectangle.
 * @return TRUE on success.
 */
static BOOL DesktopCursorRegionSubtractRectFromRect(LPRECT_REGION Region, LPRECT Source, LPRECT Occluder) {
    RECT Inter;
    RECT Piece;

    if (Region == NULL || Source == NULL || Occluder == NULL) return FALSE;

    if (IntersectRect(Source, Occluder, &Inter) == FALSE) {
        return DesktopCursorRegionAppendRectIfValid(Region, Source);
    }

    Piece = (RECT){Source->X1, Source->Y1, Source->X2, Inter.Y1 - 1};
    if (DesktopCursorRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece = (RECT){Source->X1, Inter.Y2 + 1, Source->X2, Source->Y2};
    if (DesktopCursorRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece = (RECT){Source->X1, Inter.Y1, Inter.X1 - 1, Inter.Y2};
    if (DesktopCursorRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece = (RECT){Inter.X2 + 1, Inter.Y1, Source->X2, Inter.Y2};
    if (DesktopCursorRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Subtract one occluding rectangle from one region.
 * @param Region Region to update in place.
 * @param Occluder Occluding rectangle.
 * @return TRUE on success.
 */
static BOOL DesktopCursorRegionSubtractOccluder(LPRECT_REGION Region, LPRECT Occluder) {
    RECT ExistingRects[WINDOW_DIRTY_REGION_CAPACITY];
    RECT TempStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT_REGION TempRegion;
    RECT Existing;
    UINT Count;
    UINT Index;

    if (Region == NULL || Occluder == NULL) return FALSE;

    if (RectRegionInit(&TempRegion, TempStorage, WINDOW_DIRTY_REGION_CAPACITY) == FALSE) return FALSE;
    RectRegionReset(&TempRegion);

    Count = RectRegionGetCount(Region);
    if (Count > WINDOW_DIRTY_REGION_CAPACITY) Count = WINDOW_DIRTY_REGION_CAPACITY;

    for (Index = 0; Index < Count; Index++) {
        if (RectRegionGetRect(Region, Index, &ExistingRects[Index]) == FALSE) {
            return FALSE;
        }
    }

    for (Index = 0; Index < Count; Index++) {
        Existing = ExistingRects[Index];
        if (DesktopCursorRegionSubtractRectFromRect(&TempRegion, &Existing, Occluder) == FALSE) {
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
 * @param Node Subtree root window.
 * @param Region Region to clip.
 */
static void DesktopCursorSubtractVisibleWindowTreeFromRegion(LPWINDOW Node, LPRECT_REGION Region) {
    LPLISTNODE ChildNode;
    LPWINDOW Child;
    RECT NodeRect;
    BOOL IsVisible = FALSE;

    if (Node == NULL || Node->TypeID != KOID_WINDOW) return;
    if (Region == NULL) return;

    LockMutex(&(Node->Mutex), INFINITY);
    IsVisible = ((Node->Status & WINDOW_STATUS_VISIBLE) != 0);
    NodeRect = Node->ScreenRect;

    for (ChildNode = Node->Children != NULL ? Node->Children->First : NULL; ChildNode != NULL; ChildNode = ChildNode->Next) {
        Child = (LPWINDOW)ChildNode;
        if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;
        DesktopCursorSubtractVisibleWindowTreeFromRegion(Child, Region);
    }
    UnlockMutex(&(Node->Mutex));

    if (IsVisible != FALSE) {
        (void)DesktopCursorRegionSubtractOccluder(Region, &NodeRect);
    }
}

/************************************************************************/

/**
 * @brief Build one cursor clip region excluding occluded zones for one window.
 * @param Window Target window.
 * @param BaseRect Base visible intersection rectangle.
 * @param Region Output region.
 * @param Storage Region storage.
 * @param Capacity Region capacity.
 * @return TRUE on success.
 */
static BOOL DesktopCursorBuildVisibleRegionForWindow(
    LPWINDOW Window,
    LPRECT BaseRect,
    LPRECT_REGION Region,
    LPRECT Storage,
    UINT Capacity
) {
    LPWINDOW Parent;
    LPLISTNODE Node;
    LPWINDOW Candidate;
    BOOL FoundSelf = FALSE;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (BaseRect == NULL || Region == NULL || Storage == NULL || Capacity == 0) return FALSE;

    if (RectRegionInit(Region, Storage, Capacity) == FALSE) return FALSE;
    RectRegionReset(Region);
    if (RectRegionAddRect(Region, BaseRect) == FALSE) return FALSE;

    Parent = Window->ParentWindow;
    if (Parent != NULL && Parent->TypeID == KOID_WINDOW) {
        LockMutex(&(Parent->Mutex), INFINITY);
        for (Node = Parent->Children != NULL ? Parent->Children->First : NULL; Node != NULL; Node = Node->Next) {
            Candidate = (LPWINDOW)Node;
            if (Candidate == NULL || Candidate->TypeID != KOID_WINDOW) continue;
            if (Candidate == Window) {
                FoundSelf = TRUE;
                break;
            }
            DesktopCursorSubtractVisibleWindowTreeFromRegion(Candidate, Region);
            if (RectRegionGetCount(Region) == 0) {
                UnlockMutex(&(Parent->Mutex));
                return TRUE;
            }
        }
        UnlockMutex(&(Parent->Mutex));
    }

    if (FoundSelf == FALSE && Window->ParentWindow != NULL) {
        return TRUE;
    }

    LockMutex(&(Window->Mutex), INFINITY);
    for (Node = Window->Children != NULL ? Window->Children->First : NULL; Node != NULL; Node = Node->Next) {
        Candidate = (LPWINDOW)Node;
        if (Candidate == NULL || Candidate->TypeID != KOID_WINDOW) continue;
        DesktopCursorSubtractVisibleWindowTreeFromRegion(Candidate, Region);
        if (RectRegionGetCount(Region) == 0) {
            UnlockMutex(&(Window->Mutex));
            return TRUE;
        }
    }
    UnlockMutex(&(Window->Mutex));

    return TRUE;
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

    if (Desktop->Cursor.RenderPath == Path && Desktop->Cursor.FallbackReason == Reason) {
        UnlockMutex(&(Desktop->Mutex));
        return;
    }

    Desktop->Cursor.RenderPath = Path;
    Desktop->Cursor.FallbackReason = Reason;

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
 * @brief Request bounded software cursor redraw for old and new cursor rectangles.
 * @param Desktop Target desktop.
 * @param OldX Previous rendered X.
 * @param OldY Previous rendered Y.
 * @param NewX Pending X.
 * @param NewY Pending Y.
 */
static void DesktopCursorRequestSoftwareRedraw(LPDESKTOP Desktop, I32 OldX, I32 OldY, I32 NewX, I32 NewY) {
    static RATE_LIMITER CursorRedrawLimiter;
    static BOOL CursorRedrawLimiterReady = FALSE;
    RECT OldRect;
    RECT NewRect;
    RECT DamageRect = {0};
    RECT ClipRect;
    BOOL HasOldRect = FALSE;
    BOOL HasNewRect = FALSE;
    BOOL HasDamageRect = FALSE;
    U32 Suppressed = 0;
    U32 Now = GetSystemTime();

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;
    if (Desktop->Window == NULL || Desktop->Window->TypeID != KOID_WINDOW) return;

    LockMutex(&(Desktop->Mutex), INFINITY);
    ClipRect = Desktop->Cursor.ClipRect;
    UnlockMutex(&(Desktop->Mutex));

    DesktopCursorBuildRect(Desktop, OldX, OldY, &OldRect);
    DesktopCursorBuildRect(Desktop, NewX, NewY, &NewRect);

    HasOldRect = IntersectRect(&OldRect, &ClipRect, &OldRect);
    HasNewRect = IntersectRect(&NewRect, &ClipRect, &NewRect);

    if (HasOldRect != FALSE) {
        DamageRect = OldRect;
        HasDamageRect = TRUE;
    }

    if (HasNewRect != FALSE) {
        if (HasDamageRect == FALSE) {
            DamageRect = NewRect;
            HasDamageRect = TRUE;
        } else {
            DesktopCursorUnionRect(&DamageRect, &NewRect, &DamageRect);
        }
    }

    if (HasDamageRect != FALSE) {
        DesktopOverlayInvalidateWindowTreeThenRootRect(Desktop->Window, &DamageRect);
    }

    if (CursorRedrawLimiterReady == FALSE) {
        if (RateLimiterInit(&CursorRedrawLimiter, 24, 1000) != FALSE) {
            CursorRedrawLimiterReady = TRUE;
        }
    }

    if (CursorRedrawLimiterReady != FALSE && RateLimiterShouldTrigger(&CursorRedrawLimiter, Now, &Suppressed) != FALSE) {
        DEBUG(TEXT("[DesktopCursorRequestSoftwareRedraw] old=(%u,%u) new=(%u,%u) has_old=%x has_new=%x damage=(%u,%u)-(%u,%u) old_rect=(%u,%u)-(%u,%u) new_rect=(%u,%u)-(%u,%u) clip=(%u,%u)-(%u,%u) suppressed=%u"),
            UNSIGNED(OldX),
            UNSIGNED(OldY),
            UNSIGNED(NewX),
            UNSIGNED(NewY),
            HasOldRect ? 1 : 0,
            HasNewRect ? 1 : 0,
            UNSIGNED(DamageRect.X1),
            UNSIGNED(DamageRect.Y1),
            UNSIGNED(DamageRect.X2),
            UNSIGNED(DamageRect.Y2),
            UNSIGNED(OldRect.X1),
            UNSIGNED(OldRect.Y1),
            UNSIGNED(OldRect.X2),
            UNSIGNED(OldRect.Y2),
            UNSIGNED(NewRect.X1),
            UNSIGNED(NewRect.Y1),
            UNSIGNED(NewRect.X2),
            UNSIGNED(NewRect.Y2),
            UNSIGNED(ClipRect.X1),
            UNSIGNED(ClipRect.Y1),
            UNSIGNED(ClipRect.X2),
            UNSIGNED(ClipRect.Y2),
            Suppressed);
    }
}

/************************************************************************/

/**
 * @brief Clamp one cursor size value to accepted bounds.
 * @param Value Requested value.
 * @param DefaultValue Fallback value when requested value is invalid.
 * @return Clamped size value.
 */
static U32 DesktopCursorClampSize(U32 Value, U32 DefaultValue) {
    if (Value == 0) Value = DefaultValue;
    if (Value < DESKTOP_CURSOR_MIN_SIZE) Value = DESKTOP_CURSOR_MIN_SIZE;
    if (Value > DESKTOP_CURSOR_MAX_SIZE) Value = DESKTOP_CURSOR_MAX_SIZE;
    return Value;
}

/************************************************************************/

/**
 * @brief Resolve cursor size from configuration.
 * @param Desktop Target desktop.
 */
static void DesktopCursorResolveConfiguredSize(LPDESKTOP Desktop) {
    LPCSTR WidthText;
    LPCSTR HeightText;
    U32 Width = DESKTOP_CURSOR_DEFAULT_WIDTH;
    U32 Height = DESKTOP_CURSOR_DEFAULT_HEIGHT;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;

    WidthText = GetConfigurationValue(TEXT("Desktop.CursorWidth"));
    HeightText = GetConfigurationValue(TEXT("Desktop.CursorHeight"));

    if (WidthText != NULL && StringLength(WidthText) != 0) {
        Width = StringToU32(WidthText);
    }

    if (HeightText != NULL && StringLength(HeightText) != 0) {
        Height = StringToU32(HeightText);
    }

    Width = DesktopCursorClampSize(Width, DESKTOP_CURSOR_DEFAULT_WIDTH);
    Height = DesktopCursorClampSize(Height, DESKTOP_CURSOR_DEFAULT_HEIGHT);

    LockMutex(&(Desktop->Mutex), INFINITY);
    Desktop->Cursor.Width = Width;
    Desktop->Cursor.Height = Height;
    UnlockMutex(&(Desktop->Mutex));
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
    U32 Pixels[DESKTOP_CURSOR_MAX_SIZE * DESKTOP_CURSOR_MAX_SIZE];
    U32 CursorWidth;
    U32 CursorHeight;
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

    LockMutex(&(Desktop->Mutex), INFINITY);
    CursorWidth = Desktop->Cursor.Width;
    CursorHeight = Desktop->Cursor.Height;
    UnlockMutex(&(Desktop->Mutex));

    CursorWidth = DesktopCursorClampSize(CursorWidth, DESKTOP_CURSOR_DEFAULT_WIDTH);
    CursorHeight = DesktopCursorClampSize(CursorHeight, DESKTOP_CURSOR_DEFAULT_HEIGHT);

    for (Y = 0; Y < CursorHeight; Y++) {
        for (X = 0; X < CursorWidth; X++) {
            BOOL IsBorder = (X == 0 || Y == 0 || X == CursorWidth - 1 || Y == CursorHeight - 1);
            Pixels[(Y * CursorWidth) + X] = IsBorder ? 0xFF000000 : 0xFFFFFFFF;
        }
    }

    MemorySet(&ShapeInfo, 0, sizeof(ShapeInfo));
    DesktopCursorInitializeHeader(&(ShapeInfo.Header), sizeof(ShapeInfo));
    ShapeInfo.Width = CursorWidth;
    ShapeInfo.Height = CursorHeight;
    ShapeInfo.HotspotX = 0;
    ShapeInfo.HotspotY = 0;
    ShapeInfo.Format = GFX_CURSOR_FORMAT_ARGB8888;
    ShapeInfo.Pitch = CursorWidth * sizeof(U32);
    ShapeInfo.Pixels = Pixels;

    Status = GraphicsDriver->Command(DF_GFX_CURSOR_SET_SHAPE, (UINT)(LPVOID)&ShapeInfo);
    if (Status != DF_RETURN_SUCCESS) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_SHAPE_FAILED, Status);
        return FALSE;
    }

    MemorySet(&PositionInfo, 0, sizeof(PositionInfo));
    DesktopCursorInitializeHeader(&(PositionInfo.Header), sizeof(PositionInfo));
    PositionInfo.X = Desktop->Cursor.X;
    PositionInfo.Y = Desktop->Cursor.Y;

    Status = GraphicsDriver->Command(DF_GFX_CURSOR_SET_POSITION, (UINT)(LPVOID)&PositionInfo);
    if (Status != DF_RETURN_SUCCESS) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED, Status);
        return FALSE;
    }

    MemorySet(&VisibleInfo, 0, sizeof(VisibleInfo));
    DesktopCursorInitializeHeader(&(VisibleInfo.Header), sizeof(VisibleInfo));
    VisibleInfo.IsVisible = Desktop->Cursor.Visible;

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

    Desktop->Cursor.ClipRect = ScreenRect;
    Desktop->Cursor.X = ClampI32(Desktop->Cursor.X, ScreenRect.X1, ScreenRect.X2);
    Desktop->Cursor.Y = ClampI32(Desktop->Cursor.Y, ScreenRect.Y1, ScreenRect.Y2);

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
    I32 CursorX;
    I32 CursorY;
    LPDRIVER GraphicsDriver;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;

    DesktopCursorResolveConfiguredSize(Desktop);

    LockMutex(&(Desktop->Mutex), INFINITY);

    Desktop->Cursor.Visible = TRUE;
    if (GetMousePosition(&CurrentX, &CurrentY) == TRUE) {
        Desktop->Cursor.X = CurrentX;
        Desktop->Cursor.Y = CurrentY;
    }
    Desktop->Cursor.PendingX = Desktop->Cursor.X;
    Desktop->Cursor.PendingY = Desktop->Cursor.Y;
    Desktop->Cursor.SoftwareDirty = FALSE;

    UnlockMutex(&(Desktop->Mutex));

    DesktopCursorRefreshClipAndPosition(Desktop);

    LockMutex(&(Desktop->Mutex), INFINITY);
    CursorX = Desktop->Cursor.X;
    CursorY = Desktop->Cursor.Y;
    UnlockMutex(&(Desktop->Mutex));

    if (Desktop->Mode != DESKTOP_MODE_GRAPHICS) {
        DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_NOT_GRAPHICS, DF_RETURN_SUCCESS);
        return;
    }

    GraphicsDriver = DisplaySessionGetActiveGraphicsDriver();
    if (DesktopCursorTryEnableHardware(Desktop, GraphicsDriver) == FALSE) {
        DesktopCursorRequestSoftwareRedraw(Desktop, CursorX, CursorY, CursorX, CursorY);
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
    static RATE_LIMITER CursorMoveLimiter;
    static BOOL CursorMoveLimiterReady = FALSE;
    LPDRIVER GraphicsDriver;
    GFX_CURSOR_POSITION_INFO PositionInfo;
    UINT Status;
    RECT ClipRect;
    BOOL IsVisible;
    U32 CursorPath;
    I32 ClampedOldX;
    I32 ClampedOldY;
    U32 Suppressed = 0;
    U32 Now = GetSystemTime();

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;

    DesktopCursorRefreshClipAndPosition(Desktop);

    LockMutex(&(Desktop->Mutex), INFINITY);

    ClipRect = Desktop->Cursor.ClipRect;
    NewX = ClampI32(NewX, ClipRect.X1, ClipRect.X2);
    NewY = ClampI32(NewY, ClipRect.Y1, ClipRect.Y2);
    ClampedOldX = ClampI32(OldX, ClipRect.X1, ClipRect.X2);
    ClampedOldY = ClampI32(OldY, ClipRect.Y1, ClipRect.Y2);

    Desktop->Cursor.X = NewX;
    Desktop->Cursor.Y = NewY;
    Desktop->Cursor.PendingX = NewX;
    Desktop->Cursor.PendingY = NewY;
    Desktop->Cursor.SoftwareDirty = FALSE;
    IsVisible = Desktop->Cursor.Visible;
    CursorPath = Desktop->Cursor.RenderPath;

    UnlockMutex(&(Desktop->Mutex));

    if (CursorMoveLimiterReady == FALSE) {
        if (RateLimiterInit(&CursorMoveLimiter, 24, 1000) != FALSE) {
            CursorMoveLimiterReady = TRUE;
        }
    }

    if (CursorMoveLimiterReady != FALSE && RateLimiterShouldTrigger(&CursorMoveLimiter, Now, &Suppressed) != FALSE) {
        DEBUG(TEXT("[DesktopCursorOnMousePositionChanged] path=%x visible=%x old=(%u,%u) new=(%u,%u) clamped_old=(%u,%u) clamped_new=(%u,%u) suppressed=%u"),
            CursorPath,
            IsVisible ? 1 : 0,
            UNSIGNED(OldX),
            UNSIGNED(OldY),
            UNSIGNED(NewX),
            UNSIGNED(NewY),
            UNSIGNED(ClampedOldX),
            UNSIGNED(ClampedOldY),
            UNSIGNED(NewX),
            UNSIGNED(NewY),
            Suppressed);
    }

    if (Desktop->Mode != DESKTOP_MODE_GRAPHICS || IsVisible == FALSE) {
        return;
    }

    if (CursorPath == DESKTOP_CURSOR_PATH_HARDWARE) {
        GraphicsDriver = DisplaySessionGetActiveGraphicsDriver();
        if (GraphicsDriver == NULL || GraphicsDriver->Command == NULL) {
            DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED, DF_RETURN_GENERIC);
            DesktopCursorRequestSoftwareRedraw(Desktop, ClampedOldX, ClampedOldY, NewX, NewY);
            return;
        }

        MemorySet(&PositionInfo, 0, sizeof(PositionInfo));
        DesktopCursorInitializeHeader(&(PositionInfo.Header), sizeof(PositionInfo));
        PositionInfo.X = NewX;
        PositionInfo.Y = NewY;

        Status = GraphicsDriver->Command(DF_GFX_CURSOR_SET_POSITION, (UINT)(LPVOID)&PositionInfo);
        if (Status != DF_RETURN_SUCCESS) {
            DEBUG(TEXT("[DesktopCursorOnMousePositionChanged] hardware set-position failed status=%u old=(%u,%u) new=(%u,%u)"),
                Status,
                UNSIGNED(ClampedOldX),
                UNSIGNED(ClampedOldY),
                UNSIGNED(NewX),
                UNSIGNED(NewY));
            DesktopCursorSetPathState(Desktop, DESKTOP_CURSOR_PATH_SOFTWARE, DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED, Status);
            DesktopCursorRequestSoftwareRedraw(Desktop, ClampedOldX, ClampedOldY, NewX, NewY);
        } else {
            LockMutex(&(Desktop->Mutex), INFINITY);
            Desktop->Cursor.X = NewX;
            Desktop->Cursor.Y = NewY;
            Desktop->Cursor.PendingX = NewX;
            Desktop->Cursor.PendingY = NewY;
            Desktop->Cursor.SoftwareDirty = FALSE;
            UnlockMutex(&(Desktop->Mutex));
        }

        return;
    }

    DesktopCursorRequestSoftwareRedraw(Desktop, ClampedOldX, ClampedOldY, NewX, NewY);
}

/************************************************************************/

/**
 * @brief Render software cursor overlay on one window as final pass.
 * @param Window Target window.
 */
void DesktopCursorRenderSoftwareOverlayOnWindow(LPWINDOW Window) {
    static RATE_LIMITER CursorOverlayLimiter;
    static BOOL CursorOverlayLimiterReady = FALSE;
    LPDESKTOP Desktop;
    RECT WindowRect;
    RECT CursorRect;
    RECT ClipRect;
    RECT Intersection;
    RECT DrawClipStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT_REGION DrawClipRegion;
    RECT DrawClipRect;
    I32 CursorX;
    I32 CursorY;
    U32 CursorWidth;
    U32 CursorHeight;
    BOOL IsVisible;
    U32 CursorPath;
    I32 DiagonalLength;
    I32 HorizontalLength;
    I32 LocalCursorX;
    I32 LocalCursorY;
    HANDLE GC;
    HANDLE OldPen;
    UINT ClipIndex;
    U32 Suppressed = 0;
    U32 Now = GetSystemTime();

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;

    Desktop = GetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return;

    LockMutex(&(Desktop->Mutex), INFINITY);

    CursorX = Desktop->Cursor.X;
    CursorY = Desktop->Cursor.Y;
    CursorWidth = Desktop->Cursor.Width;
    CursorHeight = Desktop->Cursor.Height;
    ClipRect = Desktop->Cursor.ClipRect;
    IsVisible = Desktop->Cursor.Visible;
    CursorPath = Desktop->Cursor.RenderPath;

    UnlockMutex(&(Desktop->Mutex));

    if (Desktop->Mode != DESKTOP_MODE_GRAPHICS) return;
    if (IsVisible == FALSE) return;
    if (CursorPath != DESKTOP_CURSOR_PATH_SOFTWARE) return;

    CursorWidth = DesktopCursorClampSize(CursorWidth, DESKTOP_CURSOR_DEFAULT_WIDTH);
    CursorHeight = DesktopCursorClampSize(CursorHeight, DESKTOP_CURSOR_DEFAULT_HEIGHT);

    DesktopCursorBuildRect(Desktop, CursorX, CursorY, &CursorRect);
    if (IntersectRect(&CursorRect, &ClipRect, &CursorRect) == FALSE) {
        return;
    }

    LockMutex(&(Window->Mutex), INFINITY);
    WindowRect = Window->ScreenRect;
    UnlockMutex(&(Window->Mutex));

    if (IntersectRect(&CursorRect, &WindowRect, &Intersection) == FALSE) {
        return;
    }

    if (DesktopCursorBuildVisibleRegionForWindow(Window, &Intersection, &DrawClipRegion, DrawClipStorage, WINDOW_DIRTY_REGION_CAPACITY) == FALSE) {
        return;
    }

    if (RectRegionGetCount(&DrawClipRegion) == 0) {
        return;
    }

    GC = GetWindowGC((HANDLE)Window);
    if (GC == NULL) return;
    LocalCursorX = CursorX - WindowRect.X1;
    LocalCursorY = CursorY - WindowRect.Y1;

    DiagonalLength = (I32)(CursorWidth < CursorHeight ? CursorWidth : CursorHeight);
    HorizontalLength = (I32)((CursorWidth * 2) / 3);
    if (DiagonalLength < 2) DiagonalLength = 2;
    if (HorizontalLength < 2) HorizontalLength = 2;

    OldPen = SelectPen(GC, GetSystemPen(SM_COLOR_TEXT_NORMAL));
    for (ClipIndex = 0; ClipIndex < RectRegionGetCount(&DrawClipRegion); ClipIndex++) {
        if (RectRegionGetRect(&DrawClipRegion, ClipIndex, &DrawClipRect) == FALSE) continue;

        SAFE_USE_VALID_ID((LPGRAPHICSCONTEXT)GC, KOID_GRAPHICSCONTEXT) {
            LockMutex(&(((LPGRAPHICSCONTEXT)GC)->Mutex), INFINITY);
            ((LPGRAPHICSCONTEXT)GC)->LoClip.X = DrawClipRect.X1;
            ((LPGRAPHICSCONTEXT)GC)->LoClip.Y = DrawClipRect.Y1;
            ((LPGRAPHICSCONTEXT)GC)->HiClip.X = DrawClipRect.X2;
            ((LPGRAPHICSCONTEXT)GC)->HiClip.Y = DrawClipRect.Y2;
            UnlockMutex(&(((LPGRAPHICSCONTEXT)GC)->Mutex));
        }

        DesktopCursorDrawLine(GC, LocalCursorX, LocalCursorY, LocalCursorX, LocalCursorY + (I32)CursorHeight - 1);
        DesktopCursorDrawLine(GC, LocalCursorX, LocalCursorY, LocalCursorX + HorizontalLength - 1, LocalCursorY);
        DesktopCursorDrawLine(GC, LocalCursorX + 1, LocalCursorY + 1, LocalCursorX + DiagonalLength - 1, LocalCursorY + DiagonalLength - 1);

        (void)SelectPen(GC, GetSystemPen(SM_COLOR_TEXT_SELECTED));
        DesktopCursorDrawLine(GC, LocalCursorX + 1, LocalCursorY + 2, LocalCursorX + 1, LocalCursorY + (I32)CursorHeight - 2);
        DesktopCursorDrawLine(GC, LocalCursorX + 2, LocalCursorY + 1, LocalCursorX + HorizontalLength - 2, LocalCursorY + 1);
        (void)SelectPen(GC, GetSystemPen(SM_COLOR_TEXT_NORMAL));
    }

    (void)SelectPen(GC, OldPen);

    (void)ReleaseWindowGC(GC);

    if (CursorOverlayLimiterReady == FALSE) {
        if (RateLimiterInit(&CursorOverlayLimiter, 24, 1000) != FALSE) {
            CursorOverlayLimiterReady = TRUE;
        }
    }

    if (CursorOverlayLimiterReady != FALSE && RateLimiterShouldTrigger(&CursorOverlayLimiter, Now, &Suppressed) != FALSE) {
        DEBUG(TEXT("[DesktopCursorRenderSoftwareOverlayOnWindow] id=%x cursor=(%u,%u) window=(%u,%u)-(%u,%u) intersection=(%u,%u)-(%u,%u) suppressed=%u"),
            Window->WindowID,
            UNSIGNED(Desktop->Cursor.X),
            UNSIGNED(Desktop->Cursor.Y),
            UNSIGNED(WindowRect.X1),
            UNSIGNED(WindowRect.Y1),
            UNSIGNED(WindowRect.X2),
            UNSIGNED(WindowRect.Y2),
            UNSIGNED(Intersection.X1),
            UNSIGNED(Intersection.Y1),
            UNSIGNED(Intersection.X2),
            UNSIGNED(Intersection.Y2),
            Suppressed);
    }
}

/************************************************************************/
