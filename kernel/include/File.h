
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
U32 CloseFile(LPFILE File);
U32 GetFilePosition(LPFILE File);
U32 SetFilePosition(LPFILEOPERATION Operation);
U32 ReadFile(LPFILEOPERATION Operation);
U32 WriteFile(LPFILEOPERATION Operation);
U32 GetFileSize(LPFILE File);
U32 DeleteFile(LPFILEOPENINFO FileOpenInfo);
U32 CreateFolder(LPFILEOPENINFO FileOpenInfo);
U32 DeleteFolder(LPFILEOPENINFO FileOpenInfo);
BOOL QualifyFileName(LPCSTR BaseFolder, LPCSTR RawName, LPSTR FileName);

LPVOID FileReadAll(LPCSTR, U32 *);
U32 FileWriteAll(LPCSTR, LPCVOID, U32);

/***************************************************************************/

#endif
