// Executable.c

/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Executable.h"
#include "../include/ExecutableEXOS.h"
#include "../include/ExecutableELF.h"
#include "../include/Log.h"

/***************************************************************************/

BOOL GetExecutableInfo(LPFILE File, LPEXECUTABLEINFO Info) {
    FILEOPERATION FileOperation;
    U32 Signature;
    U32 BytesRead;

    KernelLogText(LOG_DEBUG, TEXT("[GetExecutableInfo] Enter"));

    if (File == NULL) return FALSE;
    if (Info == NULL) return FALSE;

    FileOperation.Header.Size = sizeof(FILEOPERATION);
    FileOperation.Header.Version = EXOS_ABI_VERSION;
    FileOperation.Header.Flags = 0;
    FileOperation.File = (HANDLE)File;
    FileOperation.NumBytes = sizeof(U32);
    FileOperation.Buffer = (LPVOID)&Signature;
    BytesRead = ReadFile(&FileOperation);

    if (BytesRead != sizeof(U32)) return FALSE;

    File->Position = 0;

    if (Signature == EXOS_SIGNATURE) {
        return GetExecutableInfo_EXOS(File, Info);
    } else if (Signature == ELF_SIGNATURE) {
        return GetExecutableInfo_ELF(File, Info);
    }

    KernelLogText(LOG_DEBUG, TEXT("[GetExecutableInfo] Unknown signature %X"), Signature);

    return FALSE;
}

/***************************************************************************/

BOOL LoadExecutable(LPEXECUTABLELOAD Load) {
    FILEOPERATION FileOperation;
    U32 Signature;
    U32 BytesRead;

    KernelLogText(LOG_DEBUG, TEXT("[LoadExecutable] Enter"));

    if (Load == NULL) return FALSE;
    if (Load->File == NULL) return FALSE;

    FileOperation.Header.Size = sizeof(FILEOPERATION);
    FileOperation.Header.Version = EXOS_ABI_VERSION;
    FileOperation.Header.Flags = 0;
    FileOperation.File = (HANDLE)Load->File;
    FileOperation.NumBytes = sizeof(U32);
    FileOperation.Buffer = (LPVOID)&Signature;
    BytesRead = ReadFile(&FileOperation);

    if (BytesRead != sizeof(U32)) return FALSE;

    Load->File->Position = 0;

    if (Signature == EXOS_SIGNATURE) {
        return LoadExecutable_EXOS(Load->File, Load->Info, Load->CodeBase, Load->DataBase);
    } else if (Signature == ELF_SIGNATURE) {
        return LoadExecutable_ELF(Load->File, Load->Info, Load->CodeBase, Load->DataBase);
    }

    KernelLogText(LOG_DEBUG, TEXT("[LoadExecutable] Unknown signature %X"), Signature);

    return FALSE;
}

/***************************************************************************/
