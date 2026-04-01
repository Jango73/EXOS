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


    Shell - Embedded E0 Scripts

\************************************************************************/

#ifndef SHELL_EMBEDDED_SCRIPTS_H_INCLUDED
#define SHELL_EMBEDDED_SCRIPTS_H_INCLUDED

#include "shell/Shell-Shared.h"

/************************************************************************/

LPCSTR ShellGetEmbeddedDriverListScript(void);
LPCSTR ShellGetEmbeddedDiskListScript(void);
LPCSTR ShellGetEmbeddedUsbPortsScript(void);
LPCSTR ShellGetEmbeddedUsbDevicesScript(void);

/************************************************************************/

#endif
