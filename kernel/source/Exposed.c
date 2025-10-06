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


    Shell Script Host Exposure Helpers

\************************************************************************/

#include "ShellExposed.h"

#include "Base.h"
#include "List.h"
#include "Memory.h"
#include "Process.h"
#include "String.h"

/************************************************************************/

#define SHELL_PROCESS_BIND_INTEGER(PropertyName) \
    do { \
        if (StringCompareNC(Property, TEXT(#PropertyName)) == 0) { \
            OutValue->Type = SCRIPT_VAR_INTEGER; \
            OutValue->Value.Integer = (I32)Process->PropertyName; \
            return SCRIPT_OK; \
        } \
    } while (0)
#define SHELL_PROCESS_BIND_STRING(PropertyName) \
    do { \
        if (StringCompareNC(Property, TEXT(#PropertyName)) == 0) { \
            OutValue->Type = SCRIPT_VAR_STRING; \
            OutValue->Value.String = Process->PropertyName; \
            OutValue->OwnsValue = FALSE; \
            return SCRIPT_OK; \
        } \
    } while (0)

/************************************************************************/

SCRIPT_ERROR ShellProcessGetProperty(
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

        SHELL_PROCESS_BIND_INTEGER(Status);
        SHELL_PROCESS_BIND_INTEGER(Flags);
        SHELL_PROCESS_BIND_INTEGER(ExitCode);
        SHELL_PROCESS_BIND_STRING(FileName);
        SHELL_PROCESS_BIND_STRING(CommandLine);
        SHELL_PROCESS_BIND_STRING(WorkFolder);

        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

SCRIPT_ERROR ShellProcessArrayGetElement(
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
        OutValue->HostDescriptor = &ShellProcessDescriptor;
        OutValue->HostContext = NULL;
        OutValue->OwnsValue = FALSE;
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR ShellProcessDescriptor = {
    ShellProcessGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR ShellProcessArrayDescriptor = {
    NULL,
    ShellProcessArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/

#undef SHELL_PROCESS_BIND_INTEGER
#undef SHELL_PROCESS_BIND_STRING

/************************************************************************/
