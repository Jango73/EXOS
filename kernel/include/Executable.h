#ifndef EXECUTABLE_H_INCLUDED
#define EXECUTABLE_H_INCLUDED

/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "Base.h"
#include "File.h"

/***************************************************************************/

typedef struct tag_EXECUTABLEINFO {
    U32 EntryPoint;
    U32 CodeBase;
    U32 CodeSize;
    U32 DataBase;
    U32 DataSize;
    U32 BssBase;
    U32 BssSize;
    U32 StackMinimum;
    U32 StackRequested;
    U32 HeapMinimum;
    U32 HeapRequested;
} EXECUTABLEINFO, *LPEXECUTABLEINFO;

/***************************************************************************/
// Load request: caller provides actual target bases where segments will land.

typedef struct tag_EXECUTABLELOAD {
    LPFILE File;
    LPEXECUTABLEINFO Info;
    LINEAR CodeBase;
    LINEAR DataBase;
    LINEAR BssBase;
} EXECUTABLELOAD, *LPEXECUTABLELOAD;

/***************************************************************************/

BOOL GetExecutableInfo(LPFILE, LPEXECUTABLEINFO);
BOOL LoadExecutable(LPEXECUTABLELOAD);

/***************************************************************************/

#endif
