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

#include "ui/LogViewer.h"
#include "ui/ShellBar.h"

/***************************************************************************/

#define DESKTOP_LOG_VIEWER_WINDOW_ID 0x534C4F47

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

    LogViewerWindow = (HANDLE)FindWindow((LPWINDOW)RootWindow, DESKTOP_LOG_VIEWER_WINDOW_ID);
    if (LogViewerWindow != NULL) {
        (void)MoveWindow(LogViewerWindow, &WindowRect);
        (void)SetWindowCaption(LogViewerWindow, TEXT("Kernel Log"));
        (void)ShowWindow(LogViewerWindow, TRUE);
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
    WindowInfo.Style = EWS_VISIBLE | EWS_SYSTEM_DECORATED;
    WindowInfo.ID = DESKTOP_LOG_VIEWER_WINDOW_ID;
    WindowInfo.WindowPosition.X = WindowRect.X1;
    WindowInfo.WindowPosition.Y = WindowRect.Y1;
    WindowInfo.WindowSize.X = WindowRect.X2 - WindowRect.X1 + 1;
    WindowInfo.WindowSize.Y = WindowRect.Y2 - WindowRect.Y1 + 1;
    WindowInfo.ShowHide = TRUE;

    LogViewerWindow = (HANDLE)CreateWindow(&WindowInfo);
    if (LogViewerWindow == NULL) {
        return FALSE;
    }

    (void)SetWindowCaption(LogViewerWindow, TEXT("Kernel Log"));
    return TRUE;
}

/***************************************************************************/

BOOL StartupDesktopComponentsInitialize(LPDESKTOP Desktop) {
    BOOL LogViewerResult;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return FALSE;
    }

    if (ShellBarCreate((HANDLE)Desktop->Window) == FALSE) {
        return FALSE;
    }

    LogViewerResult = EnsureLogViewerWindow(Desktop);
    return LogViewerResult != FALSE;
}

/***************************************************************************/
