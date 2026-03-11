
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
extern const FONT_FACE FontAsciiFace;

static const FONT_GLYPH_SET* DefaultGlyphSet = &FontAscii;
static const FONT_FACE* DefaultFontFace = &FontAsciiFace;

/************************************************************************/

/**
 * @brief Resolve one glyph index with fallback handling.
 * @param Font Font glyph set.
 * @param Codepoint Requested codepoint.
 * @param IndexOut Resolved glyph index.
 * @return TRUE on success.
 */
static BOOL FontResolveGlyphIndex(const FONT_GLYPH_SET* Font, U32 Codepoint, U32* IndexOut) {
    U32 Index = 0;

    if (Font == NULL || Font->GlyphData == NULL || Font->GlyphCount == 0 ||
        Font->BytesPerRow == 0 || IndexOut == NULL) {
        return FALSE;
    }

    Index = Codepoint;
    if (Index >= Font->GlyphCount) {
        Index = FONT_FALLBACK_GLYPH;
        if (Index >= Font->GlyphCount) {
            return FALSE;
        }
    }

    *IndexOut = Index;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Provide metrics for one bitmap glyph-set backed font face.
 * @param Font Font face.
 * @param MetricsOut Output metrics.
 * @return TRUE on success.
 */
static BOOL FontGlyphSetFaceGetMetrics(const FONT_FACE* Font, LPFONT_METRICS MetricsOut) {
    const FONT_GLYPH_SET* GlyphSet = NULL;

    if (Font == NULL || MetricsOut == NULL) {
        return FALSE;
    }

    GlyphSet = (const FONT_GLYPH_SET*)Font->Context;
    if (GlyphSet == NULL || GlyphSet->GlyphData == NULL || GlyphSet->GlyphCount == 0) {
        return FALSE;
    }

    *MetricsOut = (FONT_METRICS){
        .CellWidth = GlyphSet->Width,
        .CellHeight = GlyphSet->Height,
        .AdvanceWidth = GlyphSet->Width,
        .LineHeight = GlyphSet->Height
    };
    return TRUE;
}

/************************************************************************/

/**
 * @brief Provide a bitmap glyph view for one glyph-set backed font face.
 * @param Font Font face.
 * @param Codepoint Requested codepoint.
 * @param GlyphOut Output glyph bitmap.
 * @return TRUE on success.
 */
static BOOL FontGlyphSetFaceGetGlyphBitmap(
    const FONT_FACE* Font,
    U32 Codepoint,
    LPFONT_GLYPH_BITMAP GlyphOut) {
    const FONT_GLYPH_SET* GlyphSet = NULL;
    U32 GlyphIndex = 0;

    if (Font == NULL || GlyphOut == NULL) {
        return FALSE;
    }

    GlyphSet = (const FONT_GLYPH_SET*)Font->Context;
    if (!FontResolveGlyphIndex(GlyphSet, Codepoint, &GlyphIndex)) {
        return FALSE;
    }

    *GlyphOut = (FONT_GLYPH_BITMAP){
        .OffsetX = 0,
        .OffsetY = 0,
        .Width = GlyphSet->Width,
        .Height = GlyphSet->Height,
        .BytesPerRow = GlyphSet->BytesPerRow,
        .AdvanceWidth = GlyphSet->Width,
        .Data = GlyphSet->GlyphData + (GlyphIndex * GlyphSet->Height * GlyphSet->BytesPerRow)
    };
    return TRUE;
}

/************************************************************************/

/**
 * @brief Retrieve the default font face.
 * @return Pointer to the default font face.
 */
const FONT_FACE* FontGetDefaultFace(void) {
    return DefaultFontFace;
}

/************************************************************************/

/**
 * @brief Update the default font face.
 * @param Font New font face to use.
 * @return TRUE on success, FALSE on invalid input.
 */
BOOL FontSetDefaultFace(const FONT_FACE* Font) {
    if (Font == NULL || Font->GetMetrics == NULL || Font->GetGlyphBitmap == NULL) {
        return FALSE;
    }

    DefaultFontFace = Font;
    DefaultGlyphSet = FontFaceGetGlyphSet(Font);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Query generic metrics from one font face.
 * @param Font Font face.
 * @param MetricsOut Output metrics.
 * @return TRUE on success.
 */
BOOL FontFaceGetMetrics(const FONT_FACE* Font, LPFONT_METRICS MetricsOut) {
    if (Font == NULL || Font->GetMetrics == NULL || MetricsOut == NULL) {
        return FALSE;
    }

    return Font->GetMetrics(Font, MetricsOut);
}

/************************************************************************/

/**
 * @brief Query one rasterizable glyph from one font face.
 * @param Font Font face.
 * @param Codepoint Requested codepoint.
 * @param GlyphOut Output bitmap descriptor.
 * @return TRUE on success.
 */
BOOL FontFaceGetGlyphBitmap(const FONT_FACE* Font, U32 Codepoint, LPFONT_GLYPH_BITMAP GlyphOut) {
    if (Font == NULL || Font->GetGlyphBitmap == NULL || GlyphOut == NULL) {
        return FALSE;
    }

    return Font->GetGlyphBitmap(Font, Codepoint, GlyphOut);
}

/************************************************************************/

/**
 * @brief Return the underlying glyph set when one font face is glyph-set backed.
 * @param Font Font face.
 * @return Glyph-set pointer, or NULL when the font is not glyph-set backed.
 */
const FONT_GLYPH_SET* FontFaceGetGlyphSet(const FONT_FACE* Font) {
    if (Font == NULL || Font->Kind != FONT_FACE_KIND_BITMAP_MONO) {
        return NULL;
    }

    return (const FONT_GLYPH_SET*)Font->Context;
}

/************************************************************************/

/**
 * @brief Retrieve the default font glyph set.
 * @return Pointer to the default font.
 */
const FONT_GLYPH_SET* FontGetDefault(void) {
    return DefaultGlyphSet;
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

    DefaultGlyphSet = Font;
    DefaultFontFace = &FontAsciiFace;
    if (Font != &FontAscii) {
        static FONT_FACE GlyphSetFaceAdapter;
        GlyphSetFaceAdapter = (FONT_FACE){
            .Kind = FONT_FACE_KIND_BITMAP_MONO,
            .Context = Font,
            .GetMetrics = FontGlyphSetFaceGetMetrics,
            .GetGlyphBitmap = FontGlyphSetFaceGetGlyphBitmap
        };
        DefaultFontFace = &GlyphSetFaceAdapter;
    }

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
    U32 Index = 0;

    if (!FontResolveGlyphIndex(Font, Codepoint, &Index)) {
        return NULL;
    }

    return Font->GlyphData + (Index * Font->Height * Font->BytesPerRow);
}

/************************************************************************/
