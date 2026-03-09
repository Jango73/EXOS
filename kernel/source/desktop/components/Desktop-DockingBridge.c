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


    Desktop docking bridge

\************************************************************************/

#include "desktop/components/Desktop-DockingBridge.h"
#include "desktop/components/Desktop-RootWindowClass.h"

#include "Kernel.h"

/************************************************************************/

/**
 * @brief Resolve the bridge state attached to one desktop.
 * @param Desktop Source desktop.
 * @return Bridge pointer or NULL.
 */
static LPDESKTOP_ROOT_WINDOW_CLASS_DATA DesktopDockingBridgeGetState(LPDESKTOP Desktop) {
    return DesktopRootWindowClassGetData(Desktop);
}

/************************************************************************/

/**
 * @brief Ensure one desktop bridge instance exists and is initialized.
 * @param Desktop Target desktop.
 * @return Bridge pointer or NULL.
 */
static LPDESKTOP_ROOT_WINDOW_CLASS_DATA DesktopDockingBridgeEnsureState(LPDESKTOP Desktop) {
    LPDESKTOP_ROOT_WINDOW_CLASS_DATA State;
    RECT DesktopRect;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return NULL;

    State = DesktopDockingBridgeGetState(Desktop);
    if (State == NULL) return NULL;
    if (State->DockHostInitialized != FALSE) return State;

    if (DockHostInit(&(State->DockHost), TEXT("DesktopDockHost"), Desktop) == FALSE) return NULL;

    if (GetDesktopScreenRect(Desktop, &DesktopRect) != FALSE) {
        (void)DockHostSetHostRect(&(State->DockHost), &DesktopRect);
    }

    State->DockHostInitialized = TRUE;
    return State;
}

/************************************************************************/

BOOL DesktopDockingBridgeInitialize(LPDESKTOP Desktop) {
    if (DesktopDockingBridgeEnsureState(Desktop) == NULL) return FALSE;
    return TRUE;
}

/************************************************************************/

void DesktopDockingBridgeShutdown(LPDESKTOP Desktop) {
    LPDESKTOP_ROOT_WINDOW_CLASS_DATA State;

    State = DesktopDockingBridgeGetState(Desktop);
    if (State == NULL || State->DockHostInitialized == FALSE) return;

    (void)DockHostReset(&(State->DockHost));
    State->DockHostInitialized = FALSE;
}

/************************************************************************/

U32 DesktopDockingBridgeAttachDockable(LPDESKTOP Desktop, LPDOCKABLE Dockable) {
    LPDESKTOP_ROOT_WINDOW_CLASS_DATA State;

    if (Desktop == NULL || Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    State = DesktopDockingBridgeEnsureState(Desktop);
    if (State == NULL) return DOCK_LAYOUT_STATUS_OUT_OF_MEMORY;

    return DockHostAttachDockable(&(State->DockHost), Dockable);
}

/************************************************************************/

U32 DesktopDockingBridgeDetachDockable(LPDESKTOP Desktop, LPDOCKABLE Dockable) {
    LPDESKTOP_ROOT_WINDOW_CLASS_DATA State;

    if (Desktop == NULL || Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    State = DesktopDockingBridgeGetState(Desktop);
    if (State == NULL || State->DockHostInitialized == FALSE) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostDetachDockable(&(State->DockHost), Dockable);
}

/************************************************************************/

U32 DesktopDockingBridgeMarkDirty(LPDESKTOP Desktop, U32 Reason) {
    LPDESKTOP_ROOT_WINDOW_CLASS_DATA State;

    if (Desktop == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    State = DesktopDockingBridgeGetState(Desktop);
    if (State == NULL || State->DockHostInitialized == FALSE) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostMarkDirty(&(State->DockHost), Reason);
}

/************************************************************************/

U32 DesktopDockingBridgeHandleDesktopRectChanged(LPDESKTOP Desktop, LPRECT DesktopRect) {
    LPDESKTOP_ROOT_WINDOW_CLASS_DATA State;
    U32 Status;

    if (Desktop == NULL || DesktopRect == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    State = DesktopDockingBridgeEnsureState(Desktop);
    if (State == NULL) return DOCK_LAYOUT_STATUS_OUT_OF_MEMORY;

    Status = DockHostSetHostRect(&(State->DockHost), DesktopRect);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS) return Status;

    return DesktopDockingBridgeRelayout(Desktop);
}

/************************************************************************/

U32 DesktopDockingBridgeRelayout(LPDESKTOP Desktop) {
    LPDESKTOP_ROOT_WINDOW_CLASS_DATA State;
    DOCK_LAYOUT_FRAME Frame;
    DOCK_LAYOUT_RESULT Result;
    U32 Status;

    if (Desktop == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    State = DesktopDockingBridgeGetState(Desktop);
    if (State == NULL || State->DockHostInitialized == FALSE) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    Status = DockHostBuildLayoutFrame(&(State->DockHost), &Frame);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS && Frame.Status == DOCK_LAYOUT_STATUS_SUCCESS) {
        Frame.Status = Status;
    }

    return DockHostApplyLayoutFrame(&(State->DockHost), &Frame, &Result);
}

/************************************************************************/

U32 DesktopDockingBridgeGetWorkRect(LPDESKTOP Desktop, LPRECT WorkRect) {
    LPDESKTOP_ROOT_WINDOW_CLASS_DATA State;

    if (Desktop == NULL || WorkRect == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    State = DesktopDockingBridgeGetState(Desktop);
    if (State == NULL || State->DockHostInitialized == FALSE) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostGetWorkRect(&(State->DockHost), WorkRect);
}

/************************************************************************/
