
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
#define KERNEL_LOG_TAG_FILTER_MAX_LENGTH 512

#ifndef KERNEL_LOG_DEFAULT_TAG_FILTER
#define KERNEL_LOG_DEFAULT_TAG_FILTER ""
#endif

static CSTR KernelLogDefaultTagFilter[] = KERNEL_LOG_DEFAULT_TAG_FILTER;
static STR DATA_SECTION KernelLogTagFilter[KERNEL_LOG_TAG_FILTER_MAX_LENGTH];

static UINT KernelLogDriverCommands(UINT Function, UINT Parameter);
static BOOL KernelLogIsTagSeparator(STR Char);
static BOOL KernelLogFilterContainsTag(LPCSTR Tag, U32 TagLength);
static BOOL KernelLogShouldEmit(LPCSTR Text);

DRIVER DATA_SECTION KernelLogDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_INIT,
    .VersionMajor = KERNEL_LOG_VER_MAJOR,
    .VersionMinor = KERNEL_LOG_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "KernelLog",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = KernelLogDriverCommands};

/************************************************************************/

/**
 * @brief Retrieves the kernel log driver descriptor.
 * @return Pointer to the kernel log driver.
 */
LPDRIVER KernelLogGetDriver(void) {
    return &KernelLogDriver;
}

/**
 * @brief Initializes the kernel logging system.
 *
 * Sets up the serial port used for kernel log output by resetting
 * the designated communication port.
 */
void InitKernelLog(void) {
    SerialReset(LOG_COM_INDEX);
    KernelLogSetTagFilter(KernelLogDefaultTagFilter);
}

/************************************************************************/

/**
 * @brief Configure tag-based filtering for kernel logs.
 *
 * TagFilter accepts a list of tags separated with comma, semicolon, pipe,
 * or spaces. Tags can be written with or without brackets.
 *
 * @param TagFilter Filter string, empty to disable filtering.
 */
void KernelLogSetTagFilter(LPCSTR TagFilter) {
    U32 Flags;

    SaveFlags(&Flags);
    FreezeScheduler();
    DisableInterrupts();

    if (StringEmpty(TagFilter)) {
        StringClear(KernelLogTagFilter);
    } else {
        StringCopyLimit(
            KernelLogTagFilter,
            TagFilter,
            KERNEL_LOG_TAG_FILTER_MAX_LENGTH);
    }

    UnfreezeScheduler();
    RestoreFlags(&Flags);
}

/************************************************************************/

/**
 * @brief Return the current log tag filter string.
 * @return Active tag filter string (empty means no filter).
 */
LPCSTR KernelLogGetTagFilter(void) {
    return KernelLogTagFilter;
}

/************************************************************************/

/**
 * @brief Check whether a character separates filter tags.
 * @param Char Character to evaluate.
 * @return TRUE when Char is a separator, FALSE otherwise.
 */
static BOOL KernelLogIsTagSeparator(STR Char) {
    return Char == ',' || Char == ';' || Char == '|' ||
           Char == ' ' || Char == '\t' || Char == '\n' || Char == '\r';
}

/************************************************************************/

/**
 * @brief Check whether a tag is present in the active filter list.
 * @param Tag Tag extracted from a log line (without brackets).
 * @param TagLength Tag length in characters.
 * @return TRUE when tag is allowed by filter, FALSE otherwise.
 */
static BOOL KernelLogFilterContainsTag(LPCSTR Tag, U32 TagLength) {
    U32 Index = 0;

    if (Tag == NULL || TagLength == 0) {
        return FALSE;
    }

    while (KernelLogTagFilter[Index] != STR_NULL) {
        U32 Start;
        U32 End;
        U32 TokenLength;
        U32 TokenIndex;
        BOOL Match;

        while (KernelLogTagFilter[Index] != STR_NULL &&
               KernelLogIsTagSeparator(KernelLogTagFilter[Index])) {
            Index++;
        }
        if (KernelLogTagFilter[Index] == STR_NULL) {
            break;
        }

        Start = Index;
        while (KernelLogTagFilter[Index] != STR_NULL &&
               !KernelLogIsTagSeparator(KernelLogTagFilter[Index])) {
            Index++;
        }
        End = Index;

        if (KernelLogTagFilter[Start] == '[') {
            Start++;
        }
        if (End > Start && KernelLogTagFilter[End - 1] == ']') {
            End--;
        }

        TokenLength = End - Start;
        if (TokenLength != TagLength) {
            continue;
        }

        Match = TRUE;
        for (TokenIndex = 0; TokenIndex < TokenLength; TokenIndex++) {
            if (KernelLogTagFilter[Start + TokenIndex] != Tag[TokenIndex]) {
                Match = FALSE;
                break;
            }
        }

        if (Match) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Determines if a formatted log line passes the active tag filter.
 * @param Text Fully formatted log line.
 * @return TRUE when line should be emitted, FALSE otherwise.
 */
static BOOL KernelLogShouldEmit(LPCSTR Text) {
    LPSTR OpenBracket;
    LPSTR CloseBracket;
    U32 TagLength;

    if (StringEmpty(KernelLogTagFilter)) {
        return TRUE;
    }

    if (StringEmpty(Text)) {
        return FALSE;
    }

    OpenBracket = StringFindChar(Text, '[');
    if (OpenBracket == NULL || OpenBracket[1] == STR_NULL) {
        return FALSE;
    }

    CloseBracket = StringFindChar(OpenBracket + 1, ']');
    if (CloseBracket == NULL) {
        return FALSE;
    }

    TagLength = (U32)(CloseBracket - (OpenBracket + 1));
    if (TagLength == 0) {
        return FALSE;
    }

    return KernelLogFilterContainsTag(OpenBracket + 1, TagLength);
}

/************************************************************************/

static void KernelPrintChar(STR Char) {
#if DEBUG_SPLIT == 1
    if (ConsoleIsDebugSplitEnabled() == TRUE &&
        ConsoleIsFramebufferMappingInProgress() == FALSE) {
        ConsolePrintDebugChar(Char);
        SerialOut(LOG_COM_INDEX, Char);
        return;
    }
#endif

    SerialOut(LOG_COM_INDEX, Char);
}

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
                return DF_RETURN_SUCCESS;
            }

            InitKernelLog();
            KernelLogDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((KernelLogDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            KernelLogDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(KERNEL_LOG_VER_MAJOR, KERNEL_LOG_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
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

    if (!KernelLogShouldEmit(TextBuffer)) {
        UnfreezeScheduler();
        RestoreFlags(&Flags);
        return;
    }

    switch (Type) {
        case LOG_DEBUG: {
            KernelPrintString(TimeBuffer);
            KernelPrintString(TEXT("DEBUG > "));
            KernelPrintString(TextBuffer);
            KernelPrintString(Text_NewLine);
        } break;

        case LOG_TEST: {
            KernelPrintString(TimeBuffer);
            KernelPrintString(TEXT("TEST > "));
            KernelPrintString(TextBuffer);
            KernelPrintString(Text_NewLine);
        } break;

        default:
        case LOG_VERBOSE: {
            KernelPrintString(TimeBuffer);
            KernelPrintString(TEXT("VERBOSE > "));
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
