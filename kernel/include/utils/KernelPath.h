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


    Kernel logical path resolver

\************************************************************************/

#ifndef KERNELPATH_H_INCLUDED
#define KERNELPATH_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

#define CONFIG_KERNEL_PATH_PREFIX TEXT("KernelPath.")

#define KERNEL_FILE_USERS_DATABASE TEXT("UsersDatabase")
#define KERNEL_FOLDER_KEYBOARD_LAYOUTS TEXT("KeyboardLayouts")
#define KERNEL_FOLDER_PACKAGES_LIBRARY TEXT("PackagesLibrary")
#define KERNEL_FOLDER_PACKAGES_APPS TEXT("PackagesApps")
#define KERNEL_FOLDER_USERS_ROOT TEXT("UsersRoot")
#define KERNEL_FOLDER_CURRENT_USER_ALIAS TEXT("CurrentUserAlias")
#define KERNEL_FOLDER_PRIVATE_PACKAGE_ALIAS TEXT("PrivatePackageAlias")
#define KERNEL_FOLDER_PRIVATE_USER_DATA_ALIAS TEXT("PrivateUserDataAlias")

#define KERNEL_FILE_PATH_USERS_DATABASE_DEFAULT TEXT("/system/data/users.database")
#define KERNEL_FOLDER_PATH_KEYBOARD_LAYOUTS_DEFAULT TEXT("/system/keyboard")
#define KERNEL_FOLDER_PATH_PACKAGES_LIBRARY_DEFAULT TEXT("/library/package")
#define KERNEL_FOLDER_PATH_PACKAGES_APPS_DEFAULT TEXT("/apps")
#define KERNEL_FOLDER_PATH_USERS_ROOT_DEFAULT TEXT("/users")
#define KERNEL_FOLDER_PATH_CURRENT_USER_ALIAS_DEFAULT TEXT("/current-user")
#define KERNEL_FOLDER_PATH_PRIVATE_PACKAGE_ALIAS_DEFAULT TEXT("/package")
#define KERNEL_FOLDER_PATH_PRIVATE_USER_DATA_ALIAS_DEFAULT TEXT("/user-data")
#define KERNEL_FILE_EXTENSION_KEYBOARD_LAYOUT TEXT(".ekm1")

/***************************************************************************/

BOOL KernelPathResolve(LPCSTR Name, LPCSTR DefaultPath, LPSTR OutPath, UINT OutPathSize);
BOOL KernelPathBuildFile(
    LPCSTR FolderName,
    LPCSTR DefaultFolder,
    LPCSTR LeafName,
    LPCSTR Extension,
    LPSTR OutPath,
    UINT OutPathSize);

/***************************************************************************/

#endif  // KERNELPATH_H_INCLUDED
