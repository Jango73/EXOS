/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../../include/binary/Binary-ELF.h"

/***************************************************************************/

BOOL GetExecutableInfo_ELF(LPFILE File, LPEXECUTABLEINFO Info) {
    (void)File;
    (void)Info;
    return FALSE;
}

/***************************************************************************/

BOOL LoadExecutable_ELF(LPFILE File, LPEXECUTABLEINFO Info, LINEAR CodeBase,
                        LINEAR DataBase) {
    (void)File;
    (void)Info;
    (void)CodeBase;
    (void)DataBase;
    return FALSE;
}

/***************************************************************************/
