
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef EXT4_H_INCLUDED
#define EXT4_H_INCLUDED

/***************************************************************************/

#include "FSID.h"
#include "FileSystem.h"

/***************************************************************************/

#pragma pack(1)

/***************************************************************************/
// EXT4 Super Block

typedef struct tag_EXT4SUPER {
    // TODO
} EXT4SUPER, *LPEXT4SUPER;

/***************************************************************************/
// EXT4 File Record

typedef struct tag_EXT4FILEREC {
    // TODO
} EXT4FILEREC, *LPEXT4FILEREC;

/***************************************************************************/
// EXT4 File location

typedef struct tag_XFSFILELOC {
} XFSFILELOC, *LPXFSFILELOC;

/***************************************************************************/

#endif  // EXT4_H_INCLUDED
