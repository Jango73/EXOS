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


    Console text operations through graphics backends

\************************************************************************/

#include "Console-Internal.h"

#include "DisplaySession.h"
#include "DriverGetters.h"
#include "Font.h"
#include "GFX.h"
#include "Kernel.h"

/************************************************************************/

static BOOL DATA_SECTION ConsoleTextCursorVisible = FALSE;
static U32 DATA_SECTION ConsoleTextCursorCellX = 0;
static U32 DATA_SECTION ConsoleTextCursorCellY = 0;

/************************************************************************/

/**
 * @brief Resolve current console text-cell dimensions.
 * @param CellWidth Output width in pixels.
 * @param CellHeight Output height in pixels.
 * @return TRUE when dimensions are valid.
 */
static BOOL ConsoleTextResolveCellSize(U32* CellWidth, U32* CellHeight) {
    const FONT_GLYPH_SET* Font = FontGetDefault();
    U32 ResolvedWidth = 0;
    U32 ResolvedHeight = 0;

    if (Font != NULL) {
        ResolvedWidth = Font->Width;
        ResolvedHeight = Font->Height;
    }

    if (ResolvedWidth == 0) {
        ResolvedWidth = (Console.FontWidth != 0) ? Console.FontWidth : 8;
    }

    if (ResolvedHeight == 0) {
        ResolvedHeight = (Console.FontHeight != 0) ? Console.FontHeight : 16;
    }

    if (ResolvedWidth == 0 || ResolvedHeight == 0) {
        return FALSE;
    }

    Console.FontWidth = ResolvedWidth;
    Console.FontHeight = ResolvedHeight;

    SAFE_USE_2(CellWidth, CellHeight) {
        *CellWidth = ResolvedWidth;
        *CellHeight = ResolvedHeight;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Resolve active graphics backend and context for console text path.
 * @param DriverOut Output driver pointer.
 * @param ContextOut Output graphics context pointer.
 * @return TRUE on success.
 */
static BOOL ConsoleTextAcquireContext(LPDRIVER* DriverOut, LPGRAPHICSCONTEXT* ContextOut) {
    LPDRIVER Driver = NULL;
    UINT ContextPointer = 0;
    LPGRAPHICSCONTEXT Context = NULL;

    if (DisplaySessionGetActiveFrontEnd() != DISPLAY_FRONTEND_CONSOLE) {
        return FALSE;
    }

    Driver = DisplaySessionGetActiveGraphicsDriver();
    if (Driver == NULL || Driver->Command == NULL || Driver == ConsoleGetDriver()) {
        Driver = GetGraphicsDriver();
    }

    if (Driver == NULL || Driver->Command == NULL || Driver == ConsoleGetDriver()) {
        return FALSE;
    }

    if ((Driver->Flags & DRIVER_FLAG_READY) == 0) {
        (void)Driver->Command(DF_LOAD, 0);
    }

    ContextPointer = Driver->Command(DF_GFX_CREATECONTEXT, 0);
    if (ContextPointer == 0) {
        return FALSE;
    }

    Context = (LPGRAPHICSCONTEXT)(LPVOID)ContextPointer;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) {
        return FALSE;
    }

    SAFE_USE_2(DriverOut, ContextOut) {
        *DriverOut = Driver;
        *ContextOut = Context;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Dispatch one text-cell draw to active graphics backend.
 * @param PixelX Pixel origin X.
 * @param PixelY Pixel origin Y.
 * @param Character Character to render.
 * @return TRUE when backend handled the operation.
 */
static BOOL ConsoleTextPutCell(U32 PixelX, U32 PixelY, STR Character) {
    LPDRIVER Driver = NULL;
    LPGRAPHICSCONTEXT Context = NULL;
    GFX_TEXT_CELL_INFO Info;
    U32 CellWidth = 0;
    U32 CellHeight = 0;

    if (ConsoleTextResolveCellSize(&CellWidth, &CellHeight) == FALSE) {
        return FALSE;
    }

    if (ConsoleTextAcquireContext(&Driver, &Context) == FALSE) {
        return FALSE;
    }

    Info = (GFX_TEXT_CELL_INFO){
        .Header = {.Size = sizeof(GFX_TEXT_CELL_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .GC = (HANDLE)Context,
        .CellX = PixelX / CellWidth,
        .CellY = PixelY / CellHeight,
        .CellWidth = CellWidth,
        .CellHeight = CellHeight,
        .Character = Character,
        .ForegroundColorIndex = Console.ForeColor,
        .BackgroundColorIndex = Console.BackColor
    };

    return Driver->Command(DF_GFX_TEXT_PUTCELL, (UINT)(LPVOID)&Info) != 0 ? TRUE : FALSE;
}

/************************************************************************/

/**
 * @brief Dispatch region clear to active graphics backend.
 * @param RegionIndex Console region index.
 * @return TRUE when backend handled the operation.
 */
static BOOL ConsoleTextClearRegion(U32 RegionIndex) {
    LPDRIVER Driver = NULL;
    LPGRAPHICSCONTEXT Context = NULL;
    GFX_TEXT_REGION_INFO Info;
    CONSOLE_REGION_STATE State;
    U32 CellWidth = 0;
    U32 CellHeight = 0;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) {
        return FALSE;
    }

    if (ConsoleTextResolveCellSize(&CellWidth, &CellHeight) == FALSE) {
        return FALSE;
    }

    if (ConsoleTextAcquireContext(&Driver, &Context) == FALSE) {
        return FALSE;
    }

    Info = (GFX_TEXT_REGION_INFO){
        .Header = {.Size = sizeof(GFX_TEXT_REGION_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .GC = (HANDLE)Context,
        .CellX = State.X,
        .CellY = State.Y,
        .RegionCellWidth = State.Width,
        .RegionCellHeight = State.Height,
        .GlyphCellWidth = CellWidth,
        .GlyphCellHeight = CellHeight,
        .ForegroundColorIndex = Console.ForeColor,
        .BackgroundColorIndex = Console.BackColor
    };

    return Driver->Command(DF_GFX_TEXT_CLEAR_REGION, (UINT)(LPVOID)&Info) != 0 ? TRUE : FALSE;
}

/************************************************************************/

/**
 * @brief Dispatch region scroll to active graphics backend.
 * @param RegionIndex Console region index.
 * @return TRUE when backend handled the operation.
 */
static BOOL ConsoleTextScrollRegion(U32 RegionIndex) {
    LPDRIVER Driver = NULL;
    LPGRAPHICSCONTEXT Context = NULL;
    GFX_TEXT_REGION_INFO Info;
    CONSOLE_REGION_STATE State;
    U32 CellWidth = 0;
    U32 CellHeight = 0;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) {
        return FALSE;
    }

    if (ConsoleTextResolveCellSize(&CellWidth, &CellHeight) == FALSE) {
        return FALSE;
    }

    if (ConsoleTextAcquireContext(&Driver, &Context) == FALSE) {
        return FALSE;
    }

    Info = (GFX_TEXT_REGION_INFO){
        .Header = {.Size = sizeof(GFX_TEXT_REGION_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .GC = (HANDLE)Context,
        .CellX = State.X,
        .CellY = State.Y,
        .RegionCellWidth = State.Width,
        .RegionCellHeight = State.Height,
        .GlyphCellWidth = CellWidth,
        .GlyphCellHeight = CellHeight,
        .ForegroundColorIndex = Console.ForeColor,
        .BackgroundColorIndex = Console.BackColor
    };

    return Driver->Command(DF_GFX_TEXT_SCROLL_REGION, (UINT)(LPVOID)&Info) != 0 ? TRUE : FALSE;
}

/************************************************************************/

/**
 * @brief Dispatch cursor position and visibility to active graphics backend.
 * @return TRUE when backend handled the operation.
 */
static BOOL ConsoleTextRefreshCursor(void) {
    LPDRIVER Driver = NULL;
    LPGRAPHICSCONTEXT Context = NULL;
    GFX_TEXT_CURSOR_INFO CursorInfo;
    GFX_TEXT_CURSOR_VISIBLE_INFO VisibleInfo;

    if (ConsoleTextAcquireContext(&Driver, &Context) == FALSE) {
        return FALSE;
    }

    CursorInfo = (GFX_TEXT_CURSOR_INFO){
        .Header = {.Size = sizeof(GFX_TEXT_CURSOR_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .GC = (HANDLE)Context,
        .CellX = ConsoleTextCursorCellX,
        .CellY = ConsoleTextCursorCellY
    };

    VisibleInfo = (GFX_TEXT_CURSOR_VISIBLE_INFO){
        .Header = {.Size = sizeof(GFX_TEXT_CURSOR_VISIBLE_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .GC = (HANDLE)Context,
        .IsVisible = ConsoleTextCursorVisible
    };

    if (Driver->Command(DF_GFX_TEXT_SET_CURSOR, (UINT)(LPVOID)&CursorInfo) == 0) {
        return FALSE;
    }

    return Driver->Command(DF_GFX_TEXT_SET_CURSOR_VISIBLE, (UINT)(LPVOID)&VisibleInfo) != 0 ? TRUE : FALSE;
}

/************************************************************************/

/**
 * @brief Validate console text backend availability.
 * @return TRUE when console text backend path is available.
 */
BOOL ConsoleEnsureFramebufferMapped(void) {
    LPDRIVER Driver = NULL;
    LPGRAPHICSCONTEXT Context = NULL;

    return ConsoleTextAcquireContext(&Driver, &Context);
}

/************************************************************************/

/**
 * @brief Returns TRUE while framebuffer mapping is in progress.
 * @return Always FALSE in backend-dispatch mode.
 */
BOOL ConsoleIsFramebufferMappingInProgress(void) {
    return FALSE;
}

/************************************************************************/

/**
 * @brief Invalidate direct framebuffer mapping.
 */
void ConsoleInvalidateFramebufferMapping(void) {
    // No direct framebuffer mapping is kept in console text dispatch mode.
}

/************************************************************************/

/**
 * @brief Return framebuffer cell width in pixels.
 * @return Cell width in pixels.
 */
U32 ConsoleGetCellWidth(void) {
    U32 Width = 0;
    U32 Height = 0;
    if (ConsoleTextResolveCellSize(&Width, &Height) == FALSE) {
        return 8;
    }
    return Width;
}

/************************************************************************/

/**
 * @brief Return framebuffer cell height in pixels.
 * @return Cell height in pixels.
 */
U32 ConsoleGetCellHeight(void) {
    U32 Width = 0;
    U32 Height = 0;
    if (ConsoleTextResolveCellSize(&Width, &Height) == FALSE) {
        return 16;
    }
    return Height;
}

/************************************************************************/

/**
 * @brief Draw one character cell through active graphics backend.
 * @param X Cell origin X in pixels.
 * @param Y Cell origin Y in pixels.
 * @param Char Character to draw.
 */
void ConsoleDrawGlyph(U32 X, U32 Y, STR Char) {
    (void)ConsoleTextPutCell(X, Y, Char);
}

/************************************************************************/

/**
 * @brief Hide software cursor in backend text path.
 */
void ConsoleHideFramebufferCursor(void) {
    ConsoleTextCursorVisible = FALSE;
    (void)ConsoleTextRefreshCursor();
}

/************************************************************************/

/**
 * @brief Show software cursor in backend text path.
 */
void ConsoleShowFramebufferCursor(void) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(0, &State) == FALSE) {
        return;
    }

    ConsoleTextCursorCellX = State.X + Console.CursorX;
    ConsoleTextCursorCellY = State.Y + Console.CursorY;
    ConsoleTextCursorVisible = TRUE;
    (void)ConsoleTextRefreshCursor();
}

/************************************************************************/

/**
 * @brief Reset backend cursor state cache.
 */
void ConsoleResetFramebufferCursorState(void) {
    ConsoleTextCursorVisible = FALSE;
    ConsoleTextCursorCellX = 0;
    ConsoleTextCursorCellY = 0;
}

/************************************************************************/

/**
 * @brief Clear one region through active graphics backend.
 * @param RegionIndex Console region index.
 */
void ConsoleClearRegionFramebuffer(U32 RegionIndex) {
    (void)ConsoleTextClearRegion(RegionIndex);
}

/************************************************************************/

/**
 * @brief Scroll one region through active graphics backend.
 * @param RegionIndex Console region index.
 */
void ConsoleScrollRegionFramebuffer(U32 RegionIndex) {
    (void)ConsoleTextScrollRegion(RegionIndex);
}

/************************************************************************/
