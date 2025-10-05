
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

#pragma pack(push, 1)

/***************************************************************************/
// EXT2 Super Block

typedef struct tag_EXT2SUPER {
    U32 Magic;
    U32 Revision;
    U32 BlockSize;
    U32 InodeCount;
    U32 BlockCount;
    U32 FreeInodes;
    U32 FreeBlocks;
} EXT2SUPER, *LPEXT2SUPER;

/***************************************************************************/
// EXT2 File Record

typedef struct tag_EXT2FILEREC {
    STR Name[MAX_FILE_NAME];
    U32 Attributes;
    U32 Size;
    U32 Capacity;
    U8* Data;
} EXT2FILEREC, *LPEXT2FILEREC;

/***************************************************************************/
// EXT2 File location

typedef struct tag_EXT2FILELOC {
    LPEXT2FILEREC Record;
    U32 Offset;
} EXT2FILELOC, *LPEXT2FILELOC;

/***************************************************************************/

#pragma pack(pop)

#endif  // EXT2_H_INCLUDED
