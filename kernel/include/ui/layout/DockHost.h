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


    Generic docking host contract for UI layout

\************************************************************************/

#ifndef UI_LAYOUT_DOCK_HOST_H_INCLUDED
#define UI_LAYOUT_DOCK_HOST_H_INCLUDED

/************************************************************************/

#include "ui/layout/Dockable.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#define DOCK_HOST_MAX_ITEMS 64

typedef enum tag_DOCK_OVERFLOW_POLICY {
    DOCK_OVERFLOW_POLICY_CLIP = 0,
    DOCK_OVERFLOW_POLICY_SHRINK = 1,
    DOCK_OVERFLOW_POLICY_REJECT = 2
} DOCK_OVERFLOW_POLICY, *LPDOCK_OVERFLOW_POLICY;

typedef struct tag_DOCK_EDGE_LAYOUT_POLICY {
    I32 MarginStart;
    I32 MarginEnd;
    I32 Spacing;
    U32 OverflowPolicy;
} DOCK_EDGE_LAYOUT_POLICY, *LPDOCK_EDGE_LAYOUT_POLICY;

typedef struct tag_DOCK_HOST_LAYOUT_POLICY {
    I32 PaddingTop;
    I32 PaddingBottom;
    I32 PaddingLeft;
    I32 PaddingRight;
    DOCK_EDGE_LAYOUT_POLICY Top;
    DOCK_EDGE_LAYOUT_POLICY Bottom;
    DOCK_EDGE_LAYOUT_POLICY Left;
    DOCK_EDGE_LAYOUT_POLICY Right;
} DOCK_HOST_LAYOUT_POLICY, *LPDOCK_HOST_LAYOUT_POLICY;

typedef struct tag_DOCK_LAYOUT_RESULT {
    U32 Status;
    RECT HostRect;
    RECT WorkRect;
    U32 DockableCount;
    U32 AppliedCount;
    U32 RejectedCount;
} DOCK_LAYOUT_RESULT, *LPDOCK_LAYOUT_RESULT;

typedef struct tag_DOCK_HOST {
    LPCSTR Identifier;
    LPVOID Context;
    RECT HostRect;
    RECT WorkRect;
    U32 LayoutSequence;
    BOOL LayoutDirty;
    U32 ItemCount;
    U32 Capacity;
    LPDOCKABLE Items[DOCK_HOST_MAX_ITEMS];
    DOCK_HOST_LAYOUT_POLICY Policy;
} DOCK_HOST;

/************************************************************************/

BOOL DockHostInit(LPDOCK_HOST Host, LPCSTR Identifier, LPVOID Context);
BOOL DockHostReset(LPDOCK_HOST Host);
U32 DockHostSetHostRect(LPDOCK_HOST Host, LPRECT HostRect);
U32 DockHostSetPolicy(LPDOCK_HOST Host, LPDOCK_HOST_LAYOUT_POLICY Policy);
U32 DockHostAttachDockable(LPDOCK_HOST Host, LPDOCKABLE Dockable);
U32 DockHostDetachDockable(LPDOCK_HOST Host, LPDOCKABLE Dockable);
U32 DockHostRelayout(LPDOCK_HOST Host, LPDOCK_LAYOUT_RESULT Result);
U32 DockHostGetWorkRect(LPDOCK_HOST Host, LPRECT WorkRect);

/************************************************************************/

#pragma pack(pop)

#endif
