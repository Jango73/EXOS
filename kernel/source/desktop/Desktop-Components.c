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
        DEBUG(TEXT("[DesktopComponentsInjectShellBarClock] Invalid desktop=%p"), Desktop);
        return FALSE;
    }
    if (DesktopClockWidgetEnsureClassRegistered() == FALSE) {
        DEBUG(TEXT("[DesktopComponentsInjectShellBarClock] Clock widget class registration failed"));
        return FALSE;
    }

    ShellBarWindow = ShellBarGetWindow((HANDLE)Desktop->Window);
    ComponentsSlotWindow = ShellBarGetSlotWindow(ShellBarWindow, SHELL_BAR_SLOT_COMPONENTS);
    if (ComponentsSlotWindow == NULL) {
        DEBUG(TEXT("[DesktopComponentsInjectShellBarClock] Components slot unavailable slot=%p"), ComponentsSlotWindow);
        return FALSE;
    }
    if (GetWindowRect(ComponentsSlotWindow, &SlotRect) != FALSE &&
        GetWindowClientRect(ComponentsSlotWindow, &SlotClientRect) != FALSE) {
        DEBUG(
            TEXT("[DesktopComponentsInjectShellBarClock] Components slot=%p width=%u height=%u"),
            ComponentsSlotWindow,
            (UINT)(SlotClientRect.X2 - SlotClientRect.X1 + 1),
            (UINT)(SlotClientRect.Y2 - SlotClientRect.Y1 + 1));
    }

    ClockWindow = (HANDLE)DesktopComponentsFindDirectChildByProp((LPWINDOW)ComponentsSlotWindow, DESKTOP_SHELL_BAR_CLOCK_PROP, 1);
    if (ClockWindow != NULL) {
        DEBUG(TEXT("[DesktopComponentsInjectShellBarClock] Clock already present window=%p"), ClockWindow);
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
        DEBUG(TEXT("[DesktopComponentsInjectShellBarClock] CreateWindow failed for clock id=%x"), WindowInfo.ID);
        return FALSE;
    }

    DEBUG(
        TEXT("[DesktopComponentsInjectShellBarClock] Clock window=%p parent=%p id=%x width=%u height=%u"),
        ClockWindow,
        ComponentsSlotWindow,
        WindowInfo.ID,
        (UINT)WindowInfo.WindowSize.X,
        (UINT)WindowInfo.WindowSize.Y);

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
        DEBUG(TEXT("[DesktopComponentsEnsureLogViewerWindow] Invalid desktop=%p"), Desktop);
        return FALSE;
    }
    if (DesktopLogViewerEnsureClassRegistered() == FALSE) {
        DEBUG(TEXT("[DesktopComponentsEnsureLogViewerWindow] Log viewer class registration failed"));
        return FALSE;
    }

    RootWindow = (HANDLE)Desktop->Window;
    if (RootWindow == NULL) {
        DEBUG(TEXT("[DesktopComponentsEnsureLogViewerWindow] Root window unavailable"));
        return FALSE;
    }

    if (GetDesktopScreenRect(Desktop, &ScreenRect) == FALSE) {
        DEBUG(TEXT("[DesktopComponentsEnsureLogViewerWindow] Desktop screen rect unavailable"));
        return FALSE;
    }

    ScreenWidth = ScreenRect.X2 - ScreenRect.X1 + 1;
    ScreenHeight = ScreenRect.Y2 - ScreenRect.Y1 + 1;
    if (ScreenWidth <= 0 || ScreenHeight <= 0) {
        DEBUG(TEXT("[DesktopComponentsEnsureLogViewerWindow] Invalid desktop size width=%u height=%u"),
            (UINT)(ScreenWidth > 0 ? ScreenWidth : 0),
            (UINT)(ScreenHeight > 0 ? ScreenHeight : 0));
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
        DEBUG(TEXT("[DesktopComponentsEnsureLogViewerWindow] Existing log viewer=%p"), LogViewerWindow);
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
        DEBUG(TEXT("[DesktopComponentsEnsureLogViewerWindow] CreateWindow failed for log viewer id=%x"), WindowInfo.ID);
        return FALSE;
    }

    (void)SetWindowProp(LogViewerWindow, DESKTOP_LOG_VIEWER_PROP, 1);
    (void)SetWindowCaption(LogViewerWindow, TEXT("Kernel Log"));
    DEBUG(TEXT("[DesktopComponentsEnsureLogViewerWindow] Created log viewer=%p"), LogViewerWindow);
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
        DEBUG(TEXT("[DesktopComponentsInitialize] Invalid desktop=%p"), Desktop);
        return FALSE;
    }

    if (ShellBarCreate((HANDLE)Desktop->Window) == FALSE) {
        DEBUG(TEXT("[DesktopComponentsInitialize] Shell bar creation failed"));
        return FALSE;
    }

    Desktop->PendingComponents |= DESKTOP_PENDING_COMPONENT_CLOCK;
    ClockResult = DesktopComponentsInjectShellBarClock(Desktop);
    if (ClockResult != FALSE) {
        Desktop->PendingComponents &= ~DESKTOP_PENDING_COMPONENT_CLOCK;
    }
    LogViewerResult = DesktopComponentsEnsureLogViewerWindow(Desktop);
    DEBUG(TEXT("[DesktopComponentsInitialize] Clock injection result=%u"), (UINT)ClockResult);
    DEBUG(TEXT("[DesktopComponentsInitialize] Log viewer result=%u"), (UINT)LogViewerResult);
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

    DEBUG(TEXT("[DesktopComponentsHandleChildAppended] Retrying pending clock injection child_id=%x"), ChildWindowID);
    Result = DesktopComponentsInjectShellBarClock(Desktop);
    if (Result != FALSE) {
        Desktop->PendingComponents &= ~DESKTOP_PENDING_COMPONENT_CLOCK;
    }
    DEBUG(TEXT("[DesktopComponentsHandleChildAppended] Retry result=%u"), (UINT)Result);
    return Result;
}
