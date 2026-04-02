
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


    Executable

\************************************************************************/

#ifndef EXECUTABLE_H_INCLUDED
#define EXECUTABLE_H_INCLUDED

/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "Base.h"
#include "fs/File.h"

/***************************************************************************/

typedef struct tag_EXECUTABLE_INFO {
    UINT EntryPoint;
    UINT CodeBase;
    UINT CodeSize;
    UINT DataBase;
    UINT DataSize;
    UINT BssBase;
    UINT BssSize;
    UINT StackMinimum;
    UINT StackRequested;
    UINT HeapMinimum;
    UINT HeapRequested;
} EXECUTABLE_INFO, *LPEXECUTABLE_INFO;

/***************************************************************************/
// Load request: caller provides actual target bases where segments will land.

typedef struct tag_EXECUTABLE_LOAD {
    LPFILE File;
    LPEXECUTABLE_INFO Info;
    LINEAR CodeBase;
    LINEAR DataBase;
    LINEAR BssBase;
} EXECUTABLE_LOAD, *LPEXECUTABLE_LOAD;

/***************************************************************************/

BOOL GetExecutableInfo(LPFILE, LPEXECUTABLE_INFO);
BOOL LoadExecutable(LPEXECUTABLE_LOAD);

/***************************************************************************/

#endif
