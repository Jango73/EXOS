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


    Desktop window class registry

\************************************************************************/

#ifndef DESKTOP_WINDOW_CLASS_H_INCLUDED
#define DESKTOP_WINDOW_CLASS_H_INCLUDED

/************************************************************************/

#include "Desktop.h"

/************************************************************************/

#define WINDOW_CLASS_DEFAULT_NAME TEXT("DefaultWindowClass")

/************************************************************************/

BOOL WindowClassInitializeRegistry(void);
LPWINDOW_CLASS WindowClassRegisterKernelClass(LPCSTR Name, LPWINDOW_CLASS BaseClass, WINDOWFUNC Function, U32 ClassDataSize);
LPWINDOW_CLASS WindowClassRegisterUserClass(
    LPCSTR Name,
    U32 BaseClassID,
    LPCSTR BaseClassName,
    WINDOWFUNC Function,
    U32 ClassDataSize,
    LPPROCESS OwnerProcess);
BOOL WindowClassUnregisterUserClass(U32 ClassID, LPCSTR Name, LPPROCESS OwnerProcess);
LPWINDOW_CLASS WindowClassFindByName(LPCSTR Name);
LPWINDOW_CLASS WindowClassFindByHandle(U32 ClassID);
LPWINDOW_CLASS WindowClassGetDefault(void);

/************************************************************************/

#endif
