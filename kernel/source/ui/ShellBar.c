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

#include "ui/ShellBar.h"

#include "Log.h"
#include "ui/ClockWidget.h"
#include "ui/WindowDockable.h"

/************************************************************************/

#define SHELL_BAR_HEIGHT 56
#define SHELL_BAR_WINDOW_ID 0x53484252
#define SHELL_BAR_SLOT_WINDOW_CLASS_NAME TEXT("ShellBarSlotWindowClass")
#define SHELL_BAR_SLOT_LEFT_WIDTH 240
#define SHELL_BAR_SLOT_COMPONENTS_WIDTH 168
#define SHELL_BAR_ROLE_PROP TEXT("shellbar.role")
#define SHELL_BAR_SLOT_PROP TEXT("shellbar.slot")
#define SHELL_BAR_CLOCK_PROP TEXT("desktop.shellbar.clock")
#define SHELL_BAR_CLOCK_WINDOW_ID 0x5342434C
#define SHELL_BAR_ROLE_MAIN 1

/************************************************************************/

BOOL ShellBarEnsureClassRegistered(void) {
    return WindowDockableClassEnsureDerivedRegistered(SHELL_BAR_WINDOW_CLASS_NAME, ShellBarWindowFunc);
}

/************************************************************************/

/**
 * @brief Resize all direct children of one slot to its full client rectangle.
 * @param SlotWindow Slot window.
 */
static void ShellBarSlotResizeChildrenToClient(HANDLE SlotWindow) {
    RECT ClientRect;
    HANDLE ChildWindow;
    HANDLE NextChildWindow;

    if (SlotWindow == NULL) return;
    if (GetWindowClientRect(SlotWindow, &ClientRect) == FALSE) return;


    for (ChildWindow = GetWindowChild((HANDLE)SlotWindow, 0); ChildWindow != NULL; ChildWindow = NextChildWindow) {
        NextChildWindow = GetNextWindowSibling(ChildWindow);
        (void)MoveWindow(ChildWindow, &ClientRect);
    }
}

/************************************************************************/

/**
 * @brief Resolve one direct child by window identifier.
 * @param Parent Parent window.
 * @param WindowID Window identifier.
 * @return Direct child pointer or NULL.
 */
static HANDLE ShellBarFindDirectChildByProp(HANDLE Parent, LPCSTR Name, U32 Value) {
    U32 ChildCount;
    U32 ChildIndex;
    HANDLE ChildWindow;

    if (Parent == NULL) return NULL;
    if (Name == NULL) return NULL;

    ChildCount = GetWindowChildCount(Parent);
    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        ChildWindow = GetWindowChild(Parent, ChildIndex);
        if (ChildWindow == NULL) continue;
        if (GetWindowProp(ChildWindow, Name) == Value) return ChildWindow;
    }

    return NULL;
}

/************************************************************************/

BOOL ShellBarEnsureClockWidget(HANDLE ShellBarWindow) {
    HANDLE ComponentsSlotWindow;
    HANDLE ClockWindow;
    WINDOWINFO WindowInfo;

    if (ShellBarWindow == NULL) return FALSE;
    if (DesktopClockWidgetEnsureClassRegistered() == FALSE) return FALSE;

    ComponentsSlotWindow = ShellBarGetSlotWindow(ShellBarWindow, SHELL_BAR_SLOT_COMPONENTS);
    if (ComponentsSlotWindow == NULL) return FALSE;

    ClockWindow = ShellBarFindDirectChildByProp(ComponentsSlotWindow, SHELL_BAR_CLOCK_PROP, 1);
    if (ClockWindow != NULL) return TRUE;

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = ComponentsSlotWindow;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = DESKTOP_CLOCK_WIDGET_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_VISIBLE | EWS_CLIENT_DECORATED;
    WindowInfo.ID = SHELL_BAR_CLOCK_WINDOW_ID;
    WindowInfo.WindowPosition.X = 0;
    WindowInfo.WindowPosition.Y = 0;
    WindowInfo.WindowSize.X = 1;
    WindowInfo.WindowSize.Y = 1;
    WindowInfo.ShowHide = TRUE;

    ClockWindow = (HANDLE)CreateWindow(&WindowInfo);
    if (ClockWindow == NULL) return FALSE;

    (void)SetWindowProp(ClockWindow, SHELL_BAR_CLOCK_PROP, 1);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Layout all shell bar slots in the shell bar client area.
 * @param ShellBarWindow Shell bar window.
 */
static void ShellBarLayoutSlots(HANDLE ShellBarWindow) {
    RECT ClientRect;
    RECT ComponentsRect;
    HANDLE ComponentsSlotWindow;
    I32 ClientWidth;
    I32 LeftWidth;
    I32 ComponentsWidth;

    if (ShellBarWindow == NULL) return;
    if (GetWindowClientRect(ShellBarWindow, &ClientRect) == FALSE) return;

    ClientWidth = ClientRect.X2 - ClientRect.X1 + 1;
    if (ClientWidth <= 0) return;

    LeftWidth = SHELL_BAR_SLOT_LEFT_WIDTH;
    ComponentsWidth = SHELL_BAR_SLOT_COMPONENTS_WIDTH;
    if (ComponentsWidth > ClientWidth) ComponentsWidth = ClientWidth;
    if (LeftWidth > (ClientWidth - ComponentsWidth)) LeftWidth = ClientWidth - ComponentsWidth;
    if (LeftWidth < 0) LeftWidth = 0;

    ComponentsRect = ClientRect;
    ComponentsRect.X1 = ClientRect.X1 + LeftWidth + (ClientWidth - LeftWidth - ComponentsWidth);
    if (ComponentsWidth <= 0) {
        ComponentsRect.X1 = ComponentsRect.X2;
    }

    ComponentsSlotWindow = ShellBarFindDirectChildByProp(ShellBarWindow, SHELL_BAR_SLOT_PROP, SHELL_BAR_SLOT_COMPONENTS);
    if (ComponentsSlotWindow != NULL) (void)MoveWindow(ComponentsSlotWindow, &ComponentsRect);
}

/************************************************************************/

/**
 * @brief Refit shell bar slot children after one descendant append.
 * @param ShellBarWindow Shell bar window.
 */
static void ShellBarHandleChildAppended(HANDLE ShellBarWindow) {
    HANDLE ComponentsSlotWindow;

    if (ShellBarWindow == NULL) return;


    ShellBarLayoutSlots(ShellBarWindow);

    ComponentsSlotWindow = ShellBarFindDirectChildByProp(ShellBarWindow, SHELL_BAR_SLOT_PROP, SHELL_BAR_SLOT_COMPONENTS);
    if (ComponentsSlotWindow != NULL) {
        ShellBarSlotResizeChildrenToClient(ComponentsSlotWindow);
    }
}

/************************************************************************/

/**
 * @brief Create one shell bar content slot child window.
 * @param ShellBarWindow Shell bar parent window.
 * @param WindowID Slot window identifier.
 * @return TRUE on success.
 */
static BOOL ShellBarCreateSlotWindow(HANDLE ShellBarWindow, U32 WindowID) {
    WINDOWINFO WindowInfo;
    HANDLE Window;

    if (ShellBarWindow == NULL) return FALSE;

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = ShellBarWindow;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = SHELL_BAR_SLOT_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_VISIBLE | EWS_CLIENT_DECORATED;
    WindowInfo.ID = WindowID;
    WindowInfo.WindowPosition.X = 0;
    WindowInfo.WindowPosition.Y = 0;
    WindowInfo.WindowSize.X = 1;
    WindowInfo.WindowSize.Y = 1;
    WindowInfo.ShowHide = TRUE;

    Window = (HANDLE)CreateWindow(&WindowInfo);
    if (Window != NULL) {
        U32 SlotID = 0;

        if (WindowID == SHELL_BAR_SLOT_COMPONENTS_WINDOW_ID) SlotID = SHELL_BAR_SLOT_COMPONENTS;
        if (SlotID != 0) (void)SetWindowProp((HANDLE)Window, SHELL_BAR_SLOT_PROP, SlotID);
    }
    return Window != NULL;
}

/************************************************************************/

static U32 ShellBarSlotWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    switch (Message) {
        case EWM_CREATE:
            ShellBarSlotResizeChildrenToClient(Window);
            return 1;

        case EWM_CHILD_APPENDED:
        case EWM_CHILD_REMOVED:
            ShellBarSlotResizeChildrenToClient(Window);
            return 1;

        case EWM_NOTIFY:
            if (Param1 == EWN_WINDOW_RECT_CHANGED) {
                ShellBarSlotResizeChildrenToClient(Window);
                return 1;
            }
            break;
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/************************************************************************/

/**
 * @brief Ensure slot window class registration.
 * @return TRUE on success.
 */
static BOOL ShellBarEnsureSlotClassRegistered(void) {
    if (FindWindowClass(SHELL_BAR_SLOT_WINDOW_CLASS_NAME) != NULL) return TRUE;
    return RegisterWindowClass(SHELL_BAR_SLOT_WINDOW_CLASS_NAME, 0, NULL, ShellBarSlotWindowFunc, 0) != NULL;
}

/************************************************************************/

/**
 * @brief Ensure all default slot windows exist on the shell bar.
 * @param ShellBarWindow Shell bar window.
 * @return TRUE on success.
 */
static BOOL ShellBarEnsureSlotWindows(HANDLE ShellBarWindow) {
    if (ShellBarWindow == NULL) return FALSE;
    if (ShellBarEnsureSlotClassRegistered() == FALSE) return FALSE;

    if (ShellBarFindDirectChildByProp(ShellBarWindow, SHELL_BAR_SLOT_PROP, SHELL_BAR_SLOT_COMPONENTS) == NULL) {
        if (ShellBarCreateSlotWindow(ShellBarWindow, SHELL_BAR_SLOT_COMPONENTS_WINDOW_ID) == FALSE) return FALSE;
    }

    ShellBarLayoutSlots(ShellBarWindow);
    return TRUE;
}

/************************************************************************/

BOOL ShellBarCreate(HANDLE ParentWindow) {
    WINDOWINFO WindowInfo;
    HANDLE Window;

    if (ParentWindow == NULL) return FALSE;
    if (ShellBarEnsureClassRegistered() == FALSE) return FALSE;

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = ParentWindow;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = SHELL_BAR_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_VISIBLE | EWS_CLIENT_DECORATED;
    WindowInfo.ID = SHELL_BAR_WINDOW_ID;
    WindowInfo.WindowPosition.X = 0;
    WindowInfo.WindowPosition.Y = 0;
    WindowInfo.WindowSize.X = 1;
    WindowInfo.WindowSize.Y = 1;
    WindowInfo.ShowHide = TRUE;

    Window = (HANDLE)CreateWindow(&WindowInfo);
    return Window != NULL;
}

/************************************************************************/

HANDLE ShellBarGetWindow(HANDLE ParentWindow) {
    if (ParentWindow == NULL) return NULL;
    return ShellBarFindDirectChildByProp(ParentWindow, SHELL_BAR_ROLE_PROP, SHELL_BAR_ROLE_MAIN);
}

/************************************************************************/

HANDLE ShellBarGetSlotWindow(HANDLE ShellBarWindow, U32 SlotID) {
    if (ShellBarWindow == NULL) return NULL;

    switch (SlotID) {
        case SHELL_BAR_SLOT_COMPONENTS:
            break;
        default:
            return NULL;
    }

    return ShellBarFindDirectChildByProp(ShellBarWindow, SHELL_BAR_SLOT_PROP, SlotID);
}

/************************************************************************/

U32 ShellBarWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    switch (Message) {
        case EWM_CREATE:
            (void)SetWindowProp(Window, SHELL_BAR_ROLE_PROP, SHELL_BAR_ROLE_MAIN);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_ENABLED, 1);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_EDGE, DOCK_EDGE_TOP);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_PRIORITY, 0);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_ORDER, 0);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_POLICY, DOCK_LAYOUT_POLICY_FIXED);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_PREFERRED, SHELL_BAR_HEIGHT);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_MINIMUM, SHELL_BAR_HEIGHT);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_MAXIMUM, SHELL_BAR_HEIGHT);
            (void)SetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_WEIGHT, 1);
            (void)ShellBarEnsureSlotWindows(Window);
            return BaseWindowFunc(Window, Message, Param1, Param2);

        case EWM_NOTIFY:
            if (Param1 == EWN_WINDOW_RECT_CHANGED) {
                ShellBarLayoutSlots(Window);
            }
            return BaseWindowFunc(Window, Message, Param1, Param2);

        case EWM_CHILD_APPENDED:
        case EWM_CHILD_REMOVED:
            ShellBarHandleChildAppended(Window);
            if (Message == EWM_CHILD_APPENDED && Param1 == SHELL_BAR_SLOT_COMPONENTS_WINDOW_ID) {
                (void)ShellBarEnsureClockWidget(Window);
            }
            return 1;

        case EWM_DRAW:
            (void)BaseWindowFunc(Window, EWM_CLEAR, Param1, Param2);
            return 1;
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/************************************************************************/
