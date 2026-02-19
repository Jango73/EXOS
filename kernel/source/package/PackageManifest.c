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


    Package manifest parser and model

\************************************************************************/

#include "package/PackageManifest.h"

#include "CoreString.h"
#include "Heap.h"
#include "Log.h"
#include "package/EpkParser.h"
#include "utils/TOML.h"

/***************************************************************************/

/**
 * @brief Reset manifest model to empty state.
 * @param Manifest Manifest output structure.
 */
static void PackageManifestReset(LPPACKAGE_MANIFEST Manifest) {
    if (Manifest == NULL) return;

    MemorySet(Manifest->Name, 0, sizeof(Manifest->Name));
    MemorySet(Manifest->Version, 0, sizeof(Manifest->Version));
    Manifest->ProvidesCount = 0;
    Manifest->Provides = NULL;
    Manifest->RequiresCount = 0;
    Manifest->Requires = NULL;
}

/***************************************************************************/

/**
 * @brief Check whether one character is TOML list whitespace.
 * @param Character Character to test.
 * @return TRUE when whitespace.
 */
static BOOL PackageManifestIsWhitespace(STR Character) {
    return Character == ' ' || Character == '\t' || Character == '\r' || Character == '\n';
}

/***************************************************************************/

/**
 * @brief Skip spaces in one manifest list string.
 * @param Cursor Current cursor pointer.
 * @return Pointer to first non-space character.
 */
static LPCSTR PackageManifestSkipWhitespace(LPCSTR Cursor) {
    if (Cursor == NULL) return NULL;
    while (*Cursor != STR_NULL && PackageManifestIsWhitespace(*Cursor)) Cursor++;
    return Cursor;
}

/***************************************************************************/

/**
 * @brief Duplicate one substring from manifest source.
 * @param Start Substring start.
 * @param Length Substring length.
 * @return Newly allocated null-terminated string, or NULL on allocation failure.
 */
static LPSTR PackageManifestDuplicateRange(LPCSTR Start, UINT Length) {
    LPSTR Copy;

    if (Start == NULL) return NULL;
    Copy = (LPSTR)KernelHeapAlloc(Length + 1);
    if (Copy == NULL) return NULL;

    if (Length > 0) {
        StringCopyNum(Copy, Start, Length);
    }
    Copy[Length] = STR_NULL;
    return Copy;
}

/***************************************************************************/

/**
 * @brief Parse one quoted TOML list into allocated string array.
 * @param RawValue Raw TOML value.
 * @param OutItems Receives allocated string pointer array.
 * @param OutCount Receives number of strings.
 * @return PACKAGE_MANIFEST_STATUS_* result.
 */
static U32 PackageManifestParseQuotedList(LPCSTR RawValue, LPSTR** OutItems, UINT* OutCount) {
    UINT Count = 0;
    UINT Index = 0;
    LPCSTR Cursor;
    LPSTR* Items;

    if (OutItems == NULL || OutCount == NULL) {
        return PACKAGE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }

    *OutItems = NULL;
    *OutCount = 0;

    if (RawValue == NULL || RawValue[0] == STR_NULL) {
        return PACKAGE_MANIFEST_STATUS_OK;
    }

    Cursor = PackageManifestSkipWhitespace(RawValue);
    if (Cursor == NULL) {
        return PACKAGE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }

    if (*Cursor != '[') {
        LPSTR Single = PackageManifestDuplicateRange(Cursor, StringLength(Cursor));
        if (Single == NULL) {
            return PACKAGE_MANIFEST_STATUS_OUT_OF_MEMORY;
        }

        Items = (LPSTR*)KernelHeapAlloc(sizeof(LPSTR));
        if (Items == NULL) {
            KernelHeapFree(Single);
            return PACKAGE_MANIFEST_STATUS_OUT_OF_MEMORY;
        }

        Items[0] = Single;
        *OutItems = Items;
        *OutCount = 1;
        return PACKAGE_MANIFEST_STATUS_OK;
    }

    Cursor++;
    while (TRUE) {
        Cursor = PackageManifestSkipWhitespace(Cursor);
        if (*Cursor == ']') {
            Cursor++;
            Cursor = PackageManifestSkipWhitespace(Cursor);
            if (*Cursor != STR_NULL) {
                return PACKAGE_MANIFEST_STATUS_INVALID_LIST;
            }
            break;
        }

        if (*Cursor != '"') {
            return PACKAGE_MANIFEST_STATUS_INVALID_LIST;
        }

        Cursor++;
        while (*Cursor != STR_NULL && *Cursor != '"') Cursor++;
        if (*Cursor != '"') {
            return PACKAGE_MANIFEST_STATUS_INVALID_LIST;
        }

        Count++;
        Cursor++;
        Cursor = PackageManifestSkipWhitespace(Cursor);

        if (*Cursor == ',') {
            Cursor++;
            continue;
        }
        if (*Cursor == ']') {
            continue;
        }
        return PACKAGE_MANIFEST_STATUS_INVALID_LIST;
    }

    if (Count == 0) {
        return PACKAGE_MANIFEST_STATUS_OK;
    }

    Items = (LPSTR*)KernelHeapAlloc(sizeof(LPSTR) * Count);
    if (Items == NULL) {
        return PACKAGE_MANIFEST_STATUS_OUT_OF_MEMORY;
    }
    MemorySet(Items, 0, sizeof(LPSTR) * Count);

    Cursor = PackageManifestSkipWhitespace(RawValue) + 1;
    while (Index < Count) {
        LPCSTR Start;
        UINT Length;

        Cursor = PackageManifestSkipWhitespace(Cursor);
        if (*Cursor != '"') {
            break;
        }

        Cursor++;
        Start = Cursor;
        while (*Cursor != STR_NULL && *Cursor != '"') Cursor++;
        if (*Cursor != '"') {
            break;
        }

        Length = (UINT)(Cursor - Start);
        Items[Index] = PackageManifestDuplicateRange(Start, Length);
        if (Items[Index] == NULL) {
            break;
        }
        Index++;

        Cursor++;
        Cursor = PackageManifestSkipWhitespace(Cursor);
        if (*Cursor == ',') {
            Cursor++;
        }
    }

    if (Index != Count) {
        UINT CleanupIndex;
        for (CleanupIndex = 0; CleanupIndex < Count; CleanupIndex++) {
            if (Items[CleanupIndex] != NULL) {
                KernelHeapFree(Items[CleanupIndex]);
            }
        }
        KernelHeapFree(Items);
        return PACKAGE_MANIFEST_STATUS_INVALID_LIST;
    }

    *OutItems = Items;
    *OutCount = Count;
    return PACKAGE_MANIFEST_STATUS_OK;
}

/***************************************************************************/

/**
 * @brief Parse manifest TOML text into model.
 * @param ManifestText Null-terminated manifest text.
 * @param OutManifest Receives parsed manifest model.
 * @return PACKAGE_MANIFEST_STATUS_* result.
 */
U32 PackageManifestParseText(LPCSTR ManifestText, LPPACKAGE_MANIFEST OutManifest) {
    LPTOML Toml;
    LPCSTR Name;
    LPCSTR Version;
    LPCSTR Provides;
    LPCSTR Requires;
    U32 Status;

    if (ManifestText == NULL || OutManifest == NULL) {
        return PACKAGE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }

    PackageManifestReset(OutManifest);

    Toml = TomlParse(ManifestText);
    if (Toml == NULL) {
        return PACKAGE_MANIFEST_STATUS_OUT_OF_MEMORY;
    }

    Name = TomlGet(Toml, TEXT("name"));
    if (Name == NULL || Name[0] == STR_NULL) {
        Name = TomlGet(Toml, TEXT("package.name"));
    }
    if (Name == NULL || Name[0] == STR_NULL) {
        TomlFree(Toml);
        return PACKAGE_MANIFEST_STATUS_MISSING_NAME;
    }

    Version = TomlGet(Toml, TEXT("version"));
    if (Version == NULL || Version[0] == STR_NULL) {
        Version = TomlGet(Toml, TEXT("package.version"));
    }
    if (Version == NULL || Version[0] == STR_NULL) {
        TomlFree(Toml);
        return PACKAGE_MANIFEST_STATUS_MISSING_VERSION;
    }

    StringCopyLimit(OutManifest->Name, Name, MAX_FILE_NAME - 1);
    StringCopyLimit(OutManifest->Version, Version, sizeof(OutManifest->Version) - 1);

    Provides = TomlGet(Toml, TEXT("provides"));
    if (Provides == NULL) {
        Provides = TomlGet(Toml, TEXT("package.provides"));
    }

    Requires = TomlGet(Toml, TEXT("requires"));
    if (Requires == NULL) {
        Requires = TomlGet(Toml, TEXT("package.requires"));
    }

    Status = PackageManifestParseQuotedList(Provides, &OutManifest->Provides, &OutManifest->ProvidesCount);
    if (Status != PACKAGE_MANIFEST_STATUS_OK) {
        TomlFree(Toml);
        PackageManifestRelease(OutManifest);
        return Status;
    }

    Status = PackageManifestParseQuotedList(Requires, &OutManifest->Requires, &OutManifest->RequiresCount);
    if (Status != PACKAGE_MANIFEST_STATUS_OK) {
        TomlFree(Toml);
        PackageManifestRelease(OutManifest);
        return Status;
    }

    TomlFree(Toml);
    return PACKAGE_MANIFEST_STATUS_OK;
}

/***************************************************************************/

/**
 * @brief Parse manifest from package bytes by validating EPK sections.
 * @param PackageBytes Package byte buffer.
 * @param PackageSize Package size.
 * @param OutManifest Receives parsed manifest model.
 * @return PACKAGE_MANIFEST_STATUS_* result.
 */
U32 PackageManifestParseFromPackageBuffer(LPCVOID PackageBytes,
                                          U32 PackageSize,
                                          LPPACKAGE_MANIFEST OutManifest) {
    EPK_PARSER_OPTIONS Options = {
        .VerifyPackageHash = TRUE,
        .VerifySignature = TRUE,
        .RequireSignature = FALSE};
    EPK_VALIDATED_PACKAGE Package;
    U8* ManifestText;
    U32 Status;
    U32 ParserStatus;

    if (PackageBytes == NULL || PackageSize == 0 || OutManifest == NULL) {
        return PACKAGE_MANIFEST_STATUS_INVALID_ARGUMENT;
    }

    PackageManifestReset(OutManifest);
    MemorySet(&Package, 0, sizeof(Package));

    ParserStatus = EpkValidatePackageBuffer(PackageBytes, PackageSize, &Options, &Package);
    if (ParserStatus != EPK_VALIDATION_OK) {
        return PACKAGE_MANIFEST_STATUS_INVALID_PACKAGE;
    }

    if (Package.ManifestSize == 0 || Package.ManifestOffset >= Package.PackageSize ||
        Package.ManifestOffset + Package.ManifestSize < Package.ManifestOffset ||
        Package.ManifestOffset + Package.ManifestSize > Package.PackageSize) {
        EpkReleaseValidatedPackage(&Package);
        return PACKAGE_MANIFEST_STATUS_INVALID_MANIFEST_BLOB;
    }

    ManifestText = (U8*)KernelHeapAlloc(Package.ManifestSize + 1);
    if (ManifestText == NULL) {
        EpkReleaseValidatedPackage(&Package);
        return PACKAGE_MANIFEST_STATUS_OUT_OF_MEMORY;
    }

    MemoryCopy(ManifestText, Package.PackageBytes + Package.ManifestOffset, Package.ManifestSize);
    ManifestText[Package.ManifestSize] = STR_NULL;

    Status = PackageManifestParseText((LPCSTR)ManifestText, OutManifest);

    KernelHeapFree(ManifestText);
    EpkReleaseValidatedPackage(&Package);
    return Status;
}

/***************************************************************************/

/**
 * @brief Release dynamic manifest model arrays.
 * @param Manifest Manifest model.
 */
void PackageManifestRelease(LPPACKAGE_MANIFEST Manifest) {
    UINT Index;

    if (Manifest == NULL) return;

    if (Manifest->Provides != NULL) {
        for (Index = 0; Index < Manifest->ProvidesCount; Index++) {
            if (Manifest->Provides[Index] != NULL) {
                KernelHeapFree(Manifest->Provides[Index]);
            }
        }
        KernelHeapFree(Manifest->Provides);
    }

    if (Manifest->Requires != NULL) {
        for (Index = 0; Index < Manifest->RequiresCount; Index++) {
            if (Manifest->Requires[Index] != NULL) {
                KernelHeapFree(Manifest->Requires[Index]);
            }
        }
        KernelHeapFree(Manifest->Requires);
    }

    PackageManifestReset(Manifest);
}
