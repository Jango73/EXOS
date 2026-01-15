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

#include "Font.h"

/************************************************************************/

extern const FONT_GLYPH_SET FontAscii;

static const FONT_GLYPH_SET* DefaultFont = &FontAscii;

/************************************************************************/

/**
 * @brief Retrieve the default font glyph set.
 * @return Pointer to the default font.
 */
const FONT_GLYPH_SET* FontGetDefault(void) {
    return DefaultFont;
}

/************************************************************************/

/**
 * @brief Update the default font glyph set.
 * @param Font New font to use.
 * @return TRUE on success, FALSE on invalid input.
 */
BOOL FontSetDefault(const FONT_GLYPH_SET* Font) {
    if (Font == NULL || Font->GlyphData == NULL || Font->GlyphCount == 0u) {
        return FALSE;
    }

    DefaultFont = Font;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Get the glyph bitmap for a given codepoint.
 * @param Font Font glyph set.
 * @param Codepoint ASCII codepoint.
 * @return Pointer to glyph data for the codepoint.
 */
const U8* FontGetGlyph(const FONT_GLYPH_SET* Font, U32 Codepoint) {
    if (Font == NULL || Font->GlyphData == NULL || Font->GlyphCount == 0u) {
        return NULL;
    }

    U32 Index = Codepoint;
    if (Index >= Font->GlyphCount) {
        Index = FONT_FALLBACK_GLYPH;
        if (Index >= Font->GlyphCount) {
            return NULL;
        }
    }

    return Font->GlyphData + (Index * Font->Height * Font->BytesPerRow);
}

/************************************************************************/
