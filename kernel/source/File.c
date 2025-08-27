
// File.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\***************************************************************************/

#include "../include/File.h"

#include "../include/Heap.h"
#include "../include/Kernel.h"
#include "../include/Process.h"

/***************************************************************************/

LPFILE OpenFile(LPFILEOPENINFO Info) {
    STR Volume[MAX_FS_LOGICAL_NAME];
    FILEINFO Find;
    LPFILESYSTEM FileSystem = NULL;
    LPLISTNODE Node = NULL;
    LPFILE File = NULL;
    LPFILE AlreadyOpen = NULL;
    LPCSTR Colon = NULL;
    U32 FoundFileSystem;
    U32 Index;

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

        if (StringCompare(AlreadyOpen->Name, Info->Name) == 0) {
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
        Find.FileSystem = Kernel.SystemFS;
        Find.Attributes = MAX_U32;
        StringCopy(Find.Name, Info->Name);

        File =
            (LPFILE)Kernel.SystemFS->Driver->Command(DF_FS_OPENFILE, (U32)&Find);

        if (File != NULL) {
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

    Volume[0] = STR_NULL;

    for (Index = 0; Index < MAX_FS_LOGICAL_NAME - 1; Index++) {
        if (Info->Name[Index] == STR_NULL) break;
        if (Info->Name[Index] == STR_COLON) {
            Colon = Info->Name + Index;
            break;
        }
        Volume[Index + 0] = Info->Name[Index];
        Volume[Index + 1] = STR_NULL;
    }

    if (Colon == NULL) {
        for (Node = Kernel.FileSystem->First; Node; Node = Node->Next) {
            FileSystem = (LPFILESYSTEM)Node;

            Find.Size = sizeof Find;
            Find.FileSystem = FileSystem;
            Find.Attributes = MAX_U32;
            StringCopy(Find.Name, Info->Name);

            File = (LPFILE)FileSystem->Driver->Command(DF_FS_OPENFILE, (U32)&Find);
            if (File != NULL) {
                LockMutex(MUTEX_FILE, INFINITY);

                File->OwnerTask = GetCurrentTask();
                File->OpenFlags = Info->Flags;

                ListAddItem(Kernel.File, File);

                UnlockMutex(MUTEX_FILE);
                break;
            }
        }

        goto Out;
    }

    if (Colon[0] != ':') goto Out;
    if (Colon[1] != '/') goto Out;

    //-------------------------------------
    // Find the volume in the registered file systems

    FoundFileSystem = 0;

    for (Node = Kernel.FileSystem->First; Node; Node = Node->Next) {
        FileSystem = (LPFILESYSTEM)Node;
        if (StringCompare(FileSystem->Name, Volume) == 0) {
            FoundFileSystem = 1;
            break;
        }
    }

    if (FoundFileSystem == 0) goto Out;

    //-------------------------------------
    // Fill the file system driver structure

    Find.Size = sizeof Find;
    Find.FileSystem = FileSystem;
    Find.Attributes = MAX_U32;

    StringCopy(Find.Name, Colon + 2);

    //-------------------------------------
    // Open the file

    File = (LPFILE)FileSystem->Driver->Command(DF_FS_OPENFILE, (U32)&Find);

    if (File != NULL) {
        LockMutex(MUTEX_FILE, INFINITY);

        File->OwnerTask = GetCurrentTask();
        File->OpenFlags = Info->Flags;

        ListAddItem(Kernel.File, File);

        UnlockMutex(MUTEX_FILE);
    }

Out:

    UnlockMutex(MUTEX_FILESYSTEM);

    return File;
}

/***************************************************************************/

U32 CloseFile(LPFILE File) {
    //-------------------------------------
    // Check validity of parameters

    if (File->ID != ID_FILE) return 0;

    LockMutex(&(File->Mutex), INFINITY);

    if (File->References) File->References--;

    if (File->References == 0) {
        // File->ID = ID_NONE;
        // ListEraseItem(Kernel.File, File);

        File->FileSystem->Driver->Command(DF_FS_CLOSEFILE, (U32)File);

        ListRemove(Kernel.File, File);
    } else {
        UnlockMutex(&(File->Mutex));
    }

    return 1;
}

/***************************************************************************/

U32 GetFilePosition(LPFILE File) {
    U32 Position = 0;

    SAFE_USE_VALID_ID(File, ID_FILE) {
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

U32 SetFilePosition(LPFILEOPERATION Operation) {
    SAFE_USE_VALID(Operation) {
        LPFILE File = (LPFILE)Operation->File;

        SAFE_USE_VALID_ID(File, ID_FILE) {
            //-------------------------------------
            // Lock access to the file

            LockMutex(&(File->Mutex), INFINITY);

            File->Position = Operation->NumBytes;

            //-------------------------------------
            // Unlock access to the file

            UnlockMutex(&(File->Mutex));
        }
    }

    return SUCCESS;
}

/***************************************************************************/

U32 ReadFile(LPFILEOPERATION FileOp) {
    LPFILE File = NULL;
    U32 Result = 0;
    U32 BytesRead = 0;

    //-------------------------------------
    // Check validity of parameters

    if (FileOp == NULL) return 0;
    if (FileOp->File == NULL) return 0;

    File = (LPFILE)FileOp->File;
    if (File->ID != ID_FILE) return 0;

    if ((File->OpenFlags & FILE_OPEN_READ) == 0) return 0;

    //-------------------------------------
    // Lock access to the file

    LockMutex(&(File->Mutex), INFINITY);

    File->BytesToRead = FileOp->NumBytes;
    File->Buffer = FileOp->Buffer;

    Result = File->FileSystem->Driver->Command(DF_FS_READ, (U32)File);

    if (Result == DF_ERROR_SUCCESS) {
        // File->Position += File->BytesRead;
        BytesRead = File->BytesRead;
    }

    UnlockMutex(&(File->Mutex));

    return BytesRead;
}

/***************************************************************************/

U32 WriteFile(LPFILEOPERATION FileOp) {
    LPFILE File = NULL;
    U32 Result = 0;
    U32 BytesWritten = 0;

    //-------------------------------------
    // Check validity of parameters

    if (FileOp == NULL) return 0;
    if (FileOp->File == NULL) return 0;

    File = (LPFILE)FileOp->File;
    if (File->ID != ID_FILE) return 0;

    if ((File->OpenFlags & FILE_OPEN_WRITE) == 0) return 0;

    //-------------------------------------
    // Lock access to the file

    LockMutex(&(File->Mutex), INFINITY);

    File->BytesToRead = FileOp->NumBytes;
    File->Buffer = FileOp->Buffer;

    Result = File->FileSystem->Driver->Command(DF_FS_WRITE, (U32)File);

    if (Result == DF_ERROR_SUCCESS) {
        // File->Position += File->BytesRead;
        BytesWritten = File->BytesRead;
    }

    UnlockMutex(&(File->Mutex));

    return BytesWritten;
}

/***************************************************************************/

U32 GetFileSize(LPFILE File) {
    U32 Size = 0;

    //-------------------------------------
    // Check validity of parameters

    if (File == NULL) return 0;
    if (File->ID != ID_FILE) return 0;

    LockMutex(&(File->Mutex), INFINITY);

    Size = File->SizeLow;

    UnlockMutex(&(File->Mutex));

    return Size;
}

/***************************************************************************/

LPVOID FileReadAll(LPCSTR Name, U32 *Size) {
    FILEOPENINFO OpenInfo;
    FILEOPERATION FileOp;
    LPFILE File = NULL;
    LPVOID Buffer = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (Name == NULL) return NULL;
    if (Size == NULL) return NULL;

    //-------------------------------------
    // Open the file

    OpenInfo.Header.Size = sizeof(FILEOPENINFO);
    OpenInfo.Name = (LPSTR)Name;
    OpenInfo.Flags = FILE_OPEN_READ;
    File = OpenFile(&OpenInfo);

    if (File == NULL) return NULL;

    //-------------------------------------
    // Allocate buffer and read content

    *Size = GetFileSize(File);
    Buffer = HeapAlloc(*Size + 1);

    if (Buffer != NULL) {
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

/***************************************************************************/

U32 FileWriteAll(LPCSTR Name, LPCVOID Buffer, U32 Size) {
    FILEOPENINFO OpenInfo;
    FILEOPERATION FileOp;
    LPFILE File = NULL;
    U32 BytesWritten = 0;

    //-------------------------------------
    // Check validity of parameters

    if (Name == NULL) return 0;
    if (Buffer == NULL) return 0;

    //-------------------------------------
    // Open the file

    OpenInfo.Header.Size = sizeof(FILEOPENINFO);
    OpenInfo.Name = (LPSTR)Name;
    OpenInfo.Flags = FILE_OPEN_WRITE | FILE_OPEN_CREATE_ALWAYS | FILE_OPEN_TRUNCATE;
    File = OpenFile(&OpenInfo);

    if (File == NULL) return 0;

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

/***************************************************************************/
