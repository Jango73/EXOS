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


    NTFS write-path placeholders

\************************************************************************/

#include "NTFS-Private.h"

/***************************************************************************/

/**
 * @brief Placeholder for NTFS folder creation.
 *
 * NTFS is currently mounted read-only.
 *
 * @param Info Requested folder creation information.
 * @return DF_RETURN_NO_PERMISSION for read-only policy.
 */
U32 NtfsCreateFolder(LPFILEINFO Info) {
    UNUSED(Info);
    return DF_RETURN_NO_PERMISSION;
}

/***************************************************************************/

/**
 * @brief Placeholder for NTFS folder deletion.
 *
 * NTFS is currently mounted read-only.
 *
 * @param Info Requested folder deletion information.
 * @return DF_RETURN_NO_PERMISSION for read-only policy.
 */
U32 NtfsDeleteFolder(LPFILEINFO Info) {
    UNUSED(Info);
    return DF_RETURN_NO_PERMISSION;
}

/***************************************************************************/

/**
 * @brief Placeholder for NTFS folder rename.
 *
 * NTFS is currently mounted read-only.
 *
 * @param Info Requested folder rename information.
 * @return DF_RETURN_NO_PERMISSION for read-only policy.
 */
U32 NtfsRenameFolder(LPFILEINFO Info) {
    UNUSED(Info);
    return DF_RETURN_NO_PERMISSION;
}

/***************************************************************************/

/**
 * @brief Placeholder for NTFS file deletion.
 *
 * NTFS is currently mounted read-only.
 *
 * @param Info Requested file deletion information.
 * @return DF_RETURN_NO_PERMISSION for read-only policy.
 */
U32 NtfsDeleteFile(LPFILEINFO Info) {
    UNUSED(Info);
    return DF_RETURN_NO_PERMISSION;
}

/***************************************************************************/

/**
 * @brief Placeholder for NTFS file rename.
 *
 * NTFS is currently mounted read-only.
 *
 * @param Info Requested file rename information.
 * @return DF_RETURN_NO_PERMISSION for read-only policy.
 */
U32 NtfsRenameFile(LPFILEINFO Info) {
    UNUSED(Info);
    return DF_RETURN_NO_PERMISSION;
}

/***************************************************************************/
