
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Log.h"

#include "../include/Console.h"
#include "../include/Process.h"
#include "../include/SerialPort.h"
#include "../include/String.h"
#include "../include/Text.h"
#include "../include/VarArg.h"

/***************************************************************************/

void InitKernelLog(void) {
    SerialReset(LOG_COM_INDEX);
}

/***************************************************************************/

static int SkipAToI(LPCSTR* format) {
    int result = 0;
    while (IsNumeric(**format)) {
        result = result * 10 + (**format - '0');
        (*format)++;
    }
    (*format)--; // Come back for next character
    return result;
}

/***************************************************************************/

static void KernelPrintChar(STR Char) { SerialOut(LOG_COM_INDEX, Char); }

/***************************************************************************/

void KernelPrintString(LPCSTR Text) {
    LockMutex(MUTEX_LOG, INFINITY);

    U32 Index = 0;

    if (Text) {
        for (Index = 0; Index < 0x1000; Index++) {
            if (Text[Index] == STR_NULL) break;
            KernelPrintChar(Text[Index]);
        }
    }

    UnlockMutex(MUTEX_LOG);
}

/***************************************************************************/

void VarKernelPrintNumber(I32 Number, I32 Base, I32 FieldWidth, I32 Precision, I32 Flags) {
    STR Text[128];
    NumberToString(Text, Number, Base, FieldWidth, Precision, Flags);
    KernelPrintString(Text);
}

/***************************************************************************/

void VarKernelPrint(LPCSTR Format, VarArgList Args) {
    LPCSTR Text = NULL;
    long Number;
    int Flags, FieldWidth, Precision, Qualifier, Base, Length, i;

    // Vérification de la chaîne de format
    if (Format == NULL) {
        KernelPrintChar('<');
        KernelPrintChar('N');
        KernelPrintChar('U');
        KernelPrintChar('L');
        KernelPrintChar('L');
        KernelPrintChar('>');
        return;
    }

    for (; *Format != STR_NULL; Format++) {
        if (*Format != '%') {
            KernelPrintChar(*Format);
            continue;
        }

        Flags = 0;

    Repeat:
        Format++;
        switch (*Format) {
            case '-': Flags |= PF_LEFT; goto Repeat;
            case '+': Flags |= PF_PLUS; goto Repeat;
            case ' ': Flags |= PF_SPACE; goto Repeat;
            case '#': Flags |= PF_SPECIAL; goto Repeat;
            case '0': Flags |= PF_ZEROPAD; goto Repeat;
            case STR_NULL: return; // Fin prématurée de la chaîne
        }

        // Récupérer la largeur de champ
        FieldWidth = -1;
        if (IsNumeric(*Format)) {
            FieldWidth = SkipAToI(&Format);
        } else if (*Format == '*') {
            Format++;
            FieldWidth = VarArg(Args, int);
            if (FieldWidth < 0) {
                FieldWidth = -FieldWidth;
                Flags |= PF_LEFT;
            }
        }

        // Récupérer la précision
        Precision = -1;
        if (*Format == '.') {
            Format++;
            if (IsNumeric(*Format)) {
                Precision = SkipAToI(&Format);
            } else if (*Format == '*') {
                Format++;
                Precision = VarArg(Args, int);
            }
            if (Precision < 0) Precision = 0;
        }

        // Récupérer le qualificateur
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
                KernelPrintChar((STR)VarArg(Args, int));
                while (--FieldWidth > 0) KernelPrintChar(STR_SPACE);
                continue;

            case 's':
                Text = VarArg(Args, LPCSTR);
                if (Text == NULL) Text = TEXT("<NULL>");

                // Utiliser la précision si spécifiée
                Length = StringLength(Text);
                if (Precision >= 0 && Length > Precision) Length = Precision;

                if (!(Flags & PF_LEFT)) {
                    while (Length < FieldWidth--) KernelPrintChar(STR_SPACE);
                }
                for (i = 0; i < Length && Text[i] != STR_NULL; i++) {
                    KernelPrintChar(Text[i]);
                }
                while (Length < FieldWidth--) KernelPrintChar(STR_SPACE);
                continue;

            case 'p':
                if (FieldWidth == -1) {
                    FieldWidth = 2 * sizeof(void*);
                    Flags |= PF_ZEROPAD | PF_LARGE;
                }
                // Ajouter le préfixe 0x pour %p (toujours en minuscules pour les pointeurs)
                if (Flags & PF_SPECIAL) {
                    KernelPrintChar('0');
                    KernelPrintChar('x');
                }
                VarKernelPrintNumber((unsigned long)VarArg(Args, void*), 16, FieldWidth, Precision, Flags);
                continue;

            case 'o':
                Flags |= PF_SPECIAL;
                Base = 8;
                break;

            case 'X':
                Flags |= PF_SPECIAL | PF_LARGE;
                Base = 16;
                break;

            case 'x':
                Flags |= PF_SPECIAL;
                Base = 16;
                break;

            case 'b':
                Base = 2;
                break;

            case 'd':
            case 'i':
                Flags |= PF_SIGN;
                break;

            case 'u':
                break;

            default:
                if (*Format != '%') KernelPrintChar('%');
                if (*Format) {
                    KernelPrintChar(*Format);
                } else {
                    Format--;
                }
                continue;
        }

        // Gestion des nombres
        if (Qualifier == 'l') {
            Number = VarArg(Args, long);
        } else if (Qualifier == 'h') {
            Number = (Flags & PF_SIGN) ? (short)VarArg(Args, int) : (unsigned short)VarArg(Args, int);
        } else {
            Number = (Flags & PF_SIGN) ? VarArg(Args, int) : (unsigned int)VarArg(Args, unsigned int);
        }

        // Ajouter le préfixe 0x ou 0X pour %x et %X si PF_SPECIAL est défini et le nombre n'est pas 0
        if ((Flags & PF_SPECIAL) && (*Format == 'x' || *Format == 'X') && Number != 0) {
            KernelPrintChar('0');
            KernelPrintChar((Flags & PF_LARGE) ? 'X' : 'x');
        }

        VarKernelPrintNumber(Number, Base, FieldWidth, Precision, Flags);
    }

    VarArgEnd(Args);
}

/***************************************************************************/

static void KernelLogRegistersCompact(void) {
    KernelPrintString(Text_NewLine);
    KernelPrintString(TEXT("<"));
    VarKernelPrintNumber(GetESP(), 16, 0, 0, 0);
    KernelPrintString(TEXT(":"));
    VarKernelPrintNumber(GetEBP(), 16, 0, 0, 0);
    KernelPrintString(TEXT(">"));
}

/***************************************************************************/

void KernelLogText(U32 Type, LPCSTR Format, ...) {

    // KernelPrintString(Format);
    // return;

    if (StringEmpty(Format)) return;

    VarArgList Args;

    VarArgStart(Args, Format);

    switch (Type) {
        case LOG_DEBUG: {
            KernelPrintString(TEXT("DEBUG > "));
            VarKernelPrint(Format, Args);
            KernelPrintString(Text_NewLine);
        } break;

        default:
        case LOG_VERBOSE: {
            VarKernelPrint(Format, Args);
            KernelPrintString(Text_NewLine);
        } break;

        case LOG_WARNING: {
            KernelPrintString(TEXT("WARNING > "));
            VarKernelPrint(Format, Args);
            KernelPrintString(Text_NewLine);
        } break;

        case LOG_ERROR: {
            KernelPrintString(TEXT("ERROR > "));
            VarKernelPrint(Format, Args);
            KernelPrintString(Text_NewLine);
        } break;
    }

    VarArgEnd(Args);
}

/***************************************************************************/
