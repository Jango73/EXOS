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


    PackageFS read-only driver dispatch implementation

\************************************************************************/

#include "PackageFS-Internal.h"

/************************************************************************/

#define VER_MAJOR 1
#define VER_MINOR 0

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
    .Alias = "packagefs",
    .Command = PackageFSCommands};

/************************************************************************/

/**
 * @brief Initialize PackageFS driver state.
 * @return DF_RETURN_SUCCESS always.
 */
static U32 Initialize(void) { return DF_RETURN_SUCCESS; }

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
            return PackageFSGetVolumeInfo((LPVOLUMEINFO)Parameter);
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
            return (UINT)PackageFSOpenFile((LPFILEINFO)Parameter);
        case DF_FS_OPENNEXT:
            return PackageFSOpenNext((LPPACKAGEFSFILE)Parameter);
        case DF_FS_CLOSEFILE:
            return PackageFSCloseFile((LPPACKAGEFSFILE)Parameter);
        case DF_FS_READ:
            return PackageFSReadFile((LPPACKAGEFSFILE)Parameter);
        case DF_FS_WRITE:
            return PackageFSWriteFile((LPPACKAGEFSFILE)Parameter);
        case DF_FS_PATHEXISTS:
            return (UINT)PackageFSPathExists((LPFS_PATHCHECK)Parameter);
        case DF_FS_FILEEXISTS:
            return (UINT)PackageFSFileExists((LPFILEINFO)Parameter);
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
