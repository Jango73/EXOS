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

/***************************************************************************/

typedef struct tag_FONT_GLYPH_SET {
    U32 Width;
    U32 Height;
    U32 BytesPerRow;
    U32 GlyphCount;
    const U8* GlyphData;
} FONT_GLYPH_SET, *LPFONT_GLYPH_SET;

/***************************************************************************/

const FONT_GLYPH_SET* FontGetDefault(void);
BOOL FontSetDefault(const FONT_GLYPH_SET* Font);
const U8* FontGetGlyph(const FONT_GLYPH_SET* Font, U32 Codepoint);

/***************************************************************************/

#endif  // FONT_H_INCLUDED
