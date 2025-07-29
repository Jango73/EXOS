
// File.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#include "Process.h"
#include "Kernel.h"
#include "File.h"

/***************************************************************************/

LPFILE OpenFile (LPFILEOPENINFO Info)
{
  STR          Volume [MAX_FS_LOGICAL_NAME];
  FILEINFO     Find;
  LPFILESYSTEM FileSystem  = NULL;
  LPLISTNODE   Node        = NULL;
  LPFILE       File        = NULL;
  LPFILE       AlreadyOpen = NULL;
  LPCSTR       Colon       = NULL;
  U32          FoundFileSystem;
  U32          Index;

  //-------------------------------------
  // Check validity of parameters

  if (Info == NULL) return NULL;

  //-------------------------------------
  // Lock access to file systems

  LockSemaphore(SEMAPHORE_FILESYSTEM, INFINITY);

  //-------------------------------------
  // Check if the file is already open

  LockSemaphore(SEMAPHORE_FILE, INFINITY);

  for (Node = Kernel.File->First; Node; Node = Node->Next)
  {
    LockSemaphore(&(AlreadyOpen->Semaphore), INFINITY);		// ???????????????

    AlreadyOpen = (LPFILE) Node;

    if (StringCompareNC(AlreadyOpen->Name, Info->Name) == 0)
    {
      if (AlreadyOpen->OwnerTask == GetCurrentTask())
      {
        if (AlreadyOpen->OpenFlags == Info->Flags)
        {
          File = AlreadyOpen;
          File->References++;

          UnlockSemaphore(&(AlreadyOpen->Semaphore));
          UnlockSemaphore(SEMAPHORE_FILE);
          goto Out;
        }
      }
    }

    UnlockSemaphore(&(AlreadyOpen->Semaphore));
  }

  UnlockSemaphore(SEMAPHORE_FILE);

  //-------------------------------------
  // Get the name of the volume in which the file
  // is supposed to be located

  Volume[0] = STR_NULL;

  for (Index = 0; Index < MAX_FS_LOGICAL_NAME - 1; Index++)
  {
    if (Info->Name[Index] == STR_NULL) break;
    if (Info->Name[Index] == STR_COLON)
    {
      Colon = Info->Name + Index;
      break;
    }
    Volume[Index + 0] = Info->Name[Index];
    Volume[Index + 1] = STR_NULL;
  }

  if (Colon == NULL) goto Out;

  if (Colon [0] != ':') goto Out;
  if (Colon [1] != '/') goto Out;

  //-------------------------------------
  // Find the volume in the registered file systems

  FoundFileSystem = 0;

  for (Node = Kernel.FileSystem->First; Node; Node = Node->Next)
  {
    FileSystem = (LPFILESYSTEM) Node;
    if (StringCompareNC(FileSystem->Name, Volume) == 0)
    {
      FoundFileSystem = 1;
      break;
    }
  }

  if (FoundFileSystem == 0) goto Out;

  //-------------------------------------
  // Fill the file system driver structure

  Find.Size       = sizeof Find;
  Find.FileSystem = FileSystem;
  Find.Attributes = MAX_U32;

  StringCopy(Find.Name, Colon + 2);

  //-------------------------------------
  // Open the file

  File = (LPFILE) FileSystem->Driver->Command(DF_FS_OPENFILE, (U32) &Find);

  if (File != NULL)
  {
    LockSemaphore(SEMAPHORE_FILE, INFINITY);

    File->OwnerTask = GetCurrentTask();
    File->OpenFlags = Info->Flags;

    ListAddItem(Kernel.File, File);

    UnlockSemaphore(SEMAPHORE_FILE);
  }

Out :

  UnlockSemaphore(SEMAPHORE_FILESYSTEM);

  return File;
}

/***************************************************************************/

U32 CloseFile (LPFILE File)
{
  //-------------------------------------
  // Check validity of parameters

  if (File->ID != ID_FILE) return 0;

  LockSemaphore(&(File->Semaphore), INFINITY);

  if (File->References) File->References--;

  if (File->References == 0)
  {
    // File->ID = ID_NONE;
    // ListEraseItem(Kernel.File, File);

    File->FileSystem->Driver->Command(DF_FS_CLOSEFILE, (U32) File);

    ListRemove(Kernel.File, File);
  }
  else
  {
    UnlockSemaphore(&(File->Semaphore));
  }

  return 1;
}

/***************************************************************************/

U32 ReadFile (LPFILEOPERATION FileOp)
{
  LPFILE File      = NULL;
  U32    Result    = 0;
  U32    BytesRead = 0;

  //-------------------------------------
  // Check validity of parameters

  if (FileOp == NULL) return 0;
  if (FileOp->File == NULL) return 0;

  File = (LPFILE) FileOp->File;
  if (File->ID != ID_FILE) return 0;

  if ((File->OpenFlags & FILE_OPEN_READ) == 0) return 0;

  //-------------------------------------
  // Lock access to the file

  LockSemaphore(&(File->Semaphore), INFINITY);

  File->BytesToRead = FileOp->NumBytes;
  File->Buffer      = FileOp->Buffer;

  Result = File->FileSystem->Driver->Command(DF_FS_READ, (U32) File);

  if (Result == DF_ERROR_SUCCESS)
  {
    // File->Position += File->BytesRead;
    BytesRead = File->BytesRead;
  }

Out :

  UnlockSemaphore(&(File->Semaphore));

  return BytesRead;
}

/***************************************************************************/

U32 WriteFile (LPFILEOPERATION FileOp)
{
  LPFILE File         = NULL;
  U32    Result       = 0;
  U32    BytesWritten = 0;

  //-------------------------------------
  // Check validity of parameters

  if (FileOp == NULL) return 0;
  if (FileOp->File == NULL) return 0;

  File = (LPFILE) FileOp->File;
  if (File->ID != ID_FILE) return 0;

  if ((File->OpenFlags & FILE_OPEN_WRITE) == 0) return 0;

  //-------------------------------------
  // Lock access to the file

  LockSemaphore(&(File->Semaphore), INFINITY);

  File->BytesToRead = FileOp->NumBytes;
  File->Buffer      = FileOp->Buffer;

  Result = File->FileSystem->Driver->Command(DF_FS_WRITE, (U32) File);

  if (Result == DF_ERROR_SUCCESS)
  {
    // File->Position += File->BytesRead;
    BytesWritten = File->BytesRead;
  }

Out :

  UnlockSemaphore(&(File->Semaphore));

  return BytesWritten;
}

/***************************************************************************/

U32 GetFileSize (LPFILE File)
{
  U32 Size = 0;

  //-------------------------------------
  // Check validity of parameters

  if (File == NULL) return 0;
  if (File->ID != ID_FILE) return 0;

  LockSemaphore(&(File->Semaphore), INFINITY);

  Size = File->SizeLow;

  UnlockSemaphore(&(File->Semaphore));

  return Size;
}

/***************************************************************************/
