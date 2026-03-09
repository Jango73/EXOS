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

#include "desktop/components/ShellBar.h"

#include "desktop/components/WindowDockable.h"
#include "desktop/Desktop-ThemeResolver.h"
#include "desktop/Desktop-ThemeTokens.h"
#include "GFX.h"
#include "Kernel.h"

/************************************************************************/

#define SHELL_BAR_HEIGHT 32

/************************************************************************/

BOOL ShellBarEnsureClassRegistered(void) {
    return WindowDockableClassEnsureDerivedRegistered(SHELL_BAR_WINDOW_CLASS_NAME, ShellBarWindowFunc);
}

/************************************************************************/

BOOL ShellBarCreate(LPDESKTOP Desktop) {
    WINDOWINFO WindowInfo;
    LPWINDOW Window;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;
    if (Desktop->Window == NULL || Desktop->Window->TypeID != KOID_WINDOW) return FALSE;
    if (ShellBarEnsureClassRegistered() == FALSE) return FALSE;

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = (HANDLE)Desktop->Window;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = SHELL_BAR_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_VISIBLE | EWS_CLIENT_DECORATED;
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

U32 ShellBarWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    HANDLE GC;
    RECT Rect;
    RECTINFO RectInfo;
    BRUSH Brush;
    COLOR Background;

    UNUSED(Param1);
    UNUSED(Param2);

    switch (Message) {
        case EWM_CREATE:
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_ENABLED, 1);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_EDGE, DOCK_EDGE_TOP);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_PRIORITY, 0);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_ORDER, 0);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_POLICY, DOCK_LAYOUT_POLICY_FIXED);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_PREFERRED, SHELL_BAR_HEIGHT);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_MINIMUM, SHELL_BAR_HEIGHT);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_MAXIMUM, SHELL_BAR_HEIGHT);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_WEIGHT, 1);
            break;

        case EWM_DRAW:
            GC = BeginWindowDraw(Window);
            if (GC == NULL) return 1;
            if (GetWindowRect(Window, &Rect) == FALSE) {
                EndWindowDraw(Window);
                return 1;
            }

            RectInfo.Header.Size = sizeof(RECTINFO);
            RectInfo.Header.Version = EXOS_ABI_VERSION;
            RectInfo.Header.Flags = 0;
            RectInfo.GC = GC;
            RectInfo.X1 = Rect.X1;
            RectInfo.Y1 = Rect.Y1;
            RectInfo.X2 = Rect.X2;
            RectInfo.Y2 = Rect.Y2;

            SelectPen(GC, NULL);

            if (DesktopThemeResolveLevel1Color(TEXT("window.client"), TEXT("normal"), TEXT("background"), &Background) ||
                DesktopThemeResolveTokenColorByName(TEXT("color.client.background"), &Background)) {
                MemorySet(&Brush, 0, sizeof(BRUSH));
                Brush.TypeID = KOID_BRUSH;
                Brush.References = 1;
                Brush.Color = Background;
                Brush.Pattern = MAX_U32;
                SelectBrush(GC, (HANDLE)&Brush);
            } else {
                SelectBrush(GC, GetSystemBrush(SM_COLOR_CLIENT));
            }

            (void)Rectangle(&RectInfo);
            EndWindowDraw(Window);
            return 1;
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/************************************************************************/
