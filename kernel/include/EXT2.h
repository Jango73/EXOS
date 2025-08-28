
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

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
