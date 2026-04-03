
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


    Executable

\************************************************************************/

#ifndef EDIT_PRIVATE_H
#define EDIT_PRIVATE_H

#include "Base.h"
#include "console/Console.h"
#include "utils/List.h"
#include "input/VKey.h"

#define MAX_COLUMNS (Console.Width - 10)
#define MAX_LINES (Console.Height - EDIT_MENU_HEIGHT - EDIT_TITLE_HEIGHT)

#define EDIT_TITLE_HEIGHT 1
#define EDIT_MENU_HEIGHT 2
#define EDIT_EOF_CHAR ((STR)0x1A)
#define EDIT_CLIPBOARD_NEWLINE ((STR)0x0A)

typedef struct tag_EDITLINE EDITLINE, *LPEDITLINE;
typedef struct tag_EDITFILE EDITFILE, *LPEDITFILE;
typedef struct tag_EDITCONTEXT EDITCONTEXT, *LPEDITCONTEXT;
typedef BOOL (*EDITMENUPROC)(LPEDITCONTEXT);

typedef struct tag_EDITMENUITEM {
    KEYCODE Modifier;
    KEYCODE Key;
    LPCSTR Name;
    EDITMENUPROC Function;
} EDITMENUITEM, *LPEDITMENUITEM;

struct tag_EDITLINE {
    LISTNODE_FIELDS
    I32 MaxChars;
    I32 NumChars;
    LPSTR Chars;
};

struct tag_EDITFILE {
    LISTNODE_FIELDS
    LPLIST Lines;
    POINT Cursor;
    POINT SelStart;
    POINT SelEnd;
    I32 Left;
    I32 Top;
    LPSTR Name;
    BOOL Modified;
};

struct tag_EDITCONTEXT {
    LISTNODE_FIELDS
    LPLIST Files;
    LPEDITFILE Current;
    I32 Insert;
    LPSTR Clipboard;
    I32 ClipboardSize;
    BOOL ShowLineNumbers;
};

extern EDITMENUITEM Menu[];
extern const U32 MenuItems;
extern const KEYCODE ControlKey;
extern const KEYCODE ShiftKey;

LPEDITLINE NewEditLine(I32 Size);
void DeleteEditLine(LPEDITLINE This);
LPEDITFILE NewEditFile(void);
POINT GetAbsoluteCursor(const LPEDITFILE File);
void Render(LPEDITCONTEXT Context);

void CheckPositions(LPEDITFILE File);
BOOL SelectionHasRange(const LPEDITFILE File);
void NormalizeSelection(const LPEDITFILE File, POINT* Start, POINT* End);
void CollapseSelectionToCursor(LPEDITFILE File);
void UpdateSelectionAfterMove(LPEDITFILE File, BOOL Extend, POINT Previous);
void MoveCursorToAbsolute(LPEDITFILE File, I32 Column, I32 Line);

BOOL CopySelectionToClipboard(LPEDITCONTEXT Context);
void DeleteSelection(LPEDITFILE File);
void AddCharacter(LPEDITFILE File, STR ASCIICode);
void DeleteCharacter(LPEDITFILE File, I32 Flag);
void AddLine(LPEDITFILE File);
void GotoEndOfLine(LPEDITFILE File);
void GotoStartOfFile(LPEDITFILE File);
void GotoStartOfLine(LPEDITFILE File);
void GotoEndOfFile(LPEDITFILE File);
I32 Loop(LPEDITCONTEXT Context);
BOOL OpenTextFile(LPEDITCONTEXT Context, LPCSTR Name);

#endif
