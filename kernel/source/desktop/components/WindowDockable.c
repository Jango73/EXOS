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


    Window dockable class helpers

\************************************************************************/

#include "desktop/components/WindowDockable.h"
#include "CoreString.h"

/************************************************************************/

/**
 * @brief Resolve one window class by name and ensure it is valid.
 * @param ClassName Window class name.
 * @return Class pointer or NULL.
 */
static LPWINDOW_CLASS WindowDockableResolveClassByName(LPCSTR ClassName) {
    LPWINDOW_CLASS WindowClass;

    if (ClassName == NULL) return NULL;

    WindowClass = WindowClassFindByName(ClassName);
    if (WindowClass == NULL || WindowClass->TypeID != KOID_WINDOW_CLASS) return NULL;

    return WindowClass;
}

/************************************************************************/

/**
 * @brief Resolve whether one class inherits from the dockable base class.
 * @param WindowClass Window class to inspect.
 * @return TRUE when class inherits from dockable class.
 */
static BOOL WindowDockableClassIsDerived(LPWINDOW_CLASS WindowClass) {
    LPWINDOW_CLASS DockableClass;
    LPWINDOW_CLASS Current;

    DockableClass = WindowDockableResolveClassByName(WINDOW_DOCKABLE_CLASS_NAME);
    if (DockableClass == NULL) return FALSE;

    for (Current = WindowClass; Current != NULL; Current = Current->BaseClass) {
        if (Current == DockableClass) return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Resolve one size request from window properties.
 * @param Window Source window.
 * @param Request Receives parsed request.
 */
static void WindowDockableReadSizeRequestFromProperties(HANDLE Window, LPDOCK_SIZE_REQUEST Request) {
    if (Request == NULL) return;

    Request->Policy = GetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_POLICY);
    Request->PreferredPrimarySize = (I32)GetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_PREFERRED);
    Request->MinimumPrimarySize = (I32)GetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_MINIMUM);
    Request->MaximumPrimarySize = (I32)GetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_MAXIMUM);
    Request->Weight = GetWindowProp(Window, WINDOW_DOCK_PROP_SIZE_WEIGHT);

    if (Request->Policy != DOCK_LAYOUT_POLICY_AUTO &&
        Request->Policy != DOCK_LAYOUT_POLICY_FIXED &&
        Request->Policy != DOCK_LAYOUT_POLICY_WEIGHTED) {
        Request->Policy = DOCK_LAYOUT_POLICY_AUTO;
    }

    if (Request->Weight == 0) Request->Weight = 1;
}

/************************************************************************/

/**
 * @brief Resolve one fixed-size request from one current dockable state.
 * @param Dockable Source dockable.
 * @param Host Dock host.
 * @param HostRect Current host rectangle.
 * @param Request Output size request.
 * @return Dock layout status.
 */
static U32 WindowDockableMeasure(LPDOCKABLE Dockable, LPDOCK_HOST Host, LPRECT HostRect, LPDOCK_SIZE_REQUEST Request) {
    UNUSED(Host);
    UNUSED(HostRect);

    if (Dockable == NULL || Request == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    *Request = Dockable->SizeRequest;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

/**
 * @brief Apply one assigned rectangle to one docked window geometry.
 * @param Dockable Source dockable.
 * @param Host Dock host.
 * @param AssignedRect Assigned window rectangle.
 * @param WorkRect Updated host work rectangle.
 * @return Dock layout status.
 */
static U32 WindowDockableApplyRect(LPDOCKABLE Dockable, LPDOCK_HOST Host, LPRECT AssignedRect, LPRECT WorkRect) {
    LPWINDOW Window;
    RECT Rect;

    UNUSED(Host);
    UNUSED(WorkRect);

    if (Dockable == NULL || AssignedRect == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Window = (LPWINDOW)Dockable->Context;
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Rect = *AssignedRect;
    (void)SetWindowProp((HANDLE)Window, WINDOW_PROP_BYPASS_PARENT_WORK_RECT, 1);

    if (MoveWindow((HANDLE)Window, &Rect) == FALSE) {
        (void)SetWindowProp((HANDLE)Window, WINDOW_PROP_BYPASS_PARENT_WORK_RECT, 0);
        return DOCK_LAYOUT_STATUS_LAYOUT_REJECTED;
    }

    (void)SetWindowProp((HANDLE)Window, WINDOW_PROP_BYPASS_PARENT_WORK_RECT, 0);
    (void)InvalidateWindowRect((HANDLE)Window, NULL);
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

/**
 * @brief Apply docking properties to one dockable and synchronize host attachment.
 * @param Window Target window.
 */
static void WindowDockableApplyProperties(LPWINDOW Window) {
    LPWINDOW_DOCKABLE_CLASS_DATA Data;
    LPWINDOW HostWindow;
    DOCK_SIZE_REQUEST Request;
    U32 Enabled;
    U32 Edge;
    U32 Status;

    Data = WindowDockableClassGetData(Window);
    if (Data == NULL || Data->DockableInitialized == FALSE) return;

    HostWindow = (LPWINDOW)GetWindowParent((HANDLE)Window);
    if (HostWindow == NULL || HostWindow->TypeID != KOID_WINDOW) return;

    Enabled = GetWindowProp((HANDLE)Window, WINDOW_DOCK_PROP_ENABLED);
    Edge = GetWindowProp((HANDLE)Window, WINDOW_DOCK_PROP_EDGE);

    if (Enabled == 0 || (Edge != DOCK_EDGE_TOP && Edge != DOCK_EDGE_BOTTOM && Edge != DOCK_EDGE_LEFT && Edge != DOCK_EDGE_RIGHT)) {
        if (Data->DockableAttached != FALSE) {
            (void)WindowDockHostDetachDockable((HANDLE)HostWindow, &(Data->Dockable));
            Data->DockableAttached = FALSE;
            (void)WindowDockHostRelayout((HANDLE)HostWindow);
        }
        return;
    }

    (void)DockableSetEdge(&(Data->Dockable), Edge);
    (void)DockableSetOrder(
        &(Data->Dockable),
        (I32)GetWindowProp((HANDLE)Window, WINDOW_DOCK_PROP_PRIORITY),
        (I32)GetWindowProp((HANDLE)Window, WINDOW_DOCK_PROP_ORDER));

    WindowDockableReadSizeRequestFromProperties((HANDLE)Window, &Request);
    (void)DockableSetSizeRequest(&(Data->Dockable), &Request);

    (void)WindowDockHostHandleWindowRectChanged((HANDLE)HostWindow);

    if (Data->DockableAttached == FALSE) {
        Status = WindowDockHostAttachDockable((HANDLE)HostWindow, &(Data->Dockable));
        if (Status == DOCK_LAYOUT_STATUS_SUCCESS || Status == DOCK_LAYOUT_STATUS_ALREADY_ATTACHED) {
            Data->DockableAttached = TRUE;
        } else {
            Data->DockableAttached = FALSE;
            return;
        }
    } else {
        (void)WindowDockHostMarkDirty((HANDLE)HostWindow, DOCK_DIRTY_REASON_DOCKABLE_PROPERTY_CHANGED);
    }

    (void)WindowDockHostRelayout((HANDLE)HostWindow);
}

/************************************************************************/

BOOL WindowDockableClassEnsureRegistered(void) {
    LPWINDOW_CLASS DockableClass;

    if (WindowClassInitializeRegistry() == FALSE) return FALSE;

    DockableClass = WindowClassFindByName(WINDOW_DOCKABLE_CLASS_NAME);
    if (DockableClass != NULL) return TRUE;

    DockableClass = WindowClassRegisterKernelClass(
        WINDOW_DOCKABLE_CLASS_NAME,
        WindowClassGetDefault(),
        WindowDockableWindowFunc,
        sizeof(WINDOW_DOCKABLE_CLASS_DATA));

    return DockableClass != NULL;
}

/************************************************************************/

BOOL WindowDockableClassEnsureDerivedRegistered(LPCSTR ClassName, WINDOWFUNC WindowFunction) {
    LPWINDOW_CLASS DockableClass;
    LPWINDOW_CLASS WindowClass;

    if (ClassName == NULL || WindowFunction == NULL) return FALSE;
    if (WindowDockableClassEnsureRegistered() == FALSE) return FALSE;

    WindowClass = WindowClassFindByName(ClassName);
    if (WindowClass != NULL) return TRUE;

    DockableClass = WindowDockableResolveClassByName(WINDOW_DOCKABLE_CLASS_NAME);
    if (DockableClass == NULL) return FALSE;

    WindowClass = WindowClassRegisterKernelClass(
        ClassName,
        DockableClass,
        WindowFunction,
        sizeof(WINDOW_DOCKABLE_CLASS_DATA));

    return WindowClass != NULL;
}

/************************************************************************/

BOOL WindowDockableWindowInheritsDockableClass(LPWINDOW Window) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Window->Class == NULL || Window->Class->TypeID != KOID_WINDOW_CLASS) return FALSE;

    return WindowDockableClassIsDerived(Window->Class);
}

/************************************************************************/

LPWINDOW_DOCKABLE_CLASS_DATA WindowDockableClassGetData(LPWINDOW Window) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return NULL;
    if (WindowDockableWindowInheritsDockableClass(Window) == FALSE) return NULL;
    if (Window->ClassData == NULL) return NULL;
    if (Window->Class == NULL || Window->Class->ClassDataSize < sizeof(WINDOW_DOCKABLE_CLASS_DATA)) return NULL;

    return (LPWINDOW_DOCKABLE_CLASS_DATA)Window->ClassData;
}

/************************************************************************/

void WindowDockableHandlePropertyChanged(HANDLE Window) {
    LPWINDOW This;
    LPWINDOW_DOCKABLE_CLASS_DATA Data;

    This = (LPWINDOW)Window;
    Data = WindowDockableClassGetData(This);
    if (Data == NULL || Data->DockableInitialized == FALSE) return;

    WindowDockableApplyProperties(This);
}

/************************************************************************/

U32 WindowDockableWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    LPWINDOW This = (LPWINDOW)Window;
    LPWINDOW HostWindow;
    LPWINDOW_DOCKABLE_CLASS_DATA Data;
    DOCKABLE_CALLBACKS Callbacks;
    UINT IdentifierValue;

    switch (Message) {
        case EWM_CREATE:
            Data = WindowDockableClassGetData(This);
            if (Data == NULL) return 0;

            if (Data->DockableInitialized == FALSE) {
                IdentifierValue = (UINT)(LINEAR)This;
                StringCopy(Data->Identifier, TEXT("dock-"));
                U32ToHexString((U32)IdentifierValue, Data->Identifier + 5);

                if (DockableInit(&(Data->Dockable), Data->Identifier, This) == FALSE) return 0;

                Callbacks.Measure = WindowDockableMeasure;
                Callbacks.ApplyRect = WindowDockableApplyRect;
                Callbacks.OnDockChanged = NULL;
                Callbacks.OnHostWorkRectChanged = NULL;
                (void)DockableSetCallbacks(&(Data->Dockable), &Callbacks);

                Data->DockableInitialized = TRUE;
            }

            WindowDockableApplyProperties(This);
            return BaseWindowFunc(Window, Message, Param1, Param2);

        case EWM_NOTIFY:
            if (Param1 == EWN_WINDOW_PROPERTY_CHANGED) {
                WindowDockableApplyProperties(This);
                return 1;
            }
            return BaseWindowFunc(Window, Message, Param1, Param2);

        case EWM_DELETE:
            Data = WindowDockableClassGetData(This);
            if (Data != NULL && Data->DockableAttached != FALSE) {
                HostWindow = (LPWINDOW)GetWindowParent(Window);
                if (HostWindow != NULL && HostWindow->TypeID == KOID_WINDOW) {
                    (void)WindowDockHostDetachDockable((HANDLE)HostWindow, &(Data->Dockable));
                    (void)WindowDockHostRelayout((HANDLE)HostWindow);
                }
                Data->DockableAttached = FALSE;
            }
            return BaseWindowFunc(Window, Message, Param1, Param2);
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/************************************************************************/
