
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


    GFX

\************************************************************************/

#ifndef GFX_H_INCLUDED
#define GFX_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Driver.h"
#include "process/Process.h"
#include "User.h"

/***************************************************************************/

// Functions supplied by a graphics driver

#define DF_GFX_ENUMMODES (DF_FIRSTFUNC + 0)
#define DF_GFX_GETMODEINFO (DF_FIRSTFUNC + 1)
#define DF_GFX_SETMODE (DF_FIRSTFUNC + 2)
#define DF_GFX_CREATECONTEXT (DF_FIRSTFUNC + 3)
#define DF_GFX_CREATEBRUSH (DF_FIRSTFUNC + 4)
#define DF_GFX_CREATEPEN (DF_FIRSTFUNC + 5)
#define DF_GFX_SETPIXEL (DF_FIRSTFUNC + 6)
#define DF_GFX_GETPIXEL (DF_FIRSTFUNC + 7)
#define DF_GFX_LINE (DF_FIRSTFUNC + 8)
#define DF_GFX_RECTANGLE (DF_FIRSTFUNC + 9)
#define DF_GFX_ELLIPSE (DF_FIRSTFUNC + 10)

/***************************************************************************/

// Graphics driver error codes

#define DF_GFX_ERROR_MODEUNAVAIL DF_ERROR_FIRST

/***************************************************************************/

typedef U32 (*GFXENUMMODESFUNC)(void);

#define ROP_SET 0x0001
#define ROP_AND 0x0002
#define ROP_OR 0x0003
#define ROP_XOR 0x0004

/***************************************************************************/

typedef struct tag_BRUSH {
    LISTNODE_FIELDS
    U32 Color;
    U32 Pattern;
} BRUSH, *LPBRUSH;

/***************************************************************************/

typedef struct tag_PEN {
    LISTNODE_FIELDS
    U32 Color;
    U32 Pattern;
} PEN, *LPPEN;

/***************************************************************************/

typedef struct tag_FONT {
    LISTNODE_FIELDS
} FONT, *LPFONT;

/***************************************************************************/

typedef struct tag_BITMAP {
    LISTNODE_FIELDS
    U32 Width;
    U32 Height;
    U32 BitsPerPixel;
    U32 BytesPerScanLine;
    U8* Data;
} BITMAP, *LPBITMAP;

/***************************************************************************/

// The graphics context

typedef struct tag_GRAPHICSCONTEXT {
    LISTNODE_FIELDS
    MUTEX Mutex;
    LPDRIVER Driver;
    I32 Width;
    I32 Height;
    U32 BitsPerPixel;
    U32 BytesPerScanLine;
    U8* MemoryBase;
    POINT LoClip;
    POINT HiClip;
    POINT Origin;
    U32 RasterOperation;
    LPBRUSH Brush;
    LPPEN Pen;
    LPFONT Font;
    LPBITMAP Bitmap;
} GRAPHICSCONTEXT, *LPGRAPHICSCONTEXT;

/***************************************************************************/

#endif  // GFX_H_INCLUDED
