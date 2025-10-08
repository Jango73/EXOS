
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


    Script Exposure Helpers

\************************************************************************/

#include "Exposed.h"

#include "Base.h"
#include "List.h"
#include "Memory.h"
#include "Process.h"
#include "String.h"

/************************************************************************/

#define PROCESS_BIND_INTEGER(ExposedName, PropertyName) \
    do { \
        if (STRINGS_EQUAL_NO_CASE(Property, TEXT(#ExposedName))) { \
            OutValue->Type = SCRIPT_VAR_INTEGER; \
            OutValue->Value.Integer = (I32)Process->PropertyName; \
            return SCRIPT_OK; \
        } \
    } while (0)

#define PROCESS_BIND_STRING(ExposedName, PropertyName) \
    do { \
        if (STRINGS_EQUAL_NO_CASE(Property, TEXT(#ExposedName))) { \
            OutValue->Type = SCRIPT_VAR_STRING; \
            OutValue->Value.String = Process->PropertyName; \
            OutValue->OwnsValue = FALSE; \
            return SCRIPT_OK; \
        } \
    } while (0)

/************************************************************************/

/**
 * @brief Retrieve a property value from a process exposed to the script engine.
 * @param Context Host callback context (unused for process exposure)
 * @param Parent  Handle to the process instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR ProcessGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    if (OutValue == NULL || Parent == NULL || Property == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPPROCESS Process = (LPPROCESS)Parent;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        MemorySet(OutValue, 0, sizeof(SCRIPT_VALUE));

        PROCESS_BIND_INTEGER(status, Status);
        PROCESS_BIND_INTEGER(flags, Flags);
        PROCESS_BIND_INTEGER(exitCode, ExitCode);
        PROCESS_BIND_STRING(fileName, FileName);
        PROCESS_BIND_STRING(commandLine, CommandLine);
        PROCESS_BIND_STRING(workFolder, WorkFolder);

        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a process from the exposed kernel process array.
 * @param Context Host callback context (unused for process exposure)
 * @param Parent Handle to the process list exposed by the kernel
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting process handle
 * @return SCRIPT_OK when the process exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR ProcessArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    if (OutValue == NULL || Parent == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPLIST ProcessList = (LPLIST)Parent;
    if (ProcessList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    if (Index >= ListGetSize(ProcessList)) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    LPPROCESS Process = (LPPROCESS)ListGetItem(ProcessList, Index);

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        MemorySet(OutValue, 0, sizeof(SCRIPT_VALUE));
        OutValue->Type = SCRIPT_VAR_HOST_HANDLE;
        OutValue->Value.HostHandle = Process;
        OutValue->HostDescriptor = &ProcessDescriptor;
        OutValue->HostContext = NULL;
        OutValue->OwnsValue = FALSE;
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR ProcessDescriptor = {
    ProcessGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR ProcessArrayDescriptor = {
    NULL,
    ProcessArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/

#undef PROCESS_BIND_INTEGER
#undef PROCESS_BIND_STRING

/************************************************************************/
