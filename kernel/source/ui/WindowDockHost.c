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

#include "ui/WindowDockHost.h"
#include "text/CoreString.h"
#include "Heap.h"

/************************************************************************/

#define WINDOW_DOCK_HOST_PROP_STATE TEXT("windowdockhost.state")

/************************************************************************/

static LPWINDOW_DOCK_HOST_CLASS_DATA WindowDockHostAllocateData(HANDLE Window) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    Data = (LPWINDOW_DOCK_HOST_CLASS_DATA)HeapAlloc(sizeof(WINDOW_DOCK_HOST_CLASS_DATA));
    if (Data == NULL) return NULL;

    MemorySet(Data, 0, sizeof(WINDOW_DOCK_HOST_CLASS_DATA));
    (void)SetWindowProp(Window, WINDOW_DOCK_HOST_PROP_STATE, (UINT)(LINEAR)Data);
    return Data;
}

/************************************************************************/

static LPWINDOW_DOCK_HOST_CLASS_DATA WindowDockHostEnsureData(HANDLE Window) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    Data = WindowDockHostClassGetData(Window);
    if (Data != NULL) return Data;

    if (WindowDockHostWindowInheritsDockHostClass(Window) == FALSE) return NULL;
    return WindowDockHostAllocateData(Window);
}

/************************************************************************/

static LPWINDOW_DOCK_HOST_CLASS_DATA WindowDockHostEnsureState(HANDLE Window) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;
    RECT HostRect;

    Data = WindowDockHostEnsureData(Window);
    if (Data == NULL) return NULL;
    if (Data->DockHostInitialized != FALSE) return Data;

    if (DockHostInit(&(Data->DockHost), TEXT("WindowDockHost"), (LPVOID)Window) == FALSE) return NULL;
    if (GetWindowRect(Window, &HostRect) != FALSE) {
        (void)DockHostSetHostRect(&(Data->DockHost), &HostRect);
    }

    Data->DockHostInitialized = TRUE;
    return Data;
}

/************************************************************************/

BOOL WindowDockHostClassEnsureRegistered(void) {
    if (FindWindowClass(WINDOW_DOCK_HOST_CLASS_NAME) != NULL) return TRUE;
    return RegisterWindowClass(WINDOW_DOCK_HOST_CLASS_NAME, 0, NULL, WindowDockHostWindowFunc, 0) != NULL;
}

/************************************************************************/

BOOL WindowDockHostClassEnsureDerivedRegistered(LPCSTR ClassName, WINDOWFUNC WindowFunction) {
    HANDLE DockHostClass;

    if (ClassName == NULL || WindowFunction == NULL) return FALSE;
    if (WindowDockHostClassEnsureRegistered() == FALSE) return FALSE;
    if (FindWindowClass(ClassName) != NULL) return TRUE;

    DockHostClass = FindWindowClass(WINDOW_DOCK_HOST_CLASS_NAME);
    if (DockHostClass == NULL) return FALSE;

    return RegisterWindowClass(ClassName, DockHostClass, NULL, WindowFunction, 0) != NULL;
}

/************************************************************************/

BOOL WindowDockHostWindowInheritsDockHostClass(HANDLE Window) {
    return WindowInheritsClass(Window, 0, WINDOW_DOCK_HOST_CLASS_NAME);
}

/************************************************************************/

LPWINDOW_DOCK_HOST_CLASS_DATA WindowDockHostClassGetData(HANDLE Window) {
    if (WindowDockHostWindowInheritsDockHostClass(Window) == FALSE) return NULL;
    return (LPWINDOW_DOCK_HOST_CLASS_DATA)(LPVOID)(LINEAR)GetWindowProp(Window, WINDOW_DOCK_HOST_PROP_STATE);
}

/************************************************************************/

void WindowDockHostShutdownWindow(HANDLE Window) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    Data = WindowDockHostClassGetData(Window);
    if (Data == NULL) return;

    if (Data->DockHostInitialized != FALSE) {
        (void)DockHostReset(&(Data->DockHost));
        Data->DockHostInitialized = FALSE;
    }

    (void)SetWindowProp(Window, WINDOW_DOCK_HOST_PROP_STATE, 0);
    HeapFree(Data);
}

/************************************************************************/

U32 WindowDockHostAttachDockable(HANDLE Window, LPDOCKABLE Dockable) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    if (Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Data = WindowDockHostEnsureState(Window);
    if (Data == NULL) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostAttachDockable(&(Data->DockHost), Dockable);
}

/************************************************************************/

U32 WindowDockHostDetachDockable(HANDLE Window, LPDOCKABLE Dockable) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    if (Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Data = WindowDockHostClassGetData(Window);
    if (Data == NULL || Data->DockHostInitialized == FALSE) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostDetachDockable(&(Data->DockHost), Dockable);
}

/************************************************************************/

U32 WindowDockHostMarkDirty(HANDLE Window, U32 Reason) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;

    Data = WindowDockHostClassGetData(Window);
    if (Data == NULL || Data->DockHostInitialized == FALSE) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostMarkDirty(&(Data->DockHost), Reason);
}

/************************************************************************/

U32 WindowDockHostHandleWindowRectChanged(HANDLE Window) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;
    RECT HostRect;
    U32 Status;

    Data = WindowDockHostEnsureState(Window);
    if (Data == NULL) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    if (GetWindowRect(Window, &HostRect) == FALSE) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Status = DockHostSetHostRect(&(Data->DockHost), &HostRect);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS) return Status;

    return WindowDockHostRelayout(Window);
}

/************************************************************************/

U32 WindowDockHostRelayout(HANDLE Window) {
    LPWINDOW_DOCK_HOST_CLASS_DATA Data;
    DOCK_LAYOUT_FRAME Frame;
    DOCK_LAYOUT_RESULT Result;
    HANDLE DockableWindow;
    U32 DockableStyle;
    BOOL ReservedPlacementStates[DOCK_HOST_MAX_ITEMS];
    UINT Index;
    U32 Status;

    Data = WindowDockHostClassGetData(Window);
    if (Data == NULL || Data->DockHostInitialized == FALSE) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    Status = DockHostBuildLayoutFrame(&(Data->DockHost), &Frame);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS && Frame.Status == DOCK_LAYOUT_STATUS_SUCCESS) {
        Frame.Status = Status;
    }

    for (Index = 0; Index < Data->DockHost.ItemCount && Index < DOCK_HOST_MAX_ITEMS; Index++) {
        ReservedPlacementStates[Index] = FALSE;

        if (Data->DockHost.Items[Index] == NULL) continue;
        DockableWindow = (HANDLE)Data->DockHost.Items[Index]->Context;
        if (DockableWindow == NULL) continue;
        if (GetWindowStyle(DockableWindow, &DockableStyle) == FALSE) continue;

        ReservedPlacementStates[Index] = ((DockableStyle & EWS_EXCLUDE_SIBLING_PLACEMENT) != 0);
        if (ReservedPlacementStates[Index] != FALSE) {
            (void)ClearWindowStyle(DockableWindow, EWS_EXCLUDE_SIBLING_PLACEMENT);
        }
    }

    Status = DockHostApplyLayoutFrame(&(Data->DockHost), &Frame, &Result);

    for (Index = 0; Index < Data->DockHost.ItemCount && Index < DOCK_HOST_MAX_ITEMS; Index++) {
        if (ReservedPlacementStates[Index] == FALSE) continue;
        if (Data->DockHost.Items[Index] == NULL) continue;

        DockableWindow = (HANDLE)Data->DockHost.Items[Index]->Context;
        if (DockableWindow == NULL) continue;
        (void)SetWindowStyle(DockableWindow, EWS_EXCLUDE_SIBLING_PLACEMENT);
    }

    return Status;
}

/************************************************************************/

U32 WindowDockHostWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    switch (Message) {
        case EWM_CREATE:
            if (WindowDockHostEnsureData(Window) == NULL) return 0;
            (void)WindowDockHostHandleWindowRectChanged(Window);
            return BaseWindowFunc(Window, Message, Param1, Param2);

        case EWM_NOTIFY:
            if (Param1 == EWN_WINDOW_RECT_CHANGED) {
                (void)WindowDockHostHandleWindowRectChanged(Window);
                return 1;
            }
            return BaseWindowFunc(Window, Message, Param1, Param2);

        case EWM_DELETE:
            WindowDockHostShutdownWindow(Window);
            return BaseWindowFunc(Window, Message, Param1, Param2);
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/************************************************************************/
