
/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\***************************************************************************/

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

LPVOID FileReadAll(LPCSTR, U32 *);
U32 FileWriteAll(LPCSTR, LPCVOID, U32);

/***************************************************************************/

#endif
