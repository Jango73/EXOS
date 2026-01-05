
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
#include "GFX.h"
#include "Kernel.h"
#include "drivers/VGA.h"
#include "process/Process.h"
#include "drivers/Keyboard.h"
#include "Log.h"
#include "Mutex.h"
#include "CoreString.h"
#include "System.h"
#include "VKey.h"
#include "VarArg.h"
#include "Profile.h"

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
    .Memory = (LPVOID)0xB8000};

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
        Offset = ((State.Y + Console.CursorY) * Console.ScreenWidth) + (State.X + Console.CursorX);
        Console.Memory[Offset] = Char | (CHARATTR << 0x08);
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

    Offset = ((State.Y + Console.CursorY) * Console.ScreenWidth) + (State.X + Console.CursorX);
    Console.Memory[Offset] = (U16)STR_SPACE | (CHARATTR << 0x08);

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
    VarArgList Args;

    DisableInterrupts();

    VarArgStart(Args, Format);
    StringPrintFormatArgs(Text, Format, Args);
    VarArgEnd(Args);

    ConsolePrintString(Text);
    ConsolePrintString(TEXT("\n>>> Halting system <<<"));

    DO_THE_SLEEPING_BEAUTY;
}

/***************************************************************************/

void InitializeConsole(void) {
    Console.ScreenWidth = 80;
    Console.ScreenHeight = 25;
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
