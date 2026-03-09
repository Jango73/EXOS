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


    Window docking host class helpers

\************************************************************************/

#include "ui/layout/WindowDockHost.h"

/************************************************************************/

/**
 * @brief Resolve one window class by name and ensure it is valid.
 * @param ClassName Window class name.
 * @return Class pointer or NULL.
 */
static LPWINDOW_CLASS WindowDockHostResolveClassByName(LPCSTR ClassName) {
    LPWINDOW_CLASS WindowClass;

    if (ClassName == NULL) return NULL;

    WindowClass = WindowClassFindByName(ClassName);
    if (WindowClass == NULL || WindowClass->TypeID != KOID_WINDOW_CLASS) return NULL;

    return WindowClass;
}

/************************************************************************/

/**
 * @brief Resolve whether one class inherits from the dock host base class.
 * @param WindowClass Window class to inspect.
 * @return TRUE when class inherits from dock host class.
 */
static BOOL WindowDockHostClassIsDerived(LPWINDOW_CLASS WindowClass) {
    LPWINDOW_CLASS DockHostClass;
    LPWINDOW_CLASS Current;

    DockHostClass = WindowDockHostResolveClassByName(WINDOW_DOCK_HOST_CLASS_NAME);
    if (DockHostClass == NULL) return FALSE;

    for (Current = WindowClass; Current != NULL; Current = Current->BaseClass) {
        if (Current == DockHostClass) return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Ensure one dock host state exists for one window.
 * @param Window Target window.
 * @return Class data pointer or NULL.
 */
static LPWINDOW_DOCK_HOST_CLASS_DATA WindowDockHostEnsureState(LPWINDOW Window) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;
    RECT HostRect;

    Data = WindowDockHostClassGetData(Window);
    if (Data == NULL) return NULL;
    if (Data->DockHostInitialized != FALSE) return Data;

    if (DockHostInit(&(Data->DockHost), TEXT("WindowDockHost"), Window) == FALSE) return NULL;
    if (GetWindowRect((HANDLE)Window, &HostRect) != FALSE) {
        (void)DockHostSetHostRect(&(Data->DockHost), &HostRect);
    }

    Data->DockHostInitialized = TRUE;
    return Data;
}

/************************************************************************/

BOOL WindowDockHostClassEnsureRegistered(void) {
    LPWINDOW_CLASS DockHostClass;

    if (WindowClassInitializeRegistry() == FALSE) return FALSE;

    DockHostClass = WindowClassFindByName(WINDOW_DOCK_HOST_CLASS_NAME);
    if (DockHostClass != NULL) return TRUE;

    DockHostClass = WindowClassRegisterKernelClass(
        WINDOW_DOCK_HOST_CLASS_NAME,
        WindowClassGetDefault(),
        BaseWindowFunc,
        sizeof(WINDOW_DOCK_HOST_CLASS_DATA));

    return DockHostClass != NULL;
}

/************************************************************************/

BOOL WindowDockHostClassEnsureDerivedRegistered(LPCSTR ClassName, WINDOWFUNC WindowFunction) {
    LPWINDOW_CLASS DockHostClass;
    LPWINDOW_CLASS WindowClass;

    if (ClassName == NULL || WindowFunction == NULL) return FALSE;
    if (WindowDockHostClassEnsureRegistered() == FALSE) return FALSE;

    WindowClass = WindowClassFindByName(ClassName);
    if (WindowClass != NULL) return TRUE;

    DockHostClass = WindowDockHostResolveClassByName(WINDOW_DOCK_HOST_CLASS_NAME);
    if (DockHostClass == NULL) return FALSE;

    WindowClass = WindowClassRegisterKernelClass(
        ClassName,
        DockHostClass,
        WindowFunction,
        sizeof(WINDOW_DOCK_HOST_CLASS_DATA));

    return WindowClass != NULL;
}

/************************************************************************/

BOOL WindowDockHostWindowInheritsDockHostClass(LPWINDOW Window) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Window->Class == NULL || Window->Class->TypeID != KOID_WINDOW_CLASS) return FALSE;

    return WindowDockHostClassIsDerived(Window->Class);
}

/************************************************************************/

LPWINDOW_DOCK_HOST_CLASS_DATA WindowDockHostClassGetData(LPWINDOW Window) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return NULL;
    if (WindowDockHostWindowInheritsDockHostClass(Window) == FALSE) return NULL;
    if (Window->ClassData == NULL) return NULL;
    if (Window->Class == NULL || Window->Class->ClassDataSize < sizeof(WINDOW_DOCK_HOST_CLASS_DATA)) return NULL;

    return (LPWINDOW_DOCK_HOST_CLASS_DATA)Window->ClassData;
}

/************************************************************************/

void WindowDockHostShutdownWindow(HANDLE Window) {
    LPWINDOW This;
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    This = (LPWINDOW)Window;
    Data = WindowDockHostClassGetData(This);
    if (Data == NULL || Data->DockHostInitialized == FALSE) return;

    (void)DockHostReset(&(Data->DockHost));
    Data->DockHostInitialized = FALSE;
}

/************************************************************************/

U32 WindowDockHostAttachDockable(HANDLE Window, LPDOCKABLE Dockable) {
    LPWINDOW This;
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    if (Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    This = (LPWINDOW)Window;
    if (This == NULL || This->TypeID != KOID_WINDOW) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Data = WindowDockHostEnsureState(This);
    if (Data == NULL) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostAttachDockable(&(Data->DockHost), Dockable);
}

/************************************************************************/

U32 WindowDockHostDetachDockable(HANDLE Window, LPDOCKABLE Dockable) {
    LPWINDOW This;
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    if (Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    This = (LPWINDOW)Window;
    Data = WindowDockHostClassGetData(This);
    if (Data == NULL || Data->DockHostInitialized == FALSE) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostDetachDockable(&(Data->DockHost), Dockable);
}

/************************************************************************/

U32 WindowDockHostMarkDirty(HANDLE Window, U32 Reason) {
    LPWINDOW This;
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    This = (LPWINDOW)Window;
    Data = WindowDockHostClassGetData(This);
    if (Data == NULL || Data->DockHostInitialized == FALSE) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostMarkDirty(&(Data->DockHost), Reason);
}

/************************************************************************/

U32 WindowDockHostHandleWindowRectChanged(HANDLE Window) {
    LPWINDOW This;
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;
    RECT HostRect;
    U32 Status;

    This = (LPWINDOW)Window;
    if (This == NULL || This->TypeID != KOID_WINDOW) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Data = WindowDockHostEnsureState(This);
    if (Data == NULL) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    if (GetWindowRect((HANDLE)This, &HostRect) == FALSE) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Status = DockHostSetHostRect(&(Data->DockHost), &HostRect);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS) return Status;

    return WindowDockHostRelayout((HANDLE)This);
}

/************************************************************************/

U32 WindowDockHostRelayout(HANDLE Window) {
    LPWINDOW This;
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;
    DOCK_LAYOUT_FRAME Frame;
    DOCK_LAYOUT_RESULT Result;
    U32 Status;

    This = (LPWINDOW)Window;
    Data = WindowDockHostClassGetData(This);
    if (Data == NULL || Data->DockHostInitialized == FALSE) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    Status = DockHostBuildLayoutFrame(&(Data->DockHost), &Frame);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS && Frame.Status == DOCK_LAYOUT_STATUS_SUCCESS) {
        Frame.Status = Status;
    }

    return DockHostApplyLayoutFrame(&(Data->DockHost), &Frame, &Result);
}

/************************************************************************/

U32 WindowDockHostGetWorkRect(HANDLE Window, LPRECT WorkRect) {
    LPWINDOW This;
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    if (WorkRect == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    This = (LPWINDOW)Window;
    Data = WindowDockHostClassGetData(This);
    if (Data == NULL || Data->DockHostInitialized == FALSE) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostGetWorkRect(&(Data->DockHost), WorkRect);
}

/************************************************************************/
