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
#include "package/PackageManifest.h"
#include "utils/Helpers.h"
#include "utils/KernelPath.h"

/***************************************************************************/

#define PACKAGE_NAMESPACE_ROLE_LIBRARY TEXT("pkg.library")
#define PACKAGE_NAMESPACE_ROLE_APPLICATION TEXT("pkg.app")
#define PACKAGE_NAMESPACE_ROLE_USER TEXT("pkg.user")

/***************************************************************************/

typedef struct tag_PACKAGENAMESPACE_PATHS {
    STR LibraryRoot[MAX_PATH_NAME];
    STR AppsRoot[MAX_PATH_NAME];
    STR UsersRoot[MAX_PATH_NAME];
    STR CurrentUserAlias[MAX_PATH_NAME];
    STR PrivatePackageAlias[MAX_PATH_NAME];
    STR PrivateUserDataAlias[MAX_PATH_NAME];
    BOOL Loaded;
} PACKAGENAMESPACE_PATHS;

typedef struct tag_PACKAGENAMESPACE_PROVIDER_INDEX {
    LPSTR* Contracts;
    UINT Count;
    UINT Capacity;
} PACKAGENAMESPACE_PROVIDER_INDEX, *LPPACKAGENAMESPACE_PROVIDER_INDEX;

typedef struct tag_PACKAGENAMESPACE_SCAN_ENTRY {
    STR PackageFilePath[MAX_PATH_NAME];
    STR TargetPath[MAX_PATH_NAME];
    STR PackageName[MAX_FILE_NAME];
    STR UserName[MAX_FILE_NAME];
} PACKAGENAMESPACE_SCAN_ENTRY, *LPPACKAGENAMESPACE_SCAN_ENTRY;

typedef struct tag_PACKAGENAMESPACE_SCAN_LIST {
    LPPACKAGENAMESPACE_SCAN_ENTRY Entries;
    UINT Count;
    UINT Capacity;
} PACKAGENAMESPACE_SCAN_LIST, *LPPACKAGENAMESPACE_SCAN_LIST;

static PACKAGENAMESPACE_PATHS PackageNamespacePaths = {
    .LibraryRoot = "",
    .AppsRoot = "",
    .UsersRoot = "",
    .CurrentUserAlias = "",
    .PrivatePackageAlias = "",
    .PrivateUserDataAlias = "",
    .Loaded = FALSE};

/***************************************************************************/

/**
 * @brief Resolve package namespace paths from KernelPath configuration keys.
 * @return TRUE when all paths are resolved.
 */
static BOOL PackageNamespaceLoadPaths(void) {
    if (!KernelPathResolve(KERNEL_PATH_KEY_PACKAGES_LIBRARY,
            KERNEL_PATH_DEFAULT_PACKAGES_LIBRARY,
            PackageNamespacePaths.LibraryRoot,
            MAX_PATH_NAME)) {
        return FALSE;
    }
    if (!KernelPathResolve(KERNEL_PATH_KEY_PACKAGES_APPS,
            KERNEL_PATH_DEFAULT_PACKAGES_APPS,
            PackageNamespacePaths.AppsRoot,
            MAX_PATH_NAME)) {
        return FALSE;
    }
    if (!KernelPathResolve(KERNEL_PATH_KEY_USERS_ROOT,
            KERNEL_PATH_DEFAULT_USERS_ROOT,
            PackageNamespacePaths.UsersRoot,
            MAX_PATH_NAME)) {
        return FALSE;
    }
    if (!KernelPathResolve(KERNEL_PATH_KEY_CURRENT_USER_ALIAS,
            KERNEL_PATH_DEFAULT_CURRENT_USER_ALIAS,
            PackageNamespacePaths.CurrentUserAlias,
            MAX_PATH_NAME)) {
        return FALSE;
    }
    if (!KernelPathResolve(KERNEL_PATH_KEY_PRIVATE_PACKAGE_ALIAS,
            KERNEL_PATH_DEFAULT_PRIVATE_PACKAGE_ALIAS,
            PackageNamespacePaths.PrivatePackageAlias,
            MAX_PATH_NAME)) {
        return FALSE;
    }
    if (!KernelPathResolve(KERNEL_PATH_KEY_PRIVATE_USER_DATA_ALIAS,
            KERNEL_PATH_DEFAULT_PRIVATE_USER_DATA_ALIAS,
            PackageNamespacePaths.PrivateUserDataAlias,
            MAX_PATH_NAME)) {
        return FALSE;
    }

    PackageNamespacePaths.Loaded = TRUE;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Ensure package namespace paths are resolved before use.
 * @return TRUE when paths are loaded.
 */
static BOOL PackageNamespaceEnsurePathsLoaded(void) {
    if (PackageNamespacePaths.Loaded) {
        return TRUE;
    }

    if (!PackageNamespaceLoadPaths()) {
        ERROR(TEXT("[PackageNamespaceEnsurePathsLoaded] KernelPath resolution failed"));
        return FALSE;
    }

    return TRUE;
}

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
    U32 ExtensionLength;
    LPCSTR Extension;

    if (Name == NULL) return FALSE;
    ExtensionLength = StringLength(KERNEL_FILE_EXTENSION_PACKAGE);
    NameLength = StringLength(Name);
    if (NameLength <= ExtensionLength) return FALSE;

    Extension = Name + (NameLength - ExtensionLength);
    return StringCompareNC(Extension, KERNEL_FILE_EXTENSION_PACKAGE) == 0;
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
    U32 ExtensionLength;

    if (FileName == NULL || OutName == NULL) return FALSE;
    if (!PackageNamespaceHasEpkExtension(FileName)) return FALSE;

    ExtensionLength = StringLength(KERNEL_FILE_EXTENSION_PACKAGE);
    Length = StringLength(FileName);
    CopyLength = Length - ExtensionLength;
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
 * @brief Initialize provider index storage.
 * @param Index Provider index object.
 * @return TRUE on success.
 */
static BOOL PackageNamespaceProviderIndexInit(LPPACKAGENAMESPACE_PROVIDER_INDEX Index) {
    if (Index == NULL) return FALSE;
    Index->Contracts = NULL;
    Index->Count = 0;
    Index->Capacity = 0;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Release provider index storage.
 * @param Index Provider index object.
 */
static void PackageNamespaceProviderIndexDeinit(LPPACKAGENAMESPACE_PROVIDER_INDEX Index) {
    UINT ItemIndex;

    if (Index == NULL) return;

    if (Index->Contracts != NULL) {
        for (ItemIndex = 0; ItemIndex < Index->Count; ItemIndex++) {
            if (Index->Contracts[ItemIndex] != NULL) {
                KernelHeapFree(Index->Contracts[ItemIndex]);
            }
        }
        KernelHeapFree(Index->Contracts);
    }

    Index->Contracts = NULL;
    Index->Count = 0;
    Index->Capacity = 0;
}

/***************************************************************************/

/**
 * @brief Check whether one contract exists in provider index.
 * @param Index Provider index.
 * @param Contract Contract string.
 * @return TRUE when present.
 */
static BOOL PackageNamespaceProviderIndexHas(LPPACKAGENAMESPACE_PROVIDER_INDEX Index, LPCSTR Contract) {
    UINT ItemIndex;

    if (Index == NULL || Contract == NULL || Contract[0] == STR_NULL) return FALSE;

    for (ItemIndex = 0; ItemIndex < Index->Count; ItemIndex++) {
        if (StringCompare(Index->Contracts[ItemIndex], Contract) == 0) {
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Add one contract to provider index if missing.
 * @param Index Provider index.
 * @param Contract Contract string.
 * @return TRUE on success.
 */
static BOOL PackageNamespaceProviderIndexAdd(LPPACKAGENAMESPACE_PROVIDER_INDEX Index, LPCSTR Contract) {
    LPSTR* NewContracts;
    LPSTR ContractCopy;
    UINT NewCapacity;
    UINT CopySize;

    if (Index == NULL || Contract == NULL || Contract[0] == STR_NULL) return FALSE;
    if (PackageNamespaceProviderIndexHas(Index, Contract)) return TRUE;

    if (Index->Count == Index->Capacity) {
        NewCapacity = Index->Capacity == 0 ? 16 : Index->Capacity * 2;
        CopySize = sizeof(LPSTR) * NewCapacity;
        NewContracts = (LPSTR*)KernelHeapAlloc(CopySize);
        if (NewContracts == NULL) return FALSE;
        MemorySet(NewContracts, 0, CopySize);

        if (Index->Contracts != NULL) {
            MemoryCopy(NewContracts, Index->Contracts, sizeof(LPSTR) * Index->Count);
            KernelHeapFree(Index->Contracts);
        }

        Index->Contracts = NewContracts;
        Index->Capacity = NewCapacity;
    }

    ContractCopy = (LPSTR)KernelHeapAlloc(StringLength(Contract) + 1);
    if (ContractCopy == NULL) return FALSE;
    StringCopy(ContractCopy, Contract);

    Index->Contracts[Index->Count] = ContractCopy;
    Index->Count++;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Initialize scan list.
 * @param List Scan list object.
 */
static void PackageNamespaceScanListInit(LPPACKAGENAMESPACE_SCAN_LIST List) {
    if (List == NULL) return;
    List->Entries = NULL;
    List->Count = 0;
    List->Capacity = 0;
}

/***************************************************************************/

/**
 * @brief Release scan list storage.
 * @param List Scan list object.
 */
static void PackageNamespaceScanListDeinit(LPPACKAGENAMESPACE_SCAN_LIST List) {
    if (List == NULL) return;
    if (List->Entries != NULL) {
        KernelHeapFree(List->Entries);
    }
    List->Entries = NULL;
    List->Count = 0;
    List->Capacity = 0;
}

/***************************************************************************/

/**
 * @brief Add one scan entry.
 * @param List Scan list object.
 * @param FilePath Package file path.
 * @param TargetPath Namespace mount path.
 * @param PackageName Package logical name.
 * @param UserName Optional user scope.
 * @return TRUE on success.
 */
static BOOL PackageNamespaceScanListPush(LPPACKAGENAMESPACE_SCAN_LIST List,
                                         LPCSTR FilePath,
                                         LPCSTR TargetPath,
                                         LPCSTR PackageName,
                                         LPCSTR UserName) {
    LPPACKAGENAMESPACE_SCAN_ENTRY NewEntries;
    UINT NewCapacity;
    UINT CopySize;
    LPPACKAGENAMESPACE_SCAN_ENTRY Entry;

    if (List == NULL || FilePath == NULL || TargetPath == NULL || PackageName == NULL) return FALSE;

    if (List->Count == List->Capacity) {
        NewCapacity = List->Capacity == 0 ? 8 : List->Capacity * 2;
        CopySize = sizeof(PACKAGENAMESPACE_SCAN_ENTRY) * NewCapacity;
        NewEntries = (LPPACKAGENAMESPACE_SCAN_ENTRY)KernelHeapAlloc(CopySize);
        if (NewEntries == NULL) return FALSE;
        MemorySet(NewEntries, 0, CopySize);

        if (List->Entries != NULL) {
            MemoryCopy(NewEntries, List->Entries, sizeof(PACKAGENAMESPACE_SCAN_ENTRY) * List->Count);
            KernelHeapFree(List->Entries);
        }

        List->Entries = NewEntries;
        List->Capacity = NewCapacity;
    }

    Entry = &List->Entries[List->Count];
    MemorySet(Entry, 0, sizeof(PACKAGENAMESPACE_SCAN_ENTRY));
    StringCopy(Entry->PackageFilePath, FilePath);
    StringCopy(Entry->TargetPath, TargetPath);
    StringCopy(Entry->PackageName, PackageName);
    if (UserName != NULL) {
        StringCopy(Entry->UserName, UserName);
    } else {
        Entry->UserName[0] = STR_NULL;
    }
    List->Count++;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Sort scan list by package name then file path.
 * @param List Scan list object.
 */
static void PackageNamespaceScanListSort(LPPACKAGENAMESPACE_SCAN_LIST List) {
    UINT Outer;
    UINT Inner;

    if (List == NULL || List->Count < 2) return;

    for (Outer = 0; Outer < List->Count; Outer++) {
        for (Inner = Outer + 1; Inner < List->Count; Inner++) {
            LPPACKAGENAMESPACE_SCAN_ENTRY Left = &List->Entries[Outer];
            LPPACKAGENAMESPACE_SCAN_ENTRY Right = &List->Entries[Inner];
            BOOL Swap = FALSE;
            I32 Compare = StringCompare(Left->PackageName, Right->PackageName);

            if (Compare > 0) {
                Swap = TRUE;
            } else if (Compare == 0 && StringCompare(Left->PackageFilePath, Right->PackageFilePath) > 0) {
                Swap = TRUE;
            }

            if (Swap) {
                PACKAGENAMESPACE_SCAN_ENTRY Temp = *Left;
                *Left = *Right;
                *Right = Temp;
            }
        }
    }
}

/***************************************************************************/

/**
 * @brief Mount one package from memory and attach to namespace target.
 * @param PackageBytes Package byte buffer.
 * @param PackageSize Package byte size.
 * @param TargetPath Namespace mount target.
 * @param RolePrefix Volume role prefix.
 * @param PackageName Package logical name.
 * @param UserName Optional user name.
 * @return TRUE when mount succeeds.
 */
static BOOL PackageNamespaceMountOnePackageBuffer(LPCVOID PackageBytes,
                                                  UINT PackageSize,
                                                  LPCSTR TargetPath,
                                                  LPCSTR RolePrefix,
                                                  LPCSTR PackageName,
                                                  LPCSTR UserName) {
    STR VolumeName[MAX_FS_LOGICAL_NAME];
    LPFILESYSTEM Mounted = NULL;
    U32 MountStatus;

    if (PackageBytes == NULL || PackageSize == 0 || TargetPath == NULL || RolePrefix == NULL ||
        PackageName == NULL) {
        return FALSE;
    }
    if (PackageNamespacePathExists(TargetPath)) return TRUE;

    PackageNamespaceBuildVolumeName(RolePrefix, PackageName, UserName, VolumeName);
    MountStatus = PackageFSMountFromBuffer(PackageBytes, (U32)PackageSize, VolumeName, NULL, &Mounted);
    if (MountStatus != DF_RETURN_SUCCESS || Mounted == NULL) {
        WARNING(TEXT("[PackageNamespaceMountOnePackageBuffer] Package mount failed package=%s status=%u"),
            PackageName,
            MountStatus);
        return FALSE;
    }

    if (!PackageNamespaceMountPath(Mounted, TargetPath, NULL)) {
        WARNING(TEXT("[PackageNamespaceMountOnePackageBuffer] Namespace mount failed path=%s"), TargetPath);
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Validate manifest requires contracts against provider index.
 * @param PackageName Package logical name.
 * @param Manifest Parsed package manifest.
 * @param ProviderIndex Provider contracts index.
 * @return TRUE when all requires are satisfied.
 */
static BOOL PackageNamespaceValidateRequires(LPCSTR PackageName,
                                             LPPACKAGE_MANIFEST Manifest,
                                             LPPACKAGENAMESPACE_PROVIDER_INDEX ProviderIndex) {
    UINT RequireIndex;
    BOOL AllSatisfied = TRUE;

    if (Manifest == NULL || ProviderIndex == NULL) return FALSE;

    for (RequireIndex = 0; RequireIndex < Manifest->RequiresCount; RequireIndex++) {
        LPCSTR Requirement = Manifest->Requires[RequireIndex];
        if (Requirement == NULL || Requirement[0] == STR_NULL) {
            continue;
        }

        if (!PackageNamespaceProviderIndexHas(ProviderIndex, Requirement)) {
            WARNING(TEXT("[PackageNamespaceValidateRequires] Missing dependency package=%s requires=%s"),
                PackageName,
                Requirement);
            AllSatisfied = FALSE;
        }
    }

    return AllSatisfied;
}

/***************************************************************************/

/**
 * @brief Add provided contracts from one manifest into provider index.
 * @param Manifest Parsed manifest.
 * @param ProviderIndex Provider index.
 */
static void PackageNamespaceAddManifestProviders(LPPACKAGE_MANIFEST Manifest,
                                                 LPPACKAGENAMESPACE_PROVIDER_INDEX ProviderIndex) {
    UINT ProvideIndex;

    if (Manifest == NULL || ProviderIndex == NULL) return;

    PackageNamespaceProviderIndexAdd(ProviderIndex, Manifest->Name);

    for (ProvideIndex = 0; ProvideIndex < Manifest->ProvidesCount; ProvideIndex++) {
        if (Manifest->Provides[ProvideIndex] == NULL || Manifest->Provides[ProvideIndex][0] == STR_NULL) {
            continue;
        }
        PackageNamespaceProviderIndexAdd(ProviderIndex, Manifest->Provides[ProvideIndex]);
    }
}

/***************************************************************************/

/**
 * @brief Scan one folder for package files and mount discovered packages.
 * @param PackageFolder Folder containing package files.
 * @param MountRoot Base mount root for mounted package volumes.
 * @param RolePrefix Volume role prefix.
 * @param UserName Optional user name (user package role only).
 * @param IsGlobalProvider TRUE when mounted package contracts become global providers.
 * @param ProviderIndex Provider index used for dependency checks.
 */
static void PackageNamespaceScanPackageFolder(LPCSTR PackageFolder,
                                              LPCSTR MountRoot,
                                              LPCSTR RolePrefix,
                                              LPCSTR UserName,
                                              BOOL IsGlobalProvider,
                                              LPPACKAGENAMESPACE_PROVIDER_INDEX ProviderIndex) {
    FILEINFO Find;
    LPFILE Entry;
    STR Pattern[MAX_PATH_NAME];
    PACKAGENAMESPACE_SCAN_LIST ScanList;
    UINT ScanIndex;

    if (PackageFolder == NULL || MountRoot == NULL || RolePrefix == NULL || ProviderIndex == NULL) return;
    if (!PackageNamespacePathExists(PackageFolder)) return;
    if (!PackageNamespaceEnsureFolder(MountRoot)) return;

    PackageNamespaceScanListInit(&ScanList);
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

        if (!PackageNamespaceScanListPush(&ScanList, FilePath, TargetPath, PackageName, UserName)) {
            WARNING(TEXT("[PackageNamespaceScanPackageFolder] Scan list allocation failed for %s"), FilePath);
        }
    } while (GetSystemFS()->Driver->Command(DF_FS_OPENNEXT, (UINT)Entry) == DF_RETURN_SUCCESS);

    GetSystemFS()->Driver->Command(DF_FS_CLOSEFILE, (UINT)Entry);

    PackageNamespaceScanListSort(&ScanList);

    for (ScanIndex = 0; ScanIndex < ScanList.Count; ScanIndex++) {
        LPPACKAGENAMESPACE_SCAN_ENTRY Candidate = &ScanList.Entries[ScanIndex];
        LPVOID PackageBytes;
        UINT PackageSize = 0;
        PACKAGE_MANIFEST Manifest;
        U32 ManifestStatus;
        BOOL Mounted;

        if (PackageNamespacePathExists(Candidate->TargetPath)) {
            continue;
        }

        PackageBytes = FileReadAll(Candidate->PackageFilePath, &PackageSize);
        if (PackageBytes == NULL || PackageSize == 0) {
            WARNING(TEXT("[PackageNamespaceScanPackageFolder] Cannot read package file %s"),
                Candidate->PackageFilePath);
            continue;
        }

        ManifestStatus =
            PackageManifestParseFromPackageBuffer(PackageBytes, (U32)PackageSize, &Manifest);
        if (ManifestStatus != PACKAGE_MANIFEST_STATUS_OK) {
            WARNING(TEXT("[PackageNamespaceScanPackageFolder] Manifest parse failed file=%s status=%u"),
                Candidate->PackageFilePath,
                ManifestStatus);
            KernelHeapFree(PackageBytes);
            continue;
        }

        if (Manifest.Name[0] != STR_NULL && StringCompare(Manifest.Name, Candidate->PackageName) != 0) {
            WARNING(TEXT("[PackageNamespaceScanPackageFolder] Manifest name mismatch file=%s manifest=%s filename=%s"),
                Candidate->PackageFilePath,
                Manifest.Name,
                Candidate->PackageName);
        }

        if (!PackageNamespaceValidateRequires(Candidate->PackageName, &Manifest, ProviderIndex)) {
            WARNING(TEXT("[PackageNamespaceScanPackageFolder] Dependency resolution failed package=%s"),
                Candidate->PackageName);
            PackageManifestRelease(&Manifest);
            KernelHeapFree(PackageBytes);
            continue;
        }

        Mounted = PackageNamespaceMountOnePackageBuffer(PackageBytes,
            PackageSize,
            Candidate->TargetPath,
            RolePrefix,
            Candidate->PackageName,
            Candidate->UserName[0] == STR_NULL ? NULL : Candidate->UserName);

        if (Mounted && IsGlobalProvider) {
            PackageNamespaceAddManifestProviders(&Manifest, ProviderIndex);
        }

        PackageManifestRelease(&Manifest);
        KernelHeapFree(PackageBytes);
    }

    PackageNamespaceScanListDeinit(&ScanList);
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
    if (!PackageNamespaceEnsurePathsLoaded()) return FALSE;
    ActiveFileSystem = PackageNamespaceGetActiveFileSystem();
    if (ActiveFileSystem == NULL) return FALSE;

    PackageNamespaceBuildChildPath(PackageNamespacePaths.UsersRoot, UserName, SourcePath);
    if (!PackageNamespacePathExists(SourcePath)) return FALSE;
    return PackageNamespaceMountPath(ActiveFileSystem, PackageNamespacePaths.CurrentUserAlias, SourcePath);
}

/***************************************************************************/

/**
 * @brief Scan user package folders and mount per-user package files.
 * @param ProviderIndex Provider index for dependency checks.
 */
static void PackageNamespaceScanUserPackageFolders(LPPACKAGENAMESPACE_PROVIDER_INDEX ProviderIndex) {
    FILEINFO Find;
    LPFILE UserEntry;
    STR Pattern[MAX_PATH_NAME];

    if (!PackageNamespaceEnsurePathsLoaded()) return;
    if (!PackageNamespacePathExists(PackageNamespacePaths.UsersRoot)) return;

    PackageNamespaceBuildEnumeratePattern(PackageNamespacePaths.UsersRoot, Pattern);

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

        PackageNamespaceBuildChildPath(PackageNamespacePaths.UsersRoot, UserEntry->Name, UserPackageFolder);
        PackageNamespaceBuildChildPath(
            UserPackageFolder, KERNEL_PATH_LEAF_USER_PACKAGE_ROOT, UserPackageFolder);

        PackageNamespaceBuildChildPath(PackageNamespacePaths.UsersRoot, UserEntry->Name, UserMountRoot);
        PackageNamespaceBuildChildPath(
            UserMountRoot, KERNEL_PATH_LEAF_USER_PACKAGE_ROOT, UserMountRoot);

        PackageNamespaceScanPackageFolder(UserPackageFolder,
            UserMountRoot,
            PACKAGE_NAMESPACE_ROLE_USER,
            UserEntry->Name,
            FALSE,
            ProviderIndex);
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
    PACKAGENAMESPACE_PROVIDER_INDEX ProviderIndex;

    if (!FileSystemReady()) {
        return FALSE;
    }
    if (!PackageNamespaceEnsurePathsLoaded()) {
        return FALSE;
    }

    PackageNamespaceEnsureFolder(PackageNamespacePaths.LibraryRoot);
    PackageNamespaceEnsureFolder(PackageNamespacePaths.AppsRoot);
    PackageNamespaceEnsureFolder(PackageNamespacePaths.UsersRoot);

    PackageNamespaceProviderIndexInit(&ProviderIndex);

    PackageNamespaceScanPackageFolder(PackageNamespacePaths.LibraryRoot,
        PackageNamespacePaths.LibraryRoot,
        PACKAGE_NAMESPACE_ROLE_LIBRARY,
        NULL,
        TRUE,
        &ProviderIndex);
    PackageNamespaceScanPackageFolder(PackageNamespacePaths.AppsRoot,
        PackageNamespacePaths.AppsRoot,
        PACKAGE_NAMESPACE_ROLE_APPLICATION,
        NULL,
        TRUE,
        &ProviderIndex);
    PackageNamespaceScanUserPackageFolders(&ProviderIndex);

    PackageNamespaceProviderIndexDeinit(&ProviderIndex);

    if (CurrentUser != NULL) {
        PackageNamespaceBindCurrentUserAlias(CurrentUser->UserName);
    } else {
        PackageNamespaceBindCurrentUserAlias(KERNEL_PATH_DEFAULT_ROOT_USER_NAME);
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
    if (!PackageNamespaceEnsurePathsLoaded()) return FALSE;

    if (!PackageNamespaceMountPath(PackageFileSystem, PackageNamespacePaths.PrivatePackageAlias, NULL)) {
        return FALSE;
    }

    if (CurrentUser == NULL) return FALSE;
    ActiveFileSystem = PackageNamespaceGetActiveFileSystem();
    if (ActiveFileSystem == NULL) return FALSE;

    UserDataSourcePath[0] = STR_NULL;
    PackageNamespaceBuildChildPath(PackageNamespacePaths.UsersRoot, CurrentUser->UserName, UserDataSourcePath);
    PackageNamespaceBuildChildPath(UserDataSourcePath, PackageName, UserDataSourcePath);
    PackageNamespaceBuildChildPath(UserDataSourcePath, KERNEL_PATH_LEAF_PRIVATE_USER_DATA, UserDataSourcePath);

    if (!PackageNamespacePathExists(UserDataSourcePath)) {
        return FALSE;
    }

    return PackageNamespaceMountPath(ActiveFileSystem, PackageNamespacePaths.PrivateUserDataAlias, UserDataSourcePath);
}
