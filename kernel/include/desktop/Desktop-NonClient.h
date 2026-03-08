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


    Desktop non-client rendering

\************************************************************************/

#ifndef DESKTOP_NON_CLIENT_H_INCLUDED
#define DESKTOP_NON_CLIENT_H_INCLUDED

/************************************************************************/

#include "Desktop.h"

/************************************************************************/

#define WINDOW_DECORATION_MODE_SYSTEM 0x00000000
#define WINDOW_DECORATION_MODE_CLIENT 0x00000001
#define WINDOW_DECORATION_MODE_BARE 0x00000002

/************************************************************************/

U32 GetWindowDecorationMode(LPWINDOW Window);
BOOL ShouldDrawWindowNonClient(LPWINDOW Window);
BOOL DrawWindowNonClient(HANDLE Window, HANDLE GC, LPRECT Rect);
BOOL IsPointInWindowTitleBar(LPWINDOW Window, LPPOINT ScreenPoint);

/************************************************************************/

#endif  // DESKTOP_NON_CLIENT_H_INCLUDED
