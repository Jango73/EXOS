
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

static BOOL PathExists(LPFS_PATHCHECK Control) {
    STR Temp[MAX_PATH_NAME];
    FILEINFO Find;
    LPSYSFSFILE File;
    BOOL Result;

    if (Control == NULL) return FALSE;

    if (Control->SubFolder[0] == PATH_SEP) {
        StringCopy(Temp, Control->SubFolder);
    } else {
        StringCopy(Temp, Control->CurrentFolder);
        if (Temp[StringLength(Temp) - 1] != PATH_SEP)
            StringConcat(Temp, TEXT("/"));
        StringConcat(Temp, Control->SubFolder);
    }

    Find.Size = sizeof(FILEINFO);
    Find.FileSystem = Kernel.SystemFS;
    Find.Attributes = MAX_U32;
    StringCopy(Find.Name, Temp);

    File = OpenFile(&Find);
    if (File == NULL) return FALSE;

    Result = (File->Header.Attributes & FS_ATTR_FOLDER) != 0;
    CloseFile(File);

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
    LPLIST Parts;
    LPLISTNODE Node;
    LPPATHNODE Part;
    LPSYSTEMFSFILE Parent;
    LPSYSTEMFSFILE Child;
    STR Sep[2] = {PATH_SEP, STR_NULL};

    if (Find == NULL) return NULL;

    Parts = DecompPath(Find->Name);
    if (Parts == NULL) return NULL;

    Parent = SYSTEM_FS->Root;
    for (Node = Parts->First; Node; Node = Node->Next) {
        Part = (LPPATHNODE)Node;
        if (Part->Name[0] == STR_NULL) continue;
        if (Node->Next == NULL) break;
        Child = FindChild(Parent, Part->Name);
        if (Child == NULL) {
            if (Parent->Mounted) {
                STR Remaining[MAX_PATH_NAME] = {PATH_SEP, STR_NULL};
                LPLISTNODE N;
                LPPATHNODE P;
                for (N = Node; N; N = N->Next) {
                    P = (LPPATHNODE)N;
                    if (P->Name[0] == STR_NULL) continue;
                    StringConcat(Remaining, P->Name);
                    if (N->Next) StringConcat(Remaining, Sep);
                }

                FILEINFO Local = *Find;
                LPFILESYSTEM FS = Parent->Mounted;
                LPFILE MountedFile;
                Local.FileSystem = FS;
                StringCopy(Local.Name, Remaining);
                DeleteList(Parts);
                MountedFile =
                    (LPFILE)FS->Driver->Command(DF_FS_OPENFILE, (U32)&Local);
                if (MountedFile == NULL) return NULL;

                LPSYSFSFILE File =
                    (LPSYSFSFILE)KernelMemAlloc(sizeof(SYSFSFILE));
                if (File == NULL) {
                    FS->Driver->Command(DF_FS_CLOSEFILE, (U32)MountedFile);
                    return NULL;
                }

                *File = (SYSFSFILE){0};
                File->Header.ID = ID_FILE;
                File->Header.FileSystem = Kernel.SystemFS;
                File->Parent = Parent;
                File->MountedFile = MountedFile;
                StringCopy(File->Header.Name, MountedFile->Name);
                File->Header.Attributes = MountedFile->Attributes;
                File->Header.SizeLow = MountedFile->SizeLow;
                File->Header.SizeHigh = MountedFile->SizeHigh;
                File->Header.Creation = MountedFile->Creation;
                File->Header.Accessed = MountedFile->Accessed;
                File->Header.Modified = MountedFile->Modified;
                return File;
            }

            DeleteList(Parts);
            return NULL;
        }
        Parent = Child;
    }

    Part = (LPPATHNODE)Node;

    if (Parent->Mounted && Part && Part->Name[0] != STR_NULL) {
        STR Remaining[MAX_PATH_NAME] = {PATH_SEP, STR_NULL};
        FILEINFO Local = *Find;
        LPFILE MountedFile;
        LPFILESYSTEM FS = Parent->Mounted;

        StringConcat(Remaining, Part->Name);
        Local.FileSystem = FS;
        StringCopy(Local.Name, Remaining);

        DeleteList(Parts);

        MountedFile = (LPFILE)FS->Driver->Command(DF_FS_OPENFILE, (U32)&Local);
        if (MountedFile == NULL) return NULL;

        LPSYSFSFILE File =
            (LPSYSFSFILE)KernelMemAlloc(sizeof(SYSFSFILE));
        if (File == NULL) {
            FS->Driver->Command(DF_FS_CLOSEFILE, (U32)MountedFile);
            return NULL;
        }

        *File = (SYSFSFILE){0};
        File->Header.ID = ID_FILE;
        File->Header.FileSystem = Kernel.SystemFS;
        File->Parent = Parent;
        File->MountedFile = MountedFile;
        StringCopy(File->Header.Name, MountedFile->Name);
        File->Header.Attributes = MountedFile->Attributes;
        File->Header.SizeLow = MountedFile->SizeLow;
        File->Header.SizeHigh = MountedFile->SizeHigh;
        File->Header.Creation = MountedFile->Creation;
        File->Header.Accessed = MountedFile->Accessed;
        File->Header.Modified = MountedFile->Modified;
        return File;
    }

    Child = NULL;
    if (Parent->Children) Child = (LPSYSTEMFSFILE)Parent->Children->First;
    if (Child == NULL) {
        DeleteList(Parts);
        return NULL;
    }

    LPSYSFSFILE File = (LPSYSFSFILE)KernelMemAlloc(sizeof(SYSFSFILE));
    if (File == NULL) {
        DeleteList(Parts);
        return NULL;
    }

    *File = (SYSFSFILE){0};
    File->Header.ID = ID_FILE;
    File->Header.FileSystem = Kernel.SystemFS;
    File->SystemFile = Child;
    File->Parent = Parent;
    StringCopy(File->Header.Name, Child->Name);
    File->Header.Attributes = FS_ATTR_FOLDER;

    DeleteList(Parts);

    return File;
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

    File->SystemFile = (LPSYSTEMFSFILE)File->SystemFile->Next;
    if (File->SystemFile == NULL) return DF_ERROR_GENERIC;

    StringCopy(File->Header.Name, File->SystemFile->Name);
    File->Header.Attributes = FS_ATTR_FOLDER;

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
