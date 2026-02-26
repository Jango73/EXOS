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


    Graphics text renderer helpers

\************************************************************************/

#ifndef GFX_TEXT_RENDERER_H_INCLUDED
#define GFX_TEXT_RENDERER_H_INCLUDED

/************************************************************************/

#include "GFX.h"

/************************************************************************/

BOOL GfxTextPutCell(LPGRAPHICSCONTEXT Context, LPGFX_TEXT_CELL_INFO Info);
BOOL GfxTextClearRegion(LPGRAPHICSCONTEXT Context, LPGFX_TEXT_REGION_INFO Info);
BOOL GfxTextScrollRegion(LPGRAPHICSCONTEXT Context, LPGFX_TEXT_REGION_INFO Info);
BOOL GfxTextSetCursor(LPGRAPHICSCONTEXT Context, LPGFX_TEXT_CURSOR_INFO Info);
BOOL GfxTextSetCursorVisible(LPGRAPHICSCONTEXT Context, LPGFX_TEXT_CURSOR_VISIBLE_INFO Info);

/************************************************************************/

#endif  // GFX_TEXT_RENDERER_H_INCLUDED
