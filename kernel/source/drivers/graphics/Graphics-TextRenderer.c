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

#include "drivers/graphics/Graphics-TextRenderer.h"

#include "CoreString.h"
#include "Font.h"
#include "Memory.h"

/************************************************************************/

static const U32 GfxTextPalette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

/************************************************************************/

#define GFX_TEXT_CURSOR_STATE_MAX_CONTEXTS 8
#define GFX_TEXT_CURSOR_STATE_MAX_SAVED_BYTES 2048

/************************************************************************/

typedef struct tag_GFX_TEXT_CURSOR_STATE {
    LPGRAPHICSCONTEXT Context;
    BOOL HasPosition;
    BOOL IsVisible;
    I32 PixelX;
    I32 PixelY;
    I32 PixelWidth;
    I32 CursorHeight;
    U32 ForegroundColorIndex;
    U32 SavedBytes;
    U8 SavedPixels[GFX_TEXT_CURSOR_STATE_MAX_SAVED_BYTES];
} GFX_TEXT_CURSOR_STATE, *LPGFX_TEXT_CURSOR_STATE;

/************************************************************************/

static GFX_TEXT_CURSOR_STATE GfxTextCursorStates[GFX_TEXT_CURSOR_STATE_MAX_CONTEXTS] = {0};

/************************************************************************/

static LPGFX_TEXT_CURSOR_STATE GfxTextGetCursorState(LPGRAPHICSCONTEXT Context) {
    UINT Index = 0;
    UINT FreeIndex = MAX_UINT;

    if (Context == NULL) {
        return NULL;
    }

    for (Index = 0; Index < ARRAY_COUNT(GfxTextCursorStates); Index++) {
        if (GfxTextCursorStates[Index].Context == Context) {
            return &GfxTextCursorStates[Index];
        }

        if (FreeIndex == MAX_UINT && GfxTextCursorStates[Index].Context == NULL) {
            FreeIndex = Index;
        }
    }

    if (FreeIndex == MAX_UINT) {
        return NULL;
    }

    GfxTextCursorStates[FreeIndex] = (GFX_TEXT_CURSOR_STATE){0};
    GfxTextCursorStates[FreeIndex].Context = Context;
    return &GfxTextCursorStates[FreeIndex];
}

/************************************************************************/

static BOOL GfxTextCursorComputeArea(LPGRAPHICSCONTEXT Context,
                                     LPGFX_TEXT_CURSOR_INFO Info,
                                     I32* PixelXOut,
                                     I32* PixelYOut,
                                     I32* PixelWidthOut,
                                     I32* CursorHeightOut,
                                     U32* SavedBytesOut) {
    I32 PixelX = 0;
    I32 PixelY = 0;
    I32 PixelWidth = 0;
    I32 CursorHeight = 0;
    U32 BytesPerPixel = 0;
    U32 SavedBytes = 0;

    if (Context == NULL || Info == NULL || PixelXOut == NULL || PixelYOut == NULL ||
        PixelWidthOut == NULL || CursorHeightOut == NULL || SavedBytesOut == NULL) {
        return FALSE;
    }

    if (Info->CellWidth == 0 || Info->CellHeight == 0) {
        return FALSE;
    }

    if (Context->BitsPerPixel != 16 && Context->BitsPerPixel != 24 && Context->BitsPerPixel != 32) {
        return FALSE;
    }

    BytesPerPixel = Context->BitsPerPixel / 8;
    CursorHeight = (Info->CellHeight >= 4) ? 2 : 1;
    PixelWidth = (I32)Info->CellWidth;

    PixelX = (I32)(Info->CellX * Info->CellWidth);
    PixelY = (I32)(Info->CellY * Info->CellHeight) + (I32)Info->CellHeight - CursorHeight;

    if (PixelX < 0 || PixelY < 0 || PixelWidth <= 0 || CursorHeight <= 0) {
        return FALSE;
    }

    if (PixelX >= Context->Width || PixelY >= Context->Height) {
        return FALSE;
    }

    if (PixelX + PixelWidth > Context->Width) {
        PixelWidth = Context->Width - PixelX;
    }

    if (PixelY + CursorHeight > Context->Height) {
        CursorHeight = Context->Height - PixelY;
    }

    if (PixelWidth <= 0 || CursorHeight <= 0) {
        return FALSE;
    }

    SavedBytes = (U32)PixelWidth * (U32)CursorHeight * BytesPerPixel;
    if (SavedBytes == 0 || SavedBytes > GFX_TEXT_CURSOR_STATE_MAX_SAVED_BYTES) {
        return FALSE;
    }

    *PixelXOut = PixelX;
    *PixelYOut = PixelY;
    *PixelWidthOut = PixelWidth;
    *CursorHeightOut = CursorHeight;
    *SavedBytesOut = SavedBytes;
    return TRUE;
}

/************************************************************************/

static BOOL GfxTextCursorSavePixels(LPGFX_TEXT_CURSOR_STATE State, LPGRAPHICSCONTEXT Context) {
    U32 BytesPerPixel = 0;
    U32 RowBytes = 0;
    U32 CopyOffset = 0;
    I32 Row = 0;

    SAFE_USE_2(State, Context) {
        BytesPerPixel = Context->BitsPerPixel / 8;
        RowBytes = (U32)State->PixelWidth * BytesPerPixel;
        CopyOffset = 0;

        for (Row = 0; Row < State->CursorHeight; Row++) {
            U8* Source = Context->MemoryBase +
                         ((State->PixelY + Row) * (I32)Context->BytesPerScanLine) +
                         (State->PixelX * (I32)BytesPerPixel);
            MemoryCopy(&State->SavedPixels[CopyOffset], Source, RowBytes);
            CopyOffset += RowBytes;
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

static BOOL GfxTextCursorRestorePixels(LPGFX_TEXT_CURSOR_STATE State, LPGRAPHICSCONTEXT Context) {
    U32 BytesPerPixel = 0;
    U32 RowBytes = 0;
    U32 CopyOffset = 0;
    I32 Row = 0;

    SAFE_USE_2(State, Context) {
        if (State->HasPosition == FALSE || State->SavedBytes == 0) {
            return TRUE;
        }

        BytesPerPixel = Context->BitsPerPixel / 8;
        RowBytes = (U32)State->PixelWidth * BytesPerPixel;
        CopyOffset = 0;

        for (Row = 0; Row < State->CursorHeight; Row++) {
            U8* Destination = Context->MemoryBase +
                              ((State->PixelY + Row) * (I32)Context->BytesPerScanLine) +
                              (State->PixelX * (I32)BytesPerPixel);
            MemoryCopy(Destination, &State->SavedPixels[CopyOffset], RowBytes);
            CopyOffset += RowBytes;
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Scale one color channel to target mask size.
 * @param Value Source 8-bit channel.
 * @param MaskSize Target bit width.
 * @return Scaled channel value.
 */
static U32 GfxTextScaleColor(U32 Value, U32 MaskSize) {
    if (MaskSize == 0) {
        return 0;
    }

    if (MaskSize >= 8) {
        return Value & 0xFF;
    }

    U32 MaxValue = (1 << MaskSize) - 1;
    return (Value * MaxValue) / 255;
}

/************************************************************************/

/**
 * @brief Convert VGA-like color index to context pixel format.
 * @param Context Graphics context.
 * @param ColorIndex 0..15 color index.
 * @return Packed pixel value matching context format.
 */
static U32 GfxTextPackColor(LPGRAPHICSCONTEXT Context, U32 ColorIndex) {
    U32 Color = GfxTextPalette[ColorIndex & 0x0F];
    U32 Red = (Color >> 16) & 0xFF;
    U32 Green = (Color >> 8) & 0xFF;
    U32 Blue = Color & 0xFF;

    UNUSED(Context);

    if (Context->BitsPerPixel == 16) {
        U32 R = GfxTextScaleColor(Red, 5);
        U32 G = GfxTextScaleColor(Green, 6);
        U32 B = GfxTextScaleColor(Blue, 5);
        return (R << 11) | (G << 5) | B;
    }

    if (Context->BitsPerPixel == 24) {
        return (Blue << 16) | (Green << 8) | Red;
    }

    return (Blue << 16) | (Green << 8) | Red;
}

/************************************************************************/

/**
 * @brief Write one pixel in context memory using direct set semantics.
 * @param Context Graphics context.
 * @param X Pixel X.
 * @param Y Pixel Y.
 * @param Color Packed color matching context format.
 */
static void GfxTextWritePixel(LPGRAPHICSCONTEXT Context, I32 X, I32 Y, U32 Color) {
    U32 Offset = 0;
    U8* Pixel = NULL;

    if (Context == NULL || Context->MemoryBase == NULL) {
        return;
    }

    if (X < Context->LoClip.X || X > Context->HiClip.X || Y < Context->LoClip.Y || Y > Context->HiClip.Y) {
        return;
    }

    switch (Context->BitsPerPixel) {
        case 16:
            Offset = (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)X << 1);
            *((U16*)(Context->MemoryBase + Offset)) = (U16)Color;
            return;
        case 24:
            Offset = (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)X * 3);
            Pixel = Context->MemoryBase + Offset;
            Pixel[0] = (U8)(Color & 0xFF);
            Pixel[1] = (U8)((Color >> 8) & 0xFF);
            Pixel[2] = (U8)((Color >> 16) & 0xFF);
            return;
        case 32:
            Offset = (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)X << 2);
            *((U32*)(Context->MemoryBase + Offset)) = Color;
            return;
    }
}

/************************************************************************/

/**
 * @brief Fill a rectangle with one color.
 * @param Context Graphics context.
 * @param X1 Left pixel.
 * @param Y1 Top pixel.
 * @param X2 Right pixel.
 * @param Y2 Bottom pixel.
 * @param Color Fill color.
 */
static void GfxTextFillRect(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, U32 Color) {
    if (Context == NULL || Context->MemoryBase == NULL) {
        return;
    }

    if (X1 > X2 || Y1 > Y2) {
        return;
    }

    for (I32 Y = Y1; Y <= Y2; Y++) {
        for (I32 X = X1; X <= X2; X++) {
            GfxTextWritePixel(Context, X, Y, Color);
        }
    }
}

/************************************************************************/

/**
 * @brief Draw one text cell (background + glyph).
 * @param Context Graphics context.
 * @param Info Text cell descriptor.
 * @return TRUE on success.
 */
BOOL GfxTextPutCell(LPGRAPHICSCONTEXT Context, LPGFX_TEXT_CELL_INFO Info) {
    const FONT_GLYPH_SET* Font = NULL;
    const U8* Glyph = NULL;
    U32 Foreground = 0;
    U32 Background = 0;
    I32 PixelX = 0;
    I32 PixelY = 0;

    if (Context == NULL || Info == NULL || Context->MemoryBase == NULL) {
        return FALSE;
    }

    if (Info->CellWidth == 0 || Info->CellHeight == 0) {
        return FALSE;
    }

    PixelX = (I32)(Info->CellX * Info->CellWidth);
    PixelY = (I32)(Info->CellY * Info->CellHeight);
    Foreground = GfxTextPackColor(Context, Info->ForegroundColorIndex);
    Background = GfxTextPackColor(Context, Info->BackgroundColorIndex);

    GfxTextFillRect(Context,
                    PixelX,
                    PixelY,
                    PixelX + (I32)Info->CellWidth - 1,
                    PixelY + (I32)Info->CellHeight - 1,
                    Background);

    Font = FontGetDefault();
    if (Font == NULL || Font->GlyphData == NULL) {
        return FALSE;
    }

    Glyph = FontGetGlyph(Font, (U32)Info->Character);
    if (Glyph == NULL) {
        return FALSE;
    }

    for (U32 Row = 0; Row < Font->Height && Row < Info->CellHeight; Row++) {
        for (U32 Col = 0; Col < Font->Width && Col < Info->CellWidth; Col++) {
            U32 ByteIndex = (Row * Font->BytesPerRow) + (Col / 8);
            U8 Bits = Glyph[ByteIndex];
            U32 BitMask = 0x80 >> (Col % 8);
            if ((Bits & BitMask) != 0) {
                GfxTextWritePixel(Context, PixelX + (I32)Col, PixelY + (I32)Row, Foreground);
            }
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Clear a text-cell region.
 * @param Context Graphics context.
 * @param Info Text region descriptor.
 * @return TRUE on success.
 */
BOOL GfxTextClearRegion(LPGRAPHICSCONTEXT Context, LPGFX_TEXT_REGION_INFO Info) {
    U32 Background = 0;
    I32 X1 = 0;
    I32 Y1 = 0;
    I32 X2 = 0;
    I32 Y2 = 0;

    if (Context == NULL || Info == NULL || Context->MemoryBase == NULL) {
        return FALSE;
    }

    if (Info->RegionCellWidth == 0 || Info->RegionCellHeight == 0 ||
        Info->GlyphCellWidth == 0 || Info->GlyphCellHeight == 0) {
        return FALSE;
    }

    X1 = (I32)(Info->CellX * Info->GlyphCellWidth);
    Y1 = (I32)(Info->CellY * Info->GlyphCellHeight);
    X2 = X1 + (I32)(Info->RegionCellWidth * Info->GlyphCellWidth) - 1;
    Y2 = Y1 + (I32)(Info->RegionCellHeight * Info->GlyphCellHeight) - 1;
    Background = GfxTextPackColor(Context, Info->BackgroundColorIndex);

    GfxTextFillRect(Context, X1, Y1, X2, Y2, Background);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Scroll a text-cell region by one text row.
 * @param Context Graphics context.
 * @param Info Text region descriptor.
 * @return TRUE on success.
 */
BOOL GfxTextScrollRegion(LPGRAPHICSCONTEXT Context, LPGFX_TEXT_REGION_INFO Info) {
    I32 PixelX = 0;
    I32 PixelY = 0;
    I32 PixelWidth = 0;
    I32 PixelHeight = 0;
    I32 GlyphCellHeight = 0;
    U32 Background = 0;
    U8* Dest = NULL;
    U8* Src = NULL;
    UINT RowBytes = 0;
    I32 Row = 0;

    if (Context == NULL || Info == NULL || Context->MemoryBase == NULL) {
        return FALSE;
    }

    if (Info->RegionCellWidth == 0 || Info->RegionCellHeight == 0 ||
        Info->GlyphCellWidth == 0 || Info->GlyphCellHeight == 0) {
        return FALSE;
    }

    PixelX = (I32)(Info->CellX * Info->GlyphCellWidth);
    PixelY = (I32)(Info->CellY * Info->GlyphCellHeight);
    PixelWidth = (I32)(Info->RegionCellWidth * Info->GlyphCellWidth);
    PixelHeight = (I32)(Info->RegionCellHeight * Info->GlyphCellHeight);
    GlyphCellHeight = (I32)Info->GlyphCellHeight;

    if (PixelHeight <= GlyphCellHeight || PixelWidth <= 0) {
        return TRUE;
    }

    RowBytes = (UINT)(PixelWidth * (I32)(Context->BitsPerPixel / 8));
    if (RowBytes == 0) {
        return FALSE;
    }

    for (Row = 0; Row < PixelHeight - GlyphCellHeight; Row++) {
        Dest = Context->MemoryBase + ((PixelY + Row) * (I32)Context->BytesPerScanLine) + (PixelX * (I32)(Context->BitsPerPixel / 8));
        Src = Context->MemoryBase + ((PixelY + Row + GlyphCellHeight) * (I32)Context->BytesPerScanLine) + (PixelX * (I32)(Context->BitsPerPixel / 8));
        MemoryMove(Dest, Src, RowBytes);
    }

    Background = GfxTextPackColor(Context, Info->BackgroundColorIndex);
    GfxTextFillRect(Context,
                    PixelX,
                    PixelY + (PixelHeight - GlyphCellHeight),
                    PixelX + PixelWidth - 1,
                    PixelY + PixelHeight - 1,
                    Background);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Draw a simple cursor bar at the bottom of one text cell.
 * @param Context Graphics context.
 * @param Info Cursor descriptor.
 * @return TRUE on success.
 */
BOOL GfxTextSetCursor(LPGRAPHICSCONTEXT Context, LPGFX_TEXT_CURSOR_INFO Info) {
    LPGFX_TEXT_CURSOR_STATE State = NULL;
    BOOL WasVisible = FALSE;
    I32 PixelX = 0;
    I32 PixelY = 0;
    I32 PixelWidth = 0;
    I32 CursorHeight = 0;
    U32 SavedBytes = 0;
    U32 Foreground = 0;

    if (Context == NULL || Info == NULL || Context->MemoryBase == NULL) {
        return FALSE;
    }

    State = GfxTextGetCursorState(Context);
    if (State == NULL) {
        return FALSE;
    }

    WasVisible = State->IsVisible;
    if (State->IsVisible != FALSE) {
        (void)GfxTextCursorRestorePixels(State, Context);
        State->IsVisible = FALSE;
    }

    if (!GfxTextCursorComputeArea(Context, Info, &PixelX, &PixelY, &PixelWidth, &CursorHeight, &SavedBytes)) {
        State->HasPosition = FALSE;
        State->SavedBytes = 0;
        return FALSE;
    }

    State->PixelX = PixelX;
    State->PixelY = PixelY;
    State->PixelWidth = PixelWidth;
    State->CursorHeight = CursorHeight;
    State->ForegroundColorIndex = Info->ForegroundColorIndex;
    State->SavedBytes = SavedBytes;
    State->HasPosition = TRUE;

    if (Info->ForegroundColorIndex > 15) {
        State->ForegroundColorIndex = 15;
    }

    if (WasVisible == FALSE) {
        return TRUE;
    }

    if (!GfxTextCursorSavePixels(State, Context)) {
        return FALSE;
    }

    Foreground = GfxTextPackColor(Context, State->ForegroundColorIndex);

    GfxTextFillRect(Context,
                    State->PixelX,
                    State->PixelY,
                    State->PixelX + State->PixelWidth - 1,
                    State->PixelY + State->CursorHeight - 1,
                    Foreground);
    State->IsVisible = TRUE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Cursor visibility hook.
 * @param Context Graphics context.
 * @param Info Visibility descriptor.
 * @return TRUE on success.
 */
BOOL GfxTextSetCursorVisible(LPGRAPHICSCONTEXT Context, LPGFX_TEXT_CURSOR_VISIBLE_INFO Info) {
    LPGFX_TEXT_CURSOR_STATE State = NULL;
    U32 Foreground = 0;

    if (Context == NULL || Info == NULL || Context->MemoryBase == NULL) {
        return FALSE;
    }

    State = GfxTextGetCursorState(Context);
    if (State == NULL) {
        return FALSE;
    }

    if (Info->IsVisible != FALSE) {
        if (State->HasPosition == FALSE || State->IsVisible != FALSE) {
            return TRUE;
        }

        if (!GfxTextCursorSavePixels(State, Context)) {
            return FALSE;
        }

        Foreground = GfxTextPackColor(Context, State->ForegroundColorIndex);
        GfxTextFillRect(Context,
                        State->PixelX,
                        State->PixelY,
                        State->PixelX + State->PixelWidth - 1,
                        State->PixelY + State->CursorHeight - 1,
                        Foreground);
        State->IsVisible = TRUE;
        return TRUE;
    }

    if (State->IsVisible == FALSE) {
        return TRUE;
    }

    (void)GfxTextCursorRestorePixels(State, Context);
    State->IsVisible = FALSE;
    return TRUE;
}

/************************************************************************/
