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


    Startup desktop component composition

\************************************************************************/

#include "ui/Startup-Desktop-Components.h"

#include "ui/Cube3D.h"
#include "ui/LogViewer.h"
#include "ui/OnScreenDebugInfo.h"
#include "ui/ShellBar.h"

/***************************************************************************/

#define DESKTOP_ON_SCREEN_DEBUG_INFO_WINDOW_ID 0x5344534F
#define DESKTOP_CUBE3D_WINDOW_TOP 300

/***************************************************************************/

/**
 * @brief Ensure the floating 3D cube window exists and has the expected rect.
 * @param Desktop Target desktop.
 * @return TRUE on success.
 */
static BOOL EnsureCube3DWindow(LPDESKTOP Desktop) {
    HANDLE RootWindow;
    HANDLE CubeWindow;
    WINDOWINFO WindowInfo;
    POINT PreferredSize;
    RECT ScreenRect;
    RECT WindowRect;
    I32 ScreenWidth;
    I32 ScreenHeight;
    I32 WindowWidth;
    I32 WindowHeight;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return FALSE;
    }
    if (Cube3DEnsureClassRegistered() == FALSE) {
        return FALSE;
    }

    RootWindow = (HANDLE)Desktop->Window;
    if (RootWindow == NULL) {
        return FALSE;
    }

    if (GetDesktopScreenRect(Desktop, &ScreenRect) == FALSE) {
        return FALSE;
    }

    if (Cube3DGetPreferredSize(&PreferredSize) == FALSE) {
        return FALSE;
    }

    ScreenWidth = ScreenRect.X2 - ScreenRect.X1 + 1;
    ScreenHeight = ScreenRect.Y2 - ScreenRect.Y1 + 1;
    if (ScreenWidth <= 0 || ScreenHeight <= 0) {
        return FALSE;
    }

    WindowWidth = PreferredSize.X;
    WindowHeight = PreferredSize.Y;
    if (WindowWidth > ScreenWidth - 16) WindowWidth = ScreenWidth - 16;
    if (WindowHeight > ScreenHeight - 16) WindowHeight = ScreenHeight - 16;
    if (WindowWidth < 1) WindowWidth = 1;
    if (WindowHeight < 1) WindowHeight = 1;

    WindowRect.X1 = 8;
    WindowRect.Y1 = DESKTOP_CUBE3D_WINDOW_TOP;
    WindowRect.X2 = WindowRect.X1 + WindowWidth - 1;
    WindowRect.Y2 = WindowRect.Y1 + WindowHeight - 1;
    if (WindowRect.X2 >= ScreenWidth) WindowRect.X2 = ScreenWidth - 1;
    if (WindowRect.Y2 >= ScreenHeight) WindowRect.Y2 = ScreenHeight - 1;
    if (WindowRect.X2 < WindowRect.X1) WindowRect.X2 = WindowRect.X1;
    if (WindowRect.Y2 < WindowRect.Y1) WindowRect.Y2 = WindowRect.Y1;

    CubeWindow = FindWindow(RootWindow, DESKTOP_CUBE3D_WINDOW_ID);
    if (CubeWindow != NULL) {
        (void)MoveWindow(CubeWindow, &WindowRect);
        (void)SetWindowCaption(CubeWindow, TEXT("3D Cube"));
        (void)HideWindow(CubeWindow);
        return TRUE;
    }

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = RootWindow;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = DESKTOP_CUBE3D_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_SYSTEM_DECORATED;
    WindowInfo.ID = DESKTOP_CUBE3D_WINDOW_ID;
    WindowInfo.WindowPosition.X = WindowRect.X1;
    WindowInfo.WindowPosition.Y = WindowRect.Y1;
    WindowInfo.WindowSize.X = WindowRect.X2 - WindowRect.X1 + 1;
    WindowInfo.WindowSize.Y = WindowRect.Y2 - WindowRect.Y1 + 1;
    WindowInfo.ShowHide = FALSE;

    CubeWindow = (HANDLE)CreateWindow(&WindowInfo);
    if (CubeWindow == NULL) {
        return FALSE;
    }

    (void)SetWindowCaption(CubeWindow, TEXT("3D Cube"));
    (void)HideWindow(CubeWindow);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Ensure the floating log viewer window exists and has the expected rect.
 * @param Desktop Target desktop.
 * @return TRUE on success.
 */
static BOOL EnsureLogViewerWindow(LPDESKTOP Desktop) {
    HANDLE RootWindow;
    HANDLE LogViewerWindow;
    WINDOWINFO WindowInfo;
    RECT ScreenRect;
    RECT WindowRect;
    I32 ScreenWidth;
    I32 ScreenHeight;
    I32 WindowWidth;
    I32 WindowHeight;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return FALSE;
    }
    if (LogViewerEnsureClassRegistered() == FALSE) {
        return FALSE;
    }

    RootWindow = (HANDLE)Desktop->Window;
    if (RootWindow == NULL) {
        return FALSE;
    }

    if (GetDesktopScreenRect(Desktop, &ScreenRect) == FALSE) {
        return FALSE;
    }

    ScreenWidth = ScreenRect.X2 - ScreenRect.X1 + 1;
    ScreenHeight = ScreenRect.Y2 - ScreenRect.Y1 + 1;
    if (ScreenWidth <= 0 || ScreenHeight <= 0) {
        return FALSE;
    }

    WindowWidth = ScreenWidth / 2;
    WindowHeight = (ScreenHeight * 3) / 4;
    if (WindowWidth < 1) WindowWidth = 1;
    if (WindowHeight < 1) WindowHeight = 1;

    WindowRect.X1 = ScreenWidth - WindowWidth;
    WindowRect.Y1 = 0;
    WindowRect.X2 = ScreenWidth - 1;
    WindowRect.Y2 = WindowHeight - 1;

    LogViewerWindow = FindWindow(RootWindow, DESKTOP_LOG_VIEWER_WINDOW_ID);
    if (LogViewerWindow != NULL) {
        (void)MoveWindow(LogViewerWindow, &WindowRect);
        (void)SetWindowCaption(LogViewerWindow, TEXT("Kernel Log"));
        (void)HideWindow(LogViewerWindow);
        return TRUE;
    }

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = RootWindow;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = DESKTOP_LOG_VIEWER_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_SYSTEM_DECORATED;
    WindowInfo.ID = DESKTOP_LOG_VIEWER_WINDOW_ID;
    WindowInfo.WindowPosition.X = WindowRect.X1;
    WindowInfo.WindowPosition.Y = WindowRect.Y1;
    WindowInfo.WindowSize.X = WindowRect.X2 - WindowRect.X1 + 1;
    WindowInfo.WindowSize.Y = WindowRect.Y2 - WindowRect.Y1 + 1;
    WindowInfo.ShowHide = FALSE;

    LogViewerWindow = (HANDLE)CreateWindow(&WindowInfo);
    if (LogViewerWindow == NULL) {
        return FALSE;
    }

    (void)SetWindowCaption(LogViewerWindow, TEXT("Kernel Log"));
    (void)HideWindow(LogViewerWindow);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Ensure the on-screen debug information window exists and has the expected rect.
 * @param Desktop Target desktop.
 * @return TRUE on success.
 */
static BOOL EnsureOnScreenDebugInfoWindow(LPDESKTOP Desktop) {
    HANDLE RootWindow;
    HANDLE DebugInfoWindow;
    WINDOWINFO WindowInfo;
    POINT PreferredSize;
    RECT ScreenRect;
    RECT WindowRect;
    I32 ScreenWidth;
    I32 ScreenHeight;
    I32 WindowWidth;
    I32 WindowHeight;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return FALSE;
    }

    RootWindow = (HANDLE)Desktop->Window;
    if (RootWindow == NULL) {
        return FALSE;
    }

    if (GetDesktopScreenRect(Desktop, &ScreenRect) == FALSE) {
        return FALSE;
    }

    if (OnScreenDebugInfoGetPreferredSize(&PreferredSize) == FALSE) {
        return FALSE;
    }

    ScreenWidth = ScreenRect.X2 - ScreenRect.X1 + 1;
    ScreenHeight = ScreenRect.Y2 - ScreenRect.Y1 + 1;
    if (ScreenWidth <= 0 || ScreenHeight <= 0) {
        return FALSE;
    }

    WindowWidth = PreferredSize.X;
    WindowHeight = PreferredSize.Y;
    if (WindowWidth > ScreenWidth) WindowWidth = ScreenWidth;
    if (WindowHeight > ScreenHeight) WindowHeight = ScreenHeight;
    if (WindowWidth < 1) WindowWidth = 1;
    if (WindowHeight < 1) WindowHeight = 1;

    WindowRect.X1 = 0;
    WindowRect.Y1 = 0;
    WindowRect.X2 = WindowRect.X1 + WindowWidth - 1;
    WindowRect.Y2 = WindowRect.Y1 + WindowHeight - 1;

    DebugInfoWindow = FindWindow(RootWindow, DESKTOP_ON_SCREEN_DEBUG_INFO_WINDOW_ID);
    if (DebugInfoWindow != NULL) {
        (void)MoveWindow(DebugInfoWindow, &WindowRect);
        return TRUE;
    }

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = RootWindow;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = NULL;
    WindowInfo.Function = OnScreenDebugInfoWindowFunc;
    WindowInfo.Style = EWS_VISIBLE | EWS_BARE_SURFACE | EWS_ALWAYS_AT_BOTTOM;
    WindowInfo.ID = DESKTOP_ON_SCREEN_DEBUG_INFO_WINDOW_ID;
    WindowInfo.WindowPosition.X = WindowRect.X1;
    WindowInfo.WindowPosition.Y = WindowRect.Y1;
    WindowInfo.WindowSize.X = WindowRect.X2 - WindowRect.X1 + 1;
    WindowInfo.WindowSize.Y = WindowRect.Y2 - WindowRect.Y1 + 1;
    WindowInfo.ShowHide = TRUE;

    DebugInfoWindow = (HANDLE)CreateWindow(&WindowInfo);
    if (DebugInfoWindow == NULL) {
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

BOOL StartupDesktopComponentsInitialize(LPDESKTOP Desktop) {
    HANDLE RootWindow;
    HANDLE ShellBarWindow;
    BOOL Cube3DResult;
    BOOL LogViewerResult;
    BOOL OnScreenDebugInfoResult;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return FALSE;
    }

    RootWindow = (HANDLE)Desktop->Window;
    if (RootWindow == NULL) {
        return FALSE;
    }

    ShellBarWindow = ShellBarGetWindow(RootWindow);
    if (ShellBarWindow == NULL) {
        if (ShellBarCreate(RootWindow) == FALSE) {
            // Shell bar injection is best-effort; continue with other startup components.
        }

        ShellBarWindow = ShellBarGetWindow(RootWindow);
    }
    Cube3DResult = EnsureCube3DWindow(Desktop);
    LogViewerResult = EnsureLogViewerWindow(Desktop);
    OnScreenDebugInfoResult = EnsureOnScreenDebugInfoWindow(Desktop);
    return (Cube3DResult != FALSE) &&
           (LogViewerResult != FALSE) &&
           (OnScreenDebugInfoResult != FALSE);
}

/***************************************************************************/
