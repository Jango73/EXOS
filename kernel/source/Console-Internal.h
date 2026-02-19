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


    Console internal declarations

\************************************************************************/

#ifndef CONSOLE_INTERNAL_H_INCLUDED
#define CONSOLE_INTERNAL_H_INCLUDED

#include "Console.h"

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

BOOL ConsoleEnsureFramebufferMapped(void);
U32 ConsoleGetCellWidth(void);
U32 ConsoleGetCellHeight(void);
void ConsoleDrawGlyph(U32 X, U32 Y, STR Char);
void ConsoleHideFramebufferCursor(void);
void ConsoleShowFramebufferCursor(void);
void ConsoleResetFramebufferCursorState(void);
void ConsoleClearRegionFramebuffer(U32 RegionIndex);
void ConsoleScrollRegionFramebuffer(U32 RegionIndex);

BOOL ConsoleResolveRegionState(U32 Index, LPCONSOLE_REGION_STATE State);
void ConsoleScrollRegion(U32 RegionIndex);
void ConsoleClearRegion(U32 RegionIndex);
void ConsolePrintCharRegion(U32 RegionIndex, STR Char);
void ConsoleApplyLayout(void);
void ConsoleClampCursorToRegionZero(void);

#endif
