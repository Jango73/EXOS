
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
#include "Driver.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

#include "User.h" /* For console color defines and CONSOLEBLITBUFFER */

/***************************************************************************/

typedef struct tag_CONSOLE_STRUCT {
    U32 Width;
    U32 Height;
    U32 CursorX;
    U32 CursorY;
    U32 BackColor;
    U32 ForeColor;
    U32 Blink;
    U32 PagingEnabled;
    U32 PagingActive;
    U32 PagingRemaining;
    U32 Port;
    U16* Memory;
} CONSOLE_STRUCT, *LPCONSOLE_STRUCT;

/***************************************************************************/

void SetConsoleCursorPosition(U32 CursorX, U32 CursorY);
void GetConsoleCursorPosition(U32* CursorX, U32* CursorY);
void SetConsoleCharacter(STR);
void ScrollConsole(void);
void ClearConsole(void);
void ConsolePrintChar(STR);
void ConsoleBackSpace(void);
void ConsolePrint(LPCSTR Format, ...);
void ConsolePrintLine(U32 Row, U32 Column, LPCSTR Text, U32 Length);
int SetConsoleBackColor(U32 Color);
int SetConsoleForeColor(U32 Color);
BOOL ConsoleGetString(LPSTR, U32);
void ConsolePanic(LPCSTR Format, ...);
void InitializeConsole(void);
UINT ConsoleSetMode(LPGRAPHICSMODEINFO Info);
UINT ConsoleGetModeCount(void);
UINT ConsoleGetModeInfo(LPCONSOLEMODEINFO Info);
void ConsoleSetPagingEnabled(BOOL Enabled);
BOOL ConsoleGetPagingEnabled(void);
void ConsoleSetPagingActive(BOOL Active);
void ConsoleResetPaging(void);

// Functions in Shell.c

U32 Shell(LPVOID);

/***************************************************************************/

extern CONSOLE_STRUCT Console;

/***************************************************************************/

#pragma pack(pop)

#endif
