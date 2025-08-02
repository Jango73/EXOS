/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef BINARY_ELF_H_INCLUDED
#define BINARY_ELF_H_INCLUDED

#include "../Address.h"
#include "../File.h"

typedef struct tag_EXECUTABLEINFO EXECUTABLEINFO, *LPEXECUTABLEINFO;

#define ELF_SIGNATURE 0x464C457F

/***************************************************************************/

BOOL GetExecutableInfo_ELF(LPFILE File, LPEXECUTABLEINFO Info);
BOOL LoadExecutable_ELF(LPFILE File, LPEXECUTABLEINFO Info, LINEAR CodeBase,
                        LINEAR DataBase);

/***************************************************************************/

#endif
