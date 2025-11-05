
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


    File

\************************************************************************/
#ifndef FILE_H_INCLUDED
#define FILE_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Driver.h"
#include "FileSystem.h"
#include "User.h"

/***************************************************************************/

// Functions supplied by a file driver

#define DF_FILE_OPEN (DF_FIRSTFUNC + 0)
#define DF_FILE_CLOSE (DF_FIRSTFUNC + 1)
#define DF_FILE_READ (DF_FIRSTFUNC + 2)
#define DF_FILE_WRITE (DF_FIRSTFUNC + 3)
#define DF_FILE_GETPOS (DF_FIRSTFUNC + 4)
#define DF_FILE_SETPOS (DF_FIRSTFUNC + 5)

/***************************************************************************/

LPFILE OpenFile(LPFILEOPENINFO FileOpenInfo);
UINT CloseFile(LPFILE File);
UINT GetFilePosition(LPFILE File);
UINT SetFilePosition(LPFILEOPERATION Operation);
UINT ReadFile(LPFILEOPERATION Operation);
UINT WriteFile(LPFILEOPERATION Operation);
UINT GetFileSize(LPFILE File);
UINT DeleteFile(LPFILEOPENINFO FileOpenInfo);
UINT CreateFolder(LPFILEOPENINFO FileOpenInfo);
UINT DeleteFolder(LPFILEOPENINFO FileOpenInfo);

LPVOID FileReadAll(LPCSTR, UINT *);
UINT FileWriteAll(LPCSTR, LPCVOID, UINT);

/***************************************************************************/

#endif
