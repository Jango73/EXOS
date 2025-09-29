
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

/**
 * @brief Initializes the kernel logging system.
 *
 * Sets up the serial port used for kernel log output by resetting
 * the designated communication port.
 */
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

/**
 * @brief Logs formatted text to the kernel log.
 *
 * Outputs timestamped log messages to the serial port. The function is
 * thread-safe and disables interrupts during output to ensure atomic logging.
 * Supports printf-style format strings with variable arguments.
 *
 * @param Type Log message type/severity level
 * @param Format Printf-style format string
 * @param ... Variable arguments for format string
 */
void KernelLogText(U32 Type, LPCSTR Format, ...) {
    if (StringEmpty(Format)) return;

    U32 Flags;

    SaveFlags(&Flags);
    FreezeScheduler();
    DisableInterrupts();

    STR TimeBuffer[128];
    STR TextBuffer[MAX_STRING_BUFFER];
    VarArgList Args;

    U32 Time = GetSystemTime();
    StringPrintFormat(TimeBuffer, TEXT("T%u> "), Time);

    VarArgStart(Args, Format);
    StringPrintFormatArgs(TextBuffer, Format, Args);
    VarArgEnd(Args);

    switch (Type) {
        case LOG_DEBUG: {
            KernelPrintString(TimeBuffer);
            KernelPrintString(TEXT("DEBUG > "));
            KernelPrintString(TextBuffer);
            KernelPrintString(Text_NewLine);
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

/**
 * @brief Logs a memory dump to the kernel log.
 *
 * Outputs a hexadecimal dump of memory contents to the kernel log.
 * The memory is displayed in lines of 8 32-bit values each, with
 * addresses and hex values formatted for easy reading.
 *
 * @param Type Log message type/severity level
 * @param Memory Starting address of memory to dump
 * @param Size Number of bytes to dump
 */
void KernelLogMem(U32 Type, LINEAR Memory, U32 Size) {
    U32* Pointer = (U32*)Memory;
    U32 LineCount = Size / (sizeof(U32) * 8);

    if (LineCount < 1) LineCount = 1;

    for (U32 Line = 0; Line < LineCount; Line++) {
        if (IsValidMemory((LINEAR)Pointer) == FALSE) return;
        if (IsValidMemory((LINEAR)(Pointer + 31)) == FALSE) return;

        KernelLogText(
            Type, TEXT("%08x : %08x %08x %08x %08x %08x %08x %08x %08x"), Pointer, Pointer[0], Pointer[1], Pointer[2],
            Pointer[3], Pointer[4], Pointer[5], Pointer[6], Pointer[7]);
        Pointer += 4;
    }
}

/************************************************************************/
