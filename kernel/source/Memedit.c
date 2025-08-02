
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "Base.h"
#include "Console.h"
#include "Kernel.h"
#include "Keyboard.h"
#include "String.h"
#include "VKey.h"

/***************************************************************************/

typedef struct tag_MEMEDITCONTEXT {
    U32 Base;
} MEMEDITCONTEXT, *LPMEMEDITCONTEXT;

/***************************************************************************/

static void PrintMemoryLine(U32 Base) {
    STR Num[16];
    STR Addr[16];
    STR Hexa[64];
    STR ASCII[64];
    U8* Pointer = (U8*)Base;
    U8 Data = 0;
    U32 Index = 0;

    Addr[0] = STR_NULL;
    Hexa[0] = STR_NULL;
    ASCII[0] = STR_NULL;

    U32ToHexString(Base, Addr);

    for (Index = 0; Index < 16; Index++) {
        Data = Pointer[Index];
        U32ToHexString(Data, Num);
        StringConcat(Hexa, Num + 6);
        StringConcat(Hexa, Text_Space);
        if (Index == 7) StringConcat(Hexa, Text_Space);

        if (Data < STR_SPACE) Data = '.';
        if (Data == '%') Data = '.';
        Num[0] = Data;
        Num[1] = STR_NULL;
        StringConcat(ASCII, Num);
    }

    ConsolePrint(Addr);
    ConsolePrint(Text_Space);
    ConsolePrint(Hexa);
    ConsolePrint(Text_Space);
    ConsolePrint(ASCII);
    ConsolePrint(Text_NewLine);
}

/***************************************************************************/

void PrintMemory(U32 Base, U32 Size) {
    U32 End = Base + Size;
    U32 CurrentX = 0;
    U32 CurrentY = 0;

    for (CurrentY = 0; CurrentY < 24; CurrentY++) {
        PrintMemoryLine(Base);
        Base += 16;
        if (Base >= End) break;
    }
}

/***************************************************************************/

static void PrintMemoryPage(U32 Base, U32 Size) {
    LockMutex(MUTEX_CONSOLE, INFINITY);

    Console.CursorX = 0;
    Console.CursorY = 0;

    PrintMemory(Base, Size);

    UnlockMutex(MUTEX_CONSOLE);
}

/***************************************************************************/

void MemEdit(U32 Base) {
    KEYCODE KeyCode;

    PrintMemoryPage(Base, 24 * 16);

    while (1) {
        if (PeekChar()) {
            GetKeyCode(&KeyCode);

            switch (KeyCode.VirtualKey) {
                case VK_ESCAPE:
                    return;
                case VK_DOWN: {
                    Base += 16;
                    PrintMemoryPage(Base, 24 * 16);
                } break;
                case VK_UP: {
                    if (Base >= 16) Base -= 16;
                    PrintMemoryPage(Base, 24 * 16);
                } break;
                case VK_PAGEDOWN: {
                    Base += 16 * 24;
                    PrintMemoryPage(Base, 24 * 16);
                } break;
                case VK_PAGEUP: {
                    if (Base >= (16 * 24)) Base -= 16 * 24;
                    PrintMemoryPage(Base, 24 * 16);
                } break;
            }
        }
    }
}

/***************************************************************************/
