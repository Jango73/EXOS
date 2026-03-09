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

#include "desktop/Desktop-WindowClass.h"
#include "ui/layout/WindowDockHost.h"

/************************************************************************/

#define DESKTOP_SHELL_BAR_DOCKABLE_ID TEXT("DesktopShellBar")
#define DESKTOP_SHELL_BAR_HEIGHT 32

/************************************************************************/

typedef struct tag_DESKTOP_SHELL_BAR_CLASS_DATA {
    DOCKABLE Dockable;
    BOOL DockableInitialized;
    BOOL DockableAttached;
} DESKTOP_SHELL_BAR_CLASS_DATA, *LPDESKTOP_SHELL_BAR_CLASS_DATA;

/************************************************************************/

/**
 * @brief Resolve class data from one shell bar window.
 * @param Window Source window.
 * @return Class data pointer or NULL.
 */
static LPDESKTOP_SHELL_BAR_CLASS_DATA DesktopShellBarGetData(LPWINDOW Window) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return NULL;
    if (Window->Class == NULL) return NULL;
    if (WindowClassFindByName(DESKTOP_SHELL_BAR_WINDOW_CLASS_NAME) != Window->Class) return NULL;
    return (LPDESKTOP_SHELL_BAR_CLASS_DATA)Window->ClassData;
}

/************************************************************************/

/**
 * @brief Resolve fixed shell bar size request.
 * @param Dockable Source dockable.
 * @param Host Dock host.
 * @param HostRect Current host rectangle.
 * @param Request Output size request.
 * @return Dock layout status.
 */
static U32 DesktopShellBarMeasure(LPDOCKABLE Dockable, LPDOCK_HOST Host, LPRECT HostRect, LPDOCK_SIZE_REQUEST Request) {
    UNUSED(Host);
    UNUSED(HostRect);

    if (Dockable == NULL || Request == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    *Request = Dockable->SizeRequest;
    Request->Policy = DOCK_LAYOUT_POLICY_FIXED;
    Request->PreferredPrimarySize = DESKTOP_SHELL_BAR_HEIGHT;
    Request->MinimumPrimarySize = DESKTOP_SHELL_BAR_HEIGHT;
    Request->MaximumPrimarySize = DESKTOP_SHELL_BAR_HEIGHT;
    Request->Weight = 1;
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

/**
 * @brief Apply one assigned rectangle to shell bar window geometry.
 * @param Dockable Source dockable.
 * @param Host Dock host.
 * @param AssignedRect Assigned shell bar rectangle.
 * @param WorkRect Updated host work rectangle.
 * @return Dock layout status.
 */
static U32 DesktopShellBarApplyRect(LPDOCKABLE Dockable, LPDOCK_HOST Host, LPRECT AssignedRect, LPRECT WorkRect) {
    LPWINDOW Window;
    RECT Rect;

    UNUSED(Host);
    UNUSED(WorkRect);

    if (Dockable == NULL || AssignedRect == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Window = (LPWINDOW)Dockable->Context;
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Rect = *AssignedRect;
    if (MoveWindow((HANDLE)Window, &Rect) == FALSE) return DOCK_LAYOUT_STATUS_LAYOUT_REJECTED;

    (void)InvalidateWindowRect((HANDLE)Window, NULL);
    return DOCK_LAYOUT_STATUS_SUCCESS;
}

/************************************************************************/

BOOL DesktopShellBarEnsureClassRegistered(void) {
    LPWINDOW_CLASS WindowClass;

    if (WindowClassInitializeRegistry() == FALSE) return FALSE;

    WindowClass = WindowClassFindByName(DESKTOP_SHELL_BAR_WINDOW_CLASS_NAME);
    if (WindowClass != NULL) return TRUE;

    WindowClass = WindowClassRegisterKernelClass(
        DESKTOP_SHELL_BAR_WINDOW_CLASS_NAME,
        WindowClassGetDefault(),
        DesktopShellBarWindowFunc,
        sizeof(DESKTOP_SHELL_BAR_CLASS_DATA));

    return WindowClass != NULL;
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
    LPWINDOW This = (LPWINDOW)Window;
    LPWINDOW HostWindow;
    LPDESKTOP_SHELL_BAR_CLASS_DATA Data;
    DOCKABLE_CALLBACKS Callbacks;
    DOCK_SIZE_REQUEST Request;

    switch (Message) {
        case EWM_CREATE:
            Data = DesktopShellBarGetData(This);
            if (Data == NULL) return 0;

            if (Data->DockableInitialized == FALSE) {
                if (DockableInit(&(Data->Dockable), DESKTOP_SHELL_BAR_DOCKABLE_ID, This) == FALSE) return 0;

                Request.Policy = DOCK_LAYOUT_POLICY_FIXED;
                Request.PreferredPrimarySize = DESKTOP_SHELL_BAR_HEIGHT;
                Request.MinimumPrimarySize = DESKTOP_SHELL_BAR_HEIGHT;
                Request.MaximumPrimarySize = DESKTOP_SHELL_BAR_HEIGHT;
                Request.Weight = 1;
                (void)DockableSetSizeRequest(&(Data->Dockable), &Request);
                (void)DockableSetEdge(&(Data->Dockable), DOCK_EDGE_BOTTOM);
                (void)DockableSetOrder(&(Data->Dockable), 0, 0);

                Callbacks.Measure = NULL;
                Callbacks.ApplyRect = NULL;
                Callbacks.OnDockChanged = NULL;
                Callbacks.OnHostWorkRectChanged = NULL;
                Callbacks.Measure = DesktopShellBarMeasure;
                Callbacks.ApplyRect = DesktopShellBarApplyRect;
                (void)DockableSetCallbacks(&(Data->Dockable), &Callbacks);

                Data->DockableInitialized = TRUE;
            }

            HostWindow = This->ParentWindow;
            if (HostWindow == NULL || HostWindow->TypeID != KOID_WINDOW) return 0;

            if (WindowDockHostAttachDockable((HANDLE)HostWindow, &(Data->Dockable)) != DOCK_LAYOUT_STATUS_SUCCESS) return 0;

            Data->DockableAttached = TRUE;
            (void)WindowDockHostRelayout((HANDLE)HostWindow);
            return 1;

        case EWM_DELETE:
            Data = DesktopShellBarGetData(This);
            if (Data == NULL) return 1;

            if (Data->DockableAttached != FALSE) {
                HostWindow = This->ParentWindow;
                if (HostWindow != NULL && HostWindow->TypeID == KOID_WINDOW) {
                    (void)WindowDockHostDetachDockable((HANDLE)HostWindow, &(Data->Dockable));
                    (void)WindowDockHostRelayout((HANDLE)HostWindow);
                }
                Data->DockableAttached = FALSE;
            }
            return 1;

    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/************************************************************************/
