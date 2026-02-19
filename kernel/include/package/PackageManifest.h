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
#define PACKAGE_MANIFEST_STATUS_INVALID_LIST 6
#define PACKAGE_MANIFEST_STATUS_INVALID_PACKAGE 7
#define PACKAGE_MANIFEST_STATUS_INVALID_MANIFEST_BLOB 8

/***************************************************************************/

typedef struct tag_PACKAGE_MANIFEST {
    STR Name[MAX_FILE_NAME];
    STR Version[32];
    UINT ProvidesCount;
    LPSTR* Provides;
    UINT RequiresCount;
    LPSTR* Requires;
} PACKAGE_MANIFEST, *LPPACKAGE_MANIFEST;

/***************************************************************************/

U32 PackageManifestParseText(LPCSTR ManifestText, LPPACKAGE_MANIFEST OutManifest);
U32 PackageManifestParseFromPackageBuffer(LPCVOID PackageBytes, U32 PackageSize, LPPACKAGE_MANIFEST OutManifest);
void PackageManifestRelease(LPPACKAGE_MANIFEST Manifest);

/***************************************************************************/

#endif  // PACKAGEMANIFEST_H_INCLUDED
