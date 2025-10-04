
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

#include "../include/File.h"

#include "../include/Heap.h"
#include "../include/Helpers.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Process.h"

/***************************************************************************/

/**
 * @brief Opens a file based on provided information
 * @param Info Pointer to file open information structure
 * @return Pointer to opened file structure, or NULL on failure
 */
LPFILE OpenFile(LPFILEOPENINFO Info) {
    FILEINFO Find;
    LPFILESYSTEM FileSystem = NULL;
    LPLISTNODE Node = NULL;
    LPFILE File = NULL;
    LPFILE AlreadyOpen = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (Info == NULL) return NULL;

    //-------------------------------------
    // Lock access to file systems

    LockMutex(MUTEX_FILESYSTEM, INFINITY);

    //-------------------------------------
    // Check if the file is already open

    LockMutex(MUTEX_FILE, INFINITY);

    for (Node = Kernel.File->First; Node; Node = Node->Next) {
        AlreadyOpen = (LPFILE)Node;

        LockMutex(&(AlreadyOpen->Mutex), INFINITY);

        if (STRINGS_EQUAL(AlreadyOpen->Name, Info->Name)) {
            if (AlreadyOpen->OwnerTask == GetCurrentTask()) {
                if (AlreadyOpen->OpenFlags == Info->Flags) {
                    File = AlreadyOpen;
                    File->References++;

                    UnlockMutex(&(AlreadyOpen->Mutex));
                    UnlockMutex(MUTEX_FILE);
                    goto Out;
                }
            }
        }

        UnlockMutex(&(AlreadyOpen->Mutex));
    }

    UnlockMutex(MUTEX_FILE);

    //-------------------------------------
    // Use SystemFS if an absolute path is provided

    if (Info->Name[0] == PATH_SEP) {
        Find.Size = sizeof Find;
        Find.FileSystem = GetSystemFS();
        Find.Attributes = MAX_U32;
        Find.Flags = Info->Flags;
        StringCopy(Find.Name, Info->Name);

        File = (LPFILE)GetSystemFS()->Driver->Command(DF_FS_OPENFILE, (U32)&Find);

        SAFE_USE(File) {
            LockMutex(MUTEX_FILE, INFINITY);

            File->OwnerTask = GetCurrentTask();
            File->OpenFlags = Info->Flags;

            ListAddItem(Kernel.File, File);

            UnlockMutex(MUTEX_FILE);
        }

        goto Out;
    }

    //-------------------------------------
    // Get the name of the volume in which the file
    // is supposed to be located

    DEBUG(TEXT("[OpenFile] Searching for %s in file systems"), Info->Name);

    for (Node = Kernel.FileSystem->First; Node; Node = Node->Next) {
        FileSystem = (LPFILESYSTEM)Node;

        Find.Size = sizeof Find;
        Find.FileSystem = FileSystem;
        Find.Attributes = MAX_U32;
        Find.Flags = Info->Flags;
        StringCopy(Find.Name, Info->Name);

        File = (LPFILE)FileSystem->Driver->Command(DF_FS_OPENFILE, (U32)&Find);

        SAFE_USE(File) {
            DEBUG(TEXT("[OpenFile] Found %s in %s"), Info->Name, FileSystem->Driver->Product);

            LockMutex(MUTEX_FILE, INFINITY);

            File->OwnerTask = GetCurrentTask();
            File->OpenFlags = Info->Flags;

            ListAddItem(Kernel.File, File);

            UnlockMutex(MUTEX_FILE);
            break;
        }
    }

Out:

    UnlockMutex(MUTEX_FILESYSTEM);

    return File;
}

/***************************************************************************/

/**
 * @brief Closes an open file and decrements reference count
 * @param File Pointer to file structure to close
 * @return 1 on success, 0 on failure
 */
U32 CloseFile(LPFILE File) {
    //-------------------------------------
    // Check validity of parameters

    if (File->ID != KOID_FILE) return 0;

    LockMutex(&(File->Mutex), INFINITY);

    // Call filesystem-specific close function
    File->FileSystem->Driver->Command(DF_FS_CLOSEFILE, (U32)File);

    ReleaseKernelObject(File);

    UnlockMutex(&(File->Mutex));

    return 1;
}

/***************************************************************************/

/**
 * @brief Gets current position within a file
 * @param File Pointer to file structure
 * @return Current file position
 */
U32 GetFilePosition(LPFILE File) {
    U32 Position = 0;

    SAFE_USE_VALID_ID(File, KOID_FILE) {
        //-------------------------------------
        // Lock access to the file

        LockMutex(&(File->Mutex), INFINITY);

        Position = File->Position;

        //-------------------------------------
        // Unlock access to the file

        UnlockMutex(&(File->Mutex));
    }

    return Position;
}

/***************************************************************************/

/**
 * @brief Sets current position within a file
 * @param Operation Pointer to file operation structure containing new position
 * @return DF_ERROR_SUCCESS on success, DF_ERROR_BADPARAM on failure
 */
U32 SetFilePosition(LPFILEOPERATION Operation) {
    SAFE_USE_VALID(Operation) {
        LPFILE File = (LPFILE)Operation->File;

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            //-------------------------------------
            // Lock access to the file

            LockMutex(&(File->Mutex), INFINITY);

            File->Position = Operation->NumBytes;

            //-------------------------------------
            // Unlock access to the file

            UnlockMutex(&(File->Mutex));

            return DF_ERROR_SUCCESS;
        }
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

/**
 * @brief Reads data from a file
 * @param FileOp Pointer to file operation structure
 * @return Number of bytes read, 0 on failure
 */
U32 ReadFile(LPFILEOPERATION FileOp) {
    LPFILE File = NULL;
    U32 Result = 0;
    U32 BytesTransferred = 0;

    //-------------------------------------
    // Check validity of parameters

    if (FileOp == NULL) return 0;
    if (FileOp->File == NULL) return 0;

    File = (LPFILE)FileOp->File;
    if (File->ID != KOID_FILE) return 0;

    if ((File->OpenFlags & FILE_OPEN_READ) == 0) return 0;

    //-------------------------------------
    // Lock access to the file

    LockMutex(&(File->Mutex), INFINITY);

    File->ByteCount = FileOp->NumBytes;
    File->Buffer = FileOp->Buffer;

    Result = File->FileSystem->Driver->Command(DF_FS_READ, (U32)File);

    if (Result == DF_ERROR_SUCCESS) {
        BytesTransferred = File->BytesTransferred;
    }

    UnlockMutex(&(File->Mutex));

    return BytesTransferred;
}

/***************************************************************************/

/**
 * @brief Writes data to a file
 * @param FileOp Pointer to file operation structure
 * @return Number of bytes written, 0 on failure
 */
U32 WriteFile(LPFILEOPERATION FileOp) {
    LPFILE File = NULL;
    U32 Result = 0;
    U32 BytesWritten = 0;

    //-------------------------------------
    // Check validity of parameters

    if (FileOp == NULL) return 0;
    if (FileOp->File == NULL) return 0;

    File = (LPFILE)FileOp->File;
    if (File->ID != KOID_FILE) return 0;

    if ((File->OpenFlags & FILE_OPEN_WRITE) == 0) return 0;

    //-------------------------------------
    // Lock access to the file

    LockMutex(&(File->Mutex), INFINITY);

    File->ByteCount = FileOp->NumBytes;
    File->Buffer = FileOp->Buffer;

    Result = File->FileSystem->Driver->Command(DF_FS_WRITE, (U32)File);

    if (Result == DF_ERROR_SUCCESS) {
        BytesWritten = File->BytesTransferred;
    }

    UnlockMutex(&(File->Mutex));

    return BytesWritten;
}

/***************************************************************************/

/**
 * @brief Gets the size of a file
 * @param File Pointer to file structure
 * @return File size in bytes
 */
U32 GetFileSize(LPFILE File) {
    U32 Size = 0;

    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) return 0;
    if (File->ID != KOID_FILE) return 0;

    LockMutex(&(File->Mutex), INFINITY);

    Size = File->SizeLow;

    UnlockMutex(&(File->Mutex));

    return Size;
}

/***************************************************************************/

/**
 * @brief Reads entire file content into memory
 * @param Name File name to read
 * @param Size Pointer to variable to receive file size
 * @return Pointer to allocated buffer containing file content, NULL on failure
 */
LPVOID FileReadAll(LPCSTR Name, U32 *Size) {
    FILEOPENINFO OpenInfo;
    FILEOPERATION FileOp;
    LPFILE File = NULL;
    LPVOID Buffer = NULL;

    DEBUG(TEXT("[FileReadAll] Name = %s"), Name);

    SAFE_USE_2(Name, Size) {
        //-------------------------------------
        // Open the file

        OpenInfo.Header.Size = sizeof(FILEOPENINFO);
        OpenInfo.Name = (LPSTR)Name;
        OpenInfo.Flags = FILE_OPEN_READ;
        File = OpenFile(&OpenInfo);

        if (File == NULL) return NULL;

        DEBUG(TEXT("[FileReadAll] File found"));

        //-------------------------------------
        // Allocate buffer and read content

        *Size = GetFileSize(File);
        Buffer = KernelHeapAlloc(*Size + 1);

        SAFE_USE(Buffer) {
            FileOp.Header.Size = sizeof(FILEOPERATION);
            FileOp.File = (HANDLE)File;
            FileOp.Buffer = Buffer;
            FileOp.NumBytes = *Size;
            ReadFile(&FileOp);
            ((LPSTR)Buffer)[*Size] = STR_NULL;
        }

        CloseFile(File);

        return Buffer;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Writes entire buffer content to a file
 * @param Name File name to write to
 * @param Buffer Pointer to data buffer
 * @param Size Size of data to write
 * @return Number of bytes written
 */
U32 FileWriteAll(LPCSTR Name, LPCVOID Buffer, U32 Size) {
    FILEOPENINFO OpenInfo;
    FILEOPERATION FileOp;
    LPFILE File = NULL;
    U32 BytesWritten = 0;

    DEBUG(TEXT("[FileWriteAll] name %s, size %d"), Name, Size);

    SAFE_USE_2(Name, Buffer) {
        //-------------------------------------
        // Open the file

        OpenInfo.Header.Size = sizeof(FILEOPENINFO);
        OpenInfo.Name = (LPSTR)Name;
        OpenInfo.Flags = FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_TRUNCATE;
        File = OpenFile(&OpenInfo);

        if (File == NULL) {
            DEBUG(TEXT("[FileWriteAll] OpenFile failed to create %s"), Name);
            return 0;
        }

        //-------------------------------------
        // Write the buffer to the file

        FileOp.Header.Size = sizeof(FILEOPERATION);
        FileOp.File = (HANDLE)File;
        FileOp.Buffer = (LPVOID)Buffer;
        FileOp.NumBytes = Size;
        BytesWritten = WriteFile(&FileOp);

        CloseFile(File);

        return BytesWritten;
    }

    return 0;
}

/***************************************************************************/

