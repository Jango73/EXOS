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

#include "desktop/components/ClockWidget.h"
#include "desktop/components/ShellBar.h"

/***************************************************************************/

#define DESKTOP_SHELL_BAR_CLOCK_WINDOW_ID 0x5342434C
#define DESKTOP_SHELL_BAR_CLOCK_PROP TEXT("desktop.shellbar.clock")

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
    LPWINDOW ComponentsSlotWindow;
    LPWINDOW ClockWindow;
    WINDOWINFO WindowInfo;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (DesktopClockWidgetEnsureClassRegistered() == FALSE) return FALSE;

    ComponentsSlotWindow = ShellBarGetSlotWindow(Desktop, SHELL_BAR_SLOT_COMPONENTS);
    if (ComponentsSlotWindow == NULL || ComponentsSlotWindow->TypeID != KOID_WINDOW) return FALSE;

    ClockWindow = DesktopComponentsFindDirectChildByProp(ComponentsSlotWindow, DESKTOP_SHELL_BAR_CLOCK_PROP, 1);
    if (ClockWindow != NULL && ClockWindow->TypeID == KOID_WINDOW) return TRUE;

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = (HANDLE)ComponentsSlotWindow;
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

    ClockWindow = CreateWindow(&WindowInfo);
    if (ClockWindow == NULL) return FALSE;

    (void)SetWindowProp((HANDLE)ClockWindow, DESKTOP_SHELL_BAR_CLOCK_PROP, 1);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Instantiate desktop-owned UI components.
 * @param Desktop Target desktop.
 * @return TRUE on success.
 */
BOOL DesktopComponentsInitialize(LPDESKTOP Desktop) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    if (ShellBarCreate(Desktop) == FALSE) return FALSE;
    return DesktopComponentsInjectShellBarClock(Desktop);
}
