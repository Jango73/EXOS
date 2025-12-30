
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


    Script Exposure Helpers - Task

\************************************************************************/

#include "Exposed.h"

#include "KernelData.h"
#include "Mutex.h"
#include "process/Process.h"
#include "process/Task.h"

/************************************************************************/

#define EXPOSE_ACCESS_TASK_KERNEL (EXPOSE_ACCESS_ADMIN | EXPOSE_ACCESS_KERNEL)

/************************************************************************/

static UINT ProcessTaskGetCount(LPPROCESS Process) {
    UINT Count = 0;

    if (Process == NULL) {
        return 0;
    }

    LockMutex(MUTEX_TASK, INFINITY);

    LPLIST TaskList = GetTaskList();
    if (TaskList != NULL) {
        for (LPLISTNODE Node = TaskList->First; Node; Node = Node->Next) {
            LPTASK Task = (LPTASK)Node;
            SAFE_USE_VALID_ID(Task, KOID_TASK) {
                if (Task->Process == Process) {
                    Count++;
                }
            }
        }
    }

    UnlockMutex(MUTEX_TASK);

    return Count;
}

/************************************************************************/

static LPTASK ProcessTaskGetByIndex(LPPROCESS Process, UINT Index) {
    LPTASK Found = NULL;
    UINT MatchIndex = 0;

    if (Process == NULL) {
        return NULL;
    }

    LockMutex(MUTEX_TASK, INFINITY);

    LPLIST TaskList = GetTaskList();
    if (TaskList != NULL) {
        for (LPLISTNODE Node = TaskList->First; Node; Node = Node->Next) {
            LPTASK Task = (LPTASK)Node;
            SAFE_USE_VALID_ID(Task, KOID_TASK) {
                if (Task->Process != Process) {
                    continue;
                }

                if (MatchIndex == Index) {
                    Found = Task;
                    break;
                }

                MatchIndex++;
            }
        }
    }

    UnlockMutex(MUTEX_TASK);

    return Found;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from a stack exposed to the script engine.
 * @param Context Host callback context (unused for stack exposure)
 * @param Parent Handle to the stack instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR StackGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    EXPOSE_PROPERTY_GUARD();

    EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_TASK_KERNEL, (LPPROCESS)Context);

    LPSTACK Stack = (LPSTACK)Parent;
    if (Stack == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("base", Stack->Base);
    EXPOSE_BIND_INTEGER("size", Stack->Size);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from architecture task data exposed to the script engine.
 * @param Context Host callback context (unused for task exposure)
 * @param Parent Handle to the architecture task data instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR ArchitectureTaskDataGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    EXPOSE_PROPERTY_GUARD();

    EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_TASK_KERNEL, (LPPROCESS)Context);

    LPARCH_TASK_DATA Architecture = (LPARCH_TASK_DATA)Parent;
    if (Architecture == NULL) {
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    EXPOSE_BIND_INTEGER("context", (UINT)(LPVOID)&Architecture->Context);
    EXPOSE_BIND_HOST_HANDLE("stack", &Architecture->Stack, &StackDescriptor, Context);
    EXPOSE_BIND_HOST_HANDLE("system_stack", &Architecture->SystemStack, &StackDescriptor, Context);

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from a task exposed to the script engine.
 * @param Context Host callback context (unused for task exposure)
 * @param Parent Handle to the task instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR TaskGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPTASK Task = (LPTASK)Parent;
    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        BOOL IsKernelOrAdmin = ExposeIsKernelCaller() || ExposeIsAdminCaller();
        LPPROCESS Caller = ExposeGetCallerProcess();
        BOOL IsOwnerProcess = ExposeIsOwnerProcess(Caller, Task->Process);

        if (Task->Process == &KernelProcess && IsKernelOrAdmin == FALSE) {
            return SCRIPT_ERROR_UNAUTHORIZED;
        }

        EXPOSE_BIND_INTEGER("handle", (UINT)(LPVOID)Task);
        EXPOSE_BIND_HOST_HANDLE("process", Task->Process, &ProcessDescriptor, NULL);
        EXPOSE_BIND_STRING("name", Task->Name);
        EXPOSE_BIND_INTEGER("type", Task->Type);
        EXPOSE_BIND_INTEGER("status", Task->Status);
        EXPOSE_BIND_INTEGER("priority", Task->Priority);
        EXPOSE_BIND_INTEGER("exit_code", Task->ExitCode);
        EXPOSE_BIND_INTEGER("flags", Task->Flags);

        if (STRINGS_EQUAL_NO_CASE(Property, TEXT("function"))) {
            if (IsKernelOrAdmin == FALSE && IsOwnerProcess == FALSE) {
                return SCRIPT_ERROR_UNAUTHORIZED;
            }
            OutValue->Type = SCRIPT_VAR_INTEGER;
            OutValue->Value.Integer = (I32)(UINT)(LPVOID)Task->Function;
            return SCRIPT_OK;
        }

        if (STRINGS_EQUAL_NO_CASE(Property, TEXT("parameter"))) {
            if (IsKernelOrAdmin == FALSE && IsOwnerProcess == FALSE) {
                return SCRIPT_ERROR_UNAUTHORIZED;
            }
            OutValue->Type = SCRIPT_VAR_INTEGER;
            OutValue->Value.Integer = (I32)(UINT)(LPVOID)Task->Parameter;
            return SCRIPT_OK;
        }

        if (STRINGS_EQUAL_NO_CASE(Property, TEXT("architecture"))) {
            if (IsKernelOrAdmin == FALSE) {
                return SCRIPT_ERROR_UNAUTHORIZED;
            }
            OutValue->Type = SCRIPT_VAR_HOST_HANDLE;
            OutValue->Value.HostHandle = &Task->Arch;
            OutValue->HostDescriptor = &ArchitectureTaskDataDescriptor;
            OutValue->HostContext = Task->Process;
            OutValue->OwnsValue = FALSE;
            return SCRIPT_OK;
        }

        if (STRINGS_EQUAL_NO_CASE(Property, TEXT("stack"))) {
            if (IsKernelOrAdmin == FALSE) {
                return SCRIPT_ERROR_UNAUTHORIZED;
            }
            OutValue->Type = SCRIPT_VAR_HOST_HANDLE;
            OutValue->Value.HostHandle = &Task->Arch.Stack;
            OutValue->HostDescriptor = &StackDescriptor;
            OutValue->HostContext = Task->Process;
            OutValue->OwnsValue = FALSE;
            return SCRIPT_OK;
        }

        if (STRINGS_EQUAL_NO_CASE(Property, TEXT("system_stack"))) {
            if (IsKernelOrAdmin == FALSE) {
                return SCRIPT_ERROR_UNAUTHORIZED;
            }
            OutValue->Type = SCRIPT_VAR_HOST_HANDLE;
            OutValue->Value.HostHandle = &Task->Arch.SystemStack;
            OutValue->HostDescriptor = &StackDescriptor;
            OutValue->HostContext = Task->Process;
            OutValue->OwnsValue = FALSE;
            return SCRIPT_OK;
        }

        if (STRINGS_EQUAL_NO_CASE(Property, TEXT("wake_up_time")) ||
            STRINGS_EQUAL_NO_CASE(Property, TEXT("message_queue")) ||
            STRINGS_EQUAL_NO_CASE(Property, TEXT("mutex"))) {
            if (IsKernelOrAdmin == FALSE) {
                return SCRIPT_ERROR_UNAUTHORIZED;
            }
        }

        EXPOSE_BIND_INTEGER("wake_up_time", Task->WakeUpTime);
        EXPOSE_BIND_INTEGER("message_queue", (UINT)(LPVOID)&Task->MessageQueue);
        EXPOSE_BIND_INTEGER("mutex", (UINT)(LPVOID)&Task->Mutex);

        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a property value from the exposed process task array.
 * @param Context Host callback context (unused for task exposure)
 * @param Parent Handle to the process instance requested by the script
 * @param Property Property name requested by the script
 * @param OutValue Output holder for the property value
 * @return SCRIPT_OK when the property exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR TaskArrayGetProperty(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    LPCSTR Property,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_PROPERTY_GUARD();

    LPPROCESS Process = (LPPROCESS)Parent;
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_TASK_KERNEL, Process);
        EXPOSE_BIND_INTEGER("count", ProcessTaskGetCount(Process));
        return SCRIPT_ERROR_UNDEFINED_VAR;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

/**
 * @brief Retrieve a task from the exposed process task array.
 * @param Context Host callback context (unused for task exposure)
 * @param Parent Handle to the process instance requested by the script
 * @param Index Array index requested by the script
 * @param OutValue Output holder for the resulting task handle
 * @return SCRIPT_OK when the task exists, SCRIPT_ERROR_UNDEFINED_VAR otherwise
 */
SCRIPT_ERROR TaskArrayGetElement(
    LPVOID Context,
    SCRIPT_HOST_HANDLE Parent,
    U32 Index,
    LPSCRIPT_VALUE OutValue) {

    UNUSED(Context);

    EXPOSE_ARRAY_GUARD();

    LPPROCESS Process = (LPPROCESS)Parent;
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        EXPOSE_REQUIRE_ACCESS(EXPOSE_ACCESS_TASK_KERNEL, Process);
        LPTASK Task = ProcessTaskGetByIndex(Process, Index);
        if (Task == NULL) {
            return SCRIPT_ERROR_UNDEFINED_VAR;
        }

        EXPOSE_SET_HOST_HANDLE(Task, &TaskDescriptor, NULL, FALSE);
        return SCRIPT_OK;
    }

    return SCRIPT_ERROR_UNDEFINED_VAR;
}

/************************************************************************/

const SCRIPT_HOST_DESCRIPTOR TaskDescriptor = {
    TaskGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR TaskArrayDescriptor = {
    TaskArrayGetProperty,
    TaskArrayGetElement,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR ArchitectureTaskDataDescriptor = {
    ArchitectureTaskDataGetProperty,
    NULL,
    NULL,
    NULL
};

const SCRIPT_HOST_DESCRIPTOR StackDescriptor = {
    StackGetProperty,
    NULL,
    NULL,
    NULL
};

/************************************************************************/
