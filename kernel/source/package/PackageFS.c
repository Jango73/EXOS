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


    PackageFS read-only mount implementation

\************************************************************************/

#include "package/PackageFS.h"

#include "CoreString.h"
#include "Heap.h"
#include "Kernel.h"
#include "Log.h"
#include "Mutex.h"
#include "SystemFS.h"

/************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

#define PACKAGEFS_ALIAS_MAX_DEPTH 32
#define PACKAGEFS_NODE_TYPE_ROOT 0

/************************************************************************/

typedef struct tag_PACKAGEFS_NODE PACKAGEFS_NODE, *LPPACKAGEFS_NODE;

typedef struct tag_PACKAGEFSFILESYSTEM {
    FILESYSTEM Header;
    MUTEX FilesMutex;
    U8* PackageBytes;
    U32 PackageSize;
    EPK_VALIDATED_PACKAGE Package;
    LPPACKAGEFS_NODE Root;
} PACKAGEFSFILESYSTEM, *LPPACKAGEFSFILESYSTEM;

typedef struct tag_PACKAGEFS_NODE {
    LPPACKAGEFS_NODE ParentNode;
    LPPACKAGEFS_NODE FirstChild;
    LPPACKAGEFS_NODE NextSibling;
    U32 NodeType;
    U32 Attributes;
    U32 TocIndex;
    BOOL Defined;
    DATETIME Modified;
    STR Name[MAX_FILE_NAME];
    STR AliasTarget[MAX_PATH_NAME];
} PACKAGEFS_NODE;

typedef struct tag_PACKAGEFSFILE {
    FILE Header;
    LPPACKAGEFS_NODE Node;
    LPPACKAGEFS_NODE EnumerationCursor;
    BOOL Enumerate;
    STR Pattern[MAX_FILE_NAME];
} PACKAGEFSFILE, *LPPACKAGEFSFILE;

/************************************************************************/

static UINT PackageFSCommands(UINT Function, UINT Parameter);

DRIVER PackageFSDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_FILESYSTEM,
    .VersionMajor = VER_MAJOR,
    .VersionMinor = VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "EXOS PackageFS",
    .Command = PackageFSCommands};

/************************************************************************/

/**
 * @brief Initialize PackageFS driver state.
 * @return DF_RETURN_SUCCESS always.
 */
static U32 Initialize(void) { return DF_RETURN_SUCCESS; }

/************************************************************************/

/**
 * @brief Decode packed DATETIME value stored in EPK entries.
 * @param Packed Packed U64 date/time.
 * @param OutTime Destination DATETIME.
 */
static void PackageFSDecodeDateTime(U64 Packed, LPDATETIME OutTime) {
    U32 Low;
    U32 High;

    if (OutTime == NULL) {
        return;
    }

    Low = U64_Low32(Packed);
    High = U64_High32(Packed);

    MemorySet(OutTime, 0, sizeof(DATETIME));

    OutTime->Year = Low & 0x03FFFFFF;
    OutTime->Month = (Low >> 26) & 0x0F;
    OutTime->Day = ((Low >> 30) & 0x03) | ((High & 0x0F) << 2);
    OutTime->Hour = (High >> 4) & 0x3F;
    OutTime->Minute = (High >> 10) & 0x3F;
    OutTime->Second = (High >> 16) & 0x3F;
    OutTime->Milli = (High >> 22) & 0x03FF;
}

/************************************************************************/

/**
 * @brief Allocate a new PackageFS tree node.
 * @param Name Node short name.
 * @param Parent Parent node or NULL for root.
 * @return Newly allocated node or NULL.
 */
static LPPACKAGEFS_NODE PackageFSCreateNode(LPCSTR Name, LPPACKAGEFS_NODE Parent) {
    LPPACKAGEFS_NODE Node;

    Node = (LPPACKAGEFS_NODE)KernelHeapAlloc(sizeof(PACKAGEFS_NODE));
    if (Node == NULL) {
        return NULL;
    }

    MemorySet(Node, 0, sizeof(PACKAGEFS_NODE));
    Node->ParentNode = Parent;
    Node->NodeType = PACKAGEFS_NODE_TYPE_ROOT;
    Node->TocIndex = MAX_U32;
    Node->Attributes = FS_ATTR_FOLDER | FS_ATTR_READONLY;

    if (Name != NULL) {
        StringCopy(Node->Name, Name);
    }

    return Node;
}

/************************************************************************/

/**
 * @brief Release a node tree recursively.
 * @param Node Root node to release.
 */
static void PackageFSReleaseNodeTree(LPPACKAGEFS_NODE Node) {
    LPPACKAGEFS_NODE Child;

    if (Node == NULL) {
        return;
    }

    Child = Node->FirstChild;
    while (Child != NULL) {
        LPPACKAGEFS_NODE Next = Child->NextSibling;
        PackageFSReleaseNodeTree(Child);
        Child = Next;
    }

    KernelHeapFree(Node);
}

/************************************************************************/

/**
 * @brief Find a direct child by name.
 * @param Parent Parent folder node.
 * @param Name Child name to find.
 * @return Matching child or NULL.
 */
static LPPACKAGEFS_NODE PackageFSFindChild(LPPACKAGEFS_NODE Parent, LPCSTR Name) {
    LPPACKAGEFS_NODE Child;

    if (Parent == NULL || Name == NULL) {
        return NULL;
    }

    Child = Parent->FirstChild;
    while (Child != NULL) {
        if (StringCompare(Child->Name, Name) == 0) {
            return Child;
        }
        Child = Child->NextSibling;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Append a child node to a parent.
 * @param Parent Parent folder node.
 * @param Child Child node to append.
 */
static void PackageFSAddChild(LPPACKAGEFS_NODE Parent, LPPACKAGEFS_NODE Child) {
    LPPACKAGEFS_NODE Cursor;

    if (Parent == NULL || Child == NULL) {
        return;
    }

    if (Parent->FirstChild == NULL) {
        Parent->FirstChild = Child;
        return;
    }

    Cursor = Parent->FirstChild;
    while (Cursor->NextSibling != NULL) {
        Cursor = Cursor->NextSibling;
    }
    Cursor->NextSibling = Child;
}

/************************************************************************/

/**
 * @brief Convert package node type/permissions to generic FS attributes.
 * @param Entry Parsed TOC entry.
 * @return Generic attribute flags.
 */
static U32 PackageFSBuildAttributes(const EPK_PARSED_TOC_ENTRY* Entry) {
    U32 Attributes = FS_ATTR_READONLY;

    if (Entry == NULL) {
        return Attributes;
    }

    if (Entry->NodeType == EPK_NODE_TYPE_FOLDER || Entry->NodeType == EPK_NODE_TYPE_FOLDER_ALIAS) {
        Attributes |= FS_ATTR_FOLDER;
    }

    if ((Entry->Permissions & 0x49) != 0) {
        Attributes |= FS_ATTR_EXECUTABLE;
    }

    return Attributes;
}

/************************************************************************/

/**
 * @brief Read one path component from a path string.
 * @param Path Source path.
 * @param Cursor In/out cursor index.
 * @param Component Destination component buffer.
 * @return TRUE when a component is produced, FALSE at end or error.
 */
static BOOL PackageFSNextPathComponent(LPCSTR Path, U32* Cursor, STR Component[MAX_FILE_NAME]) {
    U32 Index = 0;
    U32 Position;

    if (Path == NULL || Cursor == NULL || Component == NULL) {
        return FALSE;
    }

    Position = *Cursor;
    while (Path[Position] == PATH_SEP) {
        Position++;
    }

    if (Path[Position] == STR_NULL) {
        *Cursor = Position;
        return FALSE;
    }

    while (Path[Position] != STR_NULL && Path[Position] != PATH_SEP) {
        if (Index + 1 >= MAX_FILE_NAME) {
            return FALSE;
        }
        Component[Index++] = Path[Position++];
    }

    Component[Index] = STR_NULL;
    *Cursor = Position;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Insert or update one TOC entry into the in-memory tree.
 * @param FileSystem PackageFS instance.
 * @param TocIndex TOC entry index.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 PackageFSInsertTocEntry(LPPACKAGEFSFILESYSTEM FileSystem, U32 TocIndex) {
    const EPK_PARSED_TOC_ENTRY* Entry;
    const U8* PackageBytes;
    STR FullPath[MAX_PATH_NAME];
    STR AliasTarget[MAX_PATH_NAME];
    U32 PathCursor = 0;
    STR Component[MAX_FILE_NAME];
    LPPACKAGEFS_NODE Current;
    LPPACKAGEFS_NODE Node = NULL;
    BOOL HasComponent = FALSE;

    if (FileSystem == NULL || FileSystem->Root == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (TocIndex >= FileSystem->Package.TocEntryCount) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Entry = &FileSystem->Package.TocEntries[TocIndex];
    PackageBytes = FileSystem->Package.PackageBytes;

    if (Entry->PathLength == 0 || Entry->PathLength >= MAX_PATH_NAME) {
        return DF_RETURN_BAD_PARAMETER;
    }

    MemoryCopy(FullPath, PackageBytes + Entry->PathOffset, Entry->PathLength);
    FullPath[Entry->PathLength] = STR_NULL;

    AliasTarget[0] = STR_NULL;
    if (Entry->NodeType == EPK_NODE_TYPE_FOLDER_ALIAS) {
        if (Entry->AliasTargetLength == 0 || Entry->AliasTargetLength >= MAX_PATH_NAME) {
            return DF_RETURN_BAD_PARAMETER;
        }
        MemoryCopy(AliasTarget, PackageBytes + Entry->AliasTargetOffset, Entry->AliasTargetLength);
        AliasTarget[Entry->AliasTargetLength] = STR_NULL;
    }

    Current = FileSystem->Root;
    while (PackageFSNextPathComponent(FullPath, &PathCursor, Component)) {
        LPPACKAGEFS_NODE Existing = PackageFSFindChild(Current, Component);

        HasComponent = TRUE;
        if (Existing == NULL) {
            Existing = PackageFSCreateNode(Component, Current);
            if (Existing == NULL) {
                return DF_RETURN_NO_MEMORY;
            }
            PackageFSAddChild(Current, Existing);
        }

        Current = Existing;
        Node = Existing;

        while (FullPath[PathCursor] == PATH_SEP) {
            PathCursor++;
        }
    }

    if (!HasComponent || Node == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Node->Defined == TRUE) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Node->Defined = TRUE;
    Node->NodeType = Entry->NodeType;
    Node->TocIndex = TocIndex;
    Node->Attributes = PackageFSBuildAttributes(Entry);
    PackageFSDecodeDateTime(Entry->ModifiedTime, &Node->Modified);
    if (Entry->NodeType == EPK_NODE_TYPE_FOLDER_ALIAS) {
        StringCopy(Node->AliasTarget, AliasTarget);
    } else {
        Node->AliasTarget[0] = STR_NULL;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Ensure implicit folders are explicit folder nodes.
 * @param Node Node tree root.
 * @return TRUE when tree is valid.
 */
static BOOL PackageFSFinalizeImplicitFolders(LPPACKAGEFS_NODE Node) {
    LPPACKAGEFS_NODE Child;

    if (Node == NULL) {
        return FALSE;
    }

    if (Node->Defined == FALSE && Node->ParentNode != NULL) {
        Node->NodeType = EPK_NODE_TYPE_FOLDER;
        Node->Attributes = FS_ATTR_FOLDER | FS_ATTR_READONLY;
        Node->TocIndex = MAX_U32;
    }

    Child = Node->FirstChild;
    while (Child != NULL) {
        if (!PackageFSFinalizeImplicitFolders(Child)) {
            return FALSE;
        }
        Child = Child->NextSibling;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Build full tree from validated TOC entries.
 * @param FileSystem Target PackageFS instance.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 PackageFSBuildTree(LPPACKAGEFSFILESYSTEM FileSystem) {
    U32 TocIndex;

    if (FileSystem == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    FileSystem->Root = PackageFSCreateNode(TEXT(""), NULL);
    if (FileSystem->Root == NULL) {
        return DF_RETURN_NO_MEMORY;
    }

    FileSystem->Root->Defined = TRUE;
    FileSystem->Root->NodeType = PACKAGEFS_NODE_TYPE_ROOT;
    FileSystem->Root->Attributes = FS_ATTR_FOLDER | FS_ATTR_READONLY;
    FileSystem->Root->TocIndex = MAX_U32;

    for (TocIndex = 0; TocIndex < FileSystem->Package.TocEntryCount; TocIndex++) {
        U32 Result = PackageFSInsertTocEntry(FileSystem, TocIndex);
        if (Result != DF_RETURN_SUCCESS) {
            return Result;
        }
    }

    if (!PackageFSFinalizeImplicitFolders(FileSystem->Root)) {
        return DF_RETURN_GENERIC;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Resolve an internal path to a node without alias expansion.
 * @param Root Root node.
 * @param Path Internal package path.
 * @return Located node or NULL.
 */
static LPPACKAGEFS_NODE PackageFSResolveInternalPath(LPPACKAGEFS_NODE Root, LPCSTR Path) {
    U32 Cursor = 0;
    STR Component[MAX_FILE_NAME];
    LPPACKAGEFS_NODE Current = Root;

    if (Root == NULL || Path == NULL) {
        return NULL;
    }

    while (PackageFSNextPathComponent(Path, &Cursor, Component)) {
        if (StringCompare(Component, TEXT(".")) == 0) {
            continue;
        }
        if (StringCompare(Component, TEXT("..")) == 0) {
            if (Current->ParentNode != NULL) {
                Current = Current->ParentNode;
            }
            continue;
        }

        Current = PackageFSFindChild(Current, Component);
        if (Current == NULL) {
            return NULL;
        }
    }

    return Current;
}

/************************************************************************/

/**
 * @brief Resolve alias node target with recursion guard.
 * @param FileSystem PackageFS instance.
 * @param Node Alias node.
 * @param Depth Current alias depth.
 * @return Resolved node or NULL.
 */
static LPPACKAGEFS_NODE PackageFSResolveAliasTarget(LPPACKAGEFSFILESYSTEM FileSystem,
                                                    LPPACKAGEFS_NODE Node,
                                                    U32 Depth) {
    LPPACKAGEFS_NODE Target;
    LPCSTR Path = NULL;

    if (FileSystem == NULL || Node == NULL) {
        return NULL;
    }

    if (Depth >= PACKAGEFS_ALIAS_MAX_DEPTH) {
        WARNING(TEXT("[PackageFSResolveAliasTarget] Alias depth exceeded"));
        return NULL;
    }

    Path = Node->AliasTarget;
    if (Path == NULL || Path[0] == STR_NULL) {
        return NULL;
    }

    while (*Path == PATH_SEP) {
        Path++;
    }

    Target = PackageFSResolveInternalPath(FileSystem->Root, Path);
    if (Target == NULL) {
        WARNING(TEXT("[PackageFSResolveAliasTarget] Alias target not found path=%s"), Node->AliasTarget);
        return NULL;
    }

    if (Target->NodeType == EPK_NODE_TYPE_FOLDER_ALIAS) {
        return PackageFSResolveAliasTarget(FileSystem, Target, Depth + 1);
    }

    if ((Target->Attributes & FS_ATTR_FOLDER) == 0) {
        WARNING(TEXT("[PackageFSResolveAliasTarget] Alias target is not folder path=%s"), Node->AliasTarget);
        return NULL;
    }

    return Target;
}

/************************************************************************/

/**
 * @brief Resolve external path and optionally expand final alias.
 * @param FileSystem PackageFS instance.
 * @param Path Path to resolve.
 * @param FollowFinalAlias TRUE to resolve trailing alias.
 * @return Located node or NULL.
 */
static LPPACKAGEFS_NODE PackageFSResolvePath(LPPACKAGEFSFILESYSTEM FileSystem,
                                             LPCSTR Path,
                                             BOOL FollowFinalAlias) {
    U32 Cursor = 0;
    STR Component[MAX_FILE_NAME];
    LPPACKAGEFS_NODE Current;
    BOOL HasAny = FALSE;

    if (FileSystem == NULL || FileSystem->Root == NULL || Path == NULL) {
        return NULL;
    }

    Current = FileSystem->Root;
    while (PackageFSNextPathComponent(Path, &Cursor, Component)) {
        HasAny = TRUE;

        if (StringCompare(Component, TEXT(".")) == 0) {
            continue;
        }
        if (StringCompare(Component, TEXT("..")) == 0) {
            if (Current->ParentNode != NULL) {
                Current = Current->ParentNode;
            }
            continue;
        }

        Current = PackageFSFindChild(Current, Component);
        if (Current == NULL) {
            return NULL;
        }

        while (Path[Cursor] == PATH_SEP) {
            Cursor++;
        }

        if (Current->NodeType == EPK_NODE_TYPE_FOLDER_ALIAS &&
            (Path[Cursor] != STR_NULL || FollowFinalAlias == TRUE)) {
            Current = PackageFSResolveAliasTarget(FileSystem, Current, 0);
            if (Current == NULL) {
                return NULL;
            }
        }
    }

    if (!HasAny) {
        return FileSystem->Root;
    }

    return Current;
}

/************************************************************************/

/**
 * @brief Create a file object bound to a package node.
 * @param FileSystem Owning PackageFS instance.
 * @param Node Target node.
 * @return Allocated file object or NULL.
 */
static LPPACKAGEFSFILE PackageFSCreateFileObject(LPPACKAGEFSFILESYSTEM FileSystem, LPPACKAGEFS_NODE Node) {
    LPPACKAGEFSFILE File;
    const EPK_PARSED_TOC_ENTRY* Entry = NULL;

    if (FileSystem == NULL || Node == NULL) {
        return NULL;
    }

    File = (LPPACKAGEFSFILE)CreateKernelObject(sizeof(PACKAGEFSFILE), KOID_FILE);
    if (File == NULL) {
        return NULL;
    }

    File->Header.FileSystem = &FileSystem->Header;
    File->Node = Node;
    File->EnumerationCursor = NULL;
    File->Enumerate = FALSE;
    File->Pattern[0] = STR_NULL;
    InitMutex(&File->Header.Mutex);
    InitSecurity(&File->Header.Security);

    if (Node->ParentNode == NULL) {
        StringCopy(File->Header.Name, TEXT("/"));
    } else {
        StringCopy(File->Header.Name, Node->Name);
    }

    File->Header.Attributes = Node->Attributes;
    File->Header.Creation = Node->Modified;
    File->Header.Accessed = Node->Modified;
    File->Header.Modified = Node->Modified;
    File->Header.Position = 0;
    File->Header.BytesTransferred = 0;

    if ((Node->Attributes & FS_ATTR_FOLDER) == 0 &&
        Node->TocIndex != MAX_U32 &&
        Node->TocIndex < FileSystem->Package.TocEntryCount) {
        Entry = &FileSystem->Package.TocEntries[Node->TocIndex];
        File->Header.SizeLow = U64_Low32(Entry->FileSize);
        File->Header.SizeHigh = U64_High32(Entry->FileSize);
    } else {
        File->Header.SizeLow = 0;
        File->Header.SizeHigh = 0;
    }

    return File;
}

/************************************************************************/

/**
 * @brief Wildcard matcher for folder enumeration.
 * @param Pattern Pattern containing '*' and '?'.
 * @param Name Candidate name.
 * @return TRUE when pattern matches.
 */
static BOOL PackageFSWildcardMatch(LPCSTR Pattern, LPCSTR Name) {
    if (Pattern == NULL || Name == NULL) {
        return FALSE;
    }

    if (*Pattern == STR_NULL) {
        return *Name == STR_NULL;
    }

    if (*Pattern == '*') {
        LPCSTR NextPattern = Pattern + 1;
        LPCSTR Cursor = Name;

        while (*NextPattern == '*') {
            NextPattern++;
        }

        if (*NextPattern == STR_NULL) {
            return TRUE;
        }

        while (*Cursor != STR_NULL) {
            if (PackageFSWildcardMatch(NextPattern, Cursor)) {
                return TRUE;
            }
            Cursor++;
        }

        return PackageFSWildcardMatch(NextPattern, Cursor);
    }

    if (*Pattern == '?') {
        if (*Name == STR_NULL) {
            return FALSE;
        }
        return PackageFSWildcardMatch(Pattern + 1, Name + 1);
    }

    if (*Pattern != *Name) {
        return FALSE;
    }

    return PackageFSWildcardMatch(Pattern + 1, Name + 1);
}

/************************************************************************/

/**
 * @brief Advance a directory enumeration file handle.
 * @param File Enumeration handle.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 PackageFSAdvanceEnumeration(LPPACKAGEFSFILE File) {
    LPPACKAGEFS_NODE Cursor;

    if (File == NULL || File->Node == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Cursor = File->EnumerationCursor;
    while (Cursor != NULL) {
        File->EnumerationCursor = Cursor->NextSibling;

        if (!PackageFSWildcardMatch(File->Pattern, Cursor->Name)) {
            Cursor = File->EnumerationCursor;
            continue;
        }

        StringCopy(File->Header.Name, Cursor->Name);
        File->Header.Attributes = Cursor->Attributes;
        File->Header.Creation = Cursor->Modified;
        File->Header.Accessed = Cursor->Modified;
        File->Header.Modified = Cursor->Modified;

        if ((Cursor->Attributes & FS_ATTR_FOLDER) != 0 ||
            Cursor->TocIndex == MAX_U32 ||
            Cursor->TocIndex >= ((LPPACKAGEFSFILESYSTEM)File->Header.FileSystem)->Package.TocEntryCount) {
            File->Header.SizeLow = 0;
            File->Header.SizeHigh = 0;
        } else {
            const EPK_PARSED_TOC_ENTRY* Entry =
                &((LPPACKAGEFSFILESYSTEM)File->Header.FileSystem)->Package.TocEntries[Cursor->TocIndex];
            File->Header.SizeLow = U64_Low32(Entry->FileSize);
            File->Header.SizeHigh = U64_High32(Entry->FileSize);
        }

        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_NO_MORE;
}

/************************************************************************/

/**
 * @brief Open file or folder in PackageFS.
 * @param Info Open request.
 * @return Opened file handle or NULL.
 */
static LPPACKAGEFSFILE OpenFile(LPFILEINFO Info) {
    LPPACKAGEFSFILESYSTEM FileSystem;
    STR PathText[MAX_PATH_NAME];
    LPSTR LastSlash;
    BOOL Wildcard = FALSE;
    LPPACKAGEFS_NODE Node;
    LPPACKAGEFSFILE File;

    if (Info == NULL || Info->FileSystem == NULL) {
        return NULL;
    }

    FileSystem = (LPPACKAGEFSFILESYSTEM)Info->FileSystem;

    StringCopy(PathText, Info->Name);
    if (PathText[0] == STR_NULL) {
        StringCopy(PathText, TEXT("/"));
    }

    LockMutex(&FileSystem->FilesMutex, INFINITY);

    if ((Info->Flags & FILE_OPEN_WRITE) != 0 ||
        (Info->Flags & FILE_OPEN_APPEND) != 0 ||
        (Info->Flags & FILE_OPEN_TRUNCATE) != 0 ||
        (Info->Flags & FILE_OPEN_CREATE_ALWAYS) != 0) {
        UnlockMutex(&FileSystem->FilesMutex);
        return NULL;
    }

    if (StringFindChar(PathText, '*') != NULL || StringFindChar(PathText, '?') != NULL) {
        STR PatternPath[MAX_PATH_NAME];
        STR Pattern[MAX_FILE_NAME];

        Wildcard = TRUE;
        StringCopy(PatternPath, PathText);
        LastSlash = StringFindCharR(PatternPath, PATH_SEP);
        if (LastSlash != NULL) {
            StringCopy(Pattern, LastSlash + 1);
            *LastSlash = STR_NULL;
            if (PatternPath[0] == STR_NULL) {
                StringCopy(PatternPath, TEXT("/"));
            }
        } else {
            StringCopy(Pattern, PatternPath);
            StringCopy(PatternPath, TEXT("/"));
        }

        Node = PackageFSResolvePath(FileSystem, PatternPath, TRUE);
        if (Node == NULL || (Node->Attributes & FS_ATTR_FOLDER) == 0) {
            UnlockMutex(&FileSystem->FilesMutex);
            return NULL;
        }

        File = PackageFSCreateFileObject(FileSystem, Node);
        if (File == NULL) {
            UnlockMutex(&FileSystem->FilesMutex);
            return NULL;
        }

        File->Enumerate = TRUE;
        StringCopy(File->Pattern, Pattern);
        File->EnumerationCursor = Node->FirstChild;

        if (PackageFSAdvanceEnumeration(File) != DF_RETURN_SUCCESS) {
            ReleaseKernelObject(File);
            UnlockMutex(&FileSystem->FilesMutex);
            return NULL;
        }

        UnlockMutex(&FileSystem->FilesMutex);
        return File;
    }

    Node = PackageFSResolvePath(FileSystem, PathText, FALSE);
    if (Node == NULL) {
        UnlockMutex(&FileSystem->FilesMutex);
        return NULL;
    }

    File = PackageFSCreateFileObject(FileSystem, Node);
    if (File == NULL) {
        UnlockMutex(&FileSystem->FilesMutex);
        return NULL;
    }

    File->Enumerate = Wildcard;

    UnlockMutex(&FileSystem->FilesMutex);
    return File;
}

/************************************************************************/

/**
 * @brief Enumerate next folder entry.
 * @param File Enumeration file handle.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 OpenNext(LPPACKAGEFSFILE File) {
    if (File == NULL || File->Header.TypeID != KOID_FILE) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (!File->Enumerate) {
        return DF_RETURN_GENERIC;
    }

    return PackageFSAdvanceEnumeration(File);
}

/************************************************************************/

/**
 * @brief Close PackageFS file handle.
 * @param File File handle.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 CloseFile(LPPACKAGEFSFILE File) {
    if (File == NULL || File->Header.TypeID != KOID_FILE) {
        return DF_RETURN_BAD_PARAMETER;
    }

    ReleaseKernelObject(File);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Read file bytes from PackageFS.
 * @param File File handle.
 * @return DF_RETURN_SUCCESS when read succeeds.
 */
static U32 ReadFile(LPPACKAGEFSFILE File) {
    LPPACKAGEFSFILESYSTEM FileSystem;
    const EPK_PARSED_TOC_ENTRY* Entry;
    U32 DataSize;
    U32 Remaining;
    U32 ReadBytes;
    U32 Position;

    if (File == NULL || File->Header.TypeID != KOID_FILE) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (File->Header.Buffer == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if ((File->Header.OpenFlags & FILE_OPEN_READ) == 0) {
        return DF_RETURN_NO_PERMISSION;
    }

    if (File->Node == NULL || (File->Node->Attributes & FS_ATTR_FOLDER) != 0) {
        return DF_RETURN_GENERIC;
    }

    if (File->Node->TocIndex == MAX_U32) {
        return DF_RETURN_GENERIC;
    }

    FileSystem = (LPPACKAGEFSFILESYSTEM)File->Header.FileSystem;
    if (FileSystem == NULL || File->Node->TocIndex >= FileSystem->Package.TocEntryCount) {
        return DF_RETURN_GENERIC;
    }

    Entry = &FileSystem->Package.TocEntries[File->Node->TocIndex];
    File->Header.BytesTransferred = 0;

    if ((Entry->EntryFlags & EPK_TOC_ENTRY_FLAG_HAS_INLINE_DATA) == 0) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    DataSize = Entry->InlineDataSize;
    Position = (U32)File->Header.Position;
    if (Position >= DataSize || File->Header.ByteCount == 0) {
        return DF_RETURN_SUCCESS;
    }

    Remaining = DataSize - Position;
    ReadBytes = File->Header.ByteCount;
    if (ReadBytes > Remaining) {
        ReadBytes = Remaining;
    }

    MemoryCopy(File->Header.Buffer,
        FileSystem->Package.PackageBytes + U64_Low32(Entry->InlineDataOffset) + Position,
        ReadBytes);

    File->Header.Position += ReadBytes;
    File->Header.BytesTransferred = ReadBytes;

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Reject write operations on PackageFS.
 * @param File File handle.
 * @return DF_RETURN_NO_PERMISSION always.
 */
static U32 WriteFile(LPPACKAGEFSFILE File) {
    UNUSED(File);
    return DF_RETURN_NO_PERMISSION;
}

/************************************************************************/

/**
 * @brief Check whether a path exists in PackageFS.
 * @param Check Path check structure.
 * @return TRUE when path resolves to a folder.
 */
static BOOL PathExists(LPFS_PATHCHECK Check) {
    UNUSED(Check);
    return FALSE;
}

/************************************************************************/

/**
 * @brief Check whether one file or folder exists in PackageFS.
 * @param Info File info containing target path.
 * @return TRUE when target exists.
 */
static BOOL FileExists(LPFILEINFO Info) {
    LPPACKAGEFSFILESYSTEM FileSystem;
    LPPACKAGEFS_NODE Node;
    STR FullPath[MAX_PATH_NAME];

    if (Info == NULL || Info->FileSystem == NULL) {
        return FALSE;
    }

    FileSystem = (LPPACKAGEFSFILESYSTEM)Info->FileSystem;

    if (Info->Name[0] == PATH_SEP) {
        StringCopy(FullPath, Info->Name);
    } else {
        StringCopy(FullPath, TEXT("/"));
        StringConcat(FullPath, Info->Name);
    }

    Node = PackageFSResolvePath(FileSystem, FullPath, FALSE);
    return Node != NULL;
}

/************************************************************************/

/**
 * @brief Build and mount one PackageFS from validated package bytes.
 * @param PackageBytes Package bytes.
 * @param PackageSize Package size.
 * @param VolumeName Filesystem name.
 * @param Options Parser options.
 * @param MountedFileSystemOut Optional output pointer.
 * @return DF_RETURN_SUCCESS on success.
 */
U32 PackageFSMountFromBuffer(LPCVOID PackageBytes,
                             U32 PackageSize,
                             LPCSTR VolumeName,
                             const EPK_PARSER_OPTIONS* Options,
                             LPFILESYSTEM* MountedFileSystemOut) {
    LPPACKAGEFSFILESYSTEM FileSystem;
    EPK_PARSER_OPTIONS EffectiveOptions = {
        .VerifyPackageHash = TRUE,
        .VerifySignature = TRUE,
        .RequireSignature = FALSE};
    U32 ValidationStatus;
    U32 Result;

    if (PackageBytes == NULL || PackageSize == 0 || STRING_EMPTY(VolumeName)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Options != NULL) {
        EffectiveOptions = *Options;
    }

    FileSystem = (LPPACKAGEFSFILESYSTEM)CreateKernelObject(sizeof(PACKAGEFSFILESYSTEM), KOID_FILESYSTEM);
    if (FileSystem == NULL) {
        return DF_RETURN_NO_MEMORY;
    }

    FileSystem->Root = NULL;
    FileSystem->PackageBytes = NULL;
    FileSystem->PackageSize = 0;
    MemorySet(&FileSystem->Package, 0, sizeof(EPK_VALIDATED_PACKAGE));
    FileSystem->Header.Mounted = TRUE;
    FileSystem->Header.Driver = &PackageFSDriver;
    FileSystem->Header.StorageUnit = NULL;
    FileSystem->Header.Partition.Scheme = PARTITION_SCHEME_VIRTUAL;
    FileSystem->Header.Partition.Type = FSID_NONE;
    FileSystem->Header.Partition.Format = PARTITION_FORMAT_UNKNOWN;
    FileSystem->Header.Partition.Index = 0;
    FileSystem->Header.Partition.Flags = 0;
    FileSystem->Header.Partition.StartSector = 0;
    FileSystem->Header.Partition.NumSectors = 0;
    MemorySet(FileSystem->Header.Partition.TypeGuid, 0, GPT_GUID_LENGTH);
    StringCopy(FileSystem->Header.Name, VolumeName);

    InitMutex(&FileSystem->Header.Mutex);
    InitMutex(&FileSystem->FilesMutex);

    FileSystem->PackageBytes = (U8*)KernelHeapAlloc(PackageSize);
    if (FileSystem->PackageBytes == NULL) {
        ReleaseKernelObject(FileSystem);
        return DF_RETURN_NO_MEMORY;
    }

    MemoryCopy(FileSystem->PackageBytes, PackageBytes, PackageSize);
    FileSystem->PackageSize = PackageSize;

    ValidationStatus = EpkValidatePackageBuffer(FileSystem->PackageBytes,
                                                FileSystem->PackageSize,
                                                &EffectiveOptions,
                                                &FileSystem->Package);
    if (ValidationStatus != EPK_VALIDATION_OK) {
        ERROR(TEXT("[PackageFSMountFromBuffer] Package validation failed status=%u"), ValidationStatus);
        KernelHeapFree(FileSystem->PackageBytes);
        ReleaseKernelObject(FileSystem);
        return DF_RETURN_BAD_PARAMETER;
    }

    Result = PackageFSBuildTree(FileSystem);
    if (Result != DF_RETURN_SUCCESS) {
        ERROR(TEXT("[PackageFSMountFromBuffer] Tree build failed status=%u"), Result);
        EpkReleaseValidatedPackage(&FileSystem->Package);
        KernelHeapFree(FileSystem->PackageBytes);
        ReleaseKernelObject(FileSystem);
        return Result;
    }

    LockMutex(MUTEX_FILESYSTEM, INFINITY);
    ListAddItem(GetFileSystemList(), &FileSystem->Header);
    UnlockMutex(MUTEX_FILESYSTEM);

    if (FileSystemReady()) {
        if (!SystemFSMountFileSystem(&FileSystem->Header)) {
            WARNING(TEXT("[PackageFSMountFromBuffer] SystemFS mount failed for %s"), FileSystem->Header.Name);
        }
    }

    if (MountedFileSystemOut != NULL) {
        *MountedFileSystemOut = &FileSystem->Header;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Unmount one PackageFS instance and release resources.
 * @param FileSystem Filesystem pointer to unmount.
 * @return TRUE on success.
 */
BOOL PackageFSUnmount(LPFILESYSTEM FileSystem) {
    LPPACKAGEFSFILESYSTEM This;
    LPLISTNODE Node;

    if (FileSystem == NULL || FileSystem->Driver != &PackageFSDriver) {
        return FALSE;
    }

    This = (LPPACKAGEFSFILESYSTEM)FileSystem;

    LockMutex(MUTEX_FILESYSTEM, INFINITY);

    for (Node = GetFileList()->First; Node != NULL; Node = Node->Next) {
        LPFILE Open = (LPFILE)Node;
        if (Open->FileSystem == FileSystem) {
            UnlockMutex(MUTEX_FILESYSTEM);
            WARNING(TEXT("[PackageFSUnmount] Cannot unmount %s while files are open"), FileSystem->Name);
            return FALSE;
        }
    }

    if (FileSystemReady()) {
        SystemFSUnmountFileSystem(FileSystem);
    }

    ListErase(GetFileSystemList(), FileSystem);
    FileSystem->Mounted = FALSE;
    UnlockMutex(MUTEX_FILESYSTEM);

    PackageFSReleaseNodeTree(This->Root);
    This->Root = NULL;

    EpkReleaseValidatedPackage(&This->Package);

    if (This->PackageBytes != NULL) {
        KernelHeapFree(This->PackageBytes);
        This->PackageBytes = NULL;
    }

    ReleaseKernelObject(This);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Return mounted volume label.
 * @param Info Volume information structure.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 GetVolumeInfo(LPVOLUMEINFO Info) {
    LPFILESYSTEM FileSystem;

    if (Info == NULL || Info->Size != sizeof(VOLUMEINFO) || Info->Volume == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }

    FileSystem = (LPFILESYSTEM)Info->Volume;
    if (FileSystem == NULL || FileSystem->Driver != &PackageFSDriver) {
        return DF_RETURN_BAD_PARAMETER;
    }

    StringCopy(Info->Name, FileSystem->Name);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief PackageFS driver command dispatcher.
 * @param Function Command identifier.
 * @param Parameter Optional command parameter.
 * @return Command-specific status.
 */
static UINT PackageFSCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return Initialize();
        case DF_GET_VERSION:
            return MAKE_VERSION(VER_MAJOR, VER_MINOR);
        case DF_FS_GETVOLUMEINFO:
            return GetVolumeInfo((LPVOLUMEINFO)Parameter);
        case DF_FS_SETVOLUMEINFO:
            return DF_RETURN_NO_PERMISSION;
        case DF_FS_CREATEFOLDER:
        case DF_FS_DELETEFOLDER:
        case DF_FS_RENAMEFOLDER:
        case DF_FS_DELETEFILE:
        case DF_FS_RENAMEFILE:
        case DF_FS_SETATTRIBUTES:
            return DF_RETURN_NO_PERMISSION;
        case DF_FS_OPENFILE:
            return (UINT)OpenFile((LPFILEINFO)Parameter);
        case DF_FS_OPENNEXT:
            return OpenNext((LPPACKAGEFSFILE)Parameter);
        case DF_FS_CLOSEFILE:
            return CloseFile((LPPACKAGEFSFILE)Parameter);
        case DF_FS_READ:
            return ReadFile((LPPACKAGEFSFILE)Parameter);
        case DF_FS_WRITE:
            return WriteFile((LPPACKAGEFSFILE)Parameter);
        case DF_FS_PATHEXISTS:
            return (UINT)PathExists((LPFS_PATHCHECK)Parameter);
        case DF_FS_FILEEXISTS:
            return (UINT)FileExists((LPFILEINFO)Parameter);
        case DF_FS_GETPOSITION:
        case DF_FS_SETPOSITION:
        case DF_FS_GETATTRIBUTES:
        case DF_FS_CREATEPARTITION:
        case DF_FS_MOUNTOBJECT:
        case DF_FS_UNMOUNTOBJECT:
        default:
            return DF_RETURN_NOT_IMPLEMENTED;
    }
}
