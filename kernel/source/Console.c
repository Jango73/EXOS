
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

#include "../include/Console.h"

#include "../include/Kernel.h"
#include "../include/Keyboard.h"
#include "../include/Log.h"
#include "../include/String.h"
#include "../include/System.h"
#include "../include/VKey.h"
#include "../include/VarArg.h"

/***************************************************************************/

#define CHARATTR (Console.ForeColor | (Console.BackColor << 0x04) | (Console.Blink << 0x07))

#define CGA_REGISTER 0x00
#define CGA_DATA 0x01

/***************************************************************************/

ConsoleStruct Console = {
    .Width = 80,
    .Height = 25,
    .CursorX = 0,
    .CursorY = 0,
    .BackColor = 0,
    .ForeColor = 0,
    .Blink = 0,
    .Port = 0x03D4,
    .Memory = (LPVOID)0xB8000};

/***************************************************************************/

/**
 * @brief Move the hardware and logical console cursor.
 * @param CursorX X coordinate of the cursor.
 * @param CursorY Y coordinate of the cursor.
 */
void SetConsoleCursorPosition(U32 CursorX, U32 CursorY) {
    U32 Position = (CursorY * Console.Width) + CursorX;

    Console.CursorX = CursorX;
    Console.CursorY = CursorY;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    OutPortByte(Console.Port + CGA_REGISTER, 14);
    OutPortByte(Console.Port + CGA_DATA, (Position >> 8) & 0xFF);
    OutPortByte(Console.Port + CGA_REGISTER, 15);
    OutPortByte(Console.Port + CGA_DATA, (Position >> 0) & 0xFF);

    UnlockMutex(MUTEX_CONSOLE);
}

/***************************************************************************/

/**
 * @brief Place a character at the current cursor position.
 * @param Char Character to display.
 */
void SetConsoleCharacter(STR Char) {
    U32 Offset = 0;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    Offset = (Console.CursorY * Console.Width) + Console.CursorX;
    Console.Memory[Offset] = Char | (CHARATTR << 0x08);

    UnlockMutex(MUTEX_CONSOLE);
}

/***************************************************************************/

/**
 * @brief Scroll the console up by one line.
 */
void ScrollConsole(void) {
    U32 CurX, CurY, Src, Dst;
    U32 Width, Height;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    while (Keyboard.ScrollLock) {
    }

    Width = Console.Width;
    Height = Console.Height;

    for (CurY = 1; CurY < Height; CurY++) {
        Src = CurY * Width;
        Dst = Src - Width;
        for (CurX = 0; CurX < Width; CurX++) {
            Console.Memory[Dst] = Console.Memory[Src];
            Src++;
            Dst++;
        }
    }

    CurY = Height - 1;

    for (CurX = 0; CurX < Width; CurX++) {
        Console.Memory[(CurY * Width) + CurX] = CHARATTR;
    }

    UnlockMutex(MUTEX_CONSOLE);
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
    LockMutex(MUTEX_CONSOLE, INFINITY);

    if (Char == STR_NEWLINE) {
        Console.CursorX = 0;
        Console.CursorY++;
        if (Console.CursorY >= Console.Height) {
            ScrollConsole();
            Console.CursorY = Console.Height - 1;
        }
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
    U32 Index = 0;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    if (Text) {
        for (Index = 0; Index < 0x10000; Index++) {
            if (Text[Index] == STR_NULL) break;
            ConsolePrintChar(Text[Index]);
        }
    }

    UnlockMutex(MUTEX_CONSOLE);
}

/***************************************************************************/

/**
 * @brief Print a formatted string to the console.
 * @param Format Format string.
 * @return TRUE on success.
 */
void ConsolePrint(LPCSTR Format, ...) {
    STR Text[0x1000];
    VarArgList Args;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    VarArgStart(Args, Format);
    StringPrintFormatArgs(Text, Format, Args);
    VarArgEnd(Args);

    ConsolePrintString(Text);

    UnlockMutex(MUTEX_CONSOLE);
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

    KernelLogText(LOG_DEBUG, TEXT("[ConsoleGetString] Enter"));

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

    KernelLogText(LOG_DEBUG, TEXT("[ConsoleGetString] Exit"));

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

BOOL InitializeConsole(void) {
    Console.Width = 80;
    Console.Height = 25;
    Console.BackColor = 0;
    Console.ForeColor = 7;

    Console.CursorX = KernelStartup.ConsoleX;
    Console.CursorY = KernelStartup.ConsoleY;

    SetConsoleCursorPosition(Console.CursorX, Console.CursorY);

    return TRUE;
}

/***************************************************************************/
