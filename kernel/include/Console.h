
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
#ifndef CONSOLE_H_INCLUDED
#define CONSOLE_H_INCLUDED

#include "Base.h"

/***************************************************************************/

#pragma pack(1)

/***************************************************************************/

typedef struct tag_ConsoleStruct {
    U32 Width;
    U32 Height;
    U32 CursorX;
    U32 CursorY;
    U32 BackColor;
    U32 ForeColor;
    U32 Blink;
    U32 Port;
    U16* Memory;
} ConsoleStruct, *lpConsoleStruct;

/***************************************************************************/

void SetConsoleCharacter(STR);
void ScrollConsole(void);
void ClearConsole(void);
void ConsolePrintChar(STR);
void ConsoleBackSpace(void);
void ConsolePrint(LPCSTR Format, ...);
BOOL ConsoleGetString(LPSTR, U32);
BOOL InitializeConsole(void);
U32 Shell(LPVOID);

/***************************************************************************/

extern ConsoleStruct Console;

/***************************************************************************/

#endif
