
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

#include "process/Process.h"

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

    LPPROCESS Process = (LPPROCESS)Parent;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        EXPOSE_PROPERTY_GUARD();

        EXPOSE_BIND_INTEGER("status", Process->Status);
        EXPOSE_BIND_INTEGER("flags", Process->Flags);
        EXPOSE_BIND_INTEGER("exit_code", Process->ExitCode);
        EXPOSE_BIND_STRING("file_name", Process->FileName);
        EXPOSE_BIND_STRING("command_line", Process->CommandLine);
        EXPOSE_BIND_STRING("work_folder", Process->WorkFolder);
        EXPOSE_BIND_HOST_HANDLE("task", Process, &TaskArrayDescriptor, NULL);

        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed kernel process array.
 * @param Context Host callback context (unused for process exposure)
 * @param Parent Handle to the process list exposed by the kernel
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR ProcessArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPLIST ProcessList = (LPLIST)Parent;
    if (ProcessList == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("count", ListGetSize(ProcessList));

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

EXPOSE_LIST_ARRAY_GET_ELEMENT(
    ProcessArrayGetElement,
    LPPROCESS,
    SAFE_USE_VALID_ID,
    KOID_PROCESS,
    &ProcessDescriptor)

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR ProcessDescriptor = {
    ProcessGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR ProcessArrayDescriptor = {
    ProcessArrayGetProperty,
    ProcessArrayGetElement,
    NULL,
    NULL
};

/************************************************************************/
