
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

static LPSYSTEMFILE NewSystemFileRoot(void) { return NULL; }

/***************************************************************************/

static LPSYSFSFILESYSTEM NewSystemFSFileSystem(void) {
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

BOOL MountSystemFS(void) {
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

static U32 Initialize(void) { return DF_ERROR_SUCCESS; }

/***************************************************************************/

static LPSYSFSFILE OpenFile(LPFILEINFO Find) {
    UNUSED(Find);
    return NULL;
}

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
