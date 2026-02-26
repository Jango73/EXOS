
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

#include "Console-Internal.h"
#include "Console-VGATextFallback.h"
#include "DisplaySession.h"
#include "Font.h"
#include "GFX.h"
#include "Kernel.h"
#include "Memory.h"
#include "vbr-multiboot.h"
#include "drivers/graphics/VGA.h"
#include "process/Process.h"
#include "drivers/input/Keyboard.h"
#include "Log.h"
#include "Mutex.h"
#include "CoreString.h"
#include "System.h"
#include "DriverGetters.h"
#include "input/VKey.h"
#include "VarArg.h"
#include "Profile.h"
#include "SerialPort.h"

/***************************************************************************/

#define CONSOLE_VER_MAJOR 1
#define CONSOLE_VER_MINOR 0

static UINT ConsoleDriverCommands(UINT Function, UINT Parameter);

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

    LockMutex(MUTEX_CONSOLE, INFINITY);

    if (Console.UseFramebuffer != FALSE) {
        if (Console.CursorX == CursorX && Console.CursorY == CursorY) {
            UnlockMutex(MUTEX_CONSOLE);
            ProfileStop(&Scope);
            return;
        }

        ConsoleHideFramebufferCursor();
        Console.CursorX = CursorX;
        Console.CursorY = CursorY;
        ConsoleShowFramebufferCursor();
        UnlockMutex(MUTEX_CONSOLE);
        ProfileStop(&Scope);
        return;
    }

    Position = ((State.Y + CursorY) * Console.ScreenWidth) + (State.X + CursorX);

    Console.CursorX = CursorX;
    Console.CursorY = CursorY;

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

    ConsoleHideFramebufferCursor();
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

    ConsoleHideFramebufferCursor();

    if (ConsoleIsDebugSplitEnabled() != FALSE) {
        ConsoleClearRegion(0);
    } else {
        for (Index = 0; Index < Console.RegionCount; Index++) {
            ConsoleClearRegion(Index);
        }
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
            ConsoleHideFramebufferCursor();
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
    ConsoleResetFramebufferCursorState();

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
                return ConsoleVGATextFallbackActivate(Info->Width, Info->Height, NULL) != FALSE ? DF_RETURN_SUCCESS
                                                                                                : DF_GFX_ERROR_MODEUNAVAIL;
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
