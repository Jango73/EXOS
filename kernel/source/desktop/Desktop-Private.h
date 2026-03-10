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
BOOL DefaultSetWindowRect(LPWINDOW Window, LPRECT WindowRect);
BOOL GetWindowScreenRectSnapshot(LPWINDOW Window, LPRECT Rect);
BOOL GetDesktopCaptureState(LPWINDOW Window, LPWINDOW* CaptureWindow, I32* OffsetX, I32* OffsetY);
BOOL SetDesktopCaptureState(LPWINDOW Window, LPWINDOW CaptureWindow, I32 OffsetX, I32 OffsetY);

/************************************************************************/

#endif  // DESKTOP_PRIVATE_H_INCLUDED
