
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

#define KEY_USAGE_PAGE_KEYBOARD 0x07
#define KEY_USAGE_MIN 0x04
#define KEY_USAGE_MAX 0xE7

#define KEY_LAYOUT_HID_MAX_LEVELS 4
#define KEY_LAYOUT_HID_MAX_DEAD_KEYS 128
#define KEY_LAYOUT_HID_MAX_COMPOSE 256

#define KEY_LAYOUT_HID_LEVEL_BASE 0x00
#define KEY_LAYOUT_HID_LEVEL_SHIFT 0x01
#define KEY_LAYOUT_HID_LEVEL_ALTGR 0x02
#define KEY_LAYOUT_HID_LEVEL_CONTROL 0x03

#define KEY_LAYOUT_FALLBACK_CODE "en-US"

/***************************************************************************/

typedef struct tag_KEYTRANS {
    KEYCODE Normal;
    KEYCODE Shift;
    KEYCODE Alt;
} KEYTRANS, *LPKEYTRANS;

/***************************************************************************/

typedef UINT KEY_USAGE;

typedef struct tag_KEY_LAYOUT_HID_ENTRY {
    KEYCODE Levels[KEY_LAYOUT_HID_MAX_LEVELS];
} KEY_LAYOUT_HID_ENTRY, *LPKEY_LAYOUT_HID_ENTRY;

/***************************************************************************/

typedef struct tag_KEY_HID_DEAD_KEY {
    U32 DeadKey;
    U32 BaseKey;
    U32 Result;
} KEY_HID_DEAD_KEY, *LPKEY_HID_DEAD_KEY;

/***************************************************************************/

typedef struct tag_KEY_HID_COMPOSE_ENTRY {
    U32 FirstKey;
    U32 SecondKey;
    U32 Result;
} KEY_HID_COMPOSE_ENTRY, *LPKEY_HID_COMPOSE_ENTRY;

/***************************************************************************/

// HID usage page 0x07 layout (separate from legacy PS/2 scan code tables).
typedef struct tag_KEY_LAYOUT_HID {
    LPCSTR Code;
    UINT LevelCount;
    const KEY_LAYOUT_HID_ENTRY *Entries;
    UINT EntryCount;
    const KEY_HID_DEAD_KEY *DeadKeys;
    UINT DeadKeyCount;
    const KEY_HID_COMPOSE_ENTRY *ComposeEntries;
    UINT ComposeCount;
} KEY_LAYOUT_HID, *LPKEY_LAYOUT_HID;

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

void RouteKeyCode(LPKEYCODE KeyCode);
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
