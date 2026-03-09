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

#include "Kernel.h"

/************************************************************************/

typedef struct tag_DESKTOP_DOCKING_BRIDGE {
    DOCK_HOST Host;
    BOOL Initialized;
} DESKTOP_DOCKING_BRIDGE, *LPDESKTOP_DOCKING_BRIDGE;

/************************************************************************/

/**
 * @brief Resolve the bridge state attached to one desktop.
 * @param Desktop Source desktop.
 * @return Bridge pointer or NULL.
 */
static LPDESKTOP_DOCKING_BRIDGE DesktopDockingBridgeGetState(LPDESKTOP Desktop) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return NULL;
    if (Desktop->DockingBridge == NULL) return NULL;
    return (LPDESKTOP_DOCKING_BRIDGE)Desktop->DockingBridge;
}

/************************************************************************/

/**
 * @brief Ensure one desktop bridge instance exists and is initialized.
 * @param Desktop Target desktop.
 * @return Bridge pointer or NULL.
 */
static LPDESKTOP_DOCKING_BRIDGE DesktopDockingBridgeEnsureState(LPDESKTOP Desktop) {
    LPDESKTOP_DOCKING_BRIDGE Bridge;
    RECT DesktopRect;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return NULL;

    Bridge = DesktopDockingBridgeGetState(Desktop);
    if (Bridge != NULL) return Bridge;

    Bridge = (LPDESKTOP_DOCKING_BRIDGE)KernelHeapAlloc(sizeof(DESKTOP_DOCKING_BRIDGE));
    if (Bridge == NULL) return NULL;

    MemorySet(Bridge, 0, sizeof(DESKTOP_DOCKING_BRIDGE));

    if (DockHostInit(&(Bridge->Host), TEXT("DesktopDockHost"), Desktop) == FALSE) {
        KernelHeapFree(Bridge);
        return NULL;
    }

    if (GetDesktopScreenRect(Desktop, &DesktopRect) != FALSE) {
        (void)DockHostSetHostRect(&(Bridge->Host), &DesktopRect);
    }

    Bridge->Initialized = TRUE;
    Desktop->DockingBridge = Bridge;
    return Bridge;
}

/************************************************************************/

BOOL DesktopDockingBridgeInitialize(LPDESKTOP Desktop) {
    if (DesktopDockingBridgeEnsureState(Desktop) == NULL) return FALSE;
    return TRUE;
}

/************************************************************************/

void DesktopDockingBridgeShutdown(LPDESKTOP Desktop) {
    LPDESKTOP_DOCKING_BRIDGE Bridge;

    Bridge = DesktopDockingBridgeGetState(Desktop);
    if (Bridge == NULL) return;

    Desktop->DockingBridge = NULL;
    KernelHeapFree(Bridge);
}

/************************************************************************/

U32 DesktopDockingBridgeAttachDockable(LPDESKTOP Desktop, LPDOCKABLE Dockable) {
    LPDESKTOP_DOCKING_BRIDGE Bridge;

    if (Desktop == NULL || Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Bridge = DesktopDockingBridgeEnsureState(Desktop);
    if (Bridge == NULL) return DOCK_LAYOUT_STATUS_OUT_OF_MEMORY;

    return DockHostAttachDockable(&(Bridge->Host), Dockable);
}

/************************************************************************/

U32 DesktopDockingBridgeDetachDockable(LPDESKTOP Desktop, LPDOCKABLE Dockable) {
    LPDESKTOP_DOCKING_BRIDGE Bridge;

    if (Desktop == NULL || Dockable == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Bridge = DesktopDockingBridgeGetState(Desktop);
    if (Bridge == NULL) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostDetachDockable(&(Bridge->Host), Dockable);
}

/************************************************************************/

U32 DesktopDockingBridgeMarkDirty(LPDESKTOP Desktop, U32 Reason) {
    LPDESKTOP_DOCKING_BRIDGE Bridge;

    if (Desktop == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Bridge = DesktopDockingBridgeGetState(Desktop);
    if (Bridge == NULL) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostMarkDirty(&(Bridge->Host), Reason);
}

/************************************************************************/

U32 DesktopDockingBridgeHandleDesktopRectChanged(LPDESKTOP Desktop, LPRECT DesktopRect) {
    LPDESKTOP_DOCKING_BRIDGE Bridge;
    U32 Status;

    if (Desktop == NULL || DesktopRect == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Bridge = DesktopDockingBridgeEnsureState(Desktop);
    if (Bridge == NULL) return DOCK_LAYOUT_STATUS_OUT_OF_MEMORY;

    Status = DockHostSetHostRect(&(Bridge->Host), DesktopRect);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS) return Status;

    return DesktopDockingBridgeRelayout(Desktop);
}

/************************************************************************/

U32 DesktopDockingBridgeRelayout(LPDESKTOP Desktop) {
    LPDESKTOP_DOCKING_BRIDGE Bridge;
    DOCK_LAYOUT_FRAME Frame;
    DOCK_LAYOUT_RESULT Result;
    U32 Status;

    if (Desktop == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Bridge = DesktopDockingBridgeGetState(Desktop);
    if (Bridge == NULL) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    Status = DockHostBuildLayoutFrame(&(Bridge->Host), &Frame);
    if (Status != DOCK_LAYOUT_STATUS_SUCCESS && Frame.Status == DOCK_LAYOUT_STATUS_SUCCESS) {
        Frame.Status = Status;
    }

    return DockHostApplyLayoutFrame(&(Bridge->Host), &Frame, &Result);
}

/************************************************************************/

U32 DesktopDockingBridgeGetWorkRect(LPDESKTOP Desktop, LPRECT WorkRect) {
    LPDESKTOP_DOCKING_BRIDGE Bridge;

    if (Desktop == NULL || WorkRect == NULL) return DOCK_LAYOUT_STATUS_INVALID_PARAMETER;

    Bridge = DesktopDockingBridgeGetState(Desktop);
    if (Bridge == NULL) return DOCK_LAYOUT_STATUS_NOT_ATTACHED;

    return DockHostGetWorkRect(&(Bridge->Host), WorkRect);
}

/************************************************************************/
