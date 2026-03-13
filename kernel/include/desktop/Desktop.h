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


    Desktop

\************************************************************************/

#ifndef DESKTOP_H_INCLUDED
#define DESKTOP_H_INCLUDED

/************************************************************************/

#include "User.h"
#include "process/Process.h"
#include "Desktop-ThemeRuntime.h"
#include "GFX.h"

/************************************************************************/
// Windowing lock contract
//
// Lock domains used by desktop/windowing code:
// - TaskMessageMutex
// - DesktopTreeMutex
// - DesktopStateMutex
// - DesktopTimerMutex
// - WindowMutex (per window)
//
// Required acquisition order:
// 1. TaskMessageMutex
// 2. DesktopTreeMutex
// 3. DesktopStateMutex
// 4. WindowMutex
// 5. GraphicsContextMutex
//
// Forbidden while holding DesktopTree/DesktopState/Window mutexes:
// - calling PostMessage
// - calling window callbacks (Window->Function)
//
// Structural tree/list traversals that can race with z-order/tree mutations
// must use one stable snapshot and execute callbacks outside structural locks.

typedef enum tag_DESKTOP_LOCK_ROLE {
    DESKTOP_LOCK_ROLE_NONE = 0,
    DESKTOP_LOCK_ROLE_TASK_MESSAGE = 1,
    DESKTOP_LOCK_ROLE_TREE = 2,
    DESKTOP_LOCK_ROLE_STATE = 3,
    DESKTOP_LOCK_ROLE_TIMER = 4,
    DESKTOP_LOCK_ROLE_WINDOW = 5,
    DESKTOP_LOCK_ROLE_GRAPHICS_CONTEXT = 6
} DESKTOP_LOCK_ROLE, *LPDESKTOP_LOCK_ROLE;

/************************************************************************/
// Functions in Desktop.c

LPDESKTOP CreateDesktop(void);
BOOL DeleteDesktop(LPDESKTOP);
BOOL ShowDesktop(LPDESKTOP);
LPWINDOW DesktopCreateWindow(LPWINDOWINFO);
BOOL DesktopDeleteWindow(LPWINDOW);
LPWINDOW DesktopFindWindow(LPWINDOW, U32);
LPWINDOW DesktopContainsWindow(LPWINDOW, LPWINDOW);
LPDESKTOP DesktopGetWindowDesktop(LPWINDOW);
BOOL DesktopBroadcastMessageToWindow(LPWINDOW This, U32 Msg, U32 Param1, U32 Param2);
BOOL DesktopUpdateWindowScreenRectAndDirtyRegion(LPWINDOW Window, LPRECT Rect);
BOOL RequestWindowDraw(HANDLE Handle);
BOOL DesktopSetWindowVisibility(HANDLE, BOOL);
BOOL BringWindowToFront(HANDLE);
BOOL SizeWindow(HANDLE, LPPOINT);
BOOL SetWindowStyleState(HANDLE, U32, BOOL);
BOOL SetWindowContentTransparencyHint(HANDLE Handle, U32 Hint);
BOOL SetWindowWorkRect(HANDLE Handle, LPRECT WorkRect);
BOOL GetWindowWorkRect(HANDLE Handle, LPRECT WorkRect);
BOOL GetWindowDrawableRect(HANDLE Handle, LPRECT DrawableRect);
BOOL WindowRectToScreenRect(HANDLE Handle, LPRECT WindowRect, LPRECT ScreenRect);
BOOL GetDesktopScreenRect(LPDESKTOP, LPRECT);
HANDLE GetWindowGC(HANDLE);
BOOL ReleaseWindowGC(HANDLE);
BOOL GetWindowDrawSurfaceRect(HANDLE Handle, LPRECT Rect);
BOOL SetPixel(LPPIXELINFO);
BOOL GetPixel(LPPIXELINFO);
BOOL Rectangle(LPRECTINFO);
BOOL DesktopDrawText(LPGFX_TEXT_DRAW_INFO);
BOOL DesktopMeasureText(LPGFX_TEXT_MEASURE_INFO);
HANDLE WindowHitTest(HANDLE, LPPOINT);
BOOL LoadTheme(LPCSTR Path);
BOOL ActivateTheme(LPCSTR NameOrHandle);
BOOL GetActiveThemeInfo(LPDESKTOP_THEME_RUNTIME_INFO Info);
BOOL ResetThemeToDefault(void);

/************************************************************************/

#endif  // DESKTOP_H_INCLUDED
