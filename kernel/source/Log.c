
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


    Kernel log manager

\************************************************************************/

#include "../include/Log.h"

#include "../include/Console.h"
#include "../include/Process.h"
#include "../include/SerialPort.h"
#include "../include/String.h"
#include "../include/Text.h"
#include "../include/VarArg.h"

/***************************************************************************/

void InitKernelLog(void) { SerialReset(LOG_COM_INDEX); }

/***************************************************************************/

static void KernelPrintChar(STR Char) { SerialOut(LOG_COM_INDEX, Char); }

/***************************************************************************/

void KernelPrintString(LPCSTR Text) {
    LockMutex(MUTEX_LOG, INFINITY);

    if (Text != NULL) {
        for (U32 Index = 0; Index < 0x1000; Index++) {
            if (Text[Index] == STR_NULL) break;
            KernelPrintChar(Text[Index]);
        }
    }

    UnlockMutex(MUTEX_LOG);
}

/***************************************************************************/

void KernelPrintStringNoMutex(LPCSTR Text) {
    if (Text != NULL) {
        for (U32 Index = 0; Index < 0x1000; Index++) {
            if (Text[Index] == STR_NULL) break;
            KernelPrintChar(Text[Index]);
        }
    }
}

/***************************************************************************/

void KernelLogText(U32 Type, LPCSTR Format, ...) {
    if (StringEmpty(Format)) return;

    STR TextBuffer[0x1000];
    VarArgList Args;

    VarArgStart(Args, Format);
    StringPrintFormatArgs(TextBuffer, Format, Args);
    VarArgEnd(Args);

    switch (Type) {
        case LOG_DEBUG: {
            #ifdef DEBUG_OUTPUT
            KernelPrintString(TEXT("DEBUG > "));
            KernelPrintString(TextBuffer);
            KernelPrintString(Text_NewLine);
            #endif
        } break;

        default:
        case LOG_VERBOSE: {
            KernelPrintString(TextBuffer);
            KernelPrintString(Text_NewLine);
            ConsolePrint(TextBuffer);
            ConsolePrint(Text_NewLine);
        } break;

        case LOG_WARNING: {
            KernelPrintString(TEXT("WARNING > "));
            KernelPrintString(TextBuffer);
            KernelPrintString(Text_NewLine);
        } break;

        case LOG_ERROR: {
            KernelPrintString(TEXT("ERROR > "));
            KernelPrintString(TextBuffer);
            KernelPrintString(Text_NewLine);
        } break;
    }
}

/***************************************************************************/
