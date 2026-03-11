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


    Desktop cursor ownership and rendering

\************************************************************************/

#ifndef DESKTOP_CURSOR_H_INCLUDED
#define DESKTOP_CURSOR_H_INCLUDED

/************************************************************************/

#include "process/Process.h"

/************************************************************************/

void DesktopCursorOnDesktopActivated(LPDESKTOP Desktop);
void DesktopCursorOnMousePositionChanged(LPDESKTOP Desktop, I32 OldX, I32 OldY, I32 NewX, I32 NewY);
void DesktopCursorRenderSoftwareOverlayOnWindow(LPWINDOW Window);

/************************************************************************/

#endif  // DESKTOP_CURSOR_H_INCLUDED
