
// Keyboard.h

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#ifndef KEYBOARD_H_INCLUDED
#define KEYBOARD_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Process.h"
#include "Driver.h"
#include "User.h"

/***************************************************************************/

// Functions supplied by a keyboard driver

#define DF_KEY_GETSTATE (DF_FIRSTFUNC + 0)
#define DF_KEY_ISKEY    (DF_FIRSTFUNC + 1)
#define DF_KEY_GETKEY   (DF_FIRSTFUNC + 2)
#define DF_KEY_GETLED   (DF_FIRSTFUNC + 3)
#define DF_KEY_SETLED   (DF_FIRSTFUNC + 4)
#define DF_KEY_GETDELAY (DF_FIRSTFUNC + 5)
#define DF_KEY_SETDELAY (DF_FIRSTFUNC + 6)
#define DF_KEY_GETRATE  (DF_FIRSTFUNC + 7)
#define DF_KEY_SETRATE  (DF_FIRSTFUNC + 8)

/***************************************************************************/

#define KEYTABSIZE        128
#define MAXKEYBUFFER      128

/***************************************************************************/

typedef struct tag_KEYTRANS
{
  KEYCODE Normal;
  KEYCODE Shift;
  KEYCODE Alt;
} KEYTRANS, *LPKEYTRANS;

/***************************************************************************/

typedef struct tag_KEYBOARDSTRUCT
{
  SEMAPHORE Semaphore;

  U32 Shift;
  U32 Control;
  U32 Alt;

  U32 CapsLock;
  U32 NumLock;
  U32 ScrollLock;
  U32 Pause;

  KEYCODE Buffer [MAXKEYBUFFER];

  U8  Status [KEYTABSIZE];
} KEYBOARDSTRUCT, *LPKEYBOARDSTRUCT;

/***************************************************************************/

extern KEYBOARDSTRUCT Keyboard;

/***************************************************************************/

BOOL PeekChar           ();
STR  GetChar            ();
BOOL GetKeyCode         (LPKEYCODE);
void KeyboardHandler    ();

/***************************************************************************/

extern KEYTRANS ScanCodeToKeyCode_fr [128];

/***************************************************************************/

#endif
