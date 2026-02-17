
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


    Console

\************************************************************************/

#include "Console.h"
#include "Font.h"
#include "GFX.h"
#include "Kernel.h"
#include "Memory.h"
#include "vbr-multiboot.h"
#include "drivers/VGA.h"
#include "process/Process.h"
#include "drivers/Keyboard.h"
#include "Log.h"
#include "Mutex.h"
#include "CoreString.h"
#include "System.h"
#include "DriverGetters.h"
#include "VKey.h"
#include "VarArg.h"
#include "Profile.h"
#include "SerialPort.h"

/***************************************************************************/

#define CONSOLE_VER_MAJOR 1
#define CONSOLE_VER_MINOR 0

static UINT ConsoleDriverCommands(UINT Function, UINT Parameter);
static void UpdateConsoleDesktopState(U32 Columns, U32 Rows);

DRIVER DATA_SECTION ConsoleDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_GRAPHICS,
    .VersionMajor = CONSOLE_VER_MAJOR,
    .VersionMinor = CONSOLE_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "Console",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = ConsoleDriverCommands};

/***************************************************************************/

/**
 * @brief Retrieves the console driver descriptor.
 * @return Pointer to the console driver.
 */
LPDRIVER ConsoleGetDriver(void) {
    return &ConsoleDriver;
}

/***************************************************************************/

#define CHARATTR (Console.ForeColor | (Console.BackColor << 0x04) | (Console.Blink << 0x07))

#define CGA_REGISTER 0x00
#define CGA_DATA 0x01
/***************************************************************************/

CONSOLE_STRUCT Console = {
    .ScreenWidth = 80,
    .ScreenHeight = 25,
    .Width = 80,
    .Height = 25,
    .CursorX = 0,
    .CursorY = 0,
    .BackColor = 0,
    .ForeColor = 0,
    .Blink = 0,
    .PagingEnabled = TRUE,
    .PagingActive = FALSE,
    .PagingRemaining = 0,
    .RegionCount = 1,
    .ActiveRegion = 0,
    .DebugRegion = 0,
    .Port = 0x03D4,
    .Memory = (LPVOID)0xB8000,
    .FramebufferPhysical = 0,
    .FramebufferLinear = NULL,
    .FramebufferPitch = 0,
    .FramebufferWidth = 0,
    .FramebufferHeight = 0,
    .FramebufferBitsPerPixel = 0,
    .FramebufferType = 0,
    .FramebufferRedPosition = 0,
    .FramebufferRedMaskSize = 0,
    .FramebufferGreenPosition = 0,
    .FramebufferGreenMaskSize = 0,
    .FramebufferBluePosition = 0,
    .FramebufferBlueMaskSize = 0,
    .FramebufferBytesPerPixel = 0,
    .FontWidth = 8,
    .FontHeight = 16,
    .UseFramebuffer = FALSE};

/***************************************************************************/

static BOOL DATA_SECTION ConsoleFramebufferMappingInProgress = FALSE;

/***************************************************************************/

typedef struct tag_CONSOLE_REGION_STATE {
    U32 X;
    U32 Y;
    U32 Width;
    U32 Height;
    U32* CursorX;
    U32* CursorY;
    U32* ForeColor;
    U32* BackColor;
    U32* Blink;
    U32* PagingEnabled;
    U32* PagingActive;
    U32* PagingRemaining;
} CONSOLE_REGION_STATE, *LPCONSOLE_REGION_STATE;

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

static BOOL ConsoleEnsureFramebufferMapped(void) {
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
static U32 ConsoleGetCellWidth(void) {
    return Console.FontWidth;
}

/***************************************************************************/

/**
 * @brief Return the framebuffer cell height in pixels.
 * @return Cell height in pixels.
 */
static U32 ConsoleGetCellHeight(void) {
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

static void ConsoleDrawGlyph(U32 X, U32 Y, STR Char) {
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


/**
 * @brief Resolve a console region into a mutable state descriptor.
 *
 * Provides pointers to the mutable fields for either the standard region
 * or a dedicated region, and copies the immutable layout data.
 *
 * @param Index Region index.
 * @param State Output state descriptor.
 * @return TRUE on success, FALSE on invalid index.
 */
static BOOL ConsoleResolveRegionState(U32 Index, LPCONSOLE_REGION_STATE State) {
    LPCONSOLE_REGION Region;

    if (Index >= Console.RegionCount) return FALSE;

    Region = &Console.Regions[Index];

    State->X = Region->X;
    State->Y = Region->Y;
    State->Width = Region->Width;
    State->Height = Region->Height;

    if (Index == 0) {
        State->CursorX = &Console.CursorX;
        State->CursorY = &Console.CursorY;
        State->ForeColor = &Console.ForeColor;
        State->BackColor = &Console.BackColor;
        State->Blink = &Console.Blink;
        State->PagingEnabled = &Console.PagingEnabled;
        State->PagingActive = &Console.PagingActive;
        State->PagingRemaining = &Console.PagingRemaining;
    } else {
        State->CursorX = &Region->CursorX;
        State->CursorY = &Region->CursorY;
        State->ForeColor = &Region->ForeColor;
        State->BackColor = &Region->BackColor;
        State->Blink = &Region->Blink;
        State->PagingEnabled = &Region->PagingEnabled;
        State->PagingActive = &Region->PagingActive;
        State->PagingRemaining = &Region->PagingRemaining;
    }

    return TRUE;
}

/***************************************************************************/

static void ConsoleClearRegionFramebuffer(U32 RegionIndex) {
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

static void ConsoleScrollRegionFramebuffer(U32 RegionIndex) {
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

/***************************************************************************/

/**
 * @brief Initialize a console region layout and runtime fields.
 *
 * @param Index Region index.
 * @param X Left coordinate in screen space.
 * @param Y Top coordinate in screen space.
 * @param Width Region width.
 * @param Height Region height.
 */
static void ConsoleInitializeRegion(U32 Index, U32 X, U32 Y, U32 Width, U32 Height) {
    LPCONSOLE_REGION Region;

    if (Index >= MAX_CONSOLE_REGIONS) return;

    Region = &Console.Regions[Index];
    Region->X = X;
    Region->Y = Y;
    Region->Width = Width;
    Region->Height = Height;
    Region->CursorX = 0;
    Region->CursorY = 0;
    Region->ForeColor = Console.ForeColor;
    Region->BackColor = Console.BackColor;
    Region->Blink = Console.Blink;
    Region->PagingEnabled = FALSE;
    Region->PagingActive = FALSE;
    Region->PagingRemaining = 0;
}

/***************************************************************************/

/**
 * @brief Build a grid of console regions.
 *
 * Regions are laid out in row-major order. The first columns and rows
 * receive any extra width or height if the screen does not divide evenly.
 *
 * @param Columns Number of columns.
 * @param Rows Number of rows.
 */
static void ConsoleConfigureRegions(U32 Columns, U32 Rows) {
    U32 EffectiveColumns;
    U32 EffectiveRows;
    U32 Index;
    U32 Row;
    U32 Column;
    U32 BaseWidth;
    U32 BaseHeight;
    U32 ExtraWidth;
    U32 ExtraHeight;
    U32 CursorX;
    U32 CursorY;

    EffectiveColumns = (Columns == 0) ? 1 : Columns;
    EffectiveRows = (Rows == 0) ? 1 : Rows;

    while ((EffectiveColumns * EffectiveRows) > MAX_CONSOLE_REGIONS) {
        if (EffectiveColumns >= EffectiveRows && EffectiveColumns > 1) {
            EffectiveColumns--;
        } else if (EffectiveRows > 1) {
            EffectiveRows--;
        } else {
            break;
        }
    }

    Console.RegionCount = EffectiveColumns * EffectiveRows;

    BaseWidth = Console.ScreenWidth / EffectiveColumns;
    ExtraWidth = Console.ScreenWidth % EffectiveColumns;
    BaseHeight = Console.ScreenHeight / EffectiveRows;
    ExtraHeight = Console.ScreenHeight % EffectiveRows;

    Index = 0;
    CursorY = 0;
    for (Row = 0; Row < EffectiveRows; Row++) {
        U32 RegionHeight = BaseHeight + ((Row < ExtraHeight) ? 1 : 0);
        CursorX = 0;
        for (Column = 0; Column < EffectiveColumns; Column++) {
            U32 RegionWidth = BaseWidth + ((Column < ExtraWidth) ? 1 : 0);
            ConsoleInitializeRegion(Index, CursorX, CursorY, RegionWidth, RegionHeight);
            CursorX += RegionWidth;
            Index++;
        }
        CursorY += RegionHeight;
    }
}

/***************************************************************************/

/**
 * @brief Apply the console region layout based on build configuration.
 */
static void ConsoleApplyLayout(void) {
#if DEBUG_SPLIT == 1
    ConsoleConfigureRegions(2, 1);
    Console.DebugRegion = (Console.RegionCount > 1) ? 1 : 0;
#else
    ConsoleConfigureRegions(1, 1);
    Console.DebugRegion = 0;
#endif

    Console.ActiveRegion = 0;
    Console.Width = Console.Regions[0].Width;
    Console.Height = Console.Regions[0].Height;
}

/***************************************************************************/

/**
 * @brief Clamp the standard console cursor to its region bounds.
 */
static void ConsoleClampCursorToRegionZero(void) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(0, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) {
        Console.CursorX = 0;
        Console.CursorY = 0;
        return;
    }

    if (Console.CursorY >= State.Height) {
        Console.CursorY = State.Height - 1;
    }

    if (Console.CursorX >= State.Width) {
        Console.CursorX = 0;
        if ((Console.CursorY + 1) < State.Height) {
            Console.CursorY++;
        }
    }
}

/***************************************************************************/

/**
 * @brief Returns TRUE when the debug split is enabled.
 * @return TRUE if the debug region is active, FALSE otherwise.
 */
BOOL ConsoleIsDebugSplitEnabled(void) {
#if DEBUG_SPLIT == 1
    return (Console.RegionCount > 1 && Console.DebugRegion < Console.RegionCount) ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

/***************************************************************************/

/**
 * @brief Show the console paging prompt for a specific region.
 * @param RegionIndex Region index.
 */
static void ConsolePagerWaitLockedRegion(U32 RegionIndex) {
    CONSOLE_REGION_STATE State;
    KEYCODE KeyCode;
    U32 Row;
    U32 Column;
    U32 Offset;
    U16 Attribute;
    STR Prompt[] = "-- Press a key --";
    U32 PromptLen;
    U32 Start;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if ((*State.PagingEnabled) == FALSE || (*State.PagingActive) == FALSE) return;
    if (State.Width == 0 || State.Height < 2) return;

    Row = State.Height - 1;
    Attribute = (U16)(((*State.ForeColor) | ((*State.BackColor) << 0x04) | ((*State.Blink) << 0x07)) << 0x08);

    if (Console.UseFramebuffer != FALSE) {
        if (ConsoleEnsureFramebufferMapped() == FALSE) {
            return;
        }

        for (Column = 0; Column < State.Width; Column++) {
            U32 PixelX = (State.X + Column) * ConsoleGetCellWidth();
            U32 PixelY = (State.Y + Row) * ConsoleGetCellHeight();
            ConsoleDrawGlyph(PixelX, PixelY, STR_SPACE);
        }

        PromptLen = StringLength(Prompt);
        if (PromptLen > State.Width) PromptLen = State.Width;
        Start = (State.Width > PromptLen) ? (State.Width - PromptLen) / 2 : 0;
        for (Column = 0; Column < PromptLen; Column++) {
            U32 PixelX = (State.X + Start + Column) * ConsoleGetCellWidth();
            U32 PixelY = (State.Y + Row) * ConsoleGetCellHeight();
            ConsoleDrawGlyph(PixelX, PixelY, Prompt[Column]);
        }

        SetConsoleCursorPosition(0, Row);
        goto WaitForKey;
    }

    for (Column = 0; Column < State.Width; Column++) {
        Offset = ((State.Y + Row) * Console.ScreenWidth) + (State.X + Column);
        Console.Memory[Offset] = (U16)STR_SPACE | Attribute;
    }

    PromptLen = StringLength(Prompt);
    if (PromptLen > State.Width) PromptLen = State.Width;
    Start = (State.Width > PromptLen) ? (State.Width - PromptLen) / 2 : 0;
    Offset = ((State.Y + Row) * Console.ScreenWidth) + (State.X + Start);
    for (Column = 0; Column < PromptLen; Column++) {
        Console.Memory[Offset + Column] = (U16)Prompt[Column] | Attribute;
    }

    SetConsoleCursorPosition(0, Row);

WaitForKey:
    while (TRUE) {
        if (PeekChar()) {
            GetKeyCode(&KeyCode);
            if (KeyCode.VirtualKey == VK_SPACE || KeyCode.VirtualKey == VK_ENTER) {
                (*State.PagingRemaining) = State.Height - 1;
                break;
            }
            if (KeyCode.VirtualKey == VK_ESCAPE) {
                (*State.PagingRemaining) = State.Height - 1;
                break;
            }
        }

        Sleep(10);
    }

    for (Column = 0; Column < State.Width; Column++) {
        Offset = ((State.Y + Row) * Console.ScreenWidth) + (State.X + Column);
        Console.Memory[Offset] = (U16)STR_SPACE | Attribute;
    }
}

/**
 * @brief Show the console paging prompt and wait for user input.
 */
static void ConsolePagerWaitLocked(void) {
    ConsolePagerWaitLockedRegion(0);
}

/***************************************************************************/

/**
 * @brief Sync the desktop screen rectangle to the current console size.
 * @param Columns Number of console columns.
 * @param Rows Number of console rows.
 */
static void UpdateConsoleDesktopState(U32 Columns, U32 Rows) {
    RECT Rect;

    if (Columns == 0 || Rows == 0) return;

    Rect.X1 = 0;
    Rect.Y1 = 0;
    Rect.X2 = (I32)Columns - 1;
    Rect.Y2 = (I32)Rows - 1;

    SAFE_USE_VALID_ID(&MainDesktop, KOID_DESKTOP) {
        LockMutex(&(MainDesktop.Mutex), INFINITY);
        MainDesktop.Graphics = &ConsoleDriver;
        MainDesktop.Mode = DESKTOP_MODE_CONSOLE;

        SAFE_USE_VALID_ID(MainDesktop.Window, KOID_WINDOW) {
            LockMutex(&(MainDesktop.Window->Mutex), INFINITY);
            MainDesktop.Window->Rect = Rect;
            MainDesktop.Window->ScreenRect = Rect;
            MainDesktop.Window->InvalidRect = Rect;
            UnlockMutex(&(MainDesktop.Window->Mutex));
        }

        UnlockMutex(&(MainDesktop.Mutex));
    }
}

/***************************************************************************/

/**
 * @brief Write a character at the current cursor position inside a region.
 *
 * @param RegionIndex Region index.
 * @param Char Character to write.
 */
static void ConsoleSetCharacterRegion(U32 RegionIndex, STR Char) {
    CONSOLE_REGION_STATE State;
    U32 Offset;
    U16 Attribute;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if ((*State.CursorX) >= State.Width || (*State.CursorY) >= State.Height) return;

    if (Console.UseFramebuffer != FALSE) {
        if (ConsoleEnsureFramebufferMapped() == FALSE) {
            return;
        }

        U32 PixelX = (State.X + (*State.CursorX)) * ConsoleGetCellWidth();
        U32 PixelY = (State.Y + (*State.CursorY)) * ConsoleGetCellHeight();
        ConsoleDrawGlyph(PixelX, PixelY, Char);
        return;
    }

    Offset = ((State.Y + (*State.CursorY)) * Console.ScreenWidth) + (State.X + (*State.CursorX));
    Attribute = (U16)(((*State.ForeColor) | ((*State.BackColor) << 0x04) | ((*State.Blink) << 0x07)) << 0x08);
    Console.Memory[Offset] = (U16)Char | Attribute;
}

/***************************************************************************/

/**
 * @brief Scroll a region up by one line.
 * @param RegionIndex Region index.
 */
static void ConsoleScrollRegion(U32 RegionIndex) {
    CONSOLE_REGION_STATE State;
    U32 Row;
    U32 Column;
    U32 Src;
    U32 Dst;
    U16 Attribute;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) return;

    if ((*State.PagingRemaining) == 0) {
        ConsolePagerWaitLockedRegion(RegionIndex);
    }
    if ((*State.PagingRemaining) > 0) {
        (*State.PagingRemaining)--;
    }

    if (Console.UseFramebuffer != FALSE) {
        ConsoleScrollRegionFramebuffer(RegionIndex);
        return;
    }

    for (Row = 1; Row < State.Height; Row++) {
        for (Column = 0; Column < State.Width; Column++) {
            Src = ((State.Y + Row) * Console.ScreenWidth) + (State.X + Column);
            Dst = ((State.Y + (Row - 1)) * Console.ScreenWidth) + (State.X + Column);
            Console.Memory[Dst] = Console.Memory[Src];
        }
    }

    Attribute = (U16)(((*State.ForeColor) | ((*State.BackColor) << 0x04) | ((*State.Blink) << 0x07)) << 0x08);
    for (Column = 0; Column < State.Width; Column++) {
        Dst = ((State.Y + (State.Height - 1)) * Console.ScreenWidth) + (State.X + Column);
        Console.Memory[Dst] = (U16)STR_SPACE | Attribute;
    }
}

/***************************************************************************/

/**
 * @brief Clear a region and reset its cursor.
 * @param RegionIndex Region index.
 */
static void ConsoleClearRegion(U32 RegionIndex) {
    CONSOLE_REGION_STATE State;
    U32 Row;
    U32 Column;
    U32 Offset;
    U16 Attribute;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) return;

    if (Console.UseFramebuffer != FALSE) {
        ConsoleClearRegionFramebuffer(RegionIndex);
        (*State.CursorX) = 0;
        (*State.CursorY) = 0;
        return;
    }

    Attribute = (U16)(((*State.ForeColor) | ((*State.BackColor) << 0x04) | ((*State.Blink) << 0x07)) << 0x08);
    for (Row = 0; Row < State.Height; Row++) {
        for (Column = 0; Column < State.Width; Column++) {
            Offset = ((State.Y + Row) * Console.ScreenWidth) + (State.X + Column);
            Console.Memory[Offset] = (U16)STR_SPACE | Attribute;
        }
    }

    (*State.CursorX) = 0;
    (*State.CursorY) = 0;
}

/***************************************************************************/

/**
 * @brief Print a character into a region and update its cursor.
 * @param RegionIndex Region index.
 * @param Char Character to print.
 */
static void ConsolePrintCharRegion(U32 RegionIndex, STR Char) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) return;

    if (Char == STR_NEWLINE) {
        (*State.CursorX) = 0;
        (*State.CursorY)++;
        if ((*State.CursorY) >= State.Height) {
            ConsoleScrollRegion(RegionIndex);
            (*State.CursorY) = State.Height - 1;
        }
        if (RegionIndex == 0) {
            SetConsoleCursorPosition(*State.CursorX, *State.CursorY);
        }
        return;
    }

    if (Char == STR_RETURN) {
        return;
    }

    if (Char == STR_TAB) {
        (*State.CursorX) += 4;
        if ((*State.CursorX) >= State.Width) {
            (*State.CursorX) = 0;
            (*State.CursorY)++;
            if ((*State.CursorY) >= State.Height) {
                ConsoleScrollRegion(RegionIndex);
                (*State.CursorY) = State.Height - 1;
            }
        }
        if (RegionIndex == 0) {
            SetConsoleCursorPosition(*State.CursorX, *State.CursorY);
        }
        return;
    }

    ConsoleSetCharacterRegion(RegionIndex, Char);
    (*State.CursorX)++;
    if ((*State.CursorX) >= State.Width) {
        (*State.CursorX) = 0;
        (*State.CursorY)++;
        if ((*State.CursorY) >= State.Height) {
            ConsoleScrollRegion(RegionIndex);
            (*State.CursorY) = State.Height - 1;
        }
    }

    if (RegionIndex == 0) {
        SetConsoleCursorPosition(*State.CursorX, *State.CursorY);
    }
}

/***************************************************************************/

/**
 * @brief Move the hardware and logical console cursor.
 * @param CursorX X coordinate of the cursor.
 * @param CursorY Y coordinate of the cursor.
 */
void SetConsoleCursorPosition(U32 CursorX, U32 CursorY) {
    PROFILE_SCOPE Scope;
    ProfileStart(&Scope, TEXT("SetConsoleCursorPosition"));

    CONSOLE_REGION_STATE State;
    U32 Position;

    if (ConsoleResolveRegionState(0, &State) == FALSE) {
        ProfileStop(&Scope);
        return;
    }

    Position = ((State.Y + CursorY) * Console.ScreenWidth) + (State.X + CursorX);

    Console.CursorX = CursorX;
    Console.CursorY = CursorY;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    if (Console.UseFramebuffer != FALSE) {
        UnlockMutex(MUTEX_CONSOLE);
        ProfileStop(&Scope);
        return;
    }

    OutPortByte(Console.Port + CGA_REGISTER, 14);
    OutPortByte(Console.Port + CGA_DATA, (Position >> 8) & 0xFF);
    OutPortByte(Console.Port + CGA_REGISTER, 15);
    OutPortByte(Console.Port + CGA_DATA, (Position >> 0) & 0xFF);

    UnlockMutex(MUTEX_CONSOLE);

    ProfileStop(&Scope);
}

/***************************************************************************/

/**
 * @brief Get the current console cursor position from hardware.
 * @param CursorX Pointer to receive X coordinate of the cursor.
 * @param CursorY Pointer to receive Y coordinate of the cursor.
 */
void GetConsoleCursorPosition(U32* CursorX, U32* CursorY) {
    CONSOLE_REGION_STATE State;
    U32 Position;
    U8 PositionHigh, PositionLow;
    U32 AbsoluteX;
    U32 AbsoluteY;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    if (Console.UseFramebuffer != FALSE) {
        SAFE_USE_2(CursorX, CursorY) {
            *CursorX = Console.CursorX;
            *CursorY = Console.CursorY;
        }
        UnlockMutex(MUTEX_CONSOLE);
        return;
    }

    OutPortByte(Console.Port + CGA_REGISTER, 14);
    PositionHigh = InPortByte(Console.Port + CGA_DATA);
    OutPortByte(Console.Port + CGA_REGISTER, 15);
    PositionLow = InPortByte(Console.Port + CGA_DATA);

    Position = ((U32)PositionHigh << 8) | (U32)PositionLow;

    if (ConsoleResolveRegionState(0, &State) == FALSE) {
        SAFE_USE_2(CursorX, CursorY) {
            *CursorX = 0;
            *CursorY = 0;
        }
        UnlockMutex(MUTEX_CONSOLE);
        return;
    }

    AbsoluteY = Position / Console.ScreenWidth;
    AbsoluteX = Position % Console.ScreenWidth;

    SAFE_USE_2(CursorX, CursorY) {
        if (AbsoluteX < State.X) {
            *CursorX = 0;
        } else {
            *CursorX = AbsoluteX - State.X;
        }

        if (AbsoluteY < State.Y) {
            *CursorY = 0;
        } else {
            *CursorY = AbsoluteY - State.Y;
        }
    }

    UnlockMutex(MUTEX_CONSOLE);
}

/***************************************************************************/

/**
 * @brief Place a character at the current cursor position.
 * @param Char Character to display.
 */
void SetConsoleCharacter(STR Char) {
    PROFILE_SCOPE Scope;
    ProfileStart(&Scope, TEXT("SetConsoleCharacter"));

    U32 Offset = 0;
    CONSOLE_REGION_STATE State;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    if (ConsoleResolveRegionState(0, &State) == TRUE) {
        if (Console.UseFramebuffer != FALSE) {
            if (ConsoleEnsureFramebufferMapped() == TRUE) {
                U32 PixelX = (State.X + Console.CursorX) * ConsoleGetCellWidth();
                U32 PixelY = (State.Y + Console.CursorY) * ConsoleGetCellHeight();
                ConsoleDrawGlyph(PixelX, PixelY, Char);
            }
        } else {
            Offset = ((State.Y + Console.CursorY) * Console.ScreenWidth) + (State.X + Console.CursorX);
            Console.Memory[Offset] = Char | (CHARATTR << 0x08);
        }
    }

    UnlockMutex(MUTEX_CONSOLE);

    ProfileStop(&Scope);
}

/***************************************************************************/

/**
 * @brief Scroll the console up by one line.
 */
void ScrollConsole(void) {
    PROFILE_SCOPE Scope;
    ProfileStart(&Scope, TEXT("ScrollConsole"));

    LockMutex(MUTEX_CONSOLE, INFINITY);

    while (Keyboard.ScrollLock) {
    }

    ConsoleScrollRegion(0);

    UnlockMutex(MUTEX_CONSOLE);

    ProfileStop(&Scope);
}

/***************************************************************************/

/**
 * @brief Clear the entire console screen.
 */
void ClearConsole(void) {
    U32 Index;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    for (Index = 0; Index < Console.RegionCount; Index++) {
        ConsoleClearRegion(Index);
    }

    SetConsoleCursorPosition(Console.CursorX, Console.CursorY);

    UnlockMutex(MUTEX_CONSOLE);
}

/***************************************************************************/

/**
 * @brief Print a single character to the console handling control codes.
 * @param Char Character to print.
 */
void ConsolePrintChar(STR Char) {
    PROFILE_SCOPE Scope;
    ProfileStart(&Scope, TEXT("ConsolePrintChar"));

    LockMutex(MUTEX_CONSOLE, INFINITY);

    if (Char == STR_NEWLINE) {
        Console.CursorX = 0;
        Console.CursorY++;
        if (Console.CursorY >= Console.Height) {
            ScrollConsole();
            Console.CursorY = Console.Height - 1;
        }
    } else if (Char == STR_RETURN) {
    } else if (Char == STR_TAB) {
        Console.CursorX += 4;
        if (Console.CursorX >= Console.Width) {
            Console.CursorX = 0;
            Console.CursorY++;
            if (Console.CursorY >= Console.Height) {
                ScrollConsole();
                Console.CursorY = Console.Height - 1;
            }
        }
    } else {
        SetConsoleCharacter(Char);
        Console.CursorX++;
        if (Console.CursorX >= Console.Width) {
            Console.CursorX = 0;
            Console.CursorY++;
            if (Console.CursorY >= Console.Height) {
                ScrollConsole();
                Console.CursorY = Console.Height - 1;
            }
        }
    }

    SetConsoleCursorPosition(Console.CursorX, Console.CursorY);

    UnlockMutex(MUTEX_CONSOLE);

    ProfileStop(&Scope);
}

/***************************************************************************/

/**
 * @brief Print a single character to the debug console region.
 * @param Char Character to print.
 */
void ConsolePrintDebugChar(STR Char) {
    if (ConsoleIsDebugSplitEnabled() == FALSE) return;

    LockMutex(MUTEX_CONSOLE, INFINITY);
    ConsolePrintCharRegion(Console.DebugRegion, Char);
    UnlockMutex(MUTEX_CONSOLE);
}

/***************************************************************************/

/**
 * @brief Handle backspace at the current cursor position.
 */
void ConsoleBackSpace(void) {
    CONSOLE_REGION_STATE State;
    U32 Offset;

    if (ConsoleResolveRegionState(0, &State) == FALSE) return;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    if (Console.CursorX == 0 && Console.CursorY == 0) goto Out;

    if (Console.CursorX == 0) {
        Console.CursorX = State.Width - 1;
        Console.CursorY--;
    } else {
        Console.CursorX--;
    }

    if (Console.UseFramebuffer != FALSE) {
        if (ConsoleEnsureFramebufferMapped() == TRUE) {
            U32 PixelX = (State.X + Console.CursorX) * ConsoleGetCellWidth();
            U32 PixelY = (State.Y + Console.CursorY) * ConsoleGetCellHeight();
            ConsoleDrawGlyph(PixelX, PixelY, STR_SPACE);
        }
    } else {
        Offset = ((State.Y + Console.CursorY) * Console.ScreenWidth) + (State.X + Console.CursorX);
        Console.Memory[Offset] = (U16)STR_SPACE | (CHARATTR << 0x08);
    }

Out:

    SetConsoleCursorPosition(Console.CursorX, Console.CursorY);

    UnlockMutex(MUTEX_CONSOLE);
}

/***************************************************************************/

/**
 * @brief Print a null-terminated string to the console.
 * @param Text String to print.
 */
static void ConsolePrintString(LPCSTR Text) {
    PROFILE_SCOPE Scope;
    ProfileStart(&Scope, TEXT("ConsolePrintString"));

    U32 Index = 0;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    SAFE_USE(Text) {
        for (Index = 0; Index < MAX_STRING_BUFFER; Index++) {
            if (Text[Index] == STR_NULL) break;
            ConsolePrintChar(Text[Index]);
        }
    }

    UnlockMutex(MUTEX_CONSOLE);

    ProfileStop(&Scope);
}

/***************************************************************************/

/**
 * @brief Print a formatted string to the console.
 * @param Format Format string.
 * @return TRUE on success.
 */
void ConsolePrint(LPCSTR Format, ...) {
    STR Text[MAX_STRING_BUFFER];
    VarArgList Args;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    VarArgStart(Args, Format);
    StringPrintFormatArgs(Text, Format, Args);
    VarArgEnd(Args);

    ConsolePrintString(Text);

    UnlockMutex(MUTEX_CONSOLE);
}

/***************************************************************************/

void ConsolePrintLine(U32 Row, U32 Column, LPCSTR Text, U32 Length) {
    CONSOLE_REGION_STATE State;
    U32 Index;
    U32 Offset;
    U16 Attribute;

    if (Text == NULL) return;

    if (ConsoleResolveRegionState(0, &State) == FALSE) return;

    if (Row >= State.Height || Column >= State.Width) return;

    if (Console.UseFramebuffer != FALSE) {
        if (ConsoleEnsureFramebufferMapped() == FALSE) {
            return;
        }

        for (Index = 0; Index < Length && (Column + Index) < State.Width; Index++) {
            U32 PixelX = (State.X + Column + Index) * ConsoleGetCellWidth();
            U32 PixelY = (State.Y + Row) * ConsoleGetCellHeight();
            ConsoleDrawGlyph(PixelX, PixelY, Text[Index]);
        }
        return;
    }

    Offset = ((State.Y + Row) * Console.ScreenWidth) + (State.X + Column);
    Attribute = (U16)(Console.ForeColor | (Console.BackColor << 0x04) | (Console.Blink << 0x07));
    Attribute = (U16)(Attribute << 0x08);

    for (Index = 0; Index < Length && (Column + Index) < State.Width; Index++) {
        STR Character = Text[Index];
        Console.Memory[Offset + Index] = (U16)Character | Attribute;
    }
}

/***************************************************************************/

int SetConsoleBackColor(U32 Color) {
    Console.BackColor = Color;
    return 1;
}

/***************************************************************************/

int SetConsoleForeColor(U32 Color) {
    Console.ForeColor = Color;
    return 1;
}

/***************************************************************************/

BOOL ConsoleGetString(LPSTR Buffer, U32 Size) {
    KEYCODE KeyCode;
    U32 Index = 0;
    U32 Done = 0;

    DEBUG(TEXT("[ConsoleGetString] Enter"));

    Buffer[0] = STR_NULL;

    while (Done == 0) {
        if (PeekChar()) {
            GetKeyCode(&KeyCode);

            if (KeyCode.VirtualKey == VK_ESCAPE) {
                while (Index) {
                    Index--;
                    ConsoleBackSpace();
                }
            } else if (KeyCode.VirtualKey == VK_BACKSPACE) {
                if (Index) {
                    Index--;
                    ConsoleBackSpace();
                }
            } else if (KeyCode.VirtualKey == VK_ENTER) {
                ConsolePrintChar(STR_NEWLINE);
                Done = 1;
            } else {
                if (KeyCode.ASCIICode >= STR_SPACE) {
                    if (Index < Size - 1) {
                        ConsolePrintChar(KeyCode.ASCIICode);
                        Buffer[Index++] = KeyCode.ASCIICode;
                    }
                }
            }
        }

        Sleep(10);
    }

    Buffer[Index] = STR_NULL;

    DEBUG(TEXT("[ConsoleGetString] Exit"));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Print a formatted string to the console.
 * @param Format Format string.
 * @return TRUE on success.
 */
void ConsolePanic(LPCSTR Format, ...) {
    STR Text[0x1000];
    const STR Prefix[] = "[ConsolePanic] ";
    const STR HaltText[] = "[ConsolePanic] >>> Halting system <<<\r\n";
    VarArgList Args;
    UINT Index = 0;

    DisableInterrupts();

    VarArgStart(Args, Format);
    StringPrintFormatArgs(Text, Format, Args);
    VarArgEnd(Args);

    SerialReset(0);
    for (Index = 0; Prefix[Index] != STR_NULL; ++Index) {
        SerialOut(0, Prefix[Index]);
    }
    for (Index = 0; Text[Index] != STR_NULL; ++Index) {
        SerialOut(0, Text[Index]);
    }
    SerialOut(0, '\r');
    SerialOut(0, '\n');
    for (Index = 0; HaltText[Index] != STR_NULL; ++Index) {
        SerialOut(0, HaltText[Index]);
    }

    ConsolePrintString(Text);
    ConsolePrintString(TEXT("\n>>> Halting system <<<"));

    DO_THE_SLEEPING_BEAUTY;
}

/***************************************************************************/

void InitializeConsole(void) {
    const FONT_GLYPH_SET* Font = FontGetDefault();

    if (Font != NULL) {
        Console.FontWidth = Font->Width;
        Console.FontHeight = Font->Height;
    }

    if (Console.UseFramebuffer != FALSE) {
        U32 CellWidth = ConsoleGetCellWidth();
        U32 CellHeight = ConsoleGetCellHeight();

        Console.FramebufferBytesPerPixel = Console.FramebufferBitsPerPixel / 8u;
        if (Console.FramebufferBytesPerPixel == 0u) {
            Console.FramebufferBytesPerPixel = 4u;
        }

        Console.ScreenWidth = Console.FramebufferWidth / CellWidth;
        Console.ScreenHeight = Console.FramebufferHeight / CellHeight;
        if (Console.ScreenWidth == 0u || Console.ScreenHeight == 0u) {
            Console.UseFramebuffer = FALSE;
            Console.ScreenWidth = 80;
            Console.ScreenHeight = 25;
        }
    } else {
        if (Console.FramebufferType != MULTIBOOT_FRAMEBUFFER_TEXT ||
            Console.ScreenWidth == 0u || Console.ScreenHeight == 0u) {
            Console.ScreenWidth = 80;
            Console.ScreenHeight = 25;
        }
    }

    Console.BackColor = 0;
    Console.ForeColor = 7;
    Console.PagingEnabled = TRUE;
    Console.PagingActive = FALSE;
    Console.PagingRemaining = 0;

    ConsoleApplyLayout();

    GetConsoleCursorPosition(&Console.CursorX, &Console.CursorY);
    ConsoleClampCursorToRegionZero();
    SetConsoleCursorPosition(Console.CursorX, Console.CursorY);
}

/***************************************************************************/

/**
 * @brief Configure framebuffer metadata for console output.
 *
 * This stores framebuffer parameters for later use during console initialization.
 *
 * @param FramebufferPhysical Physical base address of the framebuffer.
 * @param Width Framebuffer width in pixels or text columns.
 * @param Height Framebuffer height in pixels or text rows.
 * @param Pitch Bytes per scan line.
 * @param BitsPerPixel Bits per pixel.
 * @param Type Multiboot framebuffer type.
 * @param RedPosition Red channel bit position.
 * @param RedMaskSize Red channel bit size.
 * @param GreenPosition Green channel bit position.
 * @param GreenMaskSize Green channel bit size.
 * @param BluePosition Blue channel bit position.
 * @param BlueMaskSize Blue channel bit size.
 */
void ConsoleSetFramebufferInfo(
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
    Console.FramebufferPhysical = FramebufferPhysical;
    Console.FramebufferLinear = NULL;
    Console.FramebufferBytesPerPixel = 0;
    Console.FramebufferWidth = Width;
    Console.FramebufferHeight = Height;
    Console.FramebufferPitch = Pitch;
    Console.FramebufferBitsPerPixel = BitsPerPixel;
    Console.FramebufferType = Type;
    Console.FramebufferRedPosition = RedPosition;
    Console.FramebufferRedMaskSize = RedMaskSize;
    Console.FramebufferGreenPosition = GreenPosition;
    Console.FramebufferGreenMaskSize = GreenMaskSize;
    Console.FramebufferBluePosition = BluePosition;
    Console.FramebufferBlueMaskSize = BlueMaskSize;

    if (Type == MULTIBOOT_FRAMEBUFFER_RGB && FramebufferPhysical != 0 && Width != 0u && Height != 0u) {
        Console.UseFramebuffer = TRUE;
        Console.Memory = NULL;
        Console.Port = 0;
    } else if (Type == MULTIBOOT_FRAMEBUFFER_TEXT && FramebufferPhysical != 0) {
        Console.UseFramebuffer = FALSE;
        Console.Memory = (U16*)(UINT)FramebufferPhysical;
        Console.ScreenWidth = (Width != 0u) ? Width : 80u;
        Console.ScreenHeight = (Height != 0u) ? Height : 25u;
    } else {
        Console.UseFramebuffer = FALSE;
    }
}

/***************************************************************************/

/**
 * @brief Enable or disable console paging.
 * @param Enabled TRUE to enable paging, FALSE to disable.
 */
void ConsoleSetPagingEnabled(BOOL Enabled) {
    Console.PagingEnabled = Enabled ? TRUE : FALSE;
    if (Console.PagingEnabled == FALSE) {
        Console.PagingRemaining = 0;
    }
}

/***************************************************************************/

/**
 * @brief Query whether console paging is enabled.
 * @return TRUE if paging is enabled, FALSE otherwise.
 */
BOOL ConsoleGetPagingEnabled(void) {
    return Console.PagingEnabled ? TRUE : FALSE;
}

/***************************************************************************/

/**
 * @brief Activate or deactivate console paging.
 * @param Active TRUE to allow paging prompts, FALSE to disable them.
 */
void ConsoleSetPagingActive(BOOL Active) {
    Console.PagingActive = Active ? TRUE : FALSE;
    if (Console.PagingActive == FALSE) {
        Console.PagingRemaining = 0;
    } else {
        ConsoleResetPaging();
    }
}

/***************************************************************************/

/**
 * @brief Reset console paging state for the next command.
 */
void ConsoleResetPaging(void) {
    if (Console.PagingEnabled == FALSE || Console.PagingActive == FALSE) {
        Console.PagingRemaining = 0;
        return;
    }

    if (Console.Height > 0) {
        Console.PagingRemaining = Console.Height - 1;
    } else {
        Console.PagingRemaining = 0;
    }
}

/***************************************************************************/

/**
 * @brief Set console text mode using a graphics mode descriptor.
 * @param Info Mode description with Width/Height in characters.
 * @return DF_RETURN_SUCCESS on success, error code otherwise.
 */
UINT ConsoleSetMode(LPGRAPHICSMODEINFO Info) { return ConsoleDriverCommands(DF_GFX_SETMODE, (UINT)Info); }

/***************************************************************************/

/**
 * @brief Return the number of available VGA console modes.
 * @return Number of console modes.
 */
UINT ConsoleGetModeCount(void) { return VGAGetModeCount(); }

/***************************************************************************/

/**
 * @brief Query a console mode by index.
 * @param Info Mode request (Index) and output (Columns/Rows/CharHeight).
 * @return DF_RETURN_SUCCESS on success, error code otherwise.
 */
UINT ConsoleGetModeInfo(LPCONSOLEMODEINFO Info) {
    VGAMODEINFO VgaInfo;

    if (Info == NULL) return DF_RETURN_GENERIC;

    if (VGAGetModeInfo(Info->Index, &VgaInfo) == FALSE) {
        return DF_RETURN_GENERIC;
    }

    Info->Columns = VgaInfo.Columns;
    Info->Rows = VgaInfo.Rows;
    Info->CharHeight = VgaInfo.CharHeight;

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

/**
 * @brief Driver command handler for the console subsystem.
 *
 * DF_LOAD initializes the console once; DF_UNLOAD clears the ready flag
 * as there is no shutdown routine.
 */
static UINT ConsoleDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((ConsoleDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            InitializeConsole();
            ConsoleDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((ConsoleDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            ConsoleDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(CONSOLE_VER_MAJOR, CONSOLE_VER_MINOR);

        case DF_GFX_GETMODEINFO: {
            LPGRAPHICSMODEINFO Info = (LPGRAPHICSMODEINFO)Parameter;
            SAFE_USE(Info) {
                Info->Width = Console.Width;
                Info->Height = Console.Height;
                Info->BitsPerPixel = 0;
                return DF_RETURN_SUCCESS;
            }
            return DF_RETURN_GENERIC;
        }

        case DF_GFX_SETMODE: {
            LPGRAPHICSMODEINFO Info = (LPGRAPHICSMODEINFO)Parameter;
            SAFE_USE(Info) {
                U32 ModeIndex;

                if (VGAFindTextMode(Info->Width, Info->Height, &ModeIndex) == FALSE) {
                    return DF_GFX_ERROR_MODEUNAVAIL;
                }

                if (VGASetMode(ModeIndex) == FALSE) {
                    return DF_RETURN_GENERIC;
                }

                Console.ScreenWidth = Info->Width;
                Console.ScreenHeight = Info->Height;
                ConsoleApplyLayout();
                Console.CursorX = 0;
                Console.CursorY = 0;
                ClearConsole();
                UpdateConsoleDesktopState(Console.Width, Console.Height);

                return DF_RETURN_SUCCESS;
            }
            return DF_RETURN_GENERIC;
        }

        case DF_GFX_CREATECONTEXT:
        case DF_GFX_CREATEBRUSH:
        case DF_GFX_CREATEPEN:
        case DF_GFX_SETPIXEL:
        case DF_GFX_GETPIXEL:
        case DF_GFX_LINE:
        case DF_GFX_RECTANGLE:
        case DF_GFX_ELLIPSE:
            return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
