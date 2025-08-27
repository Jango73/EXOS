#ifndef EXECUTABLEELF_H_INCLUDED
#define EXECUTABLEELF_H_INCLUDED

/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "Base.h"
#include "File.h"
#include "Executable.h"

/***************************************************************************/

#define ELF_SIGNATURE 0x464C457F

/***************************************************************************/

BOOL GetExecutableInfo_ELF(LPFILE, LPEXECUTABLEINFO);
BOOL LoadExecutable_ELF(LPFILE, LPEXECUTABLEINFO, LINEAR, LINEAR);

/***************************************************************************/

#endif
