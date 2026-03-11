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


    Desktop root window class

\************************************************************************/

#ifndef ROOT_WINDOW_CLASS_H_INCLUDED
#define ROOT_WINDOW_CLASS_H_INCLUDED

/************************************************************************/

#include "desktop/Desktop.h"
#include "ui/WindowDockHost.h"

/************************************************************************/

#define ROOT_WINDOW_CLASS_NAME TEXT("RootWindowClass")

/************************************************************************/

typedef WINDOW_DOCK_HOST_CLASS_DATA ROOT_WINDOW_CLASS_DATA;
typedef LPWINDOW_DOCK_HOST_CLASS_DATA LPROOT_WINDOW_CLASS_DATA;

/************************************************************************/

LPROOT_WINDOW_CLASS_DATA RootWindowClassGetData(LPDESKTOP Desktop);

/************************************************************************/

#endif
