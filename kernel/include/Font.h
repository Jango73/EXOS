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


    Font

\************************************************************************/

#ifndef FONT_H_INCLUDED
#define FONT_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

#define FONT_FALLBACK_GLYPH 0x3Fu
#define FONT_FACE_KIND_BITMAP_MONO 0x00000001
#define FONT_FACE_KIND_OUTLINE 0x00000002

/***************************************************************************/

typedef struct tag_FONT_GLYPH_SET {
    U32 Width;
    U32 Height;
    U32 BytesPerRow;
    U32 GlyphCount;
    const U8* GlyphData;
} FONT_GLYPH_SET, *LPFONT_GLYPH_SET;

/***************************************************************************/

typedef struct tag_FONT_METRICS {
    U32 CellWidth;
    U32 CellHeight;
    U32 AdvanceWidth;
    U32 LineHeight;
} FONT_METRICS, *LPFONT_METRICS;

/***************************************************************************/

typedef struct tag_FONT_GLYPH_BITMAP {
    I32 OffsetX;
    I32 OffsetY;
    U32 Width;
    U32 Height;
    U32 BytesPerRow;
    U32 AdvanceWidth;
    const U8* Data;
} FONT_GLYPH_BITMAP, *LPFONT_GLYPH_BITMAP;

/***************************************************************************/

struct tag_FONT_FACE;

typedef BOOL (*FONT_FACE_GET_METRICS_PROC)(const struct tag_FONT_FACE*, LPFONT_METRICS);
typedef BOOL (*FONT_FACE_GET_GLYPH_BITMAP_PROC)(
    const struct tag_FONT_FACE*,
    U32,
    LPFONT_GLYPH_BITMAP);

/***************************************************************************/

typedef struct tag_FONT_FACE {
    U32 Kind;
    LPCVOID Context;
    FONT_FACE_GET_METRICS_PROC GetMetrics;
    FONT_FACE_GET_GLYPH_BITMAP_PROC GetGlyphBitmap;
} FONT_FACE, *LPFONT_FACE;

/***************************************************************************/

const FONT_FACE* FontGetDefaultFace(void);
BOOL FontSetDefaultFace(const FONT_FACE* Font);
BOOL FontFaceGetMetrics(const FONT_FACE* Font, LPFONT_METRICS MetricsOut);
BOOL FontFaceGetGlyphBitmap(const FONT_FACE* Font, U32 Codepoint, LPFONT_GLYPH_BITMAP GlyphOut);
const FONT_GLYPH_SET* FontFaceGetGlyphSet(const FONT_FACE* Font);

/***************************************************************************/

const FONT_GLYPH_SET* FontGetDefault(void);
BOOL FontSetDefault(const FONT_GLYPH_SET* Font);
const U8* FontGetGlyph(const FONT_GLYPH_SET* Font, U32 Codepoint);

/***************************************************************************/

#endif  // FONT_H_INCLUDED
