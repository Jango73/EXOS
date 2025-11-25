
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

#include "File.h"

#include "Heap.h"
#include "utils/Helpers.h"
#include "Kernel.h"
#include "Log.h"
#include "process/Process.h"

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

    LPLIST FileList = GetFileList();
    for (Node = FileList != NULL ? FileList->First : NULL; Node; Node = Node->Next) {
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

        File = (LPFILE)GetSystemFS()->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);

        SAFE_USE(File) {
            LockMutex(MUTEX_FILE, INFINITY);

            File->OwnerTask = GetCurrentTask();
            File->OpenFlags = Info->Flags;

            ListAddItem(FileList, File);

            UnlockMutex(MUTEX_FILE);
        }

        goto Out;
    }

    //-------------------------------------
    // Get the name of the volume in which the file
    // is supposed to be located

    DEBUG(TEXT("[OpenFile] Searching for %s in file systems"), Info->Name);

    LPLIST FileSystemList = GetFileSystemList();
    for (Node = FileSystemList != NULL ? FileSystemList->First : NULL; Node; Node = Node->Next) {
        FileSystem = (LPFILESYSTEM)Node;

        Find.Size = sizeof Find;
        Find.FileSystem = FileSystem;
        Find.Attributes = MAX_U32;
        Find.Flags = Info->Flags;
        StringCopy(Find.Name, Info->Name);

        File = (LPFILE)FileSystem->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);

        SAFE_USE(File) {
            DEBUG(TEXT("[OpenFile] Found %s in %s"), Info->Name, FileSystem->Driver->Product);

            LockMutex(MUTEX_FILE, INFINITY);

            File->OwnerTask = GetCurrentTask();
            File->OpenFlags = Info->Flags;

            ListAddItem(FileList, File);

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
UINT CloseFile(LPFILE File) {
    //-------------------------------------
    // Check validity of parameters

    SAFE_USE_VALID_ID(File, KOID_FILE) {
        if (File->TypeID != KOID_FILE) return 0;

        LockMutex(&(File->Mutex), INFINITY);

        // Call filesystem-specific close function
        File->FileSystem->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);

        UnlockMutex(&(File->Mutex));

        return 1;
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Gets current position within a file
 * @param File Pointer to file structure
 * @return Current file position
 */
UINT GetFilePosition(LPFILE File) {
    UINT Position = 0;

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
 * @return DF_RET_SUCCESS on success, DF_RET_BADPARAM on failure
 */
UINT SetFilePosition(LPFILEOPERATION Operation) {
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

            return DF_RET_SUCCESS;
        }
    }

    return DF_RET_BADPARAM;
}

/***************************************************************************/

/**
 * @brief Reads data from a file
 * @param Operation Pointer to file operation structure
 * @return Number of bytes read, 0 on failure
 */
UINT ReadFile(LPFILEOPERATION Operation) {
    LPFILE File = NULL;
    UINT Result = 0;
    UINT BytesTransferred = 0;

    SAFE_USE_VALID(Operation) {
        File = (LPFILE)Operation->File;

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            if ((File->OpenFlags & FILE_OPEN_READ) == 0) return 0;

            //-------------------------------------
            // Lock access to the file

            LockMutex(&(File->Mutex), INFINITY);

            File->ByteCount = Operation->NumBytes;
            File->Buffer = Operation->Buffer;

            Result = File->FileSystem->Driver->Command(DF_FS_READ, (UINT)File);

            if (Result == DF_RET_SUCCESS) {
                BytesTransferred = File->BytesTransferred;
            }

            UnlockMutex(&(File->Mutex));

            return BytesTransferred;
        }
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Writes data to a file
 * @param Operation Pointer to file operation structure
 * @return Number of bytes written, 0 on failure
 */
UINT WriteFile(LPFILEOPERATION Operation) {
    LPFILE File = NULL;
    UINT Result = 0;
    UINT BytesWritten = 0;

    SAFE_USE_VALID(Operation) {
        File = (LPFILE)Operation->File;

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            if ((File->OpenFlags & FILE_OPEN_WRITE) == 0) return 0;

            //-------------------------------------
            // Lock access to the file

            LockMutex(&(File->Mutex), INFINITY);

            File->ByteCount = Operation->NumBytes;
            File->Buffer = Operation->Buffer;

            Result = File->FileSystem->Driver->Command(DF_FS_WRITE, (UINT)File);

            if (Result == DF_RET_SUCCESS) {
                BytesWritten = File->BytesTransferred;
            }

            UnlockMutex(&(File->Mutex));

            return BytesWritten;
        }
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Gets the size of a file
 * @param File Pointer to file structure
 * @return File size in bytes
 */
UINT GetFileSize(LPFILE File) {
    UINT Size = 0;

    SAFE_USE_VALID_ID(File, KOID_FILE) {
        LockMutex(&(File->Mutex), INFINITY);

#ifdef __EXOS_64__
        Size = U64_Make(File->SizeHigh, File->SizeLow);
#else
        Size = File->SizeLow;
#endif

        UnlockMutex(&(File->Mutex));

        return Size;
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Reads entire file content into memory
 * @param Name File name to read
 * @param Size Pointer to variable to receive file size
 * @return Pointer to allocated buffer containing file content, NULL on failure
 */
LPVOID FileReadAll(LPCSTR Name, UINT *Size) {
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
UINT FileWriteAll(LPCSTR Name, LPCVOID Buffer, UINT Size) {
    FILEOPENINFO OpenInfo;
    FILEOPERATION FileOp;
    LPFILE File = NULL;
    UINT BytesWritten = 0;

    DEBUG(TEXT("[FileWriteAll] name %s, size %u"), Name, Size);

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
