
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


    Keyboard

\************************************************************************/

#ifndef KEYBOARD_H_INCLUDED
#define KEYBOARD_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Driver.h"
#include "process/Process.h"
#include "User.h"

/***************************************************************************/

// Functions supplied by a keyboard driver

#define DF_KEY_GETSTATE (DF_FIRSTFUNC + 0)
#define DF_KEY_ISKEY (DF_FIRSTFUNC + 1)
#define DF_KEY_GETKEY (DF_FIRSTFUNC + 2)
#define DF_KEY_GETLED (DF_FIRSTFUNC + 3)
#define DF_KEY_SETLED (DF_FIRSTFUNC + 4)
#define DF_KEY_GETDELAY (DF_FIRSTFUNC + 5)
#define DF_KEY_SETDELAY (DF_FIRSTFUNC + 6)
#define DF_KEY_GETRATE (DF_FIRSTFUNC + 7)
#define DF_KEY_SETRATE (DF_FIRSTFUNC + 8)

/***************************************************************************/

#define KEYTABSIZE 128
#define MAXKEYBUFFER 128

/***************************************************************************/

typedef struct tag_KEYTRANS {
    KEYCODE Normal;
    KEYCODE Shift;
    KEYCODE Alt;
} KEYTRANS, *LPKEYTRANS;

/***************************************************************************/

typedef struct tag_KEYBOARDSTRUCT {
    MUTEX Mutex;

    U32 Shift;
    U32 Control;
    U32 Alt;

    U32 CapsLock;
    U32 NumLock;
    U32 ScrollLock;
    U32 Pause;

    KEYCODE Buffer[MAXKEYBUFFER];

    U8 Status[KEYTABSIZE];
} KEYBOARDSTRUCT, *LPKEYBOARDSTRUCT;

/***************************************************************************/

extern KEYBOARDSTRUCT Keyboard;

/***************************************************************************/

BOOL PeekChar(void);
STR GetChar(void);
BOOL GetKeyCode(LPKEYCODE);
BOOL GetKeyCodeDown(KEYCODE);
U32 GetKeyModifiers(void);
void WaitKey(void);
void KeyboardHandler(void);
LPCSTR GetKeyName(U8);
LPKEYTRANS GetScanCodeToKeyCodeTable(LPCSTR Code);
void UseKeyboardLayout(LPCSTR Code);
U16 DetectKeyboard(void);
void ClearKeyboardBuffer(void);

/***************************************************************************/

#endif
