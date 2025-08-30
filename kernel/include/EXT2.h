
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


    EXT2

\************************************************************************/
#ifndef EXT2_H_INCLUDED
#define EXT2_H_INCLUDED

/***************************************************************************/

#include "FSID.h"
#include "FileSystem.h"

/***************************************************************************/

#pragma pack(1)

/***************************************************************************/
// EXT4 Super Block

typedef struct tag_EXT2SUPER {
    // TODO
} EXT2SUPER, *LPEXT2SUPER;

/***************************************************************************/
// EXT4 File Record

typedef struct tag_EXT2FILEREC {
    // TODO
} EXT2FILEREC, *LPEXT2FILEREC;

/***************************************************************************/
// EXT4 File location

typedef struct tag_EXT2FILELOC {
} EXT2FILELOC, *LPEXT2FILELOC;

/***************************************************************************/

#endif  // EXT2_H_INCLUDED
