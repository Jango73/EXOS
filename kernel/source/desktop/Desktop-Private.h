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


    Desktop private declarations

\************************************************************************/

#ifndef DESKTOP_PRIVATE_H_INCLUDED
#define DESKTOP_PRIVATE_H_INCLUDED

/************************************************************************/

#include "GFX.h"

/************************************************************************/

typedef struct tag_WINDOW_STATE_SNAPSHOT {
    RECT Rect;
    RECT ScreenRect;
    RECT WorkRect;
    U32 Style;
    U32 Status;
    U32 ContentTransparencyHint;
    U32 Level;
    I32 Order;
    LPWINDOW ParentWindow;
    LPTASK Task;
    LPWINDOW_CLASS Class;
    BOOL HasWorkRect;
    STR Caption[MAX_WINDOW_CAPTION];
} WINDOW_STATE_SNAPSHOT, *LPWINDOW_STATE_SNAPSHOT;

typedef struct tag_WINDOW_DRAW_CONTEXT_SNAPSHOT {
    RECT SurfaceRect;
    RECT ClipRect;
    POINT Origin;
    U32 Flags;
} WINDOW_DRAW_CONTEXT_SNAPSHOT, *LPWINDOW_DRAW_CONTEXT_SNAPSHOT;

/************************************************************************/

extern BRUSH Brush_Desktop;
extern BRUSH Brush_High;
extern BRUSH Brush_Normal;
extern BRUSH Brush_HiShadow;
extern BRUSH Brush_LoShadow;
extern BRUSH Brush_Client;
extern BRUSH Brush_Text_Normal;
extern BRUSH Brush_Text_Select;
extern BRUSH Brush_Selection;
extern BRUSH Brush_Title_Bar;
extern BRUSH Brush_Title_Bar_2;
extern BRUSH Brush_Title_Text;

extern PEN Pen_Desktop;
extern PEN Pen_High;
extern PEN Pen_Normal;
extern PEN Pen_HiShadow;
extern PEN Pen_LoShadow;
extern PEN Pen_Client;
extern PEN Pen_Text_Normal;
extern PEN Pen_Text_Select;
extern PEN Pen_Selection;
extern PEN Pen_Title_Bar;
extern PEN Pen_Title_Bar_2;
extern PEN Pen_Title_Text;

BOOL ResetGraphicsContext(LPGRAPHICSCONTEXT This);
BOOL SetGraphicsContextClipScreenRect(HANDLE GC, LPRECT ClipRect);
BOOL DesktopBuildWindowVisibleRegion(
    LPWINDOW Window,
    LPRECT BaseRect,
    BOOL ExcludeTargetChildren,
    LPRECT_REGION Region,
    LPRECT Storage,
    UINT Capacity);
BOOL DesktopBuildRootVisibleRegion(
    LPWINDOW RootWindow,
    LPRECT BaseRect,
    LPRECT_REGION Region,
    LPRECT Storage,
    UINT Capacity);
BOOL BuildWindowDrawClipRegion(
    LPWINDOW This,
    LPRECT_REGION ClipRegion,
    LPRECT ClipStorage,
    UINT ClipCapacity
);
BOOL DesktopDispatchWindowDraw(LPWINDOW Window, HANDLE TargetHandle, U32 Param1, U32 Param2);
BOOL DesktopGetWindowDrawSurfaceRect(LPWINDOW Window, LPRECT Rect);
BOOL DesktopGetWindowDrawClipRect(LPWINDOW Window, LPRECT Rect);
BOOL BuildWindowRectAtPosition(LPWINDOW Window, LPPOINT Position, LPRECT Rect);
BOOL DesktopResolveWindowPlacementRect(LPWINDOW Window, LPRECT WindowRect);
BOOL DesktopRevalidateSiblingPlacementConstraints(LPWINDOW Window);
BOOL DefaultSetWindowRect(LPWINDOW Window, LPRECT WindowRect);
BOOL GetWindowScreenRectSnapshot(LPWINDOW Window, LPRECT Rect);
BOOL GetWindowStateSnapshot(LPWINDOW Window, LPWINDOW_STATE_SNAPSHOT Snapshot);
BOOL GetWindowOrderSnapshot(LPWINDOW Window, I32* Order);
BOOL GetWindowLevelSnapshot(LPWINDOW Window, U32* Level);
BOOL GetWindowEffectiveWorkRectSnapshot(LPWINDOW Window, LPRECT WorkRect);
BOOL GetWindowDrawContextSnapshot(LPWINDOW Window, LPWINDOW_DRAW_CONTEXT_SNAPSHOT Snapshot);
BOOL DesktopRefreshWindowChildScreenRects(LPWINDOW ParentWindow);
BOOL DesktopSnapshotWindowChildren(LPWINDOW Parent, LPWINDOW** Children, UINT* ChildCount);
void DesktopCursorRenderSoftwareOverlayOnWindow(LPWINDOW Window);
BOOL DesktopConsumeWindowDirtyRegionSnapshot(
    LPWINDOW Window,
    LPRECT_REGION ClipRegion,
    LPRECT ClipStorage,
    UINT ClipCapacity,
    LPRECT ScreenRect,
    I32* Order,
    LPWINDOW* ParentWindow);
BOOL DesktopAttachWindowChild(LPWINDOW Parent, LPWINDOW Child);
BOOL DesktopDetachWindowChild(LPWINDOW Parent, LPWINDOW Child);
BOOL DesktopSetWindowTask(LPWINDOW Window, LPTASK Task);
BOOL DesktopSetWindowVisibleState(LPWINDOW Window, BOOL ShowHide);
BOOL DesktopRefreshWindowEffectiveVisibilityTree(LPWINDOW Window);
BOOL DesktopSetWindowStyleState(LPWINDOW Window, U32 StyleMask, BOOL Enabled);
BOOL DesktopSetWindowContentTransparencyHint(LPWINDOW Window, U32 Hint);
BOOL DesktopSetWindowResolvedTransparencyState(LPWINDOW Window, BOOL Enabled);
BOOL DesktopSetWindowCaption(LPWINDOW Window, LPCSTR Caption);
BOOL DesktopSetWindowBypassParentWorkRectState(LPWINDOW Window, BOOL Enabled);
BOOL DesktopRefreshWindowZOrder(LPWINDOW Window);
I32 SortWindows_Order(LPCVOID Item1, LPCVOID Item2);
BOOL DesktopGetRootWindow(LPDESKTOP Desktop, LPWINDOW* RootWindow);
BOOL DesktopClearWindowReferences(LPDESKTOP Desktop, LPWINDOW Window);
BOOL GetDesktopCaptureState(LPWINDOW Window, LPWINDOW* CaptureWindow, I32* OffsetX, I32* OffsetY);
BOOL SetDesktopCaptureState(LPWINDOW Window, LPWINDOW CaptureWindow, I32 OffsetX, I32 OffsetY);
BOOL GetDesktopLastMouseMoveTarget(LPWINDOW Window, LPWINDOW* TargetWindow);
BOOL SetDesktopLastMouseMoveTarget(LPWINDOW Window, LPWINDOW TargetWindow);
BOOL DesktopResolveWindowTarget(LPDESKTOP Desktop, HANDLE Target, LPWINDOW* Window);
BOOL DrawWindowBackgroundResolved(HANDLE Window, HANDLE GC, LPRECT Rect, U32 ThemeToken, BOOL* Transparent);
BOOL DesktopMarkWindowDispatchBegin(LPWINDOW Window, U32 Message);
BOOL DesktopMarkWindowDispatchEnd(LPWINDOW Window, U32 Message);

/************************************************************************/

#endif  // DESKTOP_PRIVATE_H_INCLUDED
