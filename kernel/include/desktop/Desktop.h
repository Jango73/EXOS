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

#include "process/Process.h"
#include "Desktop-ThemeRuntime.h"

/************************************************************************/
// Functions in Desktop.c

LPDESKTOP CreateDesktop(void);
BOOL DeleteDesktop(LPDESKTOP);
BOOL ShowDesktop(LPDESKTOP);
LPWINDOW CreateWindow(LPWINDOWINFO);
BOOL DeleteWindow(LPWINDOW);
LPWINDOW FindWindow(LPWINDOW, LPWINDOW);
LPDESKTOP GetWindowDesktop(LPWINDOW);
BOOL BroadcastMessageToWindow(LPWINDOW This, U32 Msg, U32 Param1, U32 Param2);
BOOL UpdateWindowScreenRectAndDirtyRegion(LPWINDOW Window, LPRECT Rect);
BOOL InvalidateWindowRect(HANDLE, LPRECT);
BOOL RequestWindowDraw(HANDLE Handle);
BOOL ShowWindow(HANDLE, BOOL);
BOOL GetWindowRect(HANDLE, LPRECT);
BOOL MoveWindow(HANDLE, LPPOINT);
BOOL SizeWindow(HANDLE, LPPOINT);
HANDLE GetWindowParent(HANDLE);
BOOL GetDesktopScreenRect(LPDESKTOP, LPRECT);
U32 SetWindowProp(HANDLE, LPCSTR, U32);
U32 GetWindowProp(HANDLE, LPCSTR);
HANDLE GetWindowGC(HANDLE);
BOOL ReleaseWindowGC(HANDLE);
HANDLE BeginWindowDraw(HANDLE);
BOOL EndWindowDraw(HANDLE);
HANDLE GetSystemBrush(U32);
HANDLE GetSystemPen(U32);
HANDLE SelectBrush(HANDLE, HANDLE);
HANDLE SelectPen(HANDLE, HANDLE);
HANDLE CreateBrush(LPBRUSHINFO);
HANDLE CreatePen(LPPENINFO);
BOOL SetPixel(LPPIXELINFO);
BOOL GetPixel(LPPIXELINFO);
BOOL Line(LPLINEINFO);
BOOL Rectangle(LPRECTINFO);
BOOL Arc(LPARCINFO);
BOOL Triangle(LPTRIANGLEINFO);
BOOL SetWindowTimer(HANDLE Window, U32 TimerID, U32 IntervalMilliseconds);
BOOL KillWindowTimer(HANDLE Window, U32 TimerID);
U32 DefWindowFunc(HANDLE, U32, U32, U32);
HANDLE WindowHitTest(HANDLE, LPPOINT);
BOOL LoadTheme(LPCSTR Path);
BOOL ActivateTheme(LPCSTR NameOrHandle);
BOOL GetActiveThemeInfo(LPDESKTOP_THEME_RUNTIME_INFO Info);
BOOL ResetThemeToDefault(void);

/************************************************************************/

#endif  // DESKTOP_H_INCLUDED
