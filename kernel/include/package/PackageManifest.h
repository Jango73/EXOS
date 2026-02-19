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


    Package manifest parser and compatibility model

\************************************************************************/

#ifndef PACKAGEMANIFEST_H_INCLUDED
#define PACKAGEMANIFEST_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

#define PACKAGE_MANIFEST_STATUS_OK 0
#define PACKAGE_MANIFEST_STATUS_INVALID_ARGUMENT 1
#define PACKAGE_MANIFEST_STATUS_OUT_OF_MEMORY 2
#define PACKAGE_MANIFEST_STATUS_INVALID_TOML 3
#define PACKAGE_MANIFEST_STATUS_MISSING_NAME 4
#define PACKAGE_MANIFEST_STATUS_MISSING_VERSION 5
#define PACKAGE_MANIFEST_STATUS_MISSING_ARCH 6
#define PACKAGE_MANIFEST_STATUS_MISSING_KERNEL_API 7
#define PACKAGE_MANIFEST_STATUS_MISSING_ENTRY 8
#define PACKAGE_MANIFEST_STATUS_INVALID_PACKAGE 9
#define PACKAGE_MANIFEST_STATUS_INVALID_MANIFEST_BLOB 10
#define PACKAGE_MANIFEST_STATUS_FORBIDDEN_DEPENDENCY_GRAPH 11
#define PACKAGE_MANIFEST_STATUS_INVALID_ARCH 12
#define PACKAGE_MANIFEST_STATUS_INVALID_KERNEL_API 13
#define PACKAGE_MANIFEST_STATUS_INCOMPATIBLE_ARCH 14
#define PACKAGE_MANIFEST_STATUS_INCOMPATIBLE_KERNEL_API 15

/***************************************************************************/

typedef struct tag_PACKAGE_MANIFEST {
    STR Name[MAX_FILE_NAME];
    STR Version[32];
    STR Arch[16];
    STR KernelApi[32];
    STR Entry[MAX_PATH_NAME];
} PACKAGE_MANIFEST, *LPPACKAGE_MANIFEST;

/***************************************************************************/

U32 PackageManifestParseText(LPCSTR ManifestText, LPPACKAGE_MANIFEST OutManifest);
U32 PackageManifestParseFromPackageBuffer(LPCVOID PackageBytes, U32 PackageSize, LPPACKAGE_MANIFEST OutManifest);
U32 PackageManifestCheckCompatibility(const PACKAGE_MANIFEST* Manifest);
LPCSTR PackageManifestStatusToString(U32 Status);
void PackageManifestRelease(LPPACKAGE_MANIFEST Manifest);

/***************************************************************************/

#endif  // PACKAGEMANIFEST_H_INCLUDED
