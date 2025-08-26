
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/FileSys.h"
#include "../include/Kernel.h"
#include "../include/List.h"
#include "../include/Log.h"
#include "../include/String.h"
#include "../include/User.h"

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

static LPSYSFSFILESYSTEM g_SystemFS = NULL;

typedef struct tag_SYSTEMFILE {
    LISTNODE_FIELDS
    LPLIST Children;
    LPSYSTEMFILE Parent;
    LPFILESYSTEM Mounted;
    STR Name[MAX_FILE_NAME];
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

static LPSYSTEMFILE NewSystemFile(LPCSTR Name, LPSYSTEMFILE Parent) {
    LPSYSTEMFILE Node;

    Node = (LPSYSTEMFILE)KernelMemAlloc(sizeof(SYSTEMFILE));
    if (Node == NULL) return NULL;

    *Node = (SYSTEMFILE){
        .ID = ID_FILE,
        .References = 1,
        .Next = NULL,
        .Prev = NULL,
        .Children = NewList(NULL, KernelMemAlloc, KernelMemFree),
        .Parent = Parent,
        .Mounted = NULL};

    if (Name) {
        StringCopy(Node->Name, Name);
    } else {
        Node->Name[0] = STR_NULL;
    }

    return Node;
}

static LPSYSTEMFILE NewSystemFileRoot(void) { return NewSystemFile(TEXT(""), NULL); }

/***************************************************************************/

static LPSYSTEMFILE FindChild(LPSYSTEMFILE Parent, LPCSTR Name) {
    LPLISTNODE Node;
    LPSYSTEMFILE Child;

    if (Parent == NULL || Parent->Children == NULL) return NULL;

    for (Node = Parent->Children->First; Node; Node = Node->Next) {
        Child = (LPSYSTEMFILE)Node;
        if (StringCompareNC(Child->Name, Name) == 0) return Child;
    }

    return NULL;
}

static LPSYSTEMFILE FindNode(LPCSTR Path) {
    LPLIST Parts;
    LPLISTNODE Node;
    LPPATHNODE Part;
    LPSYSTEMFILE Current;

    if (g_SystemFS == NULL) return NULL;

    Parts = DecompPath(Path);
    if (Parts == NULL) return NULL;

    Current = g_SystemFS->Root;
    for (Node = Parts->First; Node; Node = Node->Next) {
        Part = (LPPATHNODE)Node;
        if (Part->Name[0] == STR_NULL) continue;
        Current = FindChild(Current, Part->Name);
        if (Current == NULL) break;
    }

    DeleteList(Parts);
    return Current;
}

/***************************************************************************/

static U32 MountObject(LPFS_MOUNT_CONTROL Control) {
    LPLIST Parts;
    LPLISTNODE Node;
    LPPATHNODE Part;
    LPSYSTEMFILE Parent;
    LPSYSTEMFILE Child;

    if (g_SystemFS == NULL || Control == NULL) return DF_ERROR_BADPARAM;

    Parts = DecompPath(Control->Path);
    if (Parts == NULL) return DF_ERROR_BADPARAM;

    Parent = g_SystemFS->Root;
    for (Node = Parts->First; Node; Node = Node->Next) {
        Part = (LPPATHNODE)Node;
        if (Part->Name[0] == STR_NULL) continue;
        if (Node->Next == NULL) break;
        Child = FindChild(Parent, Part->Name);
        if (Child == NULL) {
            Child = NewSystemFile(Part->Name, Parent);
            if (Child == NULL) {
                DeleteList(Parts);
                return DF_ERROR_GENERIC;
            }
            ListAddTail(Parent->Children, Child);
        }
        Parent = Child;
    }

    if (Part == NULL || Control->Node == NULL) {
        DeleteList(Parts);
        return DF_ERROR_BADPARAM;
    }

    if (FindChild(Parent, Part->Name)) {
        DeleteList(Parts);
        return DF_ERROR_GENERIC;
    }

    Child = NewSystemFile(Part->Name, Parent);
    if (Child == NULL) {
        DeleteList(Parts);
        return DF_ERROR_GENERIC;
    }

    Child->Mounted = (LPFILESYSTEM)Control->Node;
    ListAddTail(Parent->Children, Child);

    DeleteList(Parts);
    return DF_ERROR_SUCCESS;
}

static U32 UnmountObject(LPFS_UNMOUNT_CONTROL Control) {
    LPSYSTEMFILE Node;

    if (Control == NULL) return DF_ERROR_BADPARAM;

    Node = FindNode(Control->Path);
    if (Node == NULL || Node->Parent == NULL) return DF_ERROR_GENERIC;

    ListErase(Node->Parent->Children, Node);
    KernelMemFree(Node);
    return DF_ERROR_SUCCESS;
}

static BOOL PathExists(LPFS_PATHCHECK Control) {
    STR Temp[MAX_PATH_NAME];
    LPSYSTEMFILE Node;

    if (Control == NULL) return FALSE;

    if (Control->SubFolder[0] == PATH_SEP) {
        StringCopy(Temp, Control->SubFolder);
    } else {
        StringCopy(Temp, Control->CurrentFolder);
        if (Temp[StringLength(Temp) - 1] != PATH_SEP)
            StringConcat(Temp, TEXT("/"));
        StringConcat(Temp, Control->SubFolder);
    }

    Node = FindNode(Temp);
    return (Node != NULL);
}

/***************************************************************************/

static U32 CreateFolderFS(LPFILEINFO Info) {
    LPLIST Parts;
    LPLISTNODE Node;
    LPPATHNODE Part;
    LPSYSTEMFILE Parent;
    LPSYSTEMFILE Child;

    if (Info == NULL) return DF_ERROR_BADPARAM;

    Parts = DecompPath(Info->Name);
    if (Parts == NULL) return DF_ERROR_BADPARAM;

    Parent = g_SystemFS->Root;
    for (Node = Parts->First; Node; Node = Node->Next) {
        Part = (LPPATHNODE)Node;
        if (Part->Name[0] == STR_NULL) continue;
        if (Node->Next == NULL) break;
        Child = FindChild(Parent, Part->Name);
        if (Child == NULL) {
            Child = NewSystemFile(Part->Name, Parent);
            if (Child == NULL) {
                DeleteList(Parts);
                return DF_ERROR_GENERIC;
            }
            ListAddTail(Parent->Children, Child);
        }
        Parent = Child;
    }

    if (Part == NULL) {
        DeleteList(Parts);
        return DF_ERROR_BADPARAM;
    }

    if (FindChild(Parent, Part->Name)) {
        DeleteList(Parts);
        return DF_ERROR_GENERIC;
    }

    Child = NewSystemFile(Part->Name, Parent);
    if (Child == NULL) {
        DeleteList(Parts);
        return DF_ERROR_GENERIC;
    }

    ListAddTail(Parent->Children, Child);
    DeleteList(Parts);
    return DF_ERROR_SUCCESS;
}

static U32 DeleteFolderFS(LPFILEINFO Info) {
    LPSYSTEMFILE Node;

    if (Info == NULL) return DF_ERROR_BADPARAM;

    Node = FindNode(Info->Name);
    if (Node == NULL || Node->Parent == NULL) return DF_ERROR_GENERIC;
    if (Node->Children && Node->Children->NumItems) return DF_ERROR_GENERIC;

    ListErase(Node->Parent->Children, Node);
    KernelMemFree(Node);
    return DF_ERROR_SUCCESS;
}

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

    g_SystemFS = FileSystem;

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
        case DF_FS_GETVOLUMEINFO: {
            LPVOLUMEINFO Info = (LPVOLUMEINFO)Parameter;
            if (Info && Info->Size == sizeof(VOLUMEINFO)) {
                StringCopy(Info->Name, TEXT("/"));
                return DF_ERROR_SUCCESS;
            }
            return DF_ERROR_BADPARAM;
        }
        case DF_FS_SETVOLUMEINFO:
            return DF_ERROR_NOTIMPL;
        case DF_FS_CREATEFOLDER:
            return CreateFolderFS((LPFILEINFO)Parameter);
        case DF_FS_DELETEFOLDER:
            return DeleteFolderFS((LPFILEINFO)Parameter);
        case DF_FS_MOUNTOBJECT:
            return MountObject((LPFS_MOUNT_CONTROL)Parameter);
        case DF_FS_UNMOUNTOBJECT:
            return UnmountObject((LPFS_UNMOUNT_CONTROL)Parameter);
        case DF_FS_PATHEXISTS:
            return (U32)PathExists((LPFS_PATHCHECK)Parameter);
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
