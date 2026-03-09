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

#include "desktop/components/RootWindowClass.h"

/************************************************************************/

BOOL RootWindowClassEnsureRegistered(WINDOWFUNC WindowFunction) {
    return WindowDockHostClassEnsureDerivedRegistered(ROOT_WINDOW_CLASS_NAME, WindowFunction);
}

/************************************************************************/

LPROOT_WINDOW_CLASS_DATA RootWindowClassGetData(LPDESKTOP Desktop) {
    LPWINDOW Window;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return NULL;

    Window = Desktop->Window;
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return NULL;

    return (LPROOT_WINDOW_CLASS_DATA)WindowDockHostClassGetData(Window);
}

/************************************************************************/
