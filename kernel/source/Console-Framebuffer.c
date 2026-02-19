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


    Console framebuffer path

\************************************************************************/

#include "Console-Internal.h"
#include "Font.h"
#include "Kernel.h"
#include "Memory.h"
#include "Log.h"
#include "DriverGetters.h"

/***************************************************************************/

static BOOL DATA_SECTION ConsoleFramebufferMappingInProgress = FALSE;
static BOOL DATA_SECTION ConsoleFramebufferCursorVisible = FALSE;
static U32 DATA_SECTION ConsoleFramebufferCursorX = 0;
static U32 DATA_SECTION ConsoleFramebufferCursorY = 0;
static BOOL DATA_SECTION ConsoleFramebufferCursorBackupValid = FALSE;
static U32 DATA_SECTION ConsoleFramebufferCursorBackupAbsoluteX = 0;
static U32 DATA_SECTION ConsoleFramebufferCursorBackupAbsoluteY = 0;
static U32 DATA_SECTION ConsoleFramebufferCursorBackupCellWidth = 0;
static U32 DATA_SECTION ConsoleFramebufferCursorBackupCellHeight = 0;
static U32 DATA_SECTION ConsoleFramebufferCursorBackupRowBytes = 0;
#define CONSOLE_CURSOR_BACKUP_MAX_BYTES 0x4000
static U8 DATA_SECTION ConsoleFramebufferCursorBackup[CONSOLE_CURSOR_BACKUP_MAX_BYTES];

/***************************************************************************/

static const U32 ConsolePalette[16] = {
    0x000000u, 0x0000AAu, 0x00AA00u, 0x00AAAAu,
    0xAA0000u, 0xAA00AAu, 0xAA5500u, 0xAAAAAAu,
    0x555555u, 0x5555FFu, 0x55FF55u, 0x55FFFFu,
    0xFF5555u, 0xFF55FFu, 0xFFFF55u, 0xFFFFFFu
};

/***************************************************************************/

static U32 ConsoleScaleColor(U32 Value, U32 MaskSize) {
    if (MaskSize == 0u) {
        return 0u;
    }

    if (MaskSize >= 8u) {
        return Value & 0xFFu;
    }

    U32 MaxValue = (1u << MaskSize) - 1u;
    return (Value * MaxValue) / 255u;
}

/***************************************************************************/

BOOL ConsoleEnsureFramebufferMapped(void) {
    if (Console.UseFramebuffer == FALSE || Console.FramebufferPhysical == 0) {
        return FALSE;
    }

    if (ConsoleFramebufferMappingInProgress != FALSE) {
        return FALSE;
    }

    if (Console.FramebufferLinear != NULL) {
        return TRUE;
    }

    LPDRIVER MemoryDriver = MemoryManagerGetDriver();
    if (MemoryDriver == NULL || (MemoryDriver->Flags & DRIVER_FLAG_READY) == 0u) {
        return FALSE;
    }

    if (Console.FramebufferBytesPerPixel == 0u || Console.FramebufferPitch == 0u ||
        Console.FramebufferHeight == 0u) {
        return FALSE;
    }

    UINT Size = (UINT)(Console.FramebufferPitch * Console.FramebufferHeight);
    ConsoleFramebufferMappingInProgress = TRUE;
    LINEAR Linear = MapFramebufferMemory(Console.FramebufferPhysical, Size);
    ConsoleFramebufferMappingInProgress = FALSE;
    if (Linear == 0) {
        ERROR(TEXT("[ConsoleEnsureFramebufferMapped] MapIOMemory failed for %p size %u"),
              (LPVOID)(LINEAR)Console.FramebufferPhysical,
              Size);
        return FALSE;
    }

    Console.FramebufferLinear = (U8*)Linear;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Returns TRUE while the framebuffer mapping is in progress.
 * @return TRUE if a mapping operation is active.
 */
BOOL ConsoleIsFramebufferMappingInProgress(void) {
    return (ConsoleFramebufferMappingInProgress != FALSE) ? TRUE : FALSE;
}

/***************************************************************************/

/**
 * @brief Invalidates the current framebuffer mapping so it can be remapped.
 */
void ConsoleInvalidateFramebufferMapping(void) {
    if (Console.UseFramebuffer == FALSE) {
        return;
    }

    Console.FramebufferLinear = NULL;
}

/***************************************************************************/

/**
 * @brief Return the framebuffer cell width in pixels.
 * @return Cell width in pixels.
 */
U32 ConsoleGetCellWidth(void) {
    return Console.FontWidth;
}

/***************************************************************************/

/**
 * @brief Return the framebuffer cell height in pixels.
 * @return Cell height in pixels.
 */
U32 ConsoleGetCellHeight(void) {
    return Console.FontHeight;
}

/***************************************************************************/

static U32 ConsolePackColor(U32 ColorIndex) {
    U32 Color = ConsolePalette[ColorIndex & 0x0Fu];
    U32 Red = (Color >> 16) & 0xFFu;
    U32 Green = (Color >> 8) & 0xFFu;
    U32 Blue = Color & 0xFFu;

    U32 Packed = 0u;
    Packed |= ConsoleScaleColor(Red, Console.FramebufferRedMaskSize) << Console.FramebufferRedPosition;
    Packed |= ConsoleScaleColor(Green, Console.FramebufferGreenMaskSize) << Console.FramebufferGreenPosition;
    Packed |= ConsoleScaleColor(Blue, Console.FramebufferBlueMaskSize) << Console.FramebufferBluePosition;
    return Packed;
}

/***************************************************************************/

static void ConsoleWritePixel(U32 X, U32 Y, U32 Pixel) {
    if (Console.FramebufferLinear == NULL) {
        return;
    }

    U32 Offset = (Y * Console.FramebufferPitch) + (X * Console.FramebufferBytesPerPixel);
    U8* Target = Console.FramebufferLinear + Offset;

    switch (Console.FramebufferBytesPerPixel) {
        case 4:
            *((U32*)Target) = Pixel;
            break;
        case 3:
            Target[0] = (U8)(Pixel & 0xFFu);
            Target[1] = (U8)((Pixel >> 8) & 0xFFu);
            Target[2] = (U8)((Pixel >> 16) & 0xFFu);
            break;
        case 2:
            *((U16*)Target) = (U16)Pixel;
            break;
        default:
            break;
    }
}

/***************************************************************************/

/**
 * @brief Validate that a framebuffer rectangle can be written safely.
 *
 * The function probes the first and last byte of each row to ensure the
 * underlying linear mapping is present before any write takes place.
 *
 * @param X Rectangle left coordinate in pixels.
 * @param Y Rectangle top coordinate in pixels.
 * @param Width Rectangle width in pixels.
 * @param Height Rectangle height in pixels.
 * @return TRUE when all tested addresses are mapped, FALSE otherwise.
 */
static BOOL ConsoleIsFramebufferRectMapped(U32 X, U32 Y, U32 Width, U32 Height) {
    if (Console.FramebufferLinear == NULL || Width == 0u || Height == 0u) {
        return FALSE;
    }

    UINT BytesPerPixel = Console.FramebufferBytesPerPixel;
    if (BytesPerPixel == 0u) {
        return FALSE;
    }

    for (U32 Row = 0; Row < Height; ++Row) {
        LINEAR RowLinear = (LINEAR)Console.FramebufferLinear +
            (((LINEAR)(Y + Row) * Console.FramebufferPitch) + ((LINEAR)X * BytesPerPixel));
        LINEAR RowLastLinear = RowLinear + ((LINEAR)Width * BytesPerPixel) - 1u;

        if (IsValidMemory(RowLinear) == FALSE || IsValidMemory(RowLastLinear) == FALSE) {
            return FALSE;
        }
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Ensure framebuffer mapping exists and is writable for a rectangle.
 *
 * @param X Rectangle left coordinate in pixels.
 * @param Y Rectangle top coordinate in pixels.
 * @param Width Rectangle width in pixels.
 * @param Height Rectangle height in pixels.
 * @return TRUE when drawing can proceed, FALSE otherwise.
 */
static BOOL ConsoleEnsureFramebufferRectMapped(U32 X, U32 Y, U32 Width, U32 Height) {
    if (ConsoleIsFramebufferRectMapped(X, Y, Width, Height)) {
        return TRUE;
    }

    ConsoleInvalidateFramebufferMapping();
    if (ConsoleEnsureFramebufferMapped() == FALSE) {
        return FALSE;
    }

    return ConsoleIsFramebufferRectMapped(X, Y, Width, Height);
}

/***************************************************************************/

static void ConsoleFillRect32(U32 X, U32 Y, U32 Width, U32 Height, U32 Pixel) {
    if (Console.FramebufferLinear == NULL) {
        return;
    }

    if (Console.FramebufferBytesPerPixel != 4u || Width == 0u || Height == 0u) {
        return;
    }

    if (ConsoleEnsureFramebufferRectMapped(X, Y, Width, Height) == FALSE) {
        return;
    }

    for (U32 Row = 0; Row < Height; ++Row) {
        U8* Dest = Console.FramebufferLinear +
                   ((Y + Row) * Console.FramebufferPitch) +
                   (X * 4u);
        U32* Dest32 = (U32*)Dest;
        for (U32 Col = 0; Col < Width; ++Col) {
            Dest32[Col] = Pixel;
        }
    }
}

/***************************************************************************/

static BOOL ConsoleBackupFramebufferCellAbsolute(U32 AbsoluteX, U32 AbsoluteY) {
    U32 CellWidth;
    U32 CellHeight;
    U32 PixelX;
    U32 PixelY;
    U32 RowBytes;
    U32 TotalBytes;

    if (ConsoleEnsureFramebufferMapped() == FALSE) {
        return FALSE;
    }

    CellWidth = ConsoleGetCellWidth();
    CellHeight = ConsoleGetCellHeight();
    PixelX = AbsoluteX * CellWidth;
    PixelY = AbsoluteY * CellHeight;
    RowBytes = CellWidth * Console.FramebufferBytesPerPixel;
    TotalBytes = RowBytes * CellHeight;

    if (RowBytes == 0u || TotalBytes == 0u || TotalBytes > CONSOLE_CURSOR_BACKUP_MAX_BYTES) {
        return FALSE;
    }

    if (ConsoleEnsureFramebufferRectMapped(PixelX, PixelY, CellWidth, CellHeight) == FALSE) {
        return FALSE;
    }

    for (U32 Row = 0; Row < CellHeight; ++Row) {
        U8* Source = Console.FramebufferLinear +
                     ((PixelY + Row) * Console.FramebufferPitch) +
                     (PixelX * Console.FramebufferBytesPerPixel);
        MemoryMove(&ConsoleFramebufferCursorBackup[Row * RowBytes], Source, RowBytes);
    }

    ConsoleFramebufferCursorBackupAbsoluteX = AbsoluteX;
    ConsoleFramebufferCursorBackupAbsoluteY = AbsoluteY;
    ConsoleFramebufferCursorBackupCellWidth = CellWidth;
    ConsoleFramebufferCursorBackupCellHeight = CellHeight;
    ConsoleFramebufferCursorBackupRowBytes = RowBytes;
    ConsoleFramebufferCursorBackupValid = TRUE;
    return TRUE;
}

/***************************************************************************/

static void ConsoleRestoreFramebufferCursorBackup(void) {
    U32 PixelX;
    U32 PixelY;

    if (ConsoleFramebufferCursorBackupValid == FALSE) {
        return;
    }

    if (ConsoleEnsureFramebufferMapped() == FALSE) {
        ConsoleFramebufferCursorBackupValid = FALSE;
        return;
    }

    PixelX = ConsoleFramebufferCursorBackupAbsoluteX * ConsoleFramebufferCursorBackupCellWidth;
    PixelY = ConsoleFramebufferCursorBackupAbsoluteY * ConsoleFramebufferCursorBackupCellHeight;

    if (ConsoleEnsureFramebufferRectMapped(
            PixelX,
            PixelY,
            ConsoleFramebufferCursorBackupCellWidth,
            ConsoleFramebufferCursorBackupCellHeight) == FALSE) {
        ConsoleFramebufferCursorBackupValid = FALSE;
        return;
    }

    for (U32 Row = 0; Row < ConsoleFramebufferCursorBackupCellHeight; ++Row) {
        U8* Destination = Console.FramebufferLinear +
                          ((PixelY + Row) * Console.FramebufferPitch) +
                          (PixelX * Console.FramebufferBytesPerPixel);
        MemoryMove(Destination, &ConsoleFramebufferCursorBackup[Row * ConsoleFramebufferCursorBackupRowBytes], ConsoleFramebufferCursorBackupRowBytes);
    }

    ConsoleFramebufferCursorBackupValid = FALSE;
}

/***************************************************************************/

static void ConsoleDrawFramebufferCursorAbsolute(U32 AbsoluteX, U32 AbsoluteY) {
    U32 CellWidth;
    U32 CellHeight;
    U32 PixelX;
    U32 PixelY;
    U32 CursorHeight;
    U32 CursorY;
    U32 CursorColor;

    if (ConsoleEnsureFramebufferMapped() == FALSE) {
        return;
    }

    CellWidth = ConsoleGetCellWidth();
    CellHeight = ConsoleGetCellHeight();
    PixelX = AbsoluteX * CellWidth;
    PixelY = AbsoluteY * CellHeight;
    CursorHeight = (CellHeight >= 4u) ? 2u : 1u;
    CursorY = PixelY + (CellHeight - CursorHeight);
    CursorColor = ConsolePackColor(Console.ForeColor);

    if (ConsoleEnsureFramebufferRectMapped(PixelX, CursorY, CellWidth, CursorHeight) == FALSE) {
        return;
    }

    if (Console.FramebufferBytesPerPixel == 4u) {
        ConsoleFillRect32(PixelX, CursorY, CellWidth, CursorHeight, CursorColor);
        return;
    }

    for (U32 Row = 0; Row < CursorHeight; ++Row) {
        for (U32 Col = 0; Col < CellWidth; ++Col) {
            ConsoleWritePixel(PixelX + Col, CursorY + Row, CursorColor);
        }
    }
}

/***************************************************************************/

void ConsoleDrawGlyph(U32 X, U32 Y, STR Char) {
    const FONT_GLYPH_SET* Font = FontGetDefault();
    if (Font == NULL || Font->GlyphData == NULL) {
        return;
    }

    const U8* Glyph = FontGetGlyph(Font, (U32)Char);
    if (Glyph == NULL) {
        return;
    }

    U32 Foreground = ConsolePackColor(Console.ForeColor);
    U32 Background = ConsolePackColor(Console.BackColor);
    U32 CellWidth = ConsoleGetCellWidth();
    U32 CellHeight = ConsoleGetCellHeight();

    if (Console.FramebufferBytesPerPixel == 4u) {
        ConsoleFillRect32(X, Y, CellWidth, CellHeight, Background);
    } else {
        for (U32 Row = 0; Row < CellHeight; ++Row) {
            for (U32 Col = 0; Col < CellWidth; ++Col) {
                ConsoleWritePixel(X + Col, Y + Row, Background);
            }
        }
    }

    for (U32 Row = 0; Row < Font->Height; ++Row) {
        for (U32 Col = 0; Col < Font->Width; ++Col) {
            U32 ByteIndex = (Row * Font->BytesPerRow) + (Col / 8u);
            U8 Bits = Glyph[ByteIndex];
            U32 BitMask = 0x80u >> (Col % 8u);
            U32 Pixel = (Bits & BitMask) ? Foreground : Background;
            ConsoleWritePixel(X + Col, Y + Row, Pixel);
        }
    }
}

/***************************************************************************/

static BOOL ConsoleResolvePrimaryCursorAbsolute(U32 CursorX, U32 CursorY, U32* AbsoluteX, U32* AbsoluteY) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(0, &State) == FALSE) {
        return FALSE;
    }

    if (CursorX >= State.Width || CursorY >= State.Height) {
        return FALSE;
    }

    *AbsoluteX = State.X + CursorX;
    *AbsoluteY = State.Y + CursorY;
    return TRUE;
}

/***************************************************************************/

void ConsoleHideFramebufferCursor(void) {
    if (Console.UseFramebuffer == FALSE || ConsoleFramebufferCursorVisible == FALSE) {
        return;
    }

    ConsoleRestoreFramebufferCursorBackup();
    ConsoleFramebufferCursorVisible = FALSE;
}

/***************************************************************************/

void ConsoleShowFramebufferCursor(void) {
    U32 AbsoluteX;
    U32 AbsoluteY;

    if (Console.UseFramebuffer == FALSE) {
        return;
    }

    if (ConsoleResolvePrimaryCursorAbsolute(Console.CursorX, Console.CursorY, &AbsoluteX, &AbsoluteY) == FALSE) {
        ConsoleFramebufferCursorVisible = FALSE;
        return;
    }

    if (ConsoleBackupFramebufferCellAbsolute(AbsoluteX, AbsoluteY) == FALSE) {
        ConsoleFramebufferCursorVisible = FALSE;
        return;
    }

    ConsoleDrawFramebufferCursorAbsolute(AbsoluteX, AbsoluteY);
    ConsoleFramebufferCursorX = Console.CursorX;
    ConsoleFramebufferCursorY = Console.CursorY;
    ConsoleFramebufferCursorVisible = TRUE;
}

/***************************************************************************/

void ConsoleResetFramebufferCursorState(void) {
    ConsoleHideFramebufferCursor();
    ConsoleFramebufferCursorVisible = FALSE;
    ConsoleFramebufferCursorX = 0;
    ConsoleFramebufferCursorY = 0;
    ConsoleFramebufferCursorBackupValid = FALSE;
    ConsoleFramebufferCursorBackupAbsoluteX = 0;
    ConsoleFramebufferCursorBackupAbsoluteY = 0;
    ConsoleFramebufferCursorBackupCellWidth = 0;
    ConsoleFramebufferCursorBackupCellHeight = 0;
    ConsoleFramebufferCursorBackupRowBytes = 0;
}

/***************************************************************************/

void ConsoleClearRegionFramebuffer(U32 RegionIndex) {
    if (ConsoleEnsureFramebufferMapped() == FALSE) {
        return;
    }

    CONSOLE_REGION_STATE State;
    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) {
        return;
    }

    const U32 Background = ConsolePackColor(Console.BackColor);
    const U32 CellWidth = ConsoleGetCellWidth();
    const U32 CellHeight = ConsoleGetCellHeight();
    const U32 PixelX = State.X * CellWidth;
    const U32 PixelY = State.Y * CellHeight;
    const U32 PixelWidth = State.Width * CellWidth;
    const U32 PixelHeight = State.Height * CellHeight;

    for (U32 Row = 0; Row < PixelHeight; ++Row) {
        for (U32 Col = 0; Col < PixelWidth; ++Col) {
            ConsoleWritePixel(PixelX + Col, PixelY + Row, Background);
        }
    }
}

/***************************************************************************/

void ConsoleScrollRegionFramebuffer(U32 RegionIndex) {
    if (ConsoleEnsureFramebufferMapped() == FALSE) {
        return;
    }

    CONSOLE_REGION_STATE State;
    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) {
        return;
    }

    const U32 CellWidth = ConsoleGetCellWidth();
    const U32 CellHeight = ConsoleGetCellHeight();
    const U32 PixelX = State.X * CellWidth;
    const U32 PixelY = State.Y * CellHeight;
    const U32 PixelWidth = State.Width * CellWidth;
    const U32 PixelHeight = State.Height * CellHeight;
    const U32 RowBytes = PixelWidth * Console.FramebufferBytesPerPixel;
    const U32 Background = ConsolePackColor(Console.BackColor);

    if (PixelHeight <= CellHeight) {
        return;
    }

    for (U32 Row = 0; Row < PixelHeight - CellHeight; ++Row) {
        U8* Dest = Console.FramebufferLinear +
                   ((PixelY + Row) * Console.FramebufferPitch) +
                   (PixelX * Console.FramebufferBytesPerPixel);
        U8* Src = Console.FramebufferLinear +
                  ((PixelY + Row + CellHeight) * Console.FramebufferPitch) +
                  (PixelX * Console.FramebufferBytesPerPixel);
        MemoryMove(Dest, Src, RowBytes);
    }

    if (Console.FramebufferBytesPerPixel == 4u) {
        ConsoleFillRect32(PixelX, PixelY + (PixelHeight - CellHeight), PixelWidth, CellHeight, Background);
    } else {
        for (U32 Row = PixelHeight - CellHeight; Row < PixelHeight; ++Row) {
            for (U32 Col = 0; Col < PixelWidth; ++Col) {
                ConsoleWritePixel(PixelX + Col, PixelY + Row, Background);
            }
        }
    }
}
