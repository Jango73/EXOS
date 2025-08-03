
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Console.h"
#include "../include/Log.h"
#include "../include/Process.h"
#include "../include/SerialPort.h"
#include "../include/String.h"
#include "../include/Text.h"
#include "../include/VarArg.h"

/***************************************************************************/

void InitKernelLog() {
    SerialReset();
    KernelLogText(LOG_VERBOSE, TEXT("COM1 initialized"));
}

/***************************************************************************/

static INT SkipAToI(LPCSTR* s) {
    INT i = 0;
    while (IsNumeric(**s)) i = i * 10 + *((*s)++) - '0';
    return i;
}

/***************************************************************************/

static void KernelPrintChar(STR Char) {
    SerialOut(Char);
}

/***************************************************************************/

static void KernelPrintString(LPCSTR Text) {
    U32 Index = 0;

    if (Text) {
        for (Index = 0; Index < 0x1000; Index++) {
            if (Text[Index] == STR_NULL) break;
            KernelPrintChar(Text[Index]);
        }
    }
}

/***************************************************************************/

static void VarKernelPrintNumber(I32 Number, I32 Base, I32 FieldWidth,
                                 I32 Precision, I32 Flags) {
    STR Text[128];
    NumberToString(Text, Number, Base, FieldWidth, Precision, Flags);
    KernelPrintString(Text);
}

/***************************************************************************/

void VarKernelPrint(LPCSTR Format, VarArgList Args) {
    LPCSTR Text = NULL;
    I32 Flags, Number, i;
    I32 FieldWidth, Precision, Qualifier, Base, Length;

    for (; *Format != STR_NULL; Format++) {
        if (*Format != '%') {
            KernelPrintChar(*Format);
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
                    while (--FieldWidth > 0) KernelPrintChar(STR_SPACE);
                }
                KernelPrintChar((STR)VarArg(Args, INT));
                while (--FieldWidth > 0) KernelPrintChar(STR_SPACE);
                continue;

            case 's':

                Text = VarArg(Args, LPSTR);

                if (Text == NULL) Text = TEXT("<NULL>");

                // Length = strnlen(Text, Precision);
                Length = StringLength(Text);

                if (!(Flags & PF_LEFT)) {
                    while (Length < FieldWidth--) KernelPrintChar(STR_SPACE);
                }
                for (i = 0; i < Length; ++i) KernelPrintChar(*Text++);
                while (Length < FieldWidth--) KernelPrintChar(STR_SPACE);
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
                if (*Format != '%') KernelPrintChar('%');
                if (*Format)
                    KernelPrintChar(*Format);
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

    VarArgStart(Args, Format);
    VarKernelPrint(Format, Args);
    VarArgEnd(Args);
}

/***************************************************************************/

void KernelLogText(U32 Type, LPCSTR Format, ...) {
    VarArgList Args;

    VarArgStart(Args, Format);

    switch (Type) {
        case LOG_DEBUG: {
            KernelPrint(TEXT("DEBUG > "));
            VarKernelPrint(Format, Args);
            KernelPrint(Text_NewLine);
        } break;

        default:
        case LOG_VERBOSE: {
            KernelPrint(TEXT("> "));
            VarKernelPrint(Format, Args);
            KernelPrint(Text_NewLine);
        } break;

        case LOG_WARNING: {
            KernelPrint(TEXT("WARNING > "));
            VarKernelPrint(Format, Args);
            KernelPrint(Text_NewLine);
        } break;

        case LOG_ERROR: {
            KernelPrint(TEXT("ERROR > "));
            VarKernelPrint(Format, Args);
            KernelPrint(Text_NewLine);
        } break;
    }

    VarArgEnd(Args);
}

/***************************************************************************/
