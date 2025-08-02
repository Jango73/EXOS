
// File.h

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
#include "FileSys.h"
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

LPFILE OpenFile(LPFILEOPENINFO);
U32 CloseFile(LPFILE);
U32 ReadFile(LPFILEOPERATION);
U32 WriteFile(LPFILEOPERATION);
U32 GetFileSize(LPFILE);
U32 DeleteFile(LPFILEOPENINFO);
U32 CreateFolder(LPFILEOPENINFO);
U32 DeleteFolder(LPFILEOPENINFO);

/***************************************************************************/

#endif
