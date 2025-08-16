
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/FileSys.h"
#include "../include/Kernel.h"
#include "../include/Log.h"

/***************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

U32 SystemFSCommands(U32, U32);

DRIVER SystemFSDriver = {
    .ID = ID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_FILESYSTEM,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "Virtual Computer File System",
    .Command = SystemFSCommands};

/***************************************************************************/

typedef struct tag_SYSTEMFILE {
    LISTNODE_FIELDS
    LPLIST Children;
} SYSTEMFILE, *LPSYSTEMFILE;

/***************************************************************************/
// The file system object allocated when mounting

typedef struct tag_SYSFSFILESYSTEM {
    FILESYSTEM Header;
    LPSYSTEMFILE Root;
} SYSFSFILESYSTEM, *LPSYSFSFILESYSTEM;

/***************************************************************************/
// The file object created when opening a file

typedef struct tag_SYSFSFILE {
    FILE Header;
    LPSYSTEMFILE SystemFile;
    LPSYSTEMFILE Parent;
} SYSFSFILE, *LPSYSFSFILE;

/***************************************************************************/

static LPSYSTEMFILE NewSystemFileRoot() { return NULL; }

/***************************************************************************/

static LPSYSFSFILESYSTEM NewSystemFSFileSystem() {
    LPSYSFSFILESYSTEM This;

    This = (LPSYSFSFILESYSTEM)KernelMemAlloc(sizeof(SYSFSFILESYSTEM));
    if (This == NULL) return NULL;

    *This = (SYSFSFILESYSTEM){
        .Header =
            {.ID = ID_FILESYSTEM,
             .References = 1,
             .Next = NULL,
             .Prev = NULL,
             .Mutex = EMPTY_MUTEX,
             .Driver = &SystemFSDriver,
             .Name = "System"},
        .Root = NewSystemFileRoot()};

    InitMutex(&(This->Header.Mutex));

    return This;
}

/***************************************************************************/

/*
static LPSYSFSFILE NewSysFSFile(LPSYSFSFILESYSTEM FileSystem,
                                LPSYSTEMFILE SystemFile, LPSYSTEMFILE Parent) {
    LPSYSFSFILE This;

    This = (LPSYSFSFILE)KernelMemAlloc(sizeof(SYSFSFILE));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(SYSFSFILE));

    This->Header.ID = ID_FILE;
    This->Header.References = 1;
    This->Header.Next = NULL;
    This->Header.Prev = NULL;
    This->Header.FileSystem = (LPFILESYSTEM)FileSystem;
    This->SystemFile = SystemFile;
    This->Parent = Parent;

    InitMutex(&(This->Header.Mutex));
    InitSecurity(&(This->Header.Security));

    return This;
}
*/

/***************************************************************************/

BOOL MountSystemFS() {
    LPSYSFSFILESYSTEM FileSystem;

    KernelLogText(LOG_VERBOSE, TEXT("[MountSystemFS] Mouting system FileSystem"));

    //-------------------------------------
    // Create the file system object

    FileSystem = NewSystemFSFileSystem();
    if (FileSystem == NULL) return FALSE;

    //-------------------------------------
    // Register the file system

    ListAddItem(Kernel.FileSystem, FileSystem);

    return TRUE;
}

/***************************************************************************/

/*
static BOOL LocateFile
(
  LPFATFILESYSTEM FileSystem,
  LPCSTR Path,
  LPFATFILELOC FileLoc
)
{
  STR Component [MAX_FILE_NAME];
  STR Name [MAX_FILE_NAME];
  LPFATDIRENTRY_EXT DirEntry;
  U32 PathIndex = 0;
  U32 CompIndex = 0;

  FileLoc->PreviousCluster = 0;
  FileLoc->FolderCluster   = FileSystem->Master.RootCluster;
  FileLoc->FileCluster     = FileLoc->FolderCluster;
  FileLoc->Offset          = 0;
  FileLoc->DataCluster     = 0;

  if (!ReadCluster(FileSystem, FileLoc->FileCluster, FileSystem->IOBuffer))
  {
    return FALSE;
  }

  while (1)
  {
    //-------------------------------------
    // Parse the next component to look for

NextComponent :

    CompIndex = 0;

    while (1)
    {
      if (Path[PathIndex] == '/')
      {
    Component[CompIndex] = STR_NULL;
    PathIndex++;
    break;
      }
      else
      if (Path[PathIndex] == STR_NULL)
      {
    Component[CompIndex] = STR_NULL;
    break;
      }
      else
      {
    Component[CompIndex++] = Path[PathIndex++];
      }
    }

    //-------------------------------------
    // Loop through all directory entries

    while (1)
    {
      DirEntry = (LPFATDIRENTRY_EXT) (FileSystem->IOBuffer +
FileLoc->Offset);

      if
      (
    (DirEntry->ClusterLow || DirEntry->ClusterHigh) &&
    (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 &&
    (DirEntry->Name[0] != 0xE5)
      )
      {
    DecodeFileName(DirEntry, Name);

    if
    (
      StringCompareNC(Component, "*") == 0 ||
      StringCompareNC(Component, Name) == 0
    )
    {
      if (Path[PathIndex] == STR_NULL)
      {
        FileLoc->DataCluster =
        (
          ((U32) DirEntry->ClusterLow) |
          (((U32) DirEntry->ClusterHigh) << 16)
        );

        return TRUE;
      }
      else
      {
        if (DirEntry->Attributes & FAT_ATTR_FOLDER)
        {
          U32 NextDir;

          NextDir = (U32) DirEntry->ClusterLow;
          NextDir |= ((U32) DirEntry->ClusterHigh) << 16;

          FileLoc->FolderCluster = NextDir;
          FileLoc->FileCluster   = FileLoc->FolderCluster;
          FileLoc->Offset        = 0;

          if
          (
        ReadCluster
        (
          FileSystem,
          FileLoc->FileCluster,
          FileSystem->IOBuffer
        ) == FALSE
          ) return FALSE;

          goto NextComponent;
        }
        else
        {
          return FALSE;
        }
      }
    }
      }

      //-------------------------------------
      // Advance to the next entry

      FileLoc->Offset += sizeof(FATDIRENTRY_EXT);

      if (FileLoc->Offset >= FileSystem->BytesPerCluster)
      {
    FileLoc->Offset = 0;
    FileLoc->FileCluster = GetNextClusterInChain
    (
      FileSystem,
      FileLoc->FileCluster
    );

    if
    (
      FileLoc->FileCluster == 0 ||
      FileLoc->FileCluster >= FAT32_CLUSTER_RESERVED
    ) return FALSE;

    if
    (
      ReadCluster
      (
        FileSystem,
        FileLoc->FileCluster,
        FileSystem->IOBuffer
      ) == FALSE
    ) return FALSE;
      }
    }
  }
}
*/

/***************************************************************************/

static U32 Initialize() { return DF_ERROR_SUCCESS; }

/***************************************************************************/

/*
static LPSYSFSFILE OpenFile (LPFILEINFO Find)
{
  LPFATFILESYSTEM   FileSystem = NULL;
  LPFATFILE         File       = NULL;
  LPFATDIRENTRY_EXT DirEntry   = NULL;
  FATFILELOC        FileLoc;

  if (Find == NULL) return NULL;

  FileSystem = (LPSYSFSFILESYSTEM) Find->FileSystem;

  if (LocateFile(FileSystem, Find->Name, &FileLoc) == TRUE)
  {
    if (ReadCluster(FileSystem, FileLoc.FileCluster, FileSystem->IOBuffer)
    == FALSE) return FALSE;

    DirEntry = (LPFATDIRENTRY_EXT) (FileSystem->IOBuffer + FileLoc.Offset);

    File = NewFATFile(FileSystem, &FileLoc);
    if (File == NULL) return NULL;

    DecodeFileName(DirEntry, File->Header.Name);
    TranslateFileInfo(DirEntry, File);
  }

  return File;
}
*/

static LPSYSFSFILE OpenFile(LPFILEINFO Find) {
    UNUSED(Find);
    return NULL;
}

/***************************************************************************/

/*
static U32 OpenNext (LPSYSFSFILE File)
{
  LPSYSFSFILESYSTEM FileSystem = NULL;
  // LPFATDIRENTRY_EXT DirEntry   = NULL;

  //-------------------------------------
  // Check validity of parameters

  if (File == NULL) return DF_ERROR_BADPARAM;
  if (File->Header.ID != ID_FILE) return DF_ERROR_BADPARAM;

  //-------------------------------------
  // Get the associated file system

  FileSystem = (LPFATFILESYSTEM) File->Header.FileSystem;

  //-------------------------------------
  // Read the cluster containing the file

  if
  (
    ReadCluster
    (
      FileSystem,
      File->Location.FileCluster,
      FileSystem->IOBuffer
    ) == FALSE
  ) return FALSE;

  while (1)
  {
    File->Location.Offset += sizeof(FATDIRENTRY_EXT);

    if (File->Location.Offset >= FileSystem->BytesPerCluster)
    {
      File->Location.Offset = 0;

      File->Location.FileCluster =
      GetNextClusterInChain(FileSystem, File->Location.FileCluster);

      if
      (
    File->Location.FileCluster == 0 ||
    File->Location.FileCluster >= FAT32_CLUSTER_RESERVED
      ) return DF_ERROR_GENERIC;

      if
      (
    ReadCluster
    (
      FileSystem,
      File->Location.FileCluster,
      FileSystem->IOBuffer
    ) == FALSE
      ) return DF_ERROR_GENERIC;
    }

    DirEntry = (LPFATDIRENTRY_EXT) (FileSystem->IOBuffer +
File->Location.Offset);

    if
    (
      (DirEntry->ClusterLow || DirEntry->ClusterHigh) &&
      (DirEntry->Attributes & FAT_ATTR_VOLUME) == 0 &&
      (DirEntry->Name[0] != 0xE5)
    )
    {
      File->Location.DataCluster =
      (
    ((U32) DirEntry->ClusterLow) |
    (((U32) DirEntry->ClusterHigh) << 16)
      );

      DecodeFileName(DirEntry, File->Header.Name);
      TranslateFileInfo(DirEntry, File);
      break;
    }
  }

  return DF_ERROR_SUCCESS;
}
*/

/***************************************************************************/

static U32 OpenNext(LPSYSFSFILE File) {
    UNUSED(File);
    return DF_ERROR_GENERIC;
}

/***************************************************************************/

static U32 CloseFile(LPSYSFSFILE File) {
    if (File == NULL) return DF_ERROR_BADPARAM;

    KernelMemFree(File);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 ReadFile(LPSYSFSFILE File) {
    UNUSED(File);

    /*
    LPSYSFSFILESYSTEM FileSystem;

      CLUSTER RelativeCluster;
      CLUSTER Cluster;
      U32 OffsetInCluster;
      U32 BytesRemaining;
      U32 BytesToRead;
      U32 Index;

      //-------------------------------------
      // Check validity of parameters

      if (File == NULL) return DF_ERROR_BADPARAM;
      if (File->Header.ID != ID_FILE) return DF_ERROR_BADPARAM;
      if (File->Header.Buffer == NULL) return DF_ERROR_BADPARAM;

      //-------------------------------------
      // Get the associated file system

      FileSystem = (LPFATFILESYSTEM) File->Header.FileSystem;

      //-------------------------------------
      // Compute the starting cluster and the offset

      RelativeCluster = File->Header.Position / FileSystem->BytesPerCluster;
      OffsetInCluster = File->Header.Position % FileSystem->BytesPerCluster;
      BytesRemaining  = File->Header.BytesToRead;
      File->Header.BytesRead = 0;

      Cluster = File->Location.DataCluster;

      for (Index = 0; Index < RelativeCluster; Index++)
      {
    Cluster = GetNextClusterInChain(FileSystem, Cluster);
    if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED)
    {
      return DF_ERROR_IO;
    }
      }

      while (1)
      {
    //-------------------------------------
    // Read the current data cluster

    if (ReadCluster(FileSystem, Cluster, FileSystem->IOBuffer) == FALSE)
    {
      return DF_ERROR_IO;
    }

    BytesToRead = FileSystem->BytesPerCluster - OffsetInCluster;
    if (BytesToRead > BytesRemaining) BytesToRead = BytesRemaining;

    //-------------------------------------
    // Copy the data to the user buffer

    MemoryCopy
    (
      ((U8*) File->Header.Buffer) + File->Header.BytesRead,
      FileSystem->IOBuffer + OffsetInCluster,
      BytesToRead
    );

    //-------------------------------------
    // Update counters

    OffsetInCluster         = 0;
    BytesRemaining         -= BytesToRead;
    File->Header.BytesRead += BytesToRead;
    File->Header.Position  += BytesToRead;

    //-------------------------------------
    // Check if we read all data

    if (BytesRemaining == 0) break;

    //-------------------------------------
    // Get the next cluster in the chain

    Cluster = GetNextClusterInChain(FileSystem, Cluster);

    if (Cluster == 0 || Cluster >= FAT32_CLUSTER_RESERVED)
    {
      break;
    }
      }
    */

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 WriteFile(LPSYSFSFILE File) {
    UNUSED(File);
    return DF_ERROR_NOTIMPL;
}

/***************************************************************************/

U32 SystemFSCommands(U32 Function, U32 Parameter) {
    switch (Function) {
        case DF_LOAD:
            return Initialize();
        case DF_GETVERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_FS_GETVOLUMEINFO:
            return DF_ERROR_NOTIMPL;
        case DF_FS_SETVOLUMEINFO:
            return DF_ERROR_NOTIMPL;
        case DF_FS_CREATEFOLDER:
            return DF_ERROR_NOTIMPL;
        case DF_FS_DELETEFOLDER:
            return DF_ERROR_NOTIMPL;
        case DF_FS_OPENFILE:
            return (U32)OpenFile((LPFILEINFO)Parameter);
        case DF_FS_OPENNEXT:
            return (U32)OpenNext((LPSYSFSFILE)Parameter);
        case DF_FS_CLOSEFILE:
            return (U32)CloseFile((LPSYSFSFILE)Parameter);
        case DF_FS_DELETEFILE:
            return DF_ERROR_NOTIMPL;
        case DF_FS_READ:
            return (U32)ReadFile((LPSYSFSFILE)Parameter);
        case DF_FS_WRITE:
            return (U32)WriteFile((LPSYSFSFILE)Parameter);
        case DF_FS_GETPOSITION:
            return DF_ERROR_NOTIMPL;
        case DF_FS_SETPOSITION:
            return DF_ERROR_NOTIMPL;
        case DF_FS_GETATTRIBUTES:
            return DF_ERROR_NOTIMPL;
        case DF_FS_SETATTRIBUTES:
            return DF_ERROR_NOTIMPL;
    }

    return DF_ERROR_NOTIMPL;
}

/***************************************************************************/
