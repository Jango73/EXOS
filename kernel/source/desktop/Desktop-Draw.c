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


    Desktop draw dispatch

\************************************************************************/

#include "Desktop-Private.h"
#include "Desktop-NonClient.h"
#include "Desktop.h"
#include "core/Kernel.h"
#include "log/Log.h"
#include "utils/Graphics-Utils.h"

/***************************************************************************/

#define DESKTOP_DRAW_TRACE_SHELLBAR_WINDOW_ID 0x53484252
#define DESKTOP_DRAW_TRACE_TEST_WINDOW_ID 0x000085A1

/***************************************************************************/

/**
 * @brief Tell whether one window is the shellbar.
 * @param Window Target window.
 * @return TRUE when the window identifier matches the shellbar.
 */
static BOOL DesktopDrawIsShellBarWindow(LPWINDOW Window) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    return (Window->WindowID == DESKTOP_DRAW_TRACE_SHELLBAR_WINDOW_ID);
}

/***************************************************************************/

/**
 * @brief Tell whether one window is the internal floating test window.
 * @param Window Target window.
 * @return TRUE when the window identifier matches the internal test window.
 */
static BOOL DesktopDrawIsTestWindow(LPWINDOW Window) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    return (Window->WindowID == DESKTOP_DRAW_TRACE_TEST_WINDOW_ID);
}

/***************************************************************************/

/**
 * @brief Compute one full window rectangle in window coordinates.
 * @param Window Target window.
 * @param WindowRect Receives the full local rectangle.
 * @return TRUE when the rectangle was computed.
 */
static BOOL GetWindowFullLocalRect(LPWINDOW Window, LPRECT WindowRect) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (WindowRect == NULL) return FALSE;
    return GetWindowRect((HANDLE)Window, WindowRect);
}

/***************************************************************************/

/**
 * @brief Compute one system-client rectangle in screen coordinates.
 * @param Window Target window.
 * @param ClientScreenRect Receives the client rectangle in screen space.
 * @return TRUE when the rectangle was computed.
 */
static BOOL GetWindowClientScreenRect(LPWINDOW Window, LPRECT ClientScreenRect) {
    RECT ClientRect;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ClientScreenRect == NULL) return FALSE;
    if (GetWindowClientRect((HANDLE)Window, &ClientRect) == FALSE) return FALSE;

    GraphicsWindowRectToScreenRect(&Window->ScreenRect, &ClientRect, ClientScreenRect);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Activate one prepared draw context on one window.
 * @param Window Target window.
 * @param SurfaceRect Owned drawing surface in dispatch-local coordinates.
 * @param ClipRect Clip rectangle in screen coordinates.
 * @param Origin Draw origin in screen coordinates.
 * @param Flags Draw context flags.
 */
static void ActivateWindowDrawContext(
    LPWINDOW Window,
    LPRECT SurfaceRect,
    LPRECT ClipRect,
    LPPOINT Origin,
    U32 Flags) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;
    if (SurfaceRect == NULL || ClipRect == NULL || Origin == NULL) return;

    LockMutex(&(Window->Mutex), INFINITY);
    Window->DrawSurfaceRect = *SurfaceRect;
    Window->DrawClipRect = *ClipRect;
    Window->DrawOrigin = *Origin;
    Window->DrawContextFlags = WINDOW_DRAW_CONTEXT_ACTIVE | Flags;
    UnlockMutex(&(Window->Mutex));
}

/***************************************************************************/

/**
 * @brief Clear one prepared draw context after dispatch.
 * @param Window Target window.
 */
static void ClearWindowDrawContext(LPWINDOW Window) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;

    LockMutex(&(Window->Mutex), INFINITY);
    MemorySet(&Window->DrawSurfaceRect, 0, sizeof(Window->DrawSurfaceRect));
    MemorySet(&Window->DrawClipRect, 0, sizeof(Window->DrawClipRect));
    MemorySet(&Window->DrawOrigin, 0, sizeof(Window->DrawOrigin));
    Window->DrawContextFlags = 0;
    UnlockMutex(&(Window->Mutex));
}

/***************************************************************************/

/**
 * @brief Expose the current draw surface rectangle for one window dispatch.
 * @param Window Target window.
 * @param Rect Receives the surface rectangle in dispatch-local coordinates.
 * @return TRUE when a prepared surface rectangle exists.
 */
BOOL DesktopGetWindowDrawSurfaceRect(LPWINDOW Window, LPRECT Rect) {
    WINDOW_DRAW_CONTEXT_SNAPSHOT Snapshot;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Rect == NULL) return FALSE;
    if (GetWindowDrawContextSnapshot(Window, &Snapshot) == FALSE) return FALSE;
    if ((Snapshot.Flags & WINDOW_DRAW_CONTEXT_ACTIVE) == 0) return FALSE;

    *Rect = Snapshot.SurfaceRect;
    return TRUE;
}

/**
 * @brief Expose the current draw clip rectangle for one window dispatch.
 * @param Window Target window.
 * @param Rect Receives the clip rectangle in screen coordinates.
 * @return TRUE when a prepared clip rectangle exists.
 */
BOOL DesktopGetWindowDrawClipRect(LPWINDOW Window, LPRECT Rect) {
    WINDOW_DRAW_CONTEXT_SNAPSHOT Snapshot;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Rect == NULL) return FALSE;
    if (GetWindowDrawContextSnapshot(Window, &Snapshot) == FALSE) return FALSE;
    if ((Snapshot.Flags & WINDOW_DRAW_CONTEXT_ACTIVE) == 0) return FALSE;

    *Rect = Snapshot.ClipRect;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw system-managed chrome before client dispatch when required.
 * @param Window Target window.
 * @param ClipRect Clip rectangle in screen coordinates.
 * @return TRUE when preprocessing succeeded.
 */
static BOOL DrawWindowSystemChrome(LPWINDOW Window, LPRECT ClipRect) {
    HANDLE GC;
    RECT WindowRect;
    BOOL DrawNonClient;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ClipRect == NULL) return FALSE;

    DrawNonClient = ShouldDrawWindowNonClient(Window);
    if (DesktopDrawIsShellBarWindow(Window) != FALSE || DesktopDrawIsTestWindow(Window) != FALSE) {
    }
    if (DrawNonClient == FALSE) return TRUE;
    if (GetWindowFullLocalRect(Window, &WindowRect) == FALSE) return FALSE;

    GC = BeginWindowDraw((HANDLE)Window);
    if (GC == NULL) return FALSE;

    (void)SetGraphicsContextClipScreenRect(GC, ClipRect);
    (void)DrawWindowNonClient((HANDLE)Window, GC, &WindowRect);
    (void)EndWindowDraw((HANDLE)Window);

    return TRUE;
}

/**
 * @brief Dispatch one client draw callback using the structured draw context.
 * @param Window Target window.
 * @param TargetHandle Handle passed to the window procedure.
 * @param ClipRect Screen-space clip rectangle.
 * @param Param1 First `EWM_DRAW` parameter.
 * @param Param2 Second `EWM_DRAW` parameter.
 * @return TRUE when the callback was dispatched.
 */
static BOOL DispatchPreparedClientDraw(
    LPWINDOW Window,
    HANDLE TargetHandle,
    LPRECT ClipRect,
    U32 Param1,
    U32 Param2) {
    RECT SurfaceRect;
    RECT SurfaceScreenRect;
    RECT ClientScreenRect;
    RECT ClientClipRect;
    POINT DrawOrigin;
    U32 DrawFlags = 0;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ClipRect == NULL) return FALSE;
    if (Window->Function == NULL) return FALSE;
    if (GetWindowClientScreenRect(Window, &ClientScreenRect) == FALSE) {
        ClientScreenRect = Window->ScreenRect;
    }

    DesktopPipelineTraceWindowDrawDispatch(Window, ClipRect, &ClientScreenRect);

    if (ShouldDrawWindowNonClient(Window) != FALSE) {
        if (GetWindowClientScreenRect(Window, &SurfaceScreenRect) == FALSE) return FALSE;
        if (IntersectRect(ClipRect, &SurfaceScreenRect, &ClientClipRect) == FALSE) return TRUE;

        SurfaceRect.X1 = 0;
        SurfaceRect.Y1 = 0;
        SurfaceRect.X2 = SurfaceScreenRect.X2 - SurfaceScreenRect.X1;
        SurfaceRect.Y2 = SurfaceScreenRect.Y2 - SurfaceScreenRect.Y1;
        DrawOrigin.X = SurfaceScreenRect.X1;
        DrawOrigin.Y = SurfaceScreenRect.Y1;
        DrawFlags = WINDOW_DRAW_CONTEXT_CLIENT_COORDINATES;
    } else {
        if (GetWindowFullLocalRect(Window, &SurfaceRect) == FALSE) return FALSE;
        SurfaceScreenRect = Window->ScreenRect;
        ClientClipRect = *ClipRect;
        DrawOrigin.X = SurfaceScreenRect.X1;
        DrawOrigin.Y = SurfaceScreenRect.Y1;
    }

    ActivateWindowDrawContext(Window, &SurfaceRect, &ClientClipRect, &DrawOrigin, DrawFlags);
    Window->Function(TargetHandle, EWM_DRAW, Param1, Param2);
    ClearWindowDrawContext(Window);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Dispatch one structured draw cycle for one window.
 * @param Window Target window.
 * @param TargetHandle Handle passed to the window procedure.
 * @param Param1 First `EWM_DRAW` parameter.
 * @param Param2 Second `EWM_DRAW` parameter.
 * @return TRUE when the dispatch completed.
 */
BOOL DesktopDispatchWindowDraw(LPWINDOW Window, HANDLE TargetHandle, U32 Param1, U32 Param2) {
    RECT ClipStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT_REGION ClipRegion;
    RECT ClipRect;
    WINDOW_STATE_SNAPSHOT Snapshot;
    UINT ClipCount;
    UINT ClipIndex;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return FALSE;
    if ((Snapshot.Status & WINDOW_STATUS_VISIBLE) == 0) return TRUE;

    ClearWindowDrawContext(Window);

    if (BuildWindowDrawClipRegion(Window, &ClipRegion, ClipStorage, WINDOW_DIRTY_REGION_CAPACITY) == FALSE) {
        return FALSE;
    }

    ClipCount = RectRegionGetCount(&ClipRegion);
    if (ClipCount == 0) return FALSE;

    for (ClipIndex = 0; ClipIndex < ClipCount; ClipIndex++) {
        if (RectRegionGetRect(&ClipRegion, ClipIndex, &ClipRect) == FALSE) continue;

        if (DesktopDrawIsShellBarWindow(Window) != FALSE || DesktopDrawIsTestWindow(Window) != FALSE) {
        }

        if (DrawWindowSystemChrome(Window, &ClipRect) == FALSE) {
            ClearWindowDrawContext(Window);
            return FALSE;
        }

        if (DispatchPreparedClientDraw(Window, TargetHandle, &ClipRect, Param1, Param2) == FALSE) {
            ClearWindowDrawContext(Window);
            return FALSE;
        }

        if (DesktopPresentScreenRect(Window, &ClipRect) == FALSE) {
            ClearWindowDrawContext(Window);
            return FALSE;
        }
    }

    DesktopCursorRenderSoftwareOverlayOnWindow(Window);
    ClearWindowDrawContext(Window);
    return TRUE;
}

/***************************************************************************/
