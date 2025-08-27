// ExecutableELF.c

/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/ExecutableELF.h"
#include "../include/Log.h"

/***************************************************************************/

BOOL GetExecutableInfo_ELF(LPFILE File, LPEXECUTABLEINFO Info) {
    UNUSED(File);
    UNUSED(Info);
    KernelLogText(LOG_DEBUG, TEXT("GetExecutableInfo_ELF not implemented"));
    return FALSE;
}

/***************************************************************************/

BOOL LoadExecutable_ELF(LPFILE File, LPEXECUTABLEINFO Info, LINEAR CodeBase, LINEAR DataBase) {
    UNUSED(File);
    UNUSED(Info);
    UNUSED(CodeBase);
    UNUSED(DataBase);
    KernelLogText(LOG_DEBUG, TEXT("LoadExecutable_ELF not implemented"));
    return FALSE;
}

/***************************************************************************/
