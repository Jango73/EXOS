
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "Console.h"

#include "Kernel.h"
#include "Keyboard.h"
#include "VKey.h"
#include "VarArg.h"

/***************************************************************************/

#define CHARATTR \
    (Console.ForeColor | (Console.BackColor << 0x04) | (Console.Blink << 0x07))

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
    .Memory = (LPVOID)0xB8000
};

/***************************************************************************/

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

void SetConsoleCharacter(STR Char) {
    U32 Offset = 0;

    Offset = (Console.CursorY * Console.Width) + Console.CursorX;
    Console.Memory[Offset] = Char | (CHARATTR << 0x08);
}

/***************************************************************************/

void ScrollConsole() {
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

void ClearConsole() {
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

void ConsolePrintChar(STR Char) {
    if (Char == STR_NEWLINE) {
        Console.CursorX = 0;
        Console.CursorY++;
        if (Console.CursorY >= Console.Height) {
            ScrollConsole();
            Console.CursorY = Console.Height - 1;
        }
    } else if (Char == STR_TAB) {
        Console.CursorX += 4;
        if (Console.CursorX >= Console.Width) ConsolePrintChar(STR_NEWLINE);
    } else {
        SetConsoleCharacter(Char);
        Console.CursorX++;
        if (Console.CursorX >= Console.Width) ConsolePrintChar(STR_NEWLINE);
    }

    SetConsoleCursorPosition(Console.CursorX, Console.CursorY);
}

/***************************************************************************/

void ConsoleBackSpace() {
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

BOOL ConsolePrint(LPCSTR Text) {
    U32 Index = 0;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    if (Text) {
        for (Index = 0; Index < 0x10000; Index++) {
            if (Text[Index] == STR_NULL) break;
            ConsolePrintChar(Text[Index]);
        }
    }

    UnlockMutex(MUTEX_CONSOLE);

    return 1;
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

static INT SkipAToI(LPCSTR* s) {
    INT i = 0;
    while (IsNumeric(**s)) i = i * 10 + *((*s)++) - '0';
    return i;
}

/***************************************************************************/

static void VarKernelPrintNumber(I32 Number, I32 Base, I32 FieldWidth,
                                 I32 Precision, I32 Flags) {
    STR Text[128];
    NumberToString(Text, Number, Base, FieldWidth, Precision, Flags);
    ConsolePrint(Text);
}

/***************************************************************************/

static void VarKernelPrint(LPCSTR Format, VarArgList Args) {
    LPCSTR Text = NULL;
    I32 Flags, Number, i;
    I32 FieldWidth, Precision, Qualifier, Base, Length;

    for (; *Format != STR_NULL; Format++) {
        if (*Format != '%') {
            ConsolePrintChar(*Format);
            continue;
        }

        Flags = 0;

    Repeat:

        Format++;

        switch (*Format) {
            case '-':
                Flags |= PF_LEFT;
                goto Repeat;
            case '+':
                Flags |= PF_PLUS;
                goto Repeat;
            case ' ':
                Flags |= PF_SPACE;
                goto Repeat;
            case '#':
                Flags |= PF_SPECIAL;
                goto Repeat;
            case '0':
                Flags |= PF_ZEROPAD;
                goto Repeat;
        }

        FieldWidth = -1;

        if (IsNumeric(*Format))
            FieldWidth = SkipAToI(&Format);
        else if (*Format == '*') {
            Format++;
            FieldWidth = VarArg(Args, INT);
            if (FieldWidth < 0) {
                FieldWidth = -FieldWidth;
                Flags |= PF_LEFT;
            }
        }

        // Get the precision
        Precision = -1;

        if (*Format == '.') {
            Format++;
            if (IsNumeric(*Format))
                Precision = SkipAToI(&Format);
            else if (*Format == '*') {
                Format++;
                Precision = VarArg(Args, INT);
            }
            if (Precision < 0) Precision = 0;
        }

        // Get the conversion qualifier
        Qualifier = -1;

        if (*Format == 'h' || *Format == 'l' || *Format == 'L') {
            Qualifier = *Format;
            Format++;
        }

        Base = 10;

        switch (*Format) {
            case 'c':

                if (!(Flags & PF_LEFT)) {
                    while (--FieldWidth > 0) ConsolePrintChar(STR_SPACE);
                }
                ConsolePrintChar((STR)VarArg(Args, INT));
                while (--FieldWidth > 0) ConsolePrintChar(STR_SPACE);
                continue;

            case 's':

                Text = VarArg(Args, LPSTR);

                if (Text == NULL) Text = TEXT("<NULL>");

                // Length = strnlen(Text, Precision);
                Length = StringLength(Text);

                if (!(Flags & PF_LEFT)) {
                    while (Length < FieldWidth--) ConsolePrintChar(STR_SPACE);
                }
                for (i = 0; i < Length; ++i) ConsolePrintChar(*Text++);
                while (Length < FieldWidth--) ConsolePrintChar(STR_SPACE);
                continue;

            case 'p':

                if (FieldWidth == -1) {
                    FieldWidth = 2 * sizeof(LPVOID);
                    Flags |= PF_ZEROPAD;
                    Flags |= PF_LARGE;
                }
                VarKernelPrintNumber((U32)VarArg(Args, LPVOID), 16, FieldWidth,
                                     Precision, Flags);
                continue;

                /*
                      case 'n':
                    if (Qualifier == 'l')
                    {
                      I32* ip = VarArg(Args, U32*);
                      *ip = (str - buf);
                    }
                    else
                    {
                      INT* ip = VarArg(args, INT*);
                      *ip = (str - buf);
                    }
                    continue;
                */

                // Integer number formats - set up the flags and "break"

            case 'o':
                Base = 8;
                break;
            case 'X':
                Flags |= PF_LARGE;
            case 'x':
                Base = 16;
                break;
            case 'b':
                Base = 2;
                break;
            case 'd':
            case 'i':
                Flags |= PF_SIGN;
            case 'u':
                break;
            default:
                if (*Format != '%') ConsolePrintChar('%');
                if (*Format)
                    ConsolePrintChar(*Format);
                else
                    Format--;
                continue;
        }

        if (Qualifier == 'l') {
            Number = VarArg(Args, U32);
        } else if (Qualifier == 'h') {
            if (Flags & PF_SIGN)
                Number = VarArg(Args, I16);
            else
                Number = VarArg(Args, U16);
        } else {
            if (Flags & PF_SIGN)
                Number = VarArg(Args, INT);
            else
                Number = VarArg(Args, UINT);
        }

        VarKernelPrintNumber(Number, Base, FieldWidth, Precision, Flags);
    }
}

/***************************************************************************/

void KernelPrint(LPCSTR Format, ...) {
    VarArgList Args;

    LockMutex(MUTEX_CONSOLE, INFINITY);

    VarArgStart(Args, Format);
    VarKernelPrint(Format, Args);
    VarArgEnd(Args);

    UnlockMutex(MUTEX_CONSOLE);
}

/***************************************************************************/

BOOL ConsoleGetString(LPSTR Buffer, U32 Size) {
    KEYCODE KeyCode;
    U32 Index = 0;
    U32 Done = 0;

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
    }

    Buffer[Index] = STR_NULL;

    return TRUE;
}

/***************************************************************************/

BOOL InitializeConsole() {
    Console.Width = 80;
    Console.Height = 25;
    Console.BackColor = 0;
    Console.ForeColor = 7;

    /*
    Console.CursorX = KernelStartup.ConsoleCursorX;
    Console.CursorY = KernelStartup.ConsoleCursorY;

    SetConsoleCursorPosition(Console.CursorX, Console.CursorY);
    */

    ClearConsole();

    return TRUE;
}

/***************************************************************************/
