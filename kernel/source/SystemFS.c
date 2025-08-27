
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/FileSystem.h"
#include "../include/Kernel.h"
#include "../include/List.h"
#include "../include/Log.h"
#include "../include/String.h"
#include "../include/TOML.h"
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

typedef struct tag_SYSTEMFSFILE SYSTEMFSFILE, *LPSYSTEMFSFILE;

struct tag_SYSTEMFSFILE {
    LISTNODE_FIELDS
    LPLIST Children;
    LPSYSTEMFSFILE Parent;
    LPFILESYSTEM Mounted;
    STR Name[MAX_FILE_NAME];
};

/***************************************************************************/
// The file system object allocated when mounting

typedef struct tag_SYSTEMFSFILESYSTEM {
    FILESYSTEM Header;
    LPSYSTEMFSFILE Root;
} SYSTEMFSFILESYSTEM, *LPSYSTEMFSFILESYSTEM;

/***************************************************************************/
// The file object created when opening a file

typedef struct tag_SYSFSFILE {
    FILE Header;
    LPSYSTEMFSFILE SystemFile;
    LPSYSTEMFSFILE Parent;
    LPFILE MountedFile;
} SYSFSFILE, *LPSYSFSFILE;

/***************************************************************************/
static LPSYSFSFILE OpenFile(LPFILEINFO Find);
static U32 CloseFile(LPSYSFSFILE File);
static void MountConfiguredFileSystem(LPCSTR FileSystem, LPCSTR Path);

SYSTEMFSFILESYSTEM SystemFSFileSystem = {
    .Header = {.ID = ID_FILESYSTEM,
               .References = 1,
               .Next = NULL,
               .Prev = NULL,
               .Mutex = EMPTY_MUTEX,
               .Driver = &SystemFSDriver,
               .Name = "System"},
    .Root = NULL};

#define SYSTEM_FS ((LPSYSTEMFSFILESYSTEM)Kernel.SystemFS)

static LPSYSTEMFSFILE NewSystemFile(LPCSTR Name, LPSYSTEMFSFILE Parent) {
    LPSYSTEMFSFILE Node;

    Node = (LPSYSTEMFSFILE)KernelMemAlloc(sizeof(SYSTEMFSFILE));
    if (Node == NULL) return NULL;

    *Node = (SYSTEMFSFILE){
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

static LPSYSTEMFSFILE NewSystemFileRoot(void) { return NewSystemFile(TEXT(""), NULL); }

/***************************************************************************/

static LPSYSTEMFSFILE FindChild(LPSYSTEMFSFILE Parent, LPCSTR Name) {
    LPLISTNODE Node;
    LPSYSTEMFSFILE Child;

    if (Parent == NULL || Parent->Children == NULL) return NULL;

    for (Node = Parent->Children->First; Node; Node = Node->Next) {
        Child = (LPSYSTEMFSFILE)Node;
        if (StringCompare(Child->Name, Name) == 0) return Child;
    }

    return NULL;
}

static LPSYSTEMFSFILE FindNode(LPCSTR Path) {
    LPLIST Parts;
    LPLISTNODE Node;
    LPPATHNODE Part;
    LPSYSTEMFSFILE Current;

    if (Kernel.SystemFS == NULL) return NULL;

    Parts = DecompPath(Path);
    if (Parts == NULL) return NULL;

    Current = SYSTEM_FS->Root;
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
    LPPATHNODE Part = NULL;
    LPSYSTEMFSFILE Parent;
    LPSYSTEMFSFILE Child;

    if (Kernel.SystemFS == NULL || Control == NULL) return DF_ERROR_BADPARAM;

    Parts = DecompPath(Control->Path);
    if (Parts == NULL) return DF_ERROR_BADPARAM;

    Parent = SYSTEM_FS->Root;
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
    LPSYSTEMFSFILE Node;

    if (Control == NULL) return DF_ERROR_BADPARAM;

    Node = FindNode(Control->Path);
    if (Node == NULL || Node->Parent == NULL) return DF_ERROR_GENERIC;

    ListErase(Node->Parent->Children, Node);
    KernelMemFree(Node);
    return DF_ERROR_SUCCESS;
}

/*
    ResolvePath breaks a path into its SystemFS node and the remaining
    subpath that must be delegated to a mounted filesystem. The resolved
    node is returned in Node and the remaining path is stored in Remaining
    (or an empty string if fully resolved inside SystemFS).
*/
static BOOL ResolvePath(LPCSTR Path, LPSYSTEMFSFILE *Node,
                        STR Remaining[MAX_PATH_NAME]) {
    LPLIST Parts;
    LPLISTNODE It;
    LPPATHNODE Part;
    LPSYSTEMFSFILE Current;

    if (Path == NULL || Node == NULL || Remaining == NULL) return FALSE;

    Parts = DecompPath(Path);
    if (Parts == NULL) return FALSE;

    Current = SYSTEM_FS->Root;
    Remaining[0] = STR_NULL;

    for (It = Parts->First; It; It = It->Next) {
        Part = (LPPATHNODE)It;

        if (Part->Name[0] == STR_NULL ||
            StringCompare(Part->Name, TEXT(".")) == 0) {
            continue;
        }

        if (StringCompare(Part->Name, TEXT("..")) == 0) {
            if (Current && Current->Parent) Current = Current->Parent;
            continue;
        }

        LPSYSTEMFSFILE Child = FindChild(Current, Part->Name);
        if (Child == NULL) {
            if (Current->Mounted) {
                STR Sep[2] = {PATH_SEP, STR_NULL};
                Remaining[0] = PATH_SEP;
                Remaining[1] = STR_NULL;
                for (; It; It = It->Next) {
                    Part = (LPPATHNODE)It;
                    if (Part->Name[0] == STR_NULL) continue;
                    StringConcat(Remaining, Part->Name);
                    if (It->Next) StringConcat(Remaining, Sep);
                }
                *Node = Current;
                DeleteList(Parts);
                return TRUE;
            }
            DeleteList(Parts);
            return FALSE;
        }

        Current = Child;
    }

    *Node = Current;
    DeleteList(Parts);
    return TRUE;
}

/*
    WrapMountedFile allocates a SYSFSFILE object from a file returned by a
    mounted filesystem and copies the common attributes so the caller can
    interact with it as a regular SystemFS file.
*/
static LPSYSFSFILE WrapMountedFile(LPSYSTEMFSFILE Parent, LPFILE Mounted) {
    LPSYSFSFILE File;

    if (Mounted == NULL) return NULL;

    File = (LPSYSFSFILE)KernelMemAlloc(sizeof(SYSFSFILE));
    if (File == NULL) return NULL;

    *File = (SYSFSFILE){0};
    File->Header.ID = ID_FILE;
    File->Header.FileSystem = Kernel.SystemFS;
    File->Parent = Parent;
    File->MountedFile = Mounted;
    StringCopy(File->Header.Name, Mounted->Name);
    File->Header.Attributes = Mounted->Attributes;
    File->Header.SizeLow = Mounted->SizeLow;
    File->Header.SizeHigh = Mounted->SizeHigh;
    File->Header.Creation = Mounted->Creation;
    File->Header.Accessed = Mounted->Accessed;
    File->Header.Modified = Mounted->Modified;

    return File;
}

static BOOL PathExists(LPFS_PATHCHECK Control) {
    STR Temp[MAX_PATH_NAME];
    STR Remaining[MAX_PATH_NAME];
    LPSYSTEMFSFILE Node;
    FILEINFO Info;
    LPFILE Mounted;
    BOOL Result = FALSE;

    if (Control == NULL) return FALSE;

    if (Control->SubFolder[0] == PATH_SEP) {
        StringCopy(Temp, Control->SubFolder);
    } else {
        StringCopy(Temp, Control->CurrentFolder);
        if (Temp[StringLength(Temp) - 1] != PATH_SEP)
            StringConcat(Temp, TEXT("/"));
        StringConcat(Temp, Control->SubFolder);
    }

    if (!ResolvePath(Temp, &Node, Remaining)) return FALSE;

    if (Remaining[0] == STR_NULL) return TRUE;

    if (Node->Mounted == NULL) return FALSE;

    Info.Size = sizeof(FILEINFO);
    Info.FileSystem = Node->Mounted;
    Info.Attributes = MAX_U32;
    StringCopy(Info.Name, Remaining);

    Mounted =
        (LPFILE)Node->Mounted->Driver->Command(DF_FS_OPENFILE, (U32)&Info);
    if (Mounted == NULL) return FALSE;

    Result = (Mounted->Attributes & FS_ATTR_FOLDER) != 0;
    Node->Mounted->Driver->Command(DF_FS_CLOSEFILE, (U32)Mounted);

    return Result;
}

/***************************************************************************/

static U32 CreateFolder(LPFILEINFO Info) {
    LPLIST Parts;
    LPLISTNODE Node;
    LPPATHNODE Part = NULL;
    LPSYSTEMFSFILE Parent;
    LPSYSTEMFSFILE Child;

    if (Info == NULL) return DF_ERROR_BADPARAM;

    Parts = DecompPath(Info->Name);
    if (Parts == NULL) return DF_ERROR_BADPARAM;

    Parent = SYSTEM_FS->Root;
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

static U32 DeleteFolder(LPFILEINFO Info) {
    LPSYSTEMFSFILE Node;

    if (Info == NULL) return DF_ERROR_BADPARAM;

    Node = FindNode(Info->Name);
    if (Node == NULL || Node->Parent == NULL) return DF_ERROR_GENERIC;
    if (Node->Children && Node->Children->NumItems) return DF_ERROR_GENERIC;

    ListErase(Node->Parent->Children, Node);
    KernelMemFree(Node);
    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static void MountConfiguredFileSystem(LPCSTR FileSystem, LPCSTR Path) {
    LPLISTNODE Node;
    LPFILESYSTEM FS;
    FS_MOUNT_CONTROL Control;

    if (FileSystem == NULL || Path == NULL) return;

    for (Node = Kernel.FileSystem->First; Node; Node = Node->Next) {
        FS = (LPFILESYSTEM)Node;
        if (FS == Kernel.SystemFS) continue;
        if (StringCompare(FS->Name, FileSystem) == 0) {
            StringCopy(Control.Path, Path);
            Control.Node = (LPLISTNODE)FS;
            MountObject(&Control);
            break;
        }
    }
}

/***************************************************************************/

BOOL MountSystemFS(void) {
    LPLISTNODE Node;
    LPFILESYSTEM FS;
    VOLUMEINFO Volume;
    FS_MOUNT_CONTROL Control;
    FILEINFO Info;
    STR Path[MAX_PATH_NAME];
    const STR FsRoot[] = {PATH_SEP, 'f', 's', STR_NULL};
    U32 Result;
    U32 Length;

    KernelLogText(LOG_VERBOSE, TEXT("[MountSystemFS] Mounting system FileSystem"));

    SystemFSFileSystem.Root = NewSystemFileRoot();
    if (SystemFSFileSystem.Root == NULL) return FALSE;

    InitMutex(&(SystemFSFileSystem.Header.Mutex));
    Kernel.SystemFS = (LPFILESYSTEM)&SystemFSFileSystem;

    Info.Size = sizeof(FILEINFO);
    Info.FileSystem = Kernel.SystemFS;
    Info.Attributes = 0;
    Info.Flags = 0;
    StringCopy(Info.Name, FsRoot);
    CreateFolder(&Info);

    for (Node = Kernel.FileSystem->First; Node; Node = Node->Next) {
        FS = (LPFILESYSTEM)Node;
        if (FS == Kernel.SystemFS) continue;

        Volume.Size = sizeof(VOLUMEINFO);
        Volume.Volume = (HANDLE)FS;
        Volume.Name[0] = STR_NULL;
        Result = FS->Driver->Command(DF_FS_GETVOLUMEINFO, (U32)&Volume);
        if (Result != DF_ERROR_SUCCESS || Volume.Name[0] == STR_NULL) {
            StringCopy(Volume.Name, FS->Name);
        }

        StringCopy(Path, FsRoot);
        Length = StringLength(Path);
        Path[Length] = PATH_SEP;
        Path[Length + 1] = STR_NULL;
        StringConcat(Path, Volume.Name);

        StringCopy(Control.Path, Path);
        Control.Node = (LPLISTNODE)FS;
        MountObject(&Control);
    }

    if (Kernel.Configuration) {
        U32 ConfigIndex = 0;
        while (1) {
            STR Key[0x100];
            STR IndexText[0x10];
            LPCSTR FsName;
            LPCSTR MountPath;

            U32ToString(ConfigIndex, IndexText);

            StringCopy(Key, TEXT("SystemFS.Mount."));
            StringConcat(Key, IndexText);
            StringConcat(Key, TEXT(".FileSystem"));
            FsName = TomlGet(Kernel.Configuration, Key);
            if (FsName == NULL) break;

            StringCopy(Key, TEXT("SystemFS.Mount."));
            StringConcat(Key, IndexText);
            StringConcat(Key, TEXT(".Path"));
            MountPath = TomlGet(Kernel.Configuration, Key);
            if (MountPath) {
                MountConfiguredFileSystem(FsName, MountPath);
            }

            ConfigIndex++;
        }
    }

    ListAddItem(Kernel.FileSystem, Kernel.SystemFS);

    return TRUE;
}

/***************************************************************************/

static U32 Initialize(void) { return DF_ERROR_SUCCESS; }

/***************************************************************************/

static LPSYSFSFILE OpenFile(LPFILEINFO Find) {
    STR Path[MAX_PATH_NAME];
    STR Remaining[MAX_PATH_NAME];
    LPSYSTEMFSFILE Node;
    FILEINFO Local;
    LPFILE Mounted;
    BOOL Wildcard = FALSE;

    if (Find == NULL) return NULL;

    StringCopy(Path, Find->Name);
    {
        U32 Len = StringLength(Path);
        if (Len > 0 && Path[Len - 1] == '*') {
            Wildcard = TRUE;
            Path[Len - 1] = STR_NULL;
            if (Len > 1 && Path[Len - 2] == PATH_SEP) Path[Len - 2] = STR_NULL;
        }
    }

    if (!ResolvePath(Path, &Node, Remaining)) return NULL;

    if (Remaining[0] != STR_NULL) {
        if (Node->Mounted == NULL) return NULL;

        Local = *Find;
        Local.FileSystem = Node->Mounted;
        StringCopy(Local.Name, Remaining);
        if (Wildcard) {
            if (Local.Name[StringLength(Local.Name) - 1] != PATH_SEP)
                StringConcat(Local.Name, TEXT("/"));
            StringConcat(Local.Name, TEXT("*"));
        }

        Mounted = (LPFILE)Node->Mounted->Driver->Command(DF_FS_OPENFILE,
                                                         (U32)&Local);
        return WrapMountedFile(Node, Mounted);
    }

    if (Wildcard) {
        if (Node->Mounted) {
            Local = *Find;
            Local.FileSystem = Node->Mounted;
            // Request listing of the mounted filesystem root
            StringCopy(Local.Name, TEXT("*"));
            Mounted = (LPFILE)Node->Mounted->Driver->Command(
                DF_FS_OPENFILE, (U32)&Local);
            return WrapMountedFile(Node, Mounted);
        } else {
            LPSYSTEMFSFILE Child =
                (Node->Children) ? (LPSYSTEMFSFILE)Node->Children->First : NULL;
            if (Child == NULL) return NULL;

            LPSYSFSFILE File =
                (LPSYSFSFILE)KernelMemAlloc(sizeof(SYSFSFILE));
            if (File == NULL) return NULL;

            *File = (SYSFSFILE){0};
            File->Header.ID = ID_FILE;
            File->Header.FileSystem = Kernel.SystemFS;
            File->Parent = Node;
            File->MountedFile = NULL;
            StringCopy(File->Header.Name, Child->Name);
            File->Header.Attributes = FS_ATTR_FOLDER;
            File->SystemFile = (LPSYSTEMFSFILE)Child->Next;
            return File;
        }
    }

    if (Node->Mounted) {
        Local = *Find;
        Local.FileSystem = Node->Mounted;
        // Open the root of the mounted filesystem
        Local.Name[0] = STR_NULL;
        Mounted =
            (LPFILE)Node->Mounted->Driver->Command(DF_FS_OPENFILE, (U32)&Local);
        return WrapMountedFile(Node, Mounted);
    }

    {
        LPSYSFSFILE File =
            (LPSYSFSFILE)KernelMemAlloc(sizeof(SYSFSFILE));
        if (File == NULL) return NULL;

        *File = (SYSFSFILE){0};
        File->Header.ID = ID_FILE;
        File->Header.FileSystem = Kernel.SystemFS;
        File->SystemFile =
            (Node->Children) ? (LPSYSTEMFSFILE)Node->Children->First : NULL;
        File->Parent = Node->Parent;
        StringCopy(File->Header.Name, Node->Name);
        File->Header.Attributes = FS_ATTR_FOLDER;
        return File;
    }
}

/***************************************************************************/

static U32 OpenNext(LPSYSFSFILE File) {
    LPFILESYSTEM FS;
    U32 Result;

    if (File == NULL) return DF_ERROR_BADPARAM;

    if (File->MountedFile) {
        FS = File->Parent ? File->Parent->Mounted : NULL;
        if (FS == NULL) return DF_ERROR_GENERIC;
        Result = FS->Driver->Command(DF_FS_OPENNEXT, (U32)File->MountedFile);
        if (Result != DF_ERROR_SUCCESS) return Result;
        StringCopy(File->Header.Name, File->MountedFile->Name);
        File->Header.Attributes = File->MountedFile->Attributes;
        File->Header.SizeLow = File->MountedFile->SizeLow;
        File->Header.SizeHigh = File->MountedFile->SizeHigh;
        File->Header.Creation = File->MountedFile->Creation;
        File->Header.Accessed = File->MountedFile->Accessed;
        File->Header.Modified = File->MountedFile->Modified;
        return DF_ERROR_SUCCESS;
    }

    if (File->SystemFile == NULL) return DF_ERROR_GENERIC;

    // Return current entry then move to the next one
    StringCopy(File->Header.Name, File->SystemFile->Name);
    File->Header.Attributes = FS_ATTR_FOLDER;
    File->SystemFile = (LPSYSTEMFSFILE)File->SystemFile->Next;

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 CloseFile(LPSYSFSFILE File) {
    if (File == NULL) return DF_ERROR_BADPARAM;

    if (File->MountedFile && File->Parent && File->Parent->Mounted) {
        File->Parent->Mounted->Driver->Command(DF_FS_CLOSEFILE,
                                               (U32)File->MountedFile);
    }

    KernelMemFree(File);

    return DF_ERROR_SUCCESS;
}

/***************************************************************************/

static U32 ReadFile(LPSYSFSFILE File) {
    LPFILESYSTEM FS;
    LPFILE Mounted;
    U32 Result;

    if (File == NULL) return DF_ERROR_BADPARAM;

    FS = (File->Parent) ? File->Parent->Mounted : NULL;
    Mounted = File->MountedFile;
    if (FS == NULL || Mounted == NULL) return DF_ERROR_GENERIC;

    Mounted->Buffer = File->Header.Buffer;
    Mounted->BytesToRead = File->Header.BytesToRead;
    Mounted->Position = File->Header.Position;

    Result = FS->Driver->Command(DF_FS_READ, (U32)Mounted);

    File->Header.BytesRead = Mounted->BytesRead;
    File->Header.Position = Mounted->Position;

    return Result;
}

/***************************************************************************/

static U32 WriteFile(LPSYSFSFILE File) {
    LPFILESYSTEM FS;
    LPFILE Mounted;
    U32 Result;

    if (File == NULL) return DF_ERROR_BADPARAM;

    FS = (File->Parent) ? File->Parent->Mounted : NULL;
    Mounted = File->MountedFile;
    if (FS == NULL || Mounted == NULL) return DF_ERROR_NOTIMPL;

    Mounted->Buffer = File->Header.Buffer;
    Mounted->BytesToRead = File->Header.BytesToRead;
    Mounted->Position = File->Header.Position;

    Result = FS->Driver->Command(DF_FS_WRITE, (U32)Mounted);

    File->Header.BytesRead = Mounted->BytesRead;
    File->Header.Position = Mounted->Position;

    return Result;
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
            return CreateFolder((LPFILEINFO)Parameter);
        case DF_FS_DELETEFOLDER:
            return DeleteFolder((LPFILEINFO)Parameter);
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
