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


    Early boot console

\************************************************************************/

#include "EarlyBootConsole.h"

#include "Font.h"
#include "Memory.h"
#include "vbr-multiboot.h"

/************************************************************************/

#define EARLY_BOOT_CONSOLE_CHARACTER_WIDTH 8
#define EARLY_BOOT_CONSOLE_CHARACTER_HEIGHT 16
#define EARLY_BOOT_CONSOLE_FOREGROUND_R 0
#define EARLY_BOOT_CONSOLE_FOREGROUND_G 255
#define EARLY_BOOT_CONSOLE_FOREGROUND_B 0
#define EARLY_BOOT_CONSOLE_BACKGROUND_R 0
#define EARLY_BOOT_CONSOLE_BACKGROUND_G 0
#define EARLY_BOOT_CONSOLE_BACKGROUND_B 0

/************************************************************************/

typedef struct tag_EARLY_BOOT_CONSOLE_STATE {
    BOOL Initialized;
    PHYSICAL FramebufferPhysical;
    U32 Width;
    U32 Height;
    U32 Pitch;
    U32 RedPosition;
    U32 RedMaskSize;
    U32 GreenPosition;
    U32 GreenMaskSize;
    U32 BluePosition;
    U32 BlueMaskSize;
    U32 CursorColumn;
    U32 CursorRow;
    U32 MaxColumns;
    U32 MaxRows;
    U32 ForegroundPixel;
    U32 BackgroundPixel;
    PHYSICAL CachedPagePhysical;
    U8* CachedPageLinear;
} EARLY_BOOT_CONSOLE_STATE;

/************************************************************************/

static EARLY_BOOT_CONSOLE_STATE G_EarlyBootConsole = {
    .Initialized = FALSE,
    .FramebufferPhysical = 0,
    .Width = 0,
    .Height = 0,
    .Pitch = 0,
    .RedPosition = 0,
    .RedMaskSize = 0,
    .GreenPosition = 0,
    .GreenMaskSize = 0,
    .BluePosition = 0,
    .BlueMaskSize = 0,
    .CursorColumn = 0,
    .CursorRow = 0,
    .MaxColumns = 0,
    .MaxRows = 0,
    .ForegroundPixel = 0,
    .BackgroundPixel = 0,
    .CachedPagePhysical = (PHYSICAL)-1,
    .CachedPageLinear = NULL};

/************************************************************************/

static U32 EarlyBootConsoleScaleColor(U32 Value, U32 MaskSize);
static U32 EarlyBootConsoleComposeColor(U32 Red, U32 Green, U32 Blue);
static U32 EarlyBootConsoleNormalizeCharacter(U32 Character);
static void EarlyBootConsoleWritePixel(U32 X, U32 Y, U32 Pixel);
static void EarlyBootConsoleDrawCharacter(U32 Column, U32 Row, U32 Character);
static void EarlyBootConsoleNewLine(void);

/************************************************************************/

/**
 * @brief Scale an 8-bit color channel to the framebuffer mask size.
 * @param Value Channel value in range [0, 255].
 * @param MaskSize Number of channel bits.
 * @return Scaled channel value.
 */
static U32 EarlyBootConsoleScaleColor(U32 Value, U32 MaskSize) {
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
 * @brief Compose one framebuffer pixel from RGB channels.
 * @param Red Red component.
 * @param Green Green component.
 * @param Blue Blue component.
 * @return Packed framebuffer pixel.
 */
static U32 EarlyBootConsoleComposeColor(U32 Red, U32 Green, U32 Blue) {
    U32 Pixel = 0;
    Pixel |= EarlyBootConsoleScaleColor(Red, G_EarlyBootConsole.RedMaskSize) << G_EarlyBootConsole.RedPosition;
    Pixel |= EarlyBootConsoleScaleColor(Green, G_EarlyBootConsole.GreenMaskSize) << G_EarlyBootConsole.GreenPosition;
    Pixel |= EarlyBootConsoleScaleColor(Blue, G_EarlyBootConsole.BlueMaskSize) << G_EarlyBootConsole.BluePosition;
    return Pixel;
}

/************************************************************************/

/**
 * @brief Normalize control characters to printable glyphs.
 * @param Character Input character code.
 * @return Printable character code.
 */
static U32 EarlyBootConsoleNormalizeCharacter(U32 Character) {
    if (Character < 32 || Character > 126) {
        return '?';
    }

    return Character;
}

/************************************************************************/

/**
 * @brief Write one 32-bit pixel to the physical framebuffer.
 * @param X Pixel column.
 * @param Y Pixel row.
 * @param Pixel Packed pixel value.
 */
static void EarlyBootConsoleWritePixel(U32 X, U32 Y, U32 Pixel) {
    if (X >= G_EarlyBootConsole.Width || Y >= G_EarlyBootConsole.Height) {
        return;
    }

    PHYSICAL PixelPhysical = G_EarlyBootConsole.FramebufferPhysical +
                             (PHYSICAL)(Y * G_EarlyBootConsole.Pitch) +
                             (PHYSICAL)(X * 4);
    PHYSICAL PagePhysical = PixelPhysical & ~((PHYSICAL)(PAGE_SIZE - 1));
    UINT PageOffset = (UINT)(PixelPhysical - PagePhysical);

    if (G_EarlyBootConsole.CachedPageLinear == NULL || G_EarlyBootConsole.CachedPagePhysical != PagePhysical) {
        LINEAR Mapped = MapTemporaryPhysicalPage1(PagePhysical);
        if (Mapped == 0) {
            G_EarlyBootConsole.CachedPagePhysical = (PHYSICAL)-1;
            G_EarlyBootConsole.CachedPageLinear = NULL;
            return;
        }

        G_EarlyBootConsole.CachedPagePhysical = PagePhysical;
        G_EarlyBootConsole.CachedPageLinear = (U8*)Mapped;
    }

    *((U32*)(G_EarlyBootConsole.CachedPageLinear + PageOffset)) = Pixel;
}

/************************************************************************/

/**
 * @brief Draw one character cell using the default bitmap font.
 * @param Column Text column.
 * @param Row Text row.
 * @param Character ASCII codepoint.
 */
static void EarlyBootConsoleDrawCharacter(U32 Column, U32 Row, U32 Character) {
    const FONT_GLYPH_SET* Font = FontGetDefault();
    if (Font == NULL || Font->GlyphData == NULL || Font->BytesPerRow == 0) {
        return;
    }

    U32 GlyphCodepoint = EarlyBootConsoleNormalizeCharacter(Character);
    const U8* Glyph = FontGetGlyph(Font, GlyphCodepoint);
    if (Glyph == NULL) {
        return;
    }

    U32 BaseX = Column * EARLY_BOOT_CONSOLE_CHARACTER_WIDTH;
    U32 BaseY = Row * EARLY_BOOT_CONSOLE_CHARACTER_HEIGHT;
    U32 DrawHeight = Font->Height;
    U32 DrawWidth = Font->Width;
    if (DrawHeight > EARLY_BOOT_CONSOLE_CHARACTER_HEIGHT) {
        DrawHeight = EARLY_BOOT_CONSOLE_CHARACTER_HEIGHT;
    }
    if (DrawWidth > EARLY_BOOT_CONSOLE_CHARACTER_WIDTH) {
        DrawWidth = EARLY_BOOT_CONSOLE_CHARACTER_WIDTH;
    }

    for (U32 GlyphRow = 0; GlyphRow < EARLY_BOOT_CONSOLE_CHARACTER_HEIGHT; GlyphRow++) {
        for (U32 GlyphColumn = 0; GlyphColumn < EARLY_BOOT_CONSOLE_CHARACTER_WIDTH; GlyphColumn++) {
            U32 Pixel = G_EarlyBootConsole.BackgroundPixel;
            if (GlyphRow < DrawHeight && GlyphColumn < DrawWidth) {
                U32 ByteIndex = (GlyphRow * Font->BytesPerRow) + (GlyphColumn / 8);
                U8 Bits = Glyph[ByteIndex];
                U8 Mask = (U8)(0x80 >> (GlyphColumn & 0x07));
                if ((Bits & Mask) != 0) {
                    Pixel = G_EarlyBootConsole.ForegroundPixel;
                }
            }

            EarlyBootConsoleWritePixel(BaseX + GlyphColumn, BaseY + GlyphRow, Pixel);
        }
    }
}

/************************************************************************/

/**
 * @brief Move cursor to next line with wrap-to-top.
 */
static void EarlyBootConsoleNewLine(void) {
    G_EarlyBootConsole.CursorColumn = 0;
    G_EarlyBootConsole.CursorRow++;
    if (G_EarlyBootConsole.CursorRow >= G_EarlyBootConsole.MaxRows) {
        G_EarlyBootConsole.CursorRow = 0;
    }
}

/************************************************************************/

/**
 * @brief Initialize the early framebuffer console.
 *
 * @param FramebufferPhysical Framebuffer physical base.
 * @param Width Framebuffer width in pixels.
 * @param Height Framebuffer height in pixels.
 * @param Pitch Bytes per framebuffer row.
 * @param BitsPerPixel Framebuffer bit depth.
 * @param Type Multiboot framebuffer type.
 * @param RedPosition Red channel bit position.
 * @param RedMaskSize Red channel mask size.
 * @param GreenPosition Green channel bit position.
 * @param GreenMaskSize Green channel mask size.
 * @param BluePosition Blue channel bit position.
 * @param BlueMaskSize Blue channel mask size.
 */
void EarlyBootConsoleInitialize(
    PHYSICAL FramebufferPhysical,
    U32 Width,
    U32 Height,
    U32 Pitch,
    U32 BitsPerPixel,
    U32 Type,
    U32 RedPosition,
    U32 RedMaskSize,
    U32 GreenPosition,
    U32 GreenMaskSize,
    U32 BluePosition,
    U32 BlueMaskSize) {
    if (FramebufferPhysical == 0 ||
        Width == 0 ||
        Height == 0 ||
        Pitch == 0 ||
        BitsPerPixel != 32 ||
        Type != MULTIBOOT_FRAMEBUFFER_RGB) {
        G_EarlyBootConsole.Initialized = FALSE;
        return;
    }

    G_EarlyBootConsole.Initialized = TRUE;
    G_EarlyBootConsole.FramebufferPhysical = FramebufferPhysical;
    G_EarlyBootConsole.Width = Width;
    G_EarlyBootConsole.Height = Height;
    G_EarlyBootConsole.Pitch = Pitch;
    G_EarlyBootConsole.RedPosition = RedPosition;
    G_EarlyBootConsole.RedMaskSize = RedMaskSize;
    G_EarlyBootConsole.GreenPosition = GreenPosition;
    G_EarlyBootConsole.GreenMaskSize = GreenMaskSize;
    G_EarlyBootConsole.BluePosition = BluePosition;
    G_EarlyBootConsole.BlueMaskSize = BlueMaskSize;
    G_EarlyBootConsole.CursorColumn = 0;
    G_EarlyBootConsole.CursorRow = 0;
    G_EarlyBootConsole.MaxColumns = Width / EARLY_BOOT_CONSOLE_CHARACTER_WIDTH;
    G_EarlyBootConsole.MaxRows = Height / EARLY_BOOT_CONSOLE_CHARACTER_HEIGHT;
    G_EarlyBootConsole.ForegroundPixel = EarlyBootConsoleComposeColor(
        EARLY_BOOT_CONSOLE_FOREGROUND_R,
        EARLY_BOOT_CONSOLE_FOREGROUND_G,
        EARLY_BOOT_CONSOLE_FOREGROUND_B);
    G_EarlyBootConsole.BackgroundPixel = EarlyBootConsoleComposeColor(
        EARLY_BOOT_CONSOLE_BACKGROUND_R,
        EARLY_BOOT_CONSOLE_BACKGROUND_G,
        EARLY_BOOT_CONSOLE_BACKGROUND_B);
    G_EarlyBootConsole.CachedPagePhysical = (PHYSICAL)-1;
    G_EarlyBootConsole.CachedPageLinear = NULL;

    if (G_EarlyBootConsole.MaxColumns == 0 || G_EarlyBootConsole.MaxRows == 0) {
        G_EarlyBootConsole.Initialized = FALSE;
    }
}

/************************************************************************/

/**
 * @brief Return TRUE when the early boot console can render text.
 * @return TRUE if initialized.
 */
BOOL EarlyBootConsoleIsInitialized(void) {
    return G_EarlyBootConsole.Initialized;
}

/************************************************************************/

/**
 * @brief Print plain text to the early framebuffer console.
 * @param Text Null-terminated text.
 */
void EarlyBootConsoleWrite(LPCSTR Text) {
    if (G_EarlyBootConsole.Initialized == FALSE || Text == NULL) {
        return;
    }

    while (*Text != 0) {
        STR Character = *Text++;
        if (Character == '\n') {
            EarlyBootConsoleNewLine();
            continue;
        }

        EarlyBootConsoleDrawCharacter(G_EarlyBootConsole.CursorColumn, G_EarlyBootConsole.CursorRow, (U32)(U8)Character);
        G_EarlyBootConsole.CursorColumn++;
        if (G_EarlyBootConsole.CursorColumn >= G_EarlyBootConsole.MaxColumns) {
            EarlyBootConsoleNewLine();
        }
    }
}

/************************************************************************/

/**
 * @brief Print one text line and append a newline.
 * @param Text Null-terminated text.
 */
void EarlyBootConsoleWriteLine(LPCSTR Text) {
    EarlyBootConsoleWrite(Text);
    EarlyBootConsoleWrite(TEXT("\n"));
}

/************************************************************************/
