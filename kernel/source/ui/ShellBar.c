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

#include "ui/Button.h"
#include "ui/WindowDockable.h"

/************************************************************************/

#define SHELL_BAR_HEIGHT 56
#define SHELL_BAR_WINDOW_ID 0x53484252
#define SHELL_BAR_SLOT_WINDOW_CLASS_NAME TEXT("ShellBarSlotWindowClass")
#define SHELL_BAR_SLOT_LEFT_WIDTH 240
#define SHELL_BAR_SLOT_COMPONENTS_WIDTH 256
#define SHELL_BAR_ROLE_PROP TEXT("shellbar.role")
#define SHELL_BAR_SLOT_PROP TEXT("shellbar.slot")
#define SHELL_BAR_COMPONENTS_PADDING 4
#define SHELL_BAR_COMPONENTS_GAP 4
#define SHELL_BAR_ROLE_MAIN 1

/************************************************************************/

BOOL ShellBarEnsureClassRegistered(void) {
    return WindowDockableClassEnsureDerivedRegistered(SHELL_BAR_WINDOW_CLASS_NAME, ShellBarWindowFunc);
}

/************************************************************************/

/**
 * @brief Resolve one direct child by property value.
 * @param Parent Parent window.
 * @param Name Property name.
 * @param Value Property value.
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

static void ShellBarLayoutComponents(HANDLE ComponentsSlotWindow) {
    RECT ClientRect;
    RECT ChildRect;
    HANDLE CurrentWindow;
    HANDLE SelectedWindow;
    U32 ChildCount;
    U32 ChildIndex;
    U32 SelectionPass;
    U32 BestOrder;
    U32 CurrentOrder;
    U32 ComponentWidth;
    U32 SelectedWidth;
    U32 PreviousOrder;
    BOOL FoundSelection;
    I32 RightEdge;

    if (ComponentsSlotWindow == NULL) return;
    if (GetWindowClientRect(ComponentsSlotWindow, &ClientRect) == FALSE) return;

    RightEdge = ClientRect.X2 - SHELL_BAR_COMPONENTS_PADDING;
    ChildCount = GetWindowChildCount(ComponentsSlotWindow);
    PreviousOrder = 0xFFFFFFFF;
    for (SelectionPass = 0; SelectionPass < ChildCount; SelectionPass++) {
        SelectedWindow = NULL;
        SelectedWidth = 0;
        BestOrder = 0;
        FoundSelection = FALSE;

        for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
            CurrentWindow = GetWindowChild(ComponentsSlotWindow, ChildIndex);
            if (CurrentWindow == NULL) continue;

            ComponentWidth = GetWindowProp(CurrentWindow, SHELL_BAR_COMPONENT_PROP_WIDTH);
            if (ComponentWidth == 0) continue;

            CurrentOrder = GetWindowProp(CurrentWindow, SHELL_BAR_COMPONENT_PROP_ORDER);
            if (CurrentOrder == 0 || CurrentOrder >= PreviousOrder) continue;

            if (FoundSelection == FALSE || CurrentOrder > BestOrder) {
                SelectedWindow = CurrentWindow;
                SelectedWidth = ComponentWidth;
                BestOrder = CurrentOrder;
                FoundSelection = TRUE;
            }
        }

        if (SelectedWindow == NULL || SelectedWidth == 0) continue;

        ChildRect = ClientRect;
        ChildRect.X2 = RightEdge;
        ChildRect.X1 = ChildRect.X2 - (I32)SelectedWidth + 1;
        if (ChildRect.X1 < ClientRect.X1) ChildRect.X1 = ClientRect.X1;
        (void)MoveWindow(SelectedWindow, &ChildRect);
        RightEdge = ChildRect.X1 - SHELL_BAR_COMPONENTS_GAP;
        PreviousOrder = BestOrder;
    }
}

/************************************************************************/

static BOOL ShellBarToggleTargetWindow(HANDLE ShellBarWindow, U32 TargetWindowID) {
    HANDLE DesktopWindow;
    HANDLE TargetWindow;
    U32 Style;
    BOOL Result;

    if (ShellBarWindow == NULL) return FALSE;

    DesktopWindow = GetWindowParent(ShellBarWindow);
    if (DesktopWindow == NULL) return FALSE;

    TargetWindow = FindWindow(DesktopWindow, TargetWindowID);
    if (TargetWindow == NULL) return FALSE;
    if (GetWindowStyle(TargetWindow, &Style) == FALSE) return FALSE;

    if ((Style & EWS_VISIBLE) != 0) {
        Result = HideWindow(TargetWindow);
        if (Result != FALSE) {
            (void)InvalidateWindowRect(DesktopWindow, NULL);
        }
        return Result;
    }

    Result = ShowWindow(TargetWindow);
    if (Result != FALSE) {
        (void)InvalidateWindowRect(DesktopWindow, NULL);
    }
    return Result;
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
        ShellBarLayoutComponents(ComponentsSlotWindow);
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
    WINDOW_INFO WindowInfo;
    HANDLE Window;

    if (ShellBarWindow == NULL) return FALSE;

    WindowInfo.Header.Size = sizeof(WINDOW_INFO);
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
            ShellBarLayoutComponents(Window);
            return 1;

        case EWM_CHILD_APPENDED:
        case EWM_CHILD_REMOVED:
            ShellBarLayoutComponents(Window);
            return 1;

        case EWM_NOTIFY:
            if (Param1 == EWN_UI_BUTTON_CLICKED) {
                HANDLE ParentWindow = GetWindowParent(Window);

                if (ParentWindow != NULL) {
                    (void)PostMessage(ParentWindow, EWM_NOTIFY, Param1, Param2);
                }
                return 1;
            }

            if (Param1 == EWN_WINDOW_RECT_CHANGED) {
                ShellBarLayoutComponents(Window);
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
    WINDOW_INFO WindowInfo;
    HANDLE Window;

    if (ParentWindow == NULL) return FALSE;
    if (ShellBarEnsureClassRegistered() == FALSE) return FALSE;

    WindowInfo.Header.Size = sizeof(WINDOW_INFO);
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
    if (Window == NULL) return FALSE;

    (void)SetWindowProp(Window, SHELL_BAR_ROLE_PROP, SHELL_BAR_ROLE_MAIN);
    (void)ShellBarEnsureSlotWindows(Window);
    return TRUE;
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
                return BaseWindowFunc(Window, Message, Param1, Param2);
            }

            if (Param1 == EWN_UI_BUTTON_CLICKED) {
                if (Param2 != 0) {
                    (void)ShellBarToggleTargetWindow(Window, Param2);
                    return 1;
                }
            }
            return BaseWindowFunc(Window, Message, Param1, Param2);

        case EWM_CHILD_APPENDED:
        case EWM_CHILD_REMOVED:
            ShellBarHandleChildAppended(Window);
            return 1;

        case EWM_DRAW:
            (void)BaseWindowFunc(Window, EWM_CLEAR, Param1, Param2);
            return 1;
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/************************************************************************/
