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

#ifndef DESKTOP_DOCKING_BRIDGE_H_INCLUDED
#define DESKTOP_DOCKING_BRIDGE_H_INCLUDED

/************************************************************************/

#include "desktop/Desktop.h"
#include "ui/layout/DockHost.h"

/************************************************************************/

BOOL DesktopDockingBridgeInitialize(LPDESKTOP Desktop);
void DesktopDockingBridgeShutdown(LPDESKTOP Desktop);
U32 DesktopDockingBridgeAttachDockable(LPDESKTOP Desktop, LPDOCKABLE Dockable);
U32 DesktopDockingBridgeDetachDockable(LPDESKTOP Desktop, LPDOCKABLE Dockable);
U32 DesktopDockingBridgeMarkDirty(LPDESKTOP Desktop, U32 Reason);
U32 DesktopDockingBridgeHandleDesktopRectChanged(LPDESKTOP Desktop, LPRECT DesktopRect);
U32 DesktopDockingBridgeRelayout(LPDESKTOP Desktop);
U32 DesktopDockingBridgeGetWorkRect(LPDESKTOP Desktop, LPRECT WorkRect);

/************************************************************************/

#endif
