
// Console.h

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\***************************************************************************/

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
void ScrollConsole();
void ClearConsole();
void ConsolePrintChar(STR);
BOOL ConsolePrint(LPCSTR);
BOOL ConsoleGetString(LPSTR, U32);
BOOL ConsoleInitialize();
U32 Shell(LPVOID);
void KernelPrint(LPCSTR, ...);

/***************************************************************************/

extern ConsoleStruct Console;

/***************************************************************************/

#endif
