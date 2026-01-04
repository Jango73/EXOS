
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
    .Port = 0x03D4,
    .Memory = (LPVOID)0xB8000};

/***************************************************************************/

/**
 * @brief Show the console paging prompt and wait for user input.
 */
static void ConsolePagerWaitLocked(void) {
    KEYCODE KeyCode;
    U32 Width = Console.Width;
    U32 Height = Console.Height;
    U32 Row;
    U32 Col;
    U32 Offset;
    U16 Attribute;
    STR Prompt[] = "-- Press a key --";
    U32 PromptLen;
    U32 Start;

    if (Console.PagingEnabled == FALSE || Console.PagingActive == FALSE) return;
    if (Width == 0 || Height < 2) return;

    Row = Height - 1;
    Attribute = (U16)(CHARATTR << 0x08);

    for (Col = 0; Col < Width; Col++) {
        Console.Memory[(Row * Width) + Col] = (U16)STR_SPACE | Attribute;
    }

    PromptLen = StringLength(Prompt);
    if (PromptLen > Width) PromptLen = Width;
    Start = (Width > PromptLen) ? (Width - PromptLen) / 2 : 0;
    Offset = (Row * Width) + Start;
    for (Col = 0; Col < PromptLen; Col++) {
        Console.Memory[Offset + Col] = (U16)Prompt[Col] | Attribute;
    }

    SetConsoleCursorPosition(0, Row);

    while (TRUE) {
        if (PeekChar()) {
            GetKeyCode(&KeyCode);
            if (KeyCode.VirtualKey == VK_SPACE || KeyCode.VirtualKey == VK_ENTER) {
                Console.PagingRemaining = Height - 1;
                break;
            }
            if (KeyCode.VirtualKey == VK_ESCAPE) {
                Console.PagingRemaining = Height - 1;
                break;
            }
        }

        Sleep(10);
    }

    for (Col = 0; Col < Width; Col++) {
        Console.Memory[(Row * Width) + Col] = (U16)STR_SPACE | Attribute;
    }
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
 * @brief Move the hardware and logical console cursor.
 * @param CursorX X coordinate of the cursor.
 * @param CursorY Y coordinate of the cursor.
 */
void SetConsoleCursorPosition(U32 CursorX, U32 CursorY) {
    PROFILE_SCOPE Scope;
    ProfileStart(&Scope, TEXT("SetConsoleCursorPosition"));

    U32 Position = (CursorY * Console.Width) + CursorX;

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
    U32 Position;
    U8 PositionHigh, PositionLow;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    OutPortByte(Console.Port + CGA_REGISTER, 14);
    PositionHigh = InPortByte(Console.Port + CGA_DATA);
    OutPortByte(Console.Port + CGA_REGISTER, 15);
    PositionLow = InPortByte(Console.Port + CGA_DATA);

    Position = ((U32)PositionHigh << 8) | (U32)PositionLow;

    SAFE_USE_2(CursorX, CursorY) {
        *CursorY = Position / Console.Width;
        *CursorX = Position % Console.Width;
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

    LockMutex(MUTEX_CONSOLE, INFINITY);

    Offset = (Console.CursorY * Console.Width) + Console.CursorX;
    Console.Memory[Offset] = Char | (CHARATTR << 0x08);

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

    U32 CurX, CurY, Src, Dst;
    U32 Width, Height;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    while (Keyboard.ScrollLock) {
    }

    if (Console.PagingRemaining == 0) {
        ConsolePagerWaitLocked();
    }
    if (Console.PagingRemaining > 0) {
        Console.PagingRemaining--;
    }

    Width = Console.Width;
    Height = Console.Height;

    PROFILE_SCOPE ScopeCopy;
    ProfileStart(&ScopeCopy, TEXT("ScrollConsole.Copy"));

    for (CurY = 1; CurY < Height; CurY++) {
        Src = CurY * Width;
        Dst = Src - Width;
        for (CurX = 0; CurX < Width; CurX++) {
            Console.Memory[Dst] = Console.Memory[Src];
            Src++;
            Dst++;
        }
    }

    ProfileStop(&ScopeCopy);

    CurY = Height - 1;

    for (CurX = 0; CurX < Width; CurX++) {
        Console.Memory[(CurY * Width) + CurX] = CHARATTR;
    }

    UnlockMutex(MUTEX_CONSOLE);

    ProfileStop(&Scope);
}

/***************************************************************************/

/**
 * @brief Clear the entire console screen.
 */
void ClearConsole(void) {
    U32 CurX, CurY, Offset;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    for (CurY = 0; CurY < Console.Height; CurY++) {
        for (CurX = 0; CurX < Console.Width; CurX++) {
            Offset = (CurY * Console.Width) + CurX;
            Console.Memory[Offset] = 0x0720;
        }
    }

    Console.CursorX = 0;
    Console.CursorY = 0;

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
 * @brief Handle backspace at the current cursor position.
 */
void ConsoleBackSpace(void) {
    LockMutex(MUTEX_CONSOLE, INFINITY);

    if (Console.CursorX == 0 && Console.CursorY == 0) goto Out;

    if (Console.CursorX == 0) {
        Console.CursorX = Console.Width - 1;
        Console.CursorY--;
    } else {
        Console.CursorX--;
    }

    SetConsoleCharacter(STR_SPACE);

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
    U32 Index;
    U32 Width;
    U32 Height;
    U32 Offset;
    U16 Attribute;

    if (Text == NULL) return;

    Width = Console.Width;
    Height = Console.Height;

    if (Row >= Height || Column >= Width) return;

    Offset = (Row * Width) + Column;
    Attribute = (U16)(Console.ForeColor | (Console.BackColor << 0x04) | (Console.Blink << 0x07));
    Attribute = (U16)(Attribute << 0x08);

    for (Index = 0; Index < Length && (Column + Index) < Width; Index++) {
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
    Console.Width = 80;
    Console.Height = 25;
    Console.BackColor = 0;
    Console.ForeColor = 7;
    Console.PagingEnabled = TRUE;
    Console.PagingActive = FALSE;
    Console.PagingRemaining = 0;

    GetConsoleCursorPosition(&Console.CursorX, &Console.CursorY);
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

                Console.Width = Info->Width;
                Console.Height = Info->Height;
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
