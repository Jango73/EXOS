
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

#include "Log.h"

#include "Clock.h"
#include "Console.h"
#include "Driver.h"
#include "Memory.h"
#include "process/Process.h"
#include "SerialPort.h"
#include "CoreString.h"
#include "Text.h"
#include "VarArg.h"

/************************************************************************/

#define KERNEL_LOG_VER_MAJOR 1
#define KERNEL_LOG_VER_MINOR 0

static UINT KernelLogDriverCommands(UINT Function, UINT Parameter);

DRIVER DATA_SECTION KernelLogDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_OTHER,
    .VersionMajor = KERNEL_LOG_VER_MAJOR,
    .VersionMinor = KERNEL_LOG_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "KernelLog",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = KernelLogDriverCommands};

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
    SAFE_USE(Text) {
        for (U32 Index = 0; Index < 0x1000; Index++) {
            if (Text[Index] == STR_NULL) break;
            KernelPrintChar(Text[Index]);
        }
    }
}

/************************************************************************/

/**
 * @brief Driver command handler for the kernel log subsystem.
 *
 * DF_LOAD initializes the kernel logger once; DF_UNLOAD only clears readiness.
 */
static UINT KernelLogDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((KernelLogDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RET_SUCCESS;
            }

            InitKernelLog();
            KernelLogDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RET_SUCCESS;

        case DF_UNLOAD:
            if ((KernelLogDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RET_SUCCESS;
            }

            KernelLogDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RET_SUCCESS;

        case DF_GETVERSION:
            return MAKE_VERSION(KERNEL_LOG_VER_MAJOR, KERNEL_LOG_VER_MINOR);
    }

    return DF_RET_NOTIMPL;
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

    UINT Time = GetSystemTime();
    StringPrintFormat(TimeBuffer, TEXT("T%u> "), (U32)Time);

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
