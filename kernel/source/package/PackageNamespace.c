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


    Package namespace integration

\************************************************************************/

#include "package/PackageNamespace.h"

#include "CoreString.h"
#include "File.h"
#include "Heap.h"
#include "KernelData.h"
#include "Log.h"
#include "SystemFS.h"
#include "package/PackageFS.h"
#include "utils/Helpers.h"

/***************************************************************************/

static const STR PackageNamespaceLibraryRoot[] = "/library/package";
static const STR PackageNamespaceAppsRoot[] = "/apps";
static const STR PackageNamespaceUsersRoot[] = "/users";
static const STR PackageNamespaceCurrentUserAlias[] = "/current-user";
static const STR PackageNamespacePrivatePackageAlias[] = "/package";
static const STR PackageNamespacePrivateUserDataAlias[] = "/user-data";

/***************************************************************************/

/**
 * @brief Check whether a node exists in SystemFS.
 * @param Path Absolute path.
 * @return TRUE when the path exists and resolves as a folder.
 */
static BOOL PackageNamespacePathExists(LPCSTR Path) {
    FS_PATHCHECK Check;

    if (Path == NULL || Path[0] != PATH_SEP) return FALSE;

    StringCopy(Check.CurrentFolder, TEXT("/"));
    StringCopy(Check.SubFolder, Path);
    return (BOOL)GetSystemFS()->Driver->Command(DF_FS_PATHEXISTS, (UINT)&Check);
}

/***************************************************************************/

/**
 * @brief Create a folder in SystemFS if it is not already present.
 * @param Path Absolute folder path.
 * @return TRUE on success.
 */
static BOOL PackageNamespaceEnsureFolder(LPCSTR Path) {
    FILEINFO Info;
    U32 Result;

    if (Path == NULL || Path[0] != PATH_SEP) return FALSE;
    if (PackageNamespacePathExists(Path)) return TRUE;

    Info.Size = sizeof(FILEINFO);
    Info.FileSystem = GetSystemFS();
    Info.Attributes = FS_ATTR_FOLDER;
    Info.Flags = 0;
    StringCopy(Info.Name, Path);

    Result = GetSystemFS()->Driver->Command(DF_FS_CREATEFOLDER, (UINT)&Info);
    if (Result != DF_RETURN_SUCCESS) {
        WARNING(TEXT("[PackageNamespaceEnsureFolder] Create folder failed path=%s status=%u"),
            Path,
            Result);
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Mount a filesystem at one absolute SystemFS path.
 * @param FileSystem Filesystem to mount.
 * @param Path Absolute SystemFS mount path.
 * @param SourcePath Optional source folder in mounted filesystem.
 * @return TRUE when mounted or already present.
 */
static BOOL PackageNamespaceMountPath(LPFILESYSTEM FileSystem, LPCSTR Path, LPCSTR SourcePath) {
    FS_MOUNT_CONTROL Control;
    U32 Result;

    if (FileSystem == NULL || Path == NULL || Path[0] != PATH_SEP) return FALSE;

    if (PackageNamespacePathExists(Path)) return TRUE;

    StringCopy(Control.Path, Path);
    Control.Node = (LPLISTNODE)FileSystem;
    if (SourcePath != NULL && SourcePath[0] != STR_NULL) {
        StringCopy(Control.SourcePath, SourcePath);
    } else {
        Control.SourcePath[0] = STR_NULL;
    }

    Result = GetSystemFS()->Driver->Command(DF_FS_MOUNTOBJECT, (UINT)&Control);
    if (Result != DF_RETURN_SUCCESS) {
        WARNING(TEXT("[PackageNamespaceMountPath] Mount failed path=%s status=%u"), Path, Result);
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Build "Base/Name" path.
 * @param Base Path prefix.
 * @param Name Last path component.
 * @param OutPath Output absolute path.
 * @return TRUE on success.
 */
static BOOL PackageNamespaceBuildChildPath(LPCSTR Base, LPCSTR Name, STR OutPath[MAX_PATH_NAME]) {
    U32 Length;

    if (Base == NULL || Name == NULL || OutPath == NULL) return FALSE;

    StringCopy(OutPath, Base);
    Length = StringLength(OutPath);
    if (Length == 0 || OutPath[Length - 1] != PATH_SEP) {
        StringConcat(OutPath, TEXT("/"));
    }
    StringConcat(OutPath, Name);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Build wildcard enumeration pattern for one folder.
 * @param Folder Absolute folder path.
 * @param OutPattern Output pattern "folder + slash + wildcard".
 * @return TRUE on success.
 */
static BOOL PackageNamespaceBuildEnumeratePattern(LPCSTR Folder, STR OutPattern[MAX_PATH_NAME]) {
    if (Folder == NULL || OutPattern == NULL) return FALSE;

    StringCopy(OutPattern, Folder);
    if (OutPattern[StringLength(OutPattern) - 1] != PATH_SEP) {
        StringConcat(OutPattern, TEXT("/"));
    }
    StringConcat(OutPattern, TEXT("*"));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Check if one entry name is "." or "..".
 * @param Name Entry name.
 * @return TRUE for special entries.
 */
static BOOL PackageNamespaceIsDotEntry(LPCSTR Name) {
    if (Name == NULL) return TRUE;
    if (StringCompare(Name, TEXT(".")) == 0) return TRUE;
    if (StringCompare(Name, TEXT("..")) == 0) return TRUE;
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Check whether a file name ends with ".epk" (case insensitive).
 * @param Name File name.
 * @return TRUE when the name has ".epk" extension.
 */
static BOOL PackageNamespaceHasEpkExtension(LPCSTR Name) {
    U32 NameLength;
    LPCSTR Extension;

    if (Name == NULL) return FALSE;
    NameLength = StringLength(Name);
    if (NameLength <= 4) return FALSE;

    Extension = Name + (NameLength - 4);
    return StringCompareNC(Extension, TEXT(".epk")) == 0;
}

/***************************************************************************/

/**
 * @brief Extract package name from one ".epk" file name.
 * @param FileName Input file name.
 * @param OutName Package logical name.
 * @return TRUE on success.
 */
static BOOL PackageNamespaceExtractPackageName(LPCSTR FileName, STR OutName[MAX_FILE_NAME]) {
    U32 Length;
    U32 CopyLength;

    if (FileName == NULL || OutName == NULL) return FALSE;
    if (!PackageNamespaceHasEpkExtension(FileName)) return FALSE;

    Length = StringLength(FileName);
    CopyLength = Length - 4;
    if (CopyLength == 0 || CopyLength >= MAX_FILE_NAME) return FALSE;

    StringCopyNum(OutName, FileName, CopyLength);
    OutName[CopyLength] = STR_NULL;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Build package filesystem volume name from role and package name.
 * @param RolePrefix Role prefix.
 * @param PackageName Package name.
 * @param UserName Optional user name for user role packages.
 * @param OutVolume Output logical filesystem name.
 */
static void PackageNamespaceBuildVolumeName(LPCSTR RolePrefix,
                                            LPCSTR PackageName,
                                            LPCSTR UserName,
                                            STR OutVolume[MAX_FS_LOGICAL_NAME]) {
    STR Temp[MAX_PATH_NAME];

    Temp[0] = STR_NULL;
    StringCopy(Temp, RolePrefix);
    StringConcat(Temp, TEXT("."));
    if (UserName != NULL && UserName[0] != STR_NULL) {
        StringConcat(Temp, UserName);
        StringConcat(Temp, TEXT("."));
    }
    StringConcat(Temp, PackageName);

    StringCopyLimit(OutVolume, Temp, MAX_FS_LOGICAL_NAME - 1);
}

/***************************************************************************/

/**
 * @brief Mount one package file and attach it to one target namespace path.
 * @param PackageFilePath Absolute package file path.
 * @param TargetPath Absolute mount target.
 * @param RolePrefix Volume role prefix.
 * @param PackageName Package logical name.
 * @param UserName Optional user name.
 */
static void PackageNamespaceMountOnePackage(LPCSTR PackageFilePath,
                                            LPCSTR TargetPath,
                                            LPCSTR RolePrefix,
                                            LPCSTR PackageName,
                                            LPCSTR UserName) {
    STR VolumeName[MAX_FS_LOGICAL_NAME];
    LPVOID PackageBytes;
    UINT PackageSize = 0;
    LPFILESYSTEM Mounted = NULL;
    U32 MountStatus;

    if (PackageFilePath == NULL || TargetPath == NULL) return;
    if (PackageNamespacePathExists(TargetPath)) return;

    PackageBytes = FileReadAll(PackageFilePath, &PackageSize);
    if (PackageBytes == NULL || PackageSize == 0) {
        WARNING(TEXT("[PackageNamespaceMountOnePackage] Cannot read package file %s"), PackageFilePath);
        return;
    }

    PackageNamespaceBuildVolumeName(RolePrefix, PackageName, UserName, VolumeName);
    MountStatus = PackageFSMountFromBuffer(PackageBytes, (U32)PackageSize, VolumeName, NULL, &Mounted);
    KernelHeapFree(PackageBytes);

    if (MountStatus != DF_RETURN_SUCCESS || Mounted == NULL) {
        WARNING(TEXT("[PackageNamespaceMountOnePackage] Package mount failed file=%s status=%u"),
            PackageFilePath,
            MountStatus);
        return;
    }

    if (!PackageNamespaceMountPath(Mounted, TargetPath, NULL)) {
        WARNING(TEXT("[PackageNamespaceMountOnePackage] Namespace mount failed path=%s"), TargetPath);
    }
}

/***************************************************************************/

/**
 * @brief Scan one folder for package files and mount discovered packages.
 * @param PackageFolder Folder containing package files.
 * @param MountRoot Base mount root for mounted package volumes.
 * @param RolePrefix Volume role prefix.
 * @param UserName Optional user name (user package role only).
 */
static void PackageNamespaceScanPackageFolder(LPCSTR PackageFolder,
                                              LPCSTR MountRoot,
                                              LPCSTR RolePrefix,
                                              LPCSTR UserName) {
    FILEINFO Find;
    LPFILE Entry;
    STR Pattern[MAX_PATH_NAME];

    if (PackageFolder == NULL || MountRoot == NULL || RolePrefix == NULL) return;
    if (!PackageNamespacePathExists(PackageFolder)) return;
    if (!PackageNamespaceEnsureFolder(MountRoot)) return;

    PackageNamespaceBuildEnumeratePattern(PackageFolder, Pattern);

    Find.Size = sizeof(FILEINFO);
    Find.FileSystem = GetSystemFS();
    Find.Attributes = MAX_U32;
    Find.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;
    StringCopy(Find.Name, Pattern);

    Entry = (LPFILE)GetSystemFS()->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);
    if (Entry == NULL) {
        return;
    }

    do {
        STR PackageName[MAX_FILE_NAME];
        STR FilePath[MAX_PATH_NAME];
        STR TargetPath[MAX_PATH_NAME];

        if ((Entry->Attributes & FS_ATTR_FOLDER) != 0) continue;
        if (PackageNamespaceIsDotEntry(Entry->Name)) continue;
        if (!PackageNamespaceExtractPackageName(Entry->Name, PackageName)) continue;

        PackageNamespaceBuildChildPath(PackageFolder, Entry->Name, FilePath);
        PackageNamespaceBuildChildPath(MountRoot, PackageName, TargetPath);
        PackageNamespaceMountOnePackage(FilePath, TargetPath, RolePrefix, PackageName, UserName);
    } while (GetSystemFS()->Driver->Command(DF_FS_OPENNEXT, (UINT)Entry) == DF_RETURN_SUCCESS);

    GetSystemFS()->Driver->Command(DF_FS_CLOSEFILE, (UINT)Entry);
}

/***************************************************************************/

/**
 * @brief Return active filesystem object from global file system list.
 * @return Active filesystem pointer or NULL when unavailable.
 */
static LPFILESYSTEM PackageNamespaceGetActiveFileSystem(void) {
    FILESYSTEM_GLOBAL_INFO* GlobalInfo = GetFileSystemGlobalInfo();
    LPLIST FileSystemList = GetFileSystemList();
    LPLISTNODE Node;

    if (GlobalInfo == NULL || FileSystemList == NULL) return NULL;
    if (StringEmpty(GlobalInfo->ActivePartitionName)) return NULL;

    for (Node = FileSystemList->First; Node != NULL; Node = Node->Next) {
        LPFILESYSTEM FileSystem = (LPFILESYSTEM)Node;
        if (FileSystem == GetSystemFS()) continue;
        if (STRINGS_EQUAL(FileSystem->Name, GlobalInfo->ActivePartitionName)) {
            return FileSystem;
        }
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Update "/current-user" alias mount to one concrete user folder.
 * @param UserName User folder name.
 * @return TRUE when alias mount is available.
 */
static BOOL PackageNamespaceBindCurrentUserAlias(LPCSTR UserName) {
    STR SourcePath[MAX_PATH_NAME];
    LPFILESYSTEM ActiveFileSystem;

    if (UserName == NULL || UserName[0] == STR_NULL) return FALSE;
    ActiveFileSystem = PackageNamespaceGetActiveFileSystem();
    if (ActiveFileSystem == NULL) return FALSE;

    PackageNamespaceBuildChildPath(PackageNamespaceUsersRoot, UserName, SourcePath);
    if (!PackageNamespacePathExists(SourcePath)) return FALSE;
    return PackageNamespaceMountPath(ActiveFileSystem, PackageNamespaceCurrentUserAlias, SourcePath);
}

/***************************************************************************/

/**
 * @brief Scan user package folders and mount per-user package files.
 */
static void PackageNamespaceScanUserPackageFolders(void) {
    FILEINFO Find;
    LPFILE UserEntry;
    STR Pattern[MAX_PATH_NAME];

    if (!PackageNamespacePathExists(PackageNamespaceUsersRoot)) return;

    PackageNamespaceBuildEnumeratePattern(PackageNamespaceUsersRoot, Pattern);

    Find.Size = sizeof(FILEINFO);
    Find.FileSystem = GetSystemFS();
    Find.Attributes = MAX_U32;
    Find.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;
    StringCopy(Find.Name, Pattern);

    UserEntry = (LPFILE)GetSystemFS()->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);
    if (UserEntry == NULL) return;

    do {
        STR UserPackageFolder[MAX_PATH_NAME];
        STR UserMountRoot[MAX_PATH_NAME];

        if ((UserEntry->Attributes & FS_ATTR_FOLDER) == 0) continue;
        if (PackageNamespaceIsDotEntry(UserEntry->Name)) continue;

        PackageNamespaceBuildChildPath(PackageNamespaceUsersRoot, UserEntry->Name, UserPackageFolder);
        PackageNamespaceBuildChildPath(UserPackageFolder, TEXT("package"), UserPackageFolder);

        PackageNamespaceBuildChildPath(PackageNamespaceUsersRoot, UserEntry->Name, UserMountRoot);
        PackageNamespaceBuildChildPath(UserMountRoot, TEXT("package"), UserMountRoot);

        PackageNamespaceScanPackageFolder(UserPackageFolder, UserMountRoot, TEXT("pkg.user"), UserEntry->Name);
    } while (GetSystemFS()->Driver->Command(DF_FS_OPENNEXT, (UINT)UserEntry) == DF_RETURN_SUCCESS);

    GetSystemFS()->Driver->Command(DF_FS_CLOSEFILE, (UINT)UserEntry);
}

/***************************************************************************/

/**
 * @brief Scan configured global package sources and mount packages by role.
 * @return TRUE when scan completed.
 */
BOOL PackageNamespaceInitialize(void) {
    LPUSERACCOUNT CurrentUser = GetCurrentUser();

    if (!FileSystemReady()) {
        return FALSE;
    }

    PackageNamespaceEnsureFolder(PackageNamespaceLibraryRoot);
    PackageNamespaceEnsureFolder(PackageNamespaceAppsRoot);
    PackageNamespaceEnsureFolder(PackageNamespaceUsersRoot);

    PackageNamespaceScanPackageFolder(PackageNamespaceLibraryRoot,
        PackageNamespaceLibraryRoot,
        TEXT("pkg.library"),
        NULL);
    PackageNamespaceScanPackageFolder(PackageNamespaceAppsRoot,
        PackageNamespaceAppsRoot,
        TEXT("pkg.app"),
        NULL);
    PackageNamespaceScanUserPackageFolders();

    if (CurrentUser != NULL) {
        PackageNamespaceBindCurrentUserAlias(CurrentUser->UserName);
    } else {
        PackageNamespaceBindCurrentUserAlias(TEXT("root"));
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Bind package-local process aliases "/package" and "/user-data".
 * @param PackageFileSystem Mounted package filesystem.
 * @param PackageName Package name used for user data path routing.
 * @return TRUE when aliases are mounted.
 */
BOOL PackageNamespaceBindCurrentProcessPackageView(LPFILESYSTEM PackageFileSystem, LPCSTR PackageName) {
    LPUSERACCOUNT CurrentUser = GetCurrentUser();
    LPFILESYSTEM ActiveFileSystem = NULL;
    STR UserDataSourcePath[MAX_PATH_NAME];

    if (PackageFileSystem == NULL || STRING_EMPTY(PackageName)) return FALSE;

    if (!PackageNamespaceMountPath(PackageFileSystem, PackageNamespacePrivatePackageAlias, NULL)) {
        return FALSE;
    }

    if (CurrentUser == NULL) return FALSE;
    ActiveFileSystem = PackageNamespaceGetActiveFileSystem();
    if (ActiveFileSystem == NULL) return FALSE;

    UserDataSourcePath[0] = STR_NULL;
    PackageNamespaceBuildChildPath(PackageNamespaceUsersRoot, CurrentUser->UserName, UserDataSourcePath);
    PackageNamespaceBuildChildPath(UserDataSourcePath, PackageName, UserDataSourcePath);
    PackageNamespaceBuildChildPath(UserDataSourcePath, TEXT("data"), UserDataSourcePath);

    if (!PackageNamespacePathExists(UserDataSourcePath)) {
        return FALSE;
    }

    return PackageNamespaceMountPath(ActiveFileSystem, PackageNamespacePrivateUserDataAlias, UserDataSourcePath);
}
