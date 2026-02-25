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


    Display session state

\************************************************************************/

#ifndef DISPLAY_SESSION_H_INCLUDED
#define DISPLAY_SESSION_H_INCLUDED

/************************************************************************/

#include "Driver.h"
#include "User.h"
#include "process/Process.h"

/************************************************************************/

#define DISPLAY_FRONTEND_NONE 0x00000000
#define DISPLAY_FRONTEND_CONSOLE 0x00000001
#define DISPLAY_FRONTEND_DESKTOP 0x00000002

/************************************************************************/

typedef struct tag_DISPLAY_SESSION {
    LPDRIVER GraphicsDriver;
    LPDESKTOP ActiveDesktop;
    GRAPHICSMODEINFO ActiveMode;
    U32 ActiveFrontEnd;
    BOOL IsInitialized;
    BOOL HasValidMode;
} DISPLAY_SESSION, *LPDISPLAY_SESSION;

/************************************************************************/

void DisplaySessionInitialize(void);
BOOL DisplaySessionSetConsoleMode(LPGRAPHICSMODEINFO ModeInfo);
BOOL DisplaySessionSetDesktopMode(LPDESKTOP Desktop, LPDRIVER GraphicsDriver, LPGRAPHICSMODEINFO ModeInfo);
BOOL DisplaySessionGetActiveMode(LPGRAPHICSMODEINFO ModeInfoOut);
U32 DisplaySessionGetActiveFrontEnd(void);
LPDRIVER DisplaySessionGetActiveGraphicsDriver(void);
LPDESKTOP DisplaySessionGetActiveDesktop(void);

/************************************************************************/

#endif  // DISPLAY_SESSION_H_INCLUDED
