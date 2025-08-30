
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


    Executable

\************************************************************************/
#include "../include/Executable.h"

#include "../include/ExecutableELF.h"
#include "../include/ExecutableEXOS.h"
#include "../include/Log.h"

/***************************************************************************/

/**
 * @brief Determine executable format and fill information structure.
 * @param File Open file handle.
 * @param Info Output structure to populate.
 * @return TRUE on success, FALSE on error or unknown format.
 */
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

/**
 * @brief Load an executable into memory based on its format.
 * @param Load Parameters describing the load operation.
 * @return TRUE on success, FALSE on failure.
 */
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
        return LoadExecutable_ELF(Load->File, Load->Info, Load->CodeBase, Load->DataBase, Load->BssBase);
    }

    KernelLogText(LOG_DEBUG, TEXT("[LoadExecutable] Unknown signature %X"), Signature);

    return FALSE;
}

/***************************************************************************/
