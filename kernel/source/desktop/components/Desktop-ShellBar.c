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


    Desktop shell bar component

\************************************************************************/

#include "desktop/components/Desktop-ShellBar.h"

#include "desktop/components/WindowDockable.h"

/************************************************************************/

#define DESKTOP_SHELL_BAR_HEIGHT 32

/************************************************************************/

BOOL DesktopShellBarEnsureClassRegistered(void) {
    return WindowDockableClassEnsureDerivedRegistered(DESKTOP_SHELL_BAR_WINDOW_CLASS_NAME, DesktopShellBarWindowFunc);
}

/************************************************************************/

BOOL DesktopShellBarCreate(LPDESKTOP Desktop) {
    WINDOWINFO WindowInfo;
    LPWINDOW Window;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (Desktop->Window == NULL || Desktop->Window->TypeID != KOID_WINDOW) return FALSE;
    if (DesktopShellBarEnsureClassRegistered() == FALSE) return FALSE;

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = (HANDLE)Desktop->Window;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = DESKTOP_SHELL_BAR_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_VISIBLE | EWS_BARE_SURFACE;
    WindowInfo.ID = 0x53484252;
    WindowInfo.WindowPosition.X = 0;
    WindowInfo.WindowPosition.Y = 0;
    WindowInfo.WindowSize.X = 1;
    WindowInfo.WindowSize.Y = 1;
    WindowInfo.ShowHide = TRUE;

    Window = CreateWindow(&WindowInfo);
    return Window != NULL;
}

/************************************************************************/

U32 DesktopShellBarWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    switch (Message) {
        case EWM_CREATE:
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_ENABLED, 1);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_EDGE, DOCK_EDGE_BOTTOM);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_PRIORITY, 0);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_ORDER, 0);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_POLICY, DOCK_LAYOUT_POLICY_FIXED);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_PREFERRED, DESKTOP_SHELL_BAR_HEIGHT);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_MINIMUM, DESKTOP_SHELL_BAR_HEIGHT);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_MAXIMUM, DESKTOP_SHELL_BAR_HEIGHT);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_WEIGHT, 1);
            break;
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/************************************************************************/
