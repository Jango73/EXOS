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


    Desktop component composition

\************************************************************************/

#include "Desktop-Components.h"

#include "ui/ClockWidget.h"
#include "ui/LogViewer.h"
#include "ui/ShellBar.h"
#include "desktop/Desktop-NonClient.h"
#include "Log.h"

/***************************************************************************/

#define DESKTOP_SHELL_BAR_CLOCK_WINDOW_ID 0x5342434C
#define DESKTOP_SHELL_BAR_CLOCK_PROP TEXT("desktop.shellbar.clock")
#define DESKTOP_LOG_VIEWER_WINDOW_ID 0x534C4F47
#define DESKTOP_LOG_VIEWER_PROP TEXT("desktop.logviewer.window")
#define DESKTOP_PENDING_COMPONENT_CLOCK 0x00000001

/***************************************************************************/

/**
 * @brief Find one direct child window by property value.
 * @param Parent Parent window.
 * @param Name Property name.
 * @param Value Property value.
 * @return Matching child or NULL.
 */
static LPWINDOW DesktopComponentsFindDirectChildByProp(LPWINDOW Parent, LPCSTR Name, U32 Value) {
    U32 ChildCount;
    U32 ChildIndex;
    HANDLE ChildWindow;

    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return NULL;
    if (Name == NULL) return NULL;

    ChildCount = GetWindowChildCount((HANDLE)Parent);
    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        ChildWindow = GetWindowChild((HANDLE)Parent, ChildIndex);
        if (ChildWindow == NULL) continue;
        if (GetWindowProp(ChildWindow, Name) == Value) return (LPWINDOW)ChildWindow;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Inject the desktop clock widget into the shell bar components slot.
 * @param Desktop Target desktop.
 * @return TRUE on success.
 */
static BOOL DesktopComponentsInjectShellBarClock(LPDESKTOP Desktop) {
    HANDLE ShellBarWindow;
    HANDLE ComponentsSlotWindow;
    HANDLE ClockWindow;
    RECT SlotRect;
    RECT SlotClientRect;
    WINDOWINFO WindowInfo;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return FALSE;
    }
    if (DesktopClockWidgetEnsureClassRegistered() == FALSE) {
        return FALSE;
    }

    ShellBarWindow = ShellBarGetWindow((HANDLE)Desktop->Window);
    ComponentsSlotWindow = ShellBarGetSlotWindow(ShellBarWindow, SHELL_BAR_SLOT_COMPONENTS);
    if (ComponentsSlotWindow == NULL) {
        return FALSE;
    }
    if (GetWindowRect(ComponentsSlotWindow, &SlotRect) != FALSE &&
        GetWindowClientRect(ComponentsSlotWindow, &SlotClientRect) != FALSE) {
    }

    ClockWindow = (HANDLE)DesktopComponentsFindDirectChildByProp((LPWINDOW)ComponentsSlotWindow, DESKTOP_SHELL_BAR_CLOCK_PROP, 1);
    if (ClockWindow != NULL) {
        return TRUE;
    }

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = ComponentsSlotWindow;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = DESKTOP_CLOCK_WIDGET_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_VISIBLE | EWS_CLIENT_DECORATED;
    WindowInfo.ID = DESKTOP_SHELL_BAR_CLOCK_WINDOW_ID;
    WindowInfo.WindowPosition.X = 0;
    WindowInfo.WindowPosition.Y = 0;
    WindowInfo.WindowSize.X = 1;
    WindowInfo.WindowSize.Y = 1;
    WindowInfo.ShowHide = TRUE;

    ClockWindow = (HANDLE)CreateWindow(&WindowInfo);
    if (ClockWindow == NULL) {
        return FALSE;
    }


    (void)SetWindowProp(ClockWindow, DESKTOP_SHELL_BAR_CLOCK_PROP, 1);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Create or refresh the floating log viewer window on the desktop root.
 * @param Desktop Target desktop.
 * @return TRUE on success.
 */
static BOOL DesktopComponentsEnsureLogViewerWindow(LPDESKTOP Desktop) {
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
    if (DesktopLogViewerEnsureClassRegistered() == FALSE) {
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

    LogViewerWindow = (HANDLE)DesktopComponentsFindDirectChildByProp((LPWINDOW)RootWindow, DESKTOP_LOG_VIEWER_PROP, 1);
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

    (void)SetWindowProp(LogViewerWindow, DESKTOP_LOG_VIEWER_PROP, 1);
    (void)SetWindowCaption(LogViewerWindow, TEXT("Kernel Log"));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Instantiate desktop-owned UI components.
 * @param Desktop Target desktop.
 * @return TRUE on success.
 */
BOOL DesktopComponentsInitialize(LPDESKTOP Desktop) {
    BOOL ClockResult;
    BOOL LogViewerResult;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return FALSE;
    }

    if (ShellBarCreate((HANDLE)Desktop->Window) == FALSE) {
        return FALSE;
    }

    Desktop->PendingComponents |= DESKTOP_PENDING_COMPONENT_CLOCK;
    ClockResult = DesktopComponentsInjectShellBarClock(Desktop);
    if (ClockResult != FALSE) {
        Desktop->PendingComponents &= ~DESKTOP_PENDING_COMPONENT_CLOCK;
    }
    LogViewerResult = DesktopComponentsEnsureLogViewerWindow(Desktop);
    return (ClockResult != FALSE && LogViewerResult != FALSE);
}

/***************************************************************************/

/**
 * @brief Retry pending desktop-owned component injections after one child append.
 * @param Desktop Target desktop.
 * @param ChildWindowID Newly appended child window identifier.
 * @return TRUE when one pending component was realized.
 */
BOOL DesktopComponentsHandleChildAppended(LPDESKTOP Desktop, U32 ChildWindowID) {
    BOOL Result = FALSE;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (ChildWindowID != SHELL_BAR_SLOT_COMPONENTS_WINDOW_ID) return FALSE;
    if ((Desktop->PendingComponents & DESKTOP_PENDING_COMPONENT_CLOCK) == 0) return FALSE;

    Result = DesktopComponentsInjectShellBarClock(Desktop);
    if (Result != FALSE) {
        Desktop->PendingComponents &= ~DESKTOP_PENDING_COMPONENT_CLOCK;
    }
    return Result;
}
