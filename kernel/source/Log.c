
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

#include "../include/Clock.h"
#include "../include/Console.h"
#include "../include/Memory.h"
#include "../include/Process.h"
#include "../include/SerialPort.h"
#include "../include/String.h"
#include "../include/Text.h"
#include "../include/VarArg.h"

/************************************************************************/

void InitKernelLog(void) { SerialReset(LOG_COM_INDEX); }

/************************************************************************/

static void KernelPrintChar(STR Char) { SerialOut(LOG_COM_INDEX, Char); }

/************************************************************************/

static void KernelPrintString(LPCSTR Text) {
    if (Text != NULL) {
        for (U32 Index = 0; Index < 0x1000; Index++) {
            if (Text[Index] == STR_NULL) break;
            KernelPrintChar(Text[Index]);
        }
    }
}

/************************************************************************/

void KernelLogText(U32 Type, LPCSTR Format, ...) {
    if (StringEmpty(Format)) return;

    U32 Flags;

    SaveFlags(&Flags);
    FreezeScheduler();
    DisableInterrupts();

    STR TimeBuffer[128];
    STR TextBuffer[2048];
    VarArgList Args;

    U32 Time = GetSystemTime();
    StringPrintFormat(TimeBuffer, TEXT("T%u> "), Time);

    VarArgStart(Args, Format);
    StringPrintFormatArgs(TextBuffer, Format, Args);
    VarArgEnd(Args);

    switch (Type) {
        case LOG_DEBUG: {
            #if DEBUG_OUTPUT == 1
            KernelPrintString(TimeBuffer);
            KernelPrintString(TEXT("DEBUG > "));
            KernelPrintString(TextBuffer);
            KernelPrintString(Text_NewLine);
            #endif
        } break;

        default:
        case LOG_VERBOSE: {
            KernelPrintString(TimeBuffer);
            KernelPrintString(TextBuffer);
            KernelPrintString(Text_NewLine);
            ConsolePrint(TextBuffer);
            ConsolePrint(Text_NewLine);
        } break;

        case LOG_WARNING: {
            KernelPrintString(TimeBuffer);
            KernelPrintString(TEXT("WARNING > "));
            KernelPrintString(TextBuffer);
            KernelPrintString(Text_NewLine);
        } break;

        case LOG_ERROR: {
            KernelPrintString(TimeBuffer);
            KernelPrintString(TEXT("ERROR > "));
            KernelPrintString(TextBuffer);
            KernelPrintString(Text_NewLine);
            ConsolePrint(TextBuffer);
            ConsolePrint(Text_NewLine);
        } break;
    }

    UnfreezeScheduler();
    RestoreFlags(&Flags);
}

/************************************************************************/

void KernelLogMem(U32 Type, LINEAR Memory, U32 Size) {
    U32* Pointer = (U32*) Memory;
    U32 LineCount = Size / (sizeof(U32) * 8);

    if (LineCount < 1) LineCount = 1;

    for (U32 Line = 0; Line < LineCount; Line++) {
        if (IsValidMemory(Pointer) == FALSE) return;
        if (IsValidMemory(Pointer + 31) == FALSE) return;

        KernelLogText(Type, "%08x : %08x %08x %08x %08x %08x %08x %08x %08x",
            Pointer,
            Pointer[0], Pointer[1], Pointer[2], Pointer[3],
            Pointer[4], Pointer[5], Pointer[6], Pointer[7]);
        Pointer += 4;
    }
}

/************************************************************************/
