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


    Desktop high-level text drawing

\************************************************************************/

#include "Desktop.h"

#include "drivers/graphics/Graphics-TextRenderer.h"

/***************************************************************************/

/**
 * @brief Draw one string in one graphics context using the current pen/brush colors.
 * @param TextInfo Text draw parameters.
 * @return TRUE on success.
 */
BOOL DrawText(LPGFX_TEXT_DRAW_INFO TextInfo) {
    LPGRAPHICSCONTEXT Context = NULL;
    GFX_TEXT_DRAW_INFO DrawInfo;
    BOOL Result = FALSE;

    if (TextInfo == NULL) return FALSE;
    if (TextInfo->Header.Size < sizeof(GFX_TEXT_DRAW_INFO)) return FALSE;
    if (TextInfo->Text == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)TextInfo->GC;
    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    DrawInfo = *TextInfo;
    DrawInfo.X = Context->Origin.X + DrawInfo.X;
    DrawInfo.Y = Context->Origin.Y + DrawInfo.Y;

    LockMutex(&(Context->Mutex), INFINITY);
    Result = GfxTextDrawString(Context, &DrawInfo);
    UnlockMutex(&(Context->Mutex));

    return Result;
}

/***************************************************************************/

/**
 * @brief Measure one string using one font face.
 * @param TextInfo Text measure parameters.
 * @return TRUE on success.
 */
BOOL MeasureText(LPGFX_TEXT_MEASURE_INFO TextInfo) {
    if (TextInfo == NULL) return FALSE;
    if (TextInfo->Header.Size < sizeof(GFX_TEXT_MEASURE_INFO)) return FALSE;
    if (TextInfo->Text == NULL) return FALSE;

    return GfxTextMeasure(TextInfo);
}

/***************************************************************************/
