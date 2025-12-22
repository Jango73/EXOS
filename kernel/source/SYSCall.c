
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


    System call

\************************************************************************/

#include "Base.h"
#include "Clock.h"
#include "Console.h"
#include "GFX.h"
#include "File.h"
#include "Heap.h"
#include "utils/Helpers.h"
#include "ID.h"
#include "Kernel.h"
#include "drivers/Keyboard.h"
#include "Log.h"
#include "Memory.h"
#include "Mouse.h"
#include "process/Process.h"
#include "process/Schedule.h"
#include "User.h"
#include "UserAccount.h"
#include "UserSession.h"
#include "Security.h"
#include "Socket.h"
#include "SYSCall.h"

extern BOOL ReleaseWindowGC(HANDLE Handle);

/**
 * @brief Emit a debug string originating from user space.
 *
 * Validates the provided linear address before forwarding the message to
 * the kernel logger through DEBUG().
 *
 * @param Parameter Linear address of a null-terminated string.
 * @return UINT Always returns 0.
 */
UINT SysCall_Debug(UINT Parameter) {
    SAFE_USE_VALID((LPCSTR)Parameter) { DEBUG((LPCSTR)Parameter); }
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the kernel major version encoded as 16.16 fixed point.
 *
 * @param Parameter Reserved.
 * @return UINT Version number where major is stored in the high word.
 */
UINT SysCall_GetVersion(UINT Parameter) {
    UNUSED(Parameter);
    return ((UINT)1 << 16);
}

/************************************************************************/

/**
 * @brief Collect global system information for the caller.
 *
 * Ensures the SYSTEMINFO buffer is accessible, populates it, and keeps
 * handles untouched because the structure only contains plain data.
 *
 * @param Parameter Pointer to a SYSTEMINFO structure.
 * @return UINT TRUE on success, FALSE when the buffer is invalid.
 */
UINT SysCall_GetSystemInfo(UINT Parameter) {
    LPSYSTEMINFO Info = (LPSYSTEMINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SYSTEMINFO) {
        Info->TotalPhysicalMemory = KernelStartup.MemorySize;
        Info->PhysicalMemoryUsed = GetPhysicalMemoryUsed();
        Info->PhysicalMemoryAvail = KernelStartup.MemorySize - Info->PhysicalMemoryUsed;
        Info->TotalSwapMemory = 0;
        Info->SwapMemoryUsed = 0;
        Info->SwapMemoryAvail = 0;
        Info->TotalMemoryAvail = Info->TotalPhysicalMemory + Info->TotalSwapMemory;
        Info->PageSize = PAGE_SIZE;
        Info->TotalPhysicalPages = KernelStartup.PageCount;
        Info->MinimumLinearAddress = VMA_USER;
        Info->MaximumLinearAddress = VMA_KERNEL - 1;
        LPLIST ProcessList = GetProcessList();
        LPLIST TaskList = GetTaskList();
        Info->NumProcesses = ProcessList != NULL ? ProcessList->NumItems : 0;
        Info->NumTasks = TaskList != NULL ? TaskList->NumItems : 0;

        LPUSERACCOUNT User = GetCurrentUser();

        StringCopy(Info->UserName, User != NULL ? User->UserName : TEXT(""));
        StringCopy(Info->KeyboardLayout, GetKeyboardCode());

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Retrieve the thread-local last error value.
 *
 * Placeholder implementation until per-thread error codes are wired.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_GetLastError(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Record a thread-local last error value.
 *
 * Currently ignored; present for ABI compatibility.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_SetLastError(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the current system tick count.
 *
 * @param Parameter Reserved.
 * @return UINT Value returned by GetSystemTime().
 */
UINT SysCall_GetSystemTime(UINT Parameter) {
    UNUSED(Parameter);
    return GetSystemTime();
}

/************************************************************************/

/**
 * @brief Retrieve the current local time.
 *
 * @param Parameter Linear address of a DATETIME structure to fill.
 * @return UINT TRUE on success, FALSE if the buffer is invalid.
 */
UINT SysCall_GetLocalTime(UINT Parameter) {
    LPDATETIME Time = (LPDATETIME)Parameter;
    SAFE_USE_VALID(Time) { return GetLocalTime(Time); }
    return FALSE;
}

/************************************************************************/

/**
 * @brief Update the system local time.
 *
 * @param Parameter Linear address of a DATETIME structure with the new time.
 * @return UINT TRUE on success, FALSE otherwise.
 */
UINT SysCall_SetLocalTime(UINT Parameter) {
    LPDATETIME Time = (LPDATETIME)Parameter;
    SAFE_USE_VALID(Time) { return SetLocalTime(Time); }
    return FALSE;
}

/************************************************************************/

/**
 * @brief Delete a kernel object referenced by a user handle.
 *
 * Resolves the provided handle, invokes the object-specific destructor,
 * then releases the handle from the mapping when the deletion succeeds.
 *
 * @param Parameter Handle referencing the object to delete.
 * @return UINT Result code from the underlying delete operation.
 */
UINT SysCall_DeleteObject(UINT Parameter) {
    LINEAR ObjectAddress = HandleToPointer(Parameter);

    SAFE_USE(ObjectAddress) {
        LPOBJECT Object = (LPOBJECT)ObjectAddress;
        UINT Result = 0;

        SAFE_USE_VALID(Object) {
            switch (Object->TypeID) {
                case KOID_FILE:
                    Result = (UINT)CloseFile((LPFILE)Object);
                    break;
                case KOID_DESKTOP:
                    Result = (UINT)DeleteDesktop((LPDESKTOP)Object);
                    break;
                case KOID_WINDOW:
                    Result = (UINT)DeleteWindow((LPWINDOW)Object);
                    break;
                default:
                    WARNING(TEXT("[SysCall_DeleteObject] Unsupported object type=%u handle=%u"),
                            Object->TypeID, Parameter);
                    Result = 0;
                    break;
            }
        } else {
            WARNING(TEXT("[SysCall_DeleteObject] Invalid object pointer handle=%u"), Parameter);
        }

        if (Result != 0) {
            ReleaseHandle(Parameter);
        }

        return Result;
    }

    WARNING(TEXT("[SysCall_DeleteObject] Unknown handle=%u"), Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Create a process and return handles for the new process and task.
 *
 * Invokes the kernel process creation logic, replaces returned pointers
 * with user-visible handles, and tears everything down if handle export
 * fails.
 *
 * @param Parameter Pointer to PROCESSINFO structure supplied by userland.
 * @return UINT Result code from CreateProcess or an error code.
 */
UINT SysCall_CreateProcess(UINT Parameter) {
    LPPROCESSINFO Info = (LPPROCESSINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, PROCESSINFO) {
        UINT Result = (UINT)CreateProcess(Info);

        if (Result) {
            Info->Process = PointerToHandle((LINEAR)Info->Process);
            Info->Task = PointerToHandle((LINEAR)Info->Task);
        }

        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Terminate a process referenced by a handle.
 *
 * @param Parameter Handle identifying the process to terminate, or 0 for the current process.
 * @return UINT Always returns 0.
 */
UINT SysCall_KillProcess(UINT Parameter) {
    LINEAR ProcessPointer = Parameter ? HandleToPointer(Parameter) : (LINEAR)GetCurrentProcess();
    LPPROCESS Process = (LPPROCESS)ProcessPointer;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        KillProcess(Process);
        if (Parameter) ReleaseHandle(Parameter);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve information about a process, using handles for inputs.
 *
 * Converts an optional handle in PROCESSINFO into a kernel pointer before
 * filling the structure.
 *
 * @param Parameter Pointer to PROCESSINFO provided by userland.
 * @return UINT DF_RET_SUCCESS on success, DF_RET_GENERIC on error.
 */
UINT SysCall_GetProcessInfo(UINT Parameter) {
    LPPROCESSINFO Info = (LPPROCESSINFO)Parameter;
    LPPROCESS CurrentProcess;

    DEBUG(TEXT("[SysCall_GetProcessInfo] Enter, Parameter=%x"), Parameter);

    SAFE_USE_INPUT_POINTER(Info, PROCESSINFO) {
        CurrentProcess = Info->Process ? (LPPROCESS)HandleToPointer(Info->Process) : GetCurrentProcess();

        SAFE_USE_VALID_ID(CurrentProcess, KOID_PROCESS) {
            DEBUG(TEXT("[SysCall_GetProcessInfo] Info->CommandLine = %s"), Info->CommandLine);
            DEBUG(TEXT("[SysCall_GetProcessInfo] CurrentProcess=%p"), CurrentProcess);
            DEBUG(TEXT("[SysCall_GetProcessInfo] CurrentProcess->CommandLine = %s"), CurrentProcess->CommandLine);

            // Copy the command line and work folder within buffer limits
            StringCopyLimit(Info->CommandLine, CurrentProcess->CommandLine, MAX_PATH_NAME);
            StringCopyLimit(Info->WorkFolder, CurrentProcess->WorkFolder, MAX_PATH_NAME);

            return DF_RET_SUCCESS;
        }
    }

    return DF_RET_GENERIC;
}

/************************************************************************/

/**
 * @brief Create a task for the current process and return its handle.
 *
 * @param Parameter Pointer to TASKINFO structure provided by userland.
 * @return UINT Handle to the created task, or 0 on failure.
 */
UINT SysCall_CreateTask(UINT Parameter) {
    LPTASKINFO TaskInfo = (LPTASKINFO)Parameter;

    SAFE_USE_INPUT_POINTER(TaskInfo, TASKINFO) {
        LPTASK Task = CreateTask(GetCurrentProcess(), TaskInfo);
        return PointerToHandle((LINEAR)Task);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Terminate a task referenced by a handle.
 *
 * Resolves the provided task handle (or uses the current task when the handle
 * is zero) and forwards the request to KillTask. Releases the handle upon
 * successful termination.
 *
 * @param Parameter Handle identifying the task to terminate, or 0 for the current task.
 * @return UINT Non-zero on success, zero on failure.
 */
UINT SysCall_KillTask(UINT Parameter) {
    DEBUG(TEXT("[SysCall_KillTask] Enter, Parameter=%x"), Parameter);

    LINEAR TaskPointer = Parameter ? HandleToPointer(Parameter) : (LINEAR)GetCurrentTask();
    LPTASK Task = (LPTASK)TaskPointer;

    UINT Result = 0;

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        Result = (UINT)KillTask(Task);
        if (Parameter && Result) ReleaseHandle(Parameter);
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Terminate the current task with the provided exit code.
 *
 * Converts the running task pointer into a verified kernel object prior to
 * delegating to KillTask().
 *
 * @param Parameter Exit code stored in the task object.
 * @return UINT Result of KillTask().
 */
UINT SysCall_Exit(UINT Parameter) {
    DEBUG(TEXT("[SysCall_Exit] Enter, Parameter=%x"), Parameter);

    LPTASK Task = GetCurrentTask();
    UINT ReturnValue = 0;

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        SetTaskExitCode(Task, Parameter);
        ReturnValue = KillTask(Task);
    }

    DEBUG(TEXT("[SysCall_Exit] Exit"));

    return ReturnValue;
}

/************************************************************************/

/**
 * @brief Suspend execution of a task identified by handle.
 *
 * Currently not implemented; reserved for future scheduler updates.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_SuspendTask(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Resume execution of a suspended task.
 *
 * Currently not implemented; retained for ABI stability.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_ResumeTask(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Block the current task for the specified duration in milliseconds.
 *
 * @param Parameter Sleep duration in milliseconds.
 * @return UINT TRUE when the sleep request was queued.
 */
UINT SysCall_Sleep(UINT Parameter) {
    Sleep(Parameter);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Wait for one or more kernel objects referenced by handles.
 *
 * Resolves every handle in WAITINFO into a kernel pointer before delegating
 * to Wait(), restoring the original handles afterwards.
 *
 * @param Parameter Pointer to WAITINFO structure provided by userland.
 * @return UINT Wait() return code or WAIT_INVALID_PARAMETER on invalid handle.
 */
UINT SysCall_Wait(UINT Parameter) {
    LPWAITINFO WaitInfo = (LPWAITINFO)Parameter;

    SAFE_USE_INPUT_POINTER(WaitInfo, WAITINFO) {
        if (WaitInfo->Count == 0 || WaitInfo->Count > WAITINFO_MAX_OBJECTS) {
            return WAIT_INVALID_PARAMETER;
        }

        HANDLE OriginalHandles[WAITINFO_MAX_OBJECTS];

        for (UINT Index = 0; Index < WaitInfo->Count; Index++) {
            OriginalHandles[Index] = WaitInfo->Objects[Index];
            WaitInfo->Objects[Index] = (HANDLE)HandleToPointer(WaitInfo->Objects[Index]);

            if (WaitInfo->Objects[Index] == NULL) {
                for (UINT Restore = 0; Restore <= Index; Restore++) {
                    WaitInfo->Objects[Restore] = OriginalHandles[Restore];
                }
                return WAIT_INVALID_PARAMETER;
            }
        }

        UINT Result = Wait(WaitInfo);

        for (UINT Index = 0; Index < WaitInfo->Count; Index++) {
            WaitInfo->Objects[Index] = OriginalHandles[Index];
        }

        return Result;
    }

    return WAIT_INVALID_PARAMETER;
}

/************************************************************************/

/**
 * @brief Post an asynchronous message to a task or window handle.
 *
 * Resolves the target handle to its kernel pointer and forwards the request
 * to PostMessage without altering the original ABI structure.
 *
 * @param Parameter Pointer to MESSAGEINFO provided by userland.
 * @return UINT Non-zero on success, zero on failure.
 */
UINT SysCall_PostMessage(UINT Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Message, MESSAGEINFO) {
        LINEAR TargetPointer = HandleToPointer(Message->Target);

        if (Message->Target == 0) {
            return (UINT)PostMessage(NULL, Message->Message, Message->Param1, Message->Param2);
        }

        SAFE_USE_VALID((LPVOID)TargetPointer) {
            return (UINT)PostMessage((HANDLE)TargetPointer, Message->Message, Message->Param1, Message->Param2);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Send a synchronous message to a task or window handle.
 *
 * Resolves the target handle to its kernel pointer and dispatches the
 * message via SendMessage.
 *
 * @param Parameter Pointer to MESSAGEINFO supplied by userland.
 * @return UINT Non-zero on success, zero on failure.
 */
UINT SysCall_SendMessage(UINT Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Message, MESSAGEINFO) {
        LINEAR TargetPointer = HandleToPointer(Message->Target);

        if (Message->Target == 0) {
            return (UINT)SendMessage(NULL, Message->Message, Message->Param1, Message->Param2);
        }

        SAFE_USE_VALID((LPVOID)TargetPointer) {
            return (UINT)SendMessage((HANDLE)TargetPointer, Message->Message, Message->Param1, Message->Param2);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Peek at the message queue without removing entries.
 *
 * Currently unimplemented; reserved for future handle-aware implementation.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_PeekMessage(UINT Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Message, MESSAGEINFO) {
        HANDLE Filter = Message->Target;
        Message->Target = (HANDLE)HandleToPointer(Filter);

        if (Message->Target == NULL && Filter != 0) {
            Message->Target = Filter;
            return 0;
        }

        UINT Result = (UINT)PeekMessage(Message);
        Message->Target = PointerToHandle((LINEAR)Message->Target);
        if (Message->Target == 0) {
            Message->Target = Filter;
        }

        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the next message, translating handles as needed.
 *
 * Accepts an optional handle filter in MESSAGEINFO.Target, resolves it to a
 * kernel pointer before invoking GetMessage(), then converts the returned
 * pointer back into a handle.
 *
 * @param Parameter Pointer to MESSAGEINFO supplied by userland.
 * @return UINT Non-zero on success, zero on failure or invalid handle.
 */
UINT SysCall_GetMessage(UINT Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Message, MESSAGEINFO) {
        HANDLE Filter = Message->Target;
        Message->Target = (HANDLE)HandleToPointer(Filter);

        if (Message->Target == NULL && Filter != 0) {
            Message->Target = Filter;
            return 0;
        }

        UINT Result = (UINT)GetMessage(Message);
        Message->Target = PointerToHandle((LINEAR)Message->Target);
        if (Message->Target == 0) {
            Message->Target = Filter;
        }

        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Dispatch a message to its target window or task handle.
 *
 * Converts the target handle into a kernel pointer for the duration of the
 * dispatch, restoring the handle afterwards.
 *
 * @param Parameter Pointer to MESSAGEINFO provided by userland.
 * @return UINT Non-zero on success, zero on failure.
 */
UINT SysCall_DispatchMessage(UINT Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Message, MESSAGEINFO) {
        HANDLE Original = Message->Target;
        Message->Target = (HANDLE)HandleToPointer(Original);

        if (Message->Target == NULL && Original != 0) {
            Message->Target = Original;
            return 0;
        }

        UINT Result = (UINT)DispatchMessage(Message);

        Message->Target = Original;
        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Create a kernel mutex and return a handle to it.
 *
 * @param Parameter Reserved.
 * @return UINT Handle referencing the newly created mutex, or 0 on failure.
 */
UINT SysCall_CreateMutex(UINT Parameter) {
    UNUSED(Parameter);

    LPMUTEX Mutex = CreateMutex();
    HANDLE Handle = PointerToHandle((LINEAR)Mutex);

    if (Handle == 0) {
        DeleteMutex(Mutex);
    }

    return Handle;
}

/************************************************************************/

/**
 * @brief Delete a mutex referenced by a handle.
 *
 * @param Parameter Handle of the mutex to delete.
 * @return UINT Non-zero on success, zero on failure.
 */
UINT SysCall_DeleteMutex(UINT Parameter) {
    LINEAR MutexPointer = HandleToPointer(Parameter);
    LPMUTEX Mutex = (LPMUTEX)MutexPointer;
    UINT Result = 0;

    SAFE_USE_VALID_ID(Mutex, KOID_MUTEX) {
        Result = (UINT)DeleteMutex(Mutex);
        if (Parameter && Result) ReleaseHandle(Parameter);
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Lock a mutex identified by a handle.
 *
 * @param Parameter Pointer to MUTEXINFO structure containing the handle and timeout.
 * @return UINT Lock count on success, MAX_U32 on invalid handle.
 */
UINT SysCall_LockMutex(UINT Parameter) {
    LPMUTEXINFO Info = (LPMUTEXINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, MUTEXINFO) {
        LINEAR MutexPointer = HandleToPointer(Info->Mutex);
        LPMUTEX Mutex = (LPMUTEX)MutexPointer;

        SAFE_USE_VALID_ID(Mutex, KOID_MUTEX) { return LockMutex(Mutex, Info->MilliSeconds); }
    }

    return (UINT)MAX_U32;
}

/************************************************************************/

/**
 * @brief Unlock a mutex identified by a handle.
 *
 * @param Parameter Pointer to MUTEXINFO structure containing the handle.
 * @return UINT Lock count on success, MAX_U32 on invalid handle.
 */
UINT SysCall_UnlockMutex(UINT Parameter) {
    LPMUTEXINFO Info = (LPMUTEXINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, MUTEXINFO) {
        LINEAR MutexPointer = HandleToPointer(Info->Mutex);
        LPMUTEX Mutex = (LPMUTEX)MutexPointer;

        SAFE_USE_VALID_ID(Mutex, KOID_MUTEX) { return UnlockMutex(Mutex); }
    }

    return (UINT)MAX_U32;
}

/************************************************************************/

/**
 * @brief Allocate a region of virtual memory with specified attributes.
 *
 * @param Parameter Pointer to ALLOCREGIONINFO structure.
 * @return UINT Operation result from AllocRegion or 0 on invalid input.
 */
UINT SysCall_AllocRegion(UINT Parameter) {
    LPALLOCREGIONINFO Info = (LPALLOCREGIONINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, ALLOCREGIONINFO) {
        return AllocRegion(Info->Base, Info->Target, Info->Size, Info->Flags, NULL);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Free a previously allocated virtual memory region.
 *
 * @param Parameter Pointer to ALLOCREGIONINFO describing the region.
 * @return UINT Operation result from FreeRegion or 0 on invalid input.
 */
UINT SysCall_FreeRegion(UINT Parameter) {
    LPALLOCREGIONINFO Info = (LPALLOCREGIONINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, ALLOCREGIONINFO) { return FreeRegion(Info->Base, Info->Size); }

    return 0;
}

/************************************************************************/

/**
 * @brief Test whether a linear address is valid in the current context.
 *
 * @param Parameter Linear address to test.
 * @return UINT Non-zero if valid, zero otherwise.
 */
/**
 * @brief Check whether a linear address is mapped in the caller context.
 *
 * @param Parameter Linear address to validate.
 * @return UINT TRUE if the address is valid, FALSE otherwise.
 */
UINT SysCall_IsMemoryValid(UINT Parameter) {
    return (UINT)IsValidMemory((LINEAR)Parameter);
}

/************************************************************************/

/**
 * @brief Retrieve the heap base for a process referenced by handle.
 *
 * @param Parameter Process handle or 0 for the current process.
 * @return UINT Linear address of the process heap, or 0 on failure.
 */
UINT SysCall_GetProcessHeap(UINT Parameter) {
    LINEAR ProcessPointer = Parameter ? HandleToPointer(Parameter) : 0;
    LPPROCESS Process = (LPPROCESS)ProcessPointer;

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) { return (UINT)GetProcessHeap(Process); }

    if (Parameter == 0) {
        return (UINT)GetProcessHeap(NULL);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Allocate memory from the current process heap.
 *
 * @param Parameter Size in bytes to allocate.
 * @return UINT Linear address of the allocated block, or 0 on failure.
 */
UINT SysCall_HeapAlloc(UINT Parameter) { return (UINT)HeapAlloc(Parameter); }

/************************************************************************/

/**
 * @brief Free a block previously obtained from SysCall_HeapAlloc.
 *
 * @param Parameter Linear address of the block to free.
 * @return UINT Always returns 0.
 */
UINT SysCall_HeapFree(UINT Parameter) {
    HeapFree((LPVOID)Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Resize a heap allocation while preserving its contents.
 *
 * @param Parameter Linear address of HEAPREALLOCINFO describing the request.
 * @return UINT Linear address of the resized block, or 0 on failure.
 */
UINT SysCall_HeapRealloc(UINT Parameter) {
    LPHEAPREALLOCINFO Info = (LPHEAPREALLOCINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, HEAPREALLOCINFO) { return (UINT)HeapRealloc(Info->Pointer, Info->Size); }

    return 0;
}

/************************************************************************/

/**
 * @brief Enumerate mounted volumes, exposing handles to the callback.
 *
 * @param Parameter Pointer to ENUMVOLUMESINFO describing the callback and context.
 * @return UINT Non-zero when enumeration ran, zero on error.
 */
UINT SysCall_EnumVolumes(UINT Parameter) {
    LPENUMVOLUMESINFO Info = (LPENUMVOLUMESINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, ENUMVOLUMESINFO) {
        if (Info->Func == NULL) return 0;

        LockMutex(MUTEX_FILESYSTEM, INFINITY);

        LPLIST FileSystemList = GetFileSystemList();
        for (LPLISTNODE Node = FileSystemList != NULL ? FileSystemList->First : NULL; Node;
             Node = Node->Next) {
            LPFILESYSTEM FileSystem = (LPFILESYSTEM)Node;
            HANDLE VolumeHandle = PointerToHandle((LINEAR)FileSystem);

            if (VolumeHandle == 0) continue;

            U32 Result = Info->Func(VolumeHandle, Info->Parameter);
            ReleaseHandle(VolumeHandle);

            if (Result == 0) break;
        }

        UnlockMutex(MUTEX_FILESYSTEM);
        return 1;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve information for a specific volume handle.
 *
 * @param Parameter Pointer to VOLUMEINFO containing the target handle.
 * @return UINT Non-zero on success.
 */
UINT SysCall_GetVolumeInfo(UINT Parameter) {
    LPVOLUMEINFO Info = (LPVOLUMEINFO)Parameter;

    SAFE_USE_VALID(Info) {
        if (Info->Size < sizeof(VOLUMEINFO)) {
            return 0;
        }

        LPFILESYSTEM FileSystem = (LPFILESYSTEM)HandleToPointer(Info->Volume);

        SAFE_USE_VALID_ID(FileSystem, KOID_FILESYSTEM) {
            LockMutex(&(FileSystem->Mutex), INFINITY);
            StringCopy(Info->Name, FileSystem->Name);
            UnlockMutex(&(FileSystem->Mutex));
            return 1;
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Open a file and return a handle to user space.
 *
 * @param Parameter Pointer to FILEOPENINFO describing the request.
 * @return UINT File handle on success, 0 otherwise.
 */
UINT SysCall_OpenFile(UINT Parameter) {
    LPFILEOPENINFO Info = (LPFILEOPENINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, FILEOPENINFO) {
        LPFILE File = OpenFile(Info);

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            HANDLE Handle = PointerToHandle((LINEAR)File);

            if (Handle != 0) {
                return Handle;
            }

            CloseFile(File);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Read data from a file handle into a caller-provided buffer.
 *
 * @param Parameter Pointer to FILEOPERATION containing the read request.
 * @return UINT Bytes read on success, 0 on failure.
 */
UINT SysCall_ReadFile(UINT Parameter) {
    LPFILEOPERATION Operation = (LPFILEOPERATION)Parameter;

    SAFE_USE_INPUT_POINTER(Operation, FILEOPERATION) {
        HANDLE FileHandle = Operation->File;
        LPFILE File = (LPFILE)HandleToPointer(FileHandle);

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            Operation->File = (HANDLE)File;
            UINT Result = ReadFile(Operation);
            Operation->File = FileHandle;
            return Result;
        }

        Operation->File = FileHandle;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Write data from a caller buffer into a file handle.
 *
 * @param Parameter Pointer to FILEOPERATION describing the write.
 * @return UINT Bytes written on success, 0 otherwise.
 */
UINT SysCall_WriteFile(UINT Parameter) {
    LPFILEOPERATION Operation = (LPFILEOPERATION)Parameter;

    SAFE_USE_INPUT_POINTER(Operation, FILEOPERATION) {
        HANDLE FileHandle = Operation->File;
        LPFILE File = (LPFILE)HandleToPointer(FileHandle);

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            Operation->File = (HANDLE)File;
            UINT Result = WriteFile(Operation);
            Operation->File = FileHandle;
            return Result;
        }

        Operation->File = FileHandle;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the size of a file handle.
 *
 * @param Parameter File handle.
 * @return UINT File size or 0 on error.
 */
UINT SysCall_GetFileSize(UINT Parameter) {
    LPFILE File = (LPFILE)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID(File, KOID_FILE) { return GetFileSize(File); }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the current file position for a handle.
 *
 * @param Parameter File handle.
 * @return UINT File position or 0 on error.
 */
UINT SysCall_GetFilePosition(UINT Parameter) {
    LPFILE File = (LPFILE)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID(File, KOID_FILE) { return GetFilePosition(File); }

    return 0;
}

/************************************************************************/

/**
 * @brief Update the file pointer for a handle.
 *
 * @param Parameter Pointer to FILEOPERATION describing the seek.
 * @return UINT Result of SetFilePosition, 0 on failure.
 */
UINT SysCall_SetFilePosition(UINT Parameter) {
    LPFILEOPERATION Operation = (LPFILEOPERATION)Parameter;

    SAFE_USE_INPUT_POINTER(Operation, FILEOPERATION) {
        HANDLE FileHandle = Operation->File;
        LPFILE File = (LPFILE)HandleToPointer(FileHandle);

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            Operation->File = (HANDLE)File;
            UINT Result = SetFilePosition(Operation);
            Operation->File = FileHandle;
            return Result;
        }

        Operation->File = FileHandle;
    }

    return 0;
}

/************************************************************************/

static BOOL MatchPattern(LPCSTR Name, LPCSTR Pattern) {
    /* Simple '*' wildcard matcher */
    if (Pattern == NULL || Pattern[0] == STR_NULL) {
        return TRUE;
    }

    /* If no wildcard, direct compare */
    LPSTR Star = StringFindChar((LPCSTR)Pattern, '*');
    if (Star == NULL) {
        return STRINGS_EQUAL(Name, Pattern);
    }

    STR Prefix[MAX_FILE_NAME];
    STR Suffix[MAX_FILE_NAME];

    U32 PrefixLen = (U32)(Star - (LPSTR)Pattern);
    for (U32 i = 0; i < PrefixLen && i < MAX_FILE_NAME - 1; i++) {
        Prefix[i] = Pattern[i];
    }
    Prefix[PrefixLen] = STR_NULL;
    StringCopy(Suffix, (LPCSTR)(Star + 1));

    /* Check prefix */
    for (U32 i = 0; i < PrefixLen; i++) {
        if (Name[i] == STR_NULL || Name[i] != Prefix[i]) {
            return FALSE;
        }
    }

    U32 NameLen = StringLength(Name);
    U32 SuffixLen = StringLength(Suffix);
    if (SuffixLen > NameLen) return FALSE;

    if (SuffixLen == 0) return TRUE;

    return STRINGS_EQUAL(Name + (NameLen - SuffixLen), Suffix);
}

static BOOL BuildEnumeratePattern(LPCSTR Path, LPSTR OutPattern) {
    if (OutPattern == NULL) return FALSE;
    OutPattern[0] = STR_NULL;

    if (Path == NULL || Path[0] == STR_NULL) {
        StringCopy(OutPattern, TEXT("*"));
        return TRUE;
    }

    StringCopy(OutPattern, Path);
    U32 Len = StringLength(OutPattern);
    if (Len > 0 && OutPattern[Len - 1] != PATH_SEP) {
        StringConcat(OutPattern, TEXT("/"));
    }
    StringConcat(OutPattern, TEXT("*"));
    return TRUE;
}

UINT SysCall_FindFirstFile(UINT Parameter) {
    LPFILEFINDINFO Info = (LPFILEFINDINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, FILEFINDINFO) {
        STR EnumeratePattern[MAX_PATH_NAME];
        if (!BuildEnumeratePattern(Info->Path, EnumeratePattern)) {
            return FALSE;
        }

        FILEINFO Find;
        Find.Size = sizeof(FILEINFO);
        Find.FileSystem = GetSystemFS();
        Find.Attributes = MAX_U32;
        Find.Flags = FILE_OPEN_READ | FILE_OPEN_EXISTING;
        StringCopy(Find.Name, EnumeratePattern);

        LPFILESYSTEM FS = GetSystemFS();
        if (FS == NULL || FS->Driver == NULL || FS->Driver->Command == NULL) {
            return FALSE;
        }

        LPFILE File = (LPFILE)FS->Driver->Command(DF_FS_OPENFILE, (UINT)&Find);
        if (File == NULL) {
            return FALSE;
        }

        BOOL Found = FALSE;
        do {
            if (MatchPattern(File->Name, Info->Pattern)) {
                StringCopy(Info->Name, File->Name);
                Info->Attributes = File->Attributes;
                Found = TRUE;
                break;
            }
        } while (FS->Driver->Command(DF_FS_OPENNEXT, (UINT)File) == DF_RET_SUCCESS);

        if (!Found) {
            FS->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
            return FALSE;
        }

        HANDLE Handle = PointerToHandle((LINEAR)File);
        if (Handle == 0) {
            FS->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
            return FALSE;
        }

        Info->SearchHandle = Handle;
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

UINT SysCall_FindNextFile(UINT Parameter) {
    LPFILEFINDINFO Info = (LPFILEFINDINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, FILEFINDINFO) {
        LPFILE File = (LPFILE)HandleToPointer(Info->SearchHandle);
        LPFILESYSTEM FS = GetSystemFS();

        SAFE_USE_VALID_ID(File, KOID_FILE) {
            if (FS == NULL || FS->Driver == NULL || FS->Driver->Command == NULL) {
                return FALSE;
            }

            BOOL Found = FALSE;
            while (FS->Driver->Command(DF_FS_OPENNEXT, (UINT)File) == DF_RET_SUCCESS) {
                if (MatchPattern(File->Name, Info->Pattern)) {
                    StringCopy(Info->Name, File->Name);
                    Info->Attributes = File->Attributes;
                    Found = TRUE;
                    break;
                }
            }

            if (!Found) {
                FS->Driver->Command(DF_FS_CLOSEFILE, (UINT)File);
                Info->SearchHandle = 0;
                return FALSE;
            }

            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Peek the next keyboard character without removing it.
 *
 * @param Parameter Reserved.
 * @return UINT Non-zero when a key is available.
 */
UINT SysCall_ConsolePeekKey(UINT Parameter) {
    UNUSED(Parameter);
    return (UINT)PeekChar();
}

/************************************************************************/

/**
 * @brief Retrieve the next key event details.
 *
 * @param Parameter Linear address of a KEYCODE structure to fill.
 * @return UINT TRUE on success, FALSE on error.
 */
UINT SysCall_ConsoleGetKey(UINT Parameter) {
    LPKEYCODE KeyCode = (LPKEYCODE)Parameter;
    SAFE_USE_VALID(KeyCode) { return (UINT)GetKeyCode(KeyCode); }
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve current key modifier state.
 *
 * @param Parameter Linear address of a U32 to fill with KEYMOD_* flags.
 * @return UINT TRUE on success, FALSE on error.
 */
UINT SysCall_ConsoleGetKeyModifiers(UINT Parameter) {
    U32* Modifiers = (U32*)Parameter;
    SAFE_USE_VALID(Modifiers) {
        *Modifiers = GetKeyModifiers();
        return 1;
    }
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the next character from the console input.
 *
 * Currently unimplemented; reserved for future console work.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_ConsoleGetChar(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Output a string to the system console.
 *
 * @param Parameter Linear address of a null-terminated string.
 * @return UINT Always returns 0.
 */
UINT SysCall_ConsolePrint(UINT Parameter) {
    SAFE_USE_VALID((LPCSTR)Parameter) { ConsolePrint((LPCSTR)Parameter); }
    return 0;
}

/************************************************************************/

/**
 * @brief Blit a text buffer to the console at the given position.
 *
 * @param Parameter Linear address of CONSOLEBLITBUFFER.
 * @return UINT Always returns 0.
 */
UINT SysCall_ConsoleBlitBuffer(UINT Parameter) {
    LPCONSOLEBLITBUFFER Info = (LPCONSOLEBLITBUFFER)Parameter;

    if (Info != NULL && IsValidMemory((LINEAR)Info) && IsValidMemory((LINEAR)Info->Text)) {
        UINT maxWidth = Console.Width;
        UINT maxHeight = Console.Height;
        UINT row;
        UINT width = Info->Width;
        UINT height = Info->Height;
        UINT x = Info->X;
        UINT y = Info->Y;
        UINT textPitch = (Info->TextPitch != 0) ? Info->TextPitch : (Info->Width + 1);
        UINT attrPitch = (Info->AttrPitch != 0) ? Info->AttrPitch : Info->Width;
        BOOL useAttr = (Info->Attr != NULL) && IsValidMemory((LINEAR)Info->Attr);
        U32 savedFore = Console.ForeColor;
        U32 savedBack = Console.BackColor;
        U32 fore = Info->ForeColor;
        U32 back = Info->BackColor;

        if (fore > 15) fore = savedFore;
        if (back > 15) back = savedBack;

        if (width > maxWidth) width = maxWidth;
        if (height > maxHeight) height = maxHeight;
        if (x >= maxWidth || y >= maxHeight) return 0;
        if (x + width > maxWidth) width = maxWidth - x;
        if (y + height > maxHeight) height = maxHeight - y;

        if (!useAttr) {
            SetConsoleForeColor(fore);
            SetConsoleBackColor(back);
        }

        for (row = 0; row < height; row++) {
            const U8* attrRow = useAttr ? (Info->Attr + (row * attrPitch)) : NULL;
            UINT col;

            if (!useAttr) {
                ConsolePrintLine(y + row, x, Info->Text + (row * textPitch), width);
                continue;
            }

            /* Per-cell attributes */
            for (col = 0; col < width; col++) {
                U8 attr = attrRow[col];
                U32 cellFore = attr & 0x0F;
                U32 cellBack = (attr >> 4) & 0x0F;
                U16 attribute = (U16)(cellFore | (cellBack << 0x04) | (Console.Blink << 0x07));
                attribute = (U16)(attribute << 0x08);
                if (x + col < maxWidth && y + row < maxHeight) {
                    UINT offset = ((y + row) * Console.Width) + (x + col);
                    STR character = Info->Text[(row * textPitch) + col];
                    Console.Memory[offset] = (U16)character | attribute;
                }
            }
        }

        if (!useAttr) {
            SetConsoleForeColor(savedFore);
            SetConsoleBackColor(savedBack);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Read a string from the console.
 *
 * Placeholder implementation; not yet supported.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_ConsoleGetString(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Move the console cursor to the specified position.
 *
 * @param Parameter Linear address of a POINT structure.
 * @return UINT Always returns 0.
 */
UINT SysCall_ConsoleGotoXY(UINT Parameter) {
    LPPOINT Point = (LPPOINT)Parameter;
    SAFE_USE_VALID(Point) { SetConsoleCursorPosition(Point->X, Point->Y); }
    return 0;
}

/************************************************************************/

/**
 * @brief Clear the entire console screen.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_ConsoleClear(UINT Parameter) {
    UNUSED(Parameter);
    ClearConsole();
    return 0;
}

/************************************************************************/

/**
 * @brief Create a new desktop for the current process.
 *
 * @param Parameter Reserved.
 * @return UINT Desktop handle on success, 0 otherwise.
 */
UINT SysCall_CreateDesktop(UINT Parameter) {
    UNUSED(Parameter);

    LPDESKTOP Desktop = CreateDesktop();
    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        HANDLE Handle = PointerToHandle((LINEAR)Desktop);

        if (Handle != 0) {
            return Handle;
        }

        DeleteDesktop(Desktop);
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Show the desktop associated with the provided handle.
 *
 * @param Parameter Desktop handle.
 * @return UINT Result of ShowDesktop or 0 on error.
 */
UINT SysCall_ShowDesktop(UINT Parameter) {
    LPDESKTOP Desktop = (LPDESKTOP)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) { return (UINT)ShowDesktop(Desktop); }
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the top-level window handle for a desktop.
 *
 * @param Parameter Desktop handle.
 * @return UINT Window handle or 0 on error.
 */
UINT SysCall_GetDesktopWindow(UINT Parameter) {
    LPDESKTOP Desktop = (LPDESKTOP)HandleToPointer(Parameter);
    LPWINDOW Window = NULL;

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        LockMutex(&(Desktop->Mutex), INFINITY);
        Window = Desktop->Window;
        UnlockMutex(&(Desktop->Mutex));
        HANDLE Handle = PointerToHandle((LINEAR)Window);
        return Handle;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Return the desktop handle associated with the current process.
 *
 * @param Parameter Reserved.
 * @return UINT Desktop handle or 0 on failure.
 */
UINT SysCall_GetCurrentDesktop(UINT Parameter) {
    UNUSED(Parameter);

    LPPROCESS Process = GetCurrentProcess();

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        LPDESKTOP Desktop = Process->Desktop;

        SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
            HANDLE Handle = PointerToHandle((LINEAR)Desktop);
            return Handle;
        }
    }

    ERROR(TEXT("[SysCall_GetCurrentDesktop] No desktop for current process"));
    return 0;
}

/************************************************************************/

/**
 * @brief Create a window and return its handle.
 *
 * @param Parameter Pointer to WINDOWINFO describing the window.
 * @return UINT Window handle on success, 0 otherwise.
 */
UINT SysCall_CreateWindow(UINT Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    SAFE_USE_INPUT_POINTER(WindowInfo, WINDOWINFO) {
        HANDLE ParentHandle = WindowInfo->Parent;
        WindowInfo->Parent = (HANDLE)HandleToPointer(ParentHandle);

        LPWINDOW Window = CreateWindow(WindowInfo);

        WindowInfo->Parent = ParentHandle;

        SAFE_USE_VALID_ID(Window, KOID_WINDOW) {
            HANDLE WindowHandle = PointerToHandle((LINEAR)Window);

            if (WindowHandle != 0) {
                WindowInfo->Window = WindowHandle;
                return WindowHandle;
            }

            DeleteWindow(Window);
        }

        WindowInfo->Window = 0;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Show a window referenced by handle.
 *
 * @param Parameter Pointer to WINDOWINFO containing the window handle.
 * @return UINT TRUE on success.
 */
UINT SysCall_ShowWindow(UINT Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    SAFE_USE_INPUT_POINTER(WindowInfo, WINDOWINFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowInfo->Window);
        SAFE_USE_VALID_ID(Window, KOID_WINDOW) { return (UINT)ShowWindow((HANDLE)Window, TRUE); }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Hide a window referenced by handle.
 *
 * @param Parameter Pointer to WINDOWINFO containing the window handle.
 * @return UINT TRUE on success.
 */
UINT SysCall_HideWindow(UINT Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    SAFE_USE_INPUT_POINTER(WindowInfo, WINDOWINFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowInfo->Window);
        SAFE_USE_VALID_ID(Window, KOID_WINDOW) { return (UINT)ShowWindow((HANDLE)Window, FALSE); }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Move a window to a new position.
 *
 * @param Parameter Pointer to WINDOWINFO containing the target window and new position.
 * @return UINT TRUE on success.
 */
UINT SysCall_MoveWindow(UINT Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    SAFE_USE_INPUT_POINTER(WindowInfo, WINDOWINFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowInfo->Window);
        SAFE_USE_VALID_ID(Window, KOID_WINDOW) { return (UINT)MoveWindow((HANDLE)Window, &(WindowInfo->WindowPosition)); }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Resize a window.
 *
 * @param Parameter Pointer to WINDOWINFO containing the target window and new size.
 * @return UINT TRUE on success.
 */
UINT SysCall_SizeWindow(UINT Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    SAFE_USE_INPUT_POINTER(WindowInfo, WINDOWINFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowInfo->Window);
        SAFE_USE_VALID_ID(Window, KOID_WINDOW) { return (UINT)SizeWindow((HANDLE)Window, &(WindowInfo->WindowSize)); }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Set a custom window procedure.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_SetWindowFunc(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the current window procedure.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_GetWindowFunc(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Update window style flags.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_SetWindowStyle(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve window style flags.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_GetWindowStyle(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Associate a custom property with a window.
 *
 * @param Parameter Pointer to PROPINFO describing the property.
 * @return UINT Previous property value, or 0.
 */
UINT SysCall_SetWindowProp(UINT Parameter) {
    LPPROPINFO PropInfo = (LPPROPINFO)Parameter;

    SAFE_USE_INPUT_POINTER(PropInfo, PROPINFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(PropInfo->Window);
        SAFE_USE_VALID_ID(Window, KOID_WINDOW) { return SetWindowProp((HANDLE)Window, PropInfo->Name, PropInfo->Value); }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve a custom property from a window.
 *
 * @param Parameter Pointer to PROPINFO containing the window handle and property name.
 * @return UINT Property value or 0.
 */
UINT SysCall_GetWindowProp(UINT Parameter) {
    LPPROPINFO PropInfo = (LPPROPINFO)Parameter;

    SAFE_USE_INPUT_POINTER(PropInfo, PROPINFO) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(PropInfo->Window);
        SAFE_USE_VALID_ID(Window, KOID_WINDOW) { return GetWindowProp((HANDLE)Window, PropInfo->Name); }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the client rectangle for a window.
 *
 * @param Parameter Pointer to WINDOWRECT containing the window handle.
 * @return UINT TRUE on success.
 */
UINT SysCall_GetWindowRect(UINT Parameter) {
    LPWINDOWRECT WindowRect = (LPWINDOWRECT)Parameter;

    SAFE_USE_INPUT_POINTER(WindowRect, WINDOWRECT) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowRect->Window);
        SAFE_USE_VALID_ID(Window, KOID_WINDOW) { return (UINT)GetWindowRect((HANDLE)Window, &(WindowRect->Rect)); }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Mark a window region as needing redraw.
 *
 * @param Parameter Pointer to WINDOWRECT with the target window and rectangle.
 * @return UINT TRUE on success.
 */
UINT SysCall_InvalidateWindowRect(UINT Parameter) {
    LPWINDOWRECT WindowRect = (LPWINDOWRECT)Parameter;

    SAFE_USE_INPUT_POINTER(WindowRect, WINDOWRECT) {
        LPWINDOW Window = (LPWINDOW)HandleToPointer(WindowRect->Window);
        SAFE_USE_VALID_ID(Window, KOID_WINDOW) { return (UINT)InvalidateWindowRect((HANDLE)Window, &(WindowRect->Rect)); }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Obtain a graphics context for drawing into a window.
 *
 * @param Parameter Window handle.
 * @return UINT Graphics context handle or 0 on failure.
 */
UINT SysCall_GetWindowGC(UINT Parameter) {
    LPWINDOW Window = (LPWINDOW)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID(Window, KOID_WINDOW) {
        HANDLE ContextPointer = GetWindowGC((HANDLE)Window);

        SAFE_USE_VALID((LPVOID)ContextPointer) {
            HANDLE Handle = PointerToHandle((LINEAR)ContextPointer);

            if (Handle != 0) {
                return Handle;
            }

            ReleaseWindowGC(ContextPointer);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Release a previously obtained graphics context.
 *
 * @param Parameter Graphics context handle.
 * @return UINT TRUE on success.
 */
UINT SysCall_ReleaseWindowGC(UINT Parameter) {
    LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(Parameter);

    SAFE_USE_VALID_ID(Context, KOID_GRAPHICSCONTEXT) {
        UINT Result = (UINT)ReleaseWindowGC((HANDLE)Context);
        if (Result) ReleaseHandle(Parameter);
        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Enumerate windows for the current desktop.
 *
 * Not implemented yet.
 *
 * @param Parameter Reserved.
 * @return UINT Always 0.
 */
UINT SysCall_EnumWindows(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Invoke the default window procedure.
 *
 * @param Parameter Pointer to MESSAGEINFO structure.
 * @return UINT Result of DefWindowFunc on success, 0 on error.
 */
UINT SysCall_DefWindowFunc(UINT Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Message, MESSAGEINFO) {
        HANDLE Original = Message->Target;
        Message->Target = (HANDLE)HandleToPointer(Original);

        if (Message->Target == NULL && Original != 0) {
            Message->Target = Original;
            return 0;
        }

        UINT Result = (UINT)DefWindowFunc(Message->Target, Message->Message, Message->Param1, Message->Param2);

        Message->Target = Original;
        return Result;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve a system brush handle by identifier.
 *
 * @param Parameter System brush identifier.
 * @return UINT Brush handle or 0 if unavailable.
 */
UINT SysCall_GetSystemBrush(UINT Parameter) {
    HANDLE BrushPointer = GetSystemBrush(Parameter);

    SAFE_USE_VALID((LPVOID)BrushPointer) {
        HANDLE Handle = PointerToHandle((LINEAR)BrushPointer);
        if (Handle != 0) return Handle;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve a system pen handle by identifier.
 *
 * @param Parameter System pen identifier.
 * @return UINT Pen handle or 0 if unavailable.
 */
UINT SysCall_GetSystemPen(UINT Parameter) {
    HANDLE PenPointer = GetSystemPen(Parameter);

    SAFE_USE_VALID((LPVOID)PenPointer) {
        HANDLE Handle = PointerToHandle((LINEAR)PenPointer);
        if (Handle != 0) return Handle;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Create a brush and expose it as a handle.
 *
 * @param Parameter Pointer to BRUSHINFO describing the brush.
 * @return UINT Brush handle or 0 on failure.
 */
UINT SysCall_CreateBrush(UINT Parameter) {
    LPBRUSHINFO Info = (LPBRUSHINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, BRUSHINFO) {
        LPBRUSH Brush = (LPBRUSH)CreateBrush(Info);

        SAFE_USE_VALID_ID(Brush, KOID_BRUSH) {
            HANDLE Handle = PointerToHandle((LINEAR)Brush);
            if (Handle != 0) return Handle;
            KernelHeapFree(Brush);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Create a pen and expose it as a handle.
 *
 * @param Parameter Pointer to PENINFO describing the pen.
 * @return UINT Pen handle or 0 on failure.
 */
UINT SysCall_CreatePen(UINT Parameter) {
    LPPENINFO Info = (LPPENINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, PENINFO) {
        LPPEN Pen = (LPPEN)CreatePen(Info);

        SAFE_USE_VALID_ID(Pen, KOID_PEN) {
            HANDLE Handle = PointerToHandle((LINEAR)Pen);
            if (Handle != 0) return Handle;
            KernelHeapFree(Pen);
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Select a brush into a graphics context.
 *
 * @param Parameter Pointer to GCSELECT containing GC and brush handles.
 * @return UINT Previous brush handle, or 0.
 */
UINT SysCall_SelectBrush(UINT Parameter) {
    LPGCSELECT Sel = (LPGCSELECT)Parameter;

    SAFE_USE_INPUT_POINTER(Sel, GCSELECT) {
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(Sel->GC);
        SAFE_USE_VALID_ID(Context, KOID_GRAPHICSCONTEXT) {
            if (Sel->Object != 0) {
                LPBRUSH Brush = (LPBRUSH)HandleToPointer(Sel->Object);
                SAFE_USE_VALID_ID(Brush, KOID_BRUSH) {
                    HANDLE Previous = SelectBrush((HANDLE)Context, (HANDLE)Brush);
                    SAFE_USE_VALID((LPVOID)Previous) {
                        HANDLE Handle = PointerToHandle((LINEAR)Previous);
                        if (Handle != 0) return Handle;
                    }
                    return 0;
                }
                return 0;
            }

            HANDLE Previous = SelectBrush((HANDLE)Context, NULL);
            SAFE_USE_VALID((LPVOID)Previous) {
                HANDLE Handle = PointerToHandle((LINEAR)Previous);
                if (Handle != 0) return Handle;
            }
            return 0;
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Select a pen into a graphics context.
 *
 * @param Parameter Pointer to GCSELECT containing GC and pen handles.
 * @return UINT Previous pen handle, or 0.
 */
UINT SysCall_SelectPen(UINT Parameter) {
    LPGCSELECT Sel = (LPGCSELECT)Parameter;

    SAFE_USE_INPUT_POINTER(Sel, GCSELECT) {
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(Sel->GC);
        SAFE_USE_VALID_ID(Context, KOID_GRAPHICSCONTEXT) {
            if (Sel->Object != 0) {
                LPPEN Pen = (LPPEN)HandleToPointer(Sel->Object);
                SAFE_USE_VALID_ID(Pen, KOID_PEN) {
                    HANDLE Previous = SelectPen((HANDLE)Context, (HANDLE)Pen);
                    SAFE_USE_VALID((LPVOID)Previous) {
                        HANDLE Handle = PointerToHandle((LINEAR)Previous);
                        if (Handle != 0) return Handle;
                    }
                    return 0;
                }
                return 0;
            }

            HANDLE Previous = SelectPen((HANDLE)Context, NULL);
            SAFE_USE_VALID((LPVOID)Previous) {
                HANDLE Handle = PointerToHandle((LINEAR)Previous);
                if (Handle != 0) return Handle;
            }
            return 0;
        }
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Set a pixel within a graphics context.
 *
 * @param Parameter Pointer to PIXELINFO containing the draw parameters.
 * @return UINT TRUE on success.
 */
UINT SysCall_SetPixel(UINT Parameter) {
    LPPIXELINFO PixelInfo = (LPPIXELINFO)Parameter;

    SAFE_USE_INPUT_POINTER(PixelInfo, PIXELINFO) {
        HANDLE OriginalGC = PixelInfo->GC;
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(OriginalGC);

        SAFE_USE_VALID_ID(Context, KOID_GRAPHICSCONTEXT) {
            PixelInfo->GC = (HANDLE)Context;
            UINT Result = (UINT)SetPixel(PixelInfo);
            PixelInfo->GC = OriginalGC;
            return Result;
        }

        PixelInfo->GC = OriginalGC;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve a pixel from a graphics context.
 *
 * @param Parameter Pointer to PIXELINFO containing coordinates.
 * @return UINT TRUE on success.
 */
UINT SysCall_GetPixel(UINT Parameter) {
    LPPIXELINFO PixelInfo = (LPPIXELINFO)Parameter;

    SAFE_USE_INPUT_POINTER(PixelInfo, PIXELINFO) {
        HANDLE OriginalGC = PixelInfo->GC;
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(OriginalGC);

        SAFE_USE_VALID_ID(Context, KOID_GRAPHICSCONTEXT) {
            PixelInfo->GC = (HANDLE)Context;
            UINT Result = (UINT)GetPixel(PixelInfo);
            PixelInfo->GC = OriginalGC;
            return Result;
        }

        PixelInfo->GC = OriginalGC;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Draw a line using the current graphics context pen.
 *
 * @param Parameter Pointer to LINEINFO with GC handle and coordinates.
 * @return UINT TRUE on success.
 */
UINT SysCall_Line(UINT Parameter) {
    LPLINEINFO LineInfo = (LPLINEINFO)Parameter;

    SAFE_USE_INPUT_POINTER(LineInfo, LINEINFO) {
        HANDLE OriginalGC = LineInfo->GC;
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(OriginalGC);

        SAFE_USE_VALID_ID(Context, KOID_GRAPHICSCONTEXT) {
            LineInfo->GC = (HANDLE)Context;
            UINT Result = (UINT)Line(LineInfo);
            LineInfo->GC = OriginalGC;
            return Result;
        }

        LineInfo->GC = OriginalGC;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Draw a rectangle using the current pen and brush.
 *
 * @param Parameter Pointer to RECTINFO with GC handle and rectangle.
 * @return UINT TRUE on success.
 */
UINT SysCall_Rectangle(UINT Parameter) {
    LPRECTINFO RectInfo = (LPRECTINFO)Parameter;

    SAFE_USE_INPUT_POINTER(RectInfo, RECTINFO) {
        HANDLE OriginalGC = RectInfo->GC;
        LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)HandleToPointer(OriginalGC);

        SAFE_USE_VALID_ID(Context, KOID_GRAPHICSCONTEXT) {
            RectInfo->GC = (HANDLE)Context;
            UINT Result = (UINT)Rectangle(RectInfo);
            RectInfo->GC = OriginalGC;
            return Result;
        }

        RectInfo->GC = OriginalGC;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the latest mouse delta values.
 *
 * @param Parameter Linear address of a POINT structure.
 * @return UINT TRUE on success.
 */
UINT SysCall_GetMousePos(UINT Parameter) {
    LPPOINT Point = (LPPOINT)Parameter;
    U32 UX, UY;

    SAFE_USE_VALID(Point) {
        UX = GetMouseDriver()->Command(DF_MOUSE_GETDELTAX, 0);
        UY = GetMouseDriver()->Command(DF_MOUSE_GETDELTAY, 0);

        Point->X = *((I32*)&UX);
        Point->Y = *((I32*)&UY);

        return 1;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Set the mouse cursor position.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_SetMousePos(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Retrieve the state of mouse buttons.
 *
 * @param Parameter Reserved.
 * @return UINT Bitmask of button states.
 */
UINT SysCall_GetMouseButtons(UINT Parameter) {
    UNUSED(Parameter);
    return GetMouseDriver()->Command(DF_MOUSE_GETBUTTONS, 0);
}

/************************************************************************/

/**
 * @brief Show the mouse cursor.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_ShowMouse(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Hide the mouse cursor.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_HideMouse(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Confine the mouse cursor to a rectangle.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_ClipMouse(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Capture mouse input to a specific window.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_CaptureMouse(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Release mouse capture.
 *
 * Not yet implemented.
 *
 * @param Parameter Reserved.
 * @return UINT Always returns 0.
 */
UINT SysCall_ReleaseMouse(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/************************************************************************/

/**
 * @brief Authenticate a user and create a session.
 *
 * @param Parameter Pointer to LOGIN_INFO credentials.
 * @return UINT TRUE on success.
 */
UINT SysCall_Login(UINT Parameter) {
    LPLOGIN_INFO LoginInfo = (LPLOGIN_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(LoginInfo, LOGIN_INFO) {
        LPUSERACCOUNT Account = FindUserAccount(LoginInfo->UserName);
        if (Account == NULL) return FALSE;

        if (!VerifyPassword(LoginInfo->Password, Account->PasswordHash)) {
            return FALSE;
        }

        LPUSERSESSION Session = CreateUserSession(Account->UserID, (HANDLE)GetCurrentTask());
        if (Session == NULL) {
            return FALSE;
        }

        GetLocalTime(&Account->LastLoginTime);
        SetCurrentSession(Session);
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Terminate the current user session.
 *
 * @param Parameter Reserved.
 * @return UINT TRUE on success.
 */
UINT SysCall_Logout(UINT Parameter) {
    UNUSED(Parameter);
    LPUSERSESSION Session = GetCurrentSession();
    if (Session == NULL) {
        return FALSE;
    }

    DestroyUserSession(Session);
    SetCurrentSession(NULL);
    return TRUE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Retrieve information about the current user session.
 *
 * @param Parameter Pointer to CURRENT_USER_INFO buffer.
 * @return UINT TRUE on success.
 */
UINT SysCall_GetCurrentUser(UINT Parameter) {
    LPCURRENT_USER_INFO UserInfo = (LPCURRENT_USER_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(UserInfo, CURRENT_USER_INFO) {
        LPUSERACCOUNT Account = GetCurrentUser();
        if (Account == NULL) return FALSE;

        LPUSERSESSION Session = GetCurrentSession();
        if (Session == NULL) return FALSE;

        StringCopy(UserInfo->UserName, Account->UserName);
        UserInfo->Privilege = Account->Privilege;
        UserInfo->LoginTime = U64_FromUINT(GetSystemTime());
        UserInfo->SessionID = Session->SessionID;

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Change the password of the current user.
 *
 * @param Parameter Pointer to PASSWORD_CHANGE data.
 * @return UINT TRUE on success.
 */
UINT SysCall_ChangePassword(UINT Parameter) {
    LPPASSWORD_CHANGE PasswordChange = (LPPASSWORD_CHANGE)Parameter;

    SAFE_USE_INPUT_POINTER(PasswordChange, PASSWORD_CHANGE) {
        LPUSERACCOUNT Account = GetCurrentUser();
        if (Account == NULL) {
            return FALSE;
        }

        return ChangeUserPassword(Account->UserName, PasswordChange->OldPassword, PasswordChange->NewPassword);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Create a new user account.
 *
 * @param Parameter Pointer to USER_CREATE_INFO data.
 * @return UINT TRUE on success.
 */
UINT SysCall_CreateUser(UINT Parameter) {
    LPUSER_CREATE_INFO CreateInfo = (LPUSER_CREATE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(CreateInfo, USER_CREATE_INFO) {
        LPUSERACCOUNT CurrentAccount = GetCurrentUser();
        if (CurrentAccount == NULL || CurrentAccount->Privilege != EXOS_PRIVILEGE_ADMIN) {
            return FALSE;
        }

        LPUSERACCOUNT NewAccount = CreateUserAccount(CreateInfo->UserName, CreateInfo->Password, CreateInfo->Privilege);
        return (NewAccount != NULL) ? TRUE : FALSE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Delete an existing user account.
 *
 * @param Parameter Pointer to USER_DELETE_INFO data.
 * @return UINT TRUE on success.
 */
UINT SysCall_DeleteUser(UINT Parameter) {
    LPUSER_DELETE_INFO DeleteInfo = (LPUSER_DELETE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(DeleteInfo, USER_DELETE_INFO) {
        LPUSERACCOUNT CurrentAccount = GetCurrentUser();
        if (CurrentAccount == NULL || CurrentAccount->Privilege != EXOS_PRIVILEGE_ADMIN) {
            return FALSE;
        }

        return DeleteUserAccount(DeleteInfo->UserName);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Enumerate existing user accounts.
 *
 * @param Parameter Pointer to USER_LIST_INFO buffer.
 * @return UINT TRUE on success.
 */
UINT SysCall_ListUsers(UINT Parameter) {
    LPUSER_LIST_INFO ListInfo = (LPUSER_LIST_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(ListInfo, USER_LIST_INFO) {
        LPUSERACCOUNT CurrentAccount = GetCurrentUser();
        if (CurrentAccount == NULL || CurrentAccount->Privilege != EXOS_PRIVILEGE_ADMIN) {
            return FALSE;
        }

        ListInfo->UserCount = 0;
        LPLIST UserAccountList = GetUserAccountList();
        LPUSERACCOUNT Account =
            (LPUSERACCOUNT)(UserAccountList != NULL ? UserAccountList->First : NULL);

        while (Account != NULL && ListInfo->UserCount < ListInfo->MaxUsers) {
            StringCopy(ListInfo->UserNames[ListInfo->UserCount], Account->UserName);
            ListInfo->UserCount++;
            Account = (LPUSERACCOUNT)Account->Next;
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/
// Socket syscalls

/**
 * @brief Create a socket and return its descriptor.
 *
 * @param Parameter Pointer to SOCKET_CREATE_INFO request.
 * @return UINT Socket descriptor or error code.
 */
UINT SysCall_SocketCreate(UINT Parameter) {
    LPSOCKET_CREATE_INFO Info = (LPSOCKET_CREATE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_CREATE_INFO) {
        return SocketCreate(Info->AddressFamily, Info->SocketType, Info->Protocol);
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Bind a socket to a local address.
 *
 * @param Parameter Pointer to SOCKET_BIND_INFO request.
 * @return UINT Result code from SocketBind.
 */
UINT SysCall_SocketBind(UINT Parameter) {
    LPSOCKET_BIND_INFO Info = (LPSOCKET_BIND_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_BIND_INFO) {
        return SocketBind(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressData, Info->AddressLength);
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Transition a socket into listening mode.
 *
 * @param Parameter Pointer to SOCKET_LISTEN_INFO request.
 * @return UINT Result code from SocketListen.
 */
UINT SysCall_SocketListen(UINT Parameter) {
    LPSOCKET_LISTEN_INFO Info = (LPSOCKET_LISTEN_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_LISTEN_INFO) {
        return SocketListen(Info->SocketHandle, Info->Backlog);
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Accept a pending connection on a listening socket.
 *
 * @param Parameter Pointer to SOCKET_ACCEPT_INFO buffer.
 * @return UINT Result code from SocketAccept.
 */
UINT SysCall_SocketAccept(UINT Parameter) {
    LPSOCKET_ACCEPT_INFO Info = (LPSOCKET_ACCEPT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_ACCEPT_INFO) {
        return SocketAccept(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressBuffer, Info->AddressLength);
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Connect a socket to a remote endpoint.
 *
 * @param Parameter Pointer to SOCKET_CONNECT_INFO request.
 * @return UINT Result code from SocketConnect.
 */
UINT SysCall_SocketConnect(UINT Parameter) {
    LPSOCKET_CONNECT_INFO Info = (LPSOCKET_CONNECT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_CONNECT_INFO) {
        return SocketConnect(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressData, Info->AddressLength);
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Send data on a connected socket.
 *
 * @param Parameter Pointer to SOCKET_DATA_INFO request.
 * @return UINT Bytes sent or error code.
 */
UINT SysCall_SocketSend(UINT Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketSend(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags);
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Receive data from a connected socket.
 *
 * @param Parameter Pointer to SOCKET_DATA_INFO buffer.
 * @return UINT Bytes received or error code.
 */
UINT SysCall_SocketReceive(UINT Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketReceive(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags);
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Send data to a specific address using a datagram socket.
 *
 * @param Parameter Pointer to SOCKET_DATA_INFO request.
 * @return UINT Bytes sent or error code.
 */
UINT SysCall_SocketSendTo(UINT Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketSendTo(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags, (LPSOCKET_ADDRESS)Info->AddressData, Info->AddressLength);
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Receive data along with the sender address on a datagram socket.
 *
 * @param Parameter Pointer to SOCKET_DATA_INFO buffer.
 * @return UINT Bytes received or error code.
 */
UINT SysCall_SocketReceiveFrom(UINT Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        U32 AddressLength = Info->AddressLength;
        UINT Result = SocketReceiveFrom(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags, (LPSOCKET_ADDRESS)Info->AddressData, &AddressLength);
        Info->AddressLength = AddressLength;
        return Result;
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Close a socket descriptor.
 *
 * @param Parameter Socket descriptor.
 * @return UINT Result code from SocketClose.
 */
UINT SysCall_SocketClose(UINT Parameter) {
    SOCKET_HANDLE SocketHandle = (SOCKET_HANDLE)Parameter;
    return SocketClose(SocketHandle);
}

/************************************************************************/

/**
 * @brief Shut down parts of a socket connection.
 *
 * @param Parameter Pointer to SOCKET_SHUTDOWN_INFO request.
 * @return UINT Result code from SocketShutdown.
 */
UINT SysCall_SocketShutdown(UINT Parameter) {
    LPSOCKET_SHUTDOWN_INFO Info = (LPSOCKET_SHUTDOWN_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_SHUTDOWN_INFO) {
        return SocketShutdown(Info->SocketHandle, Info->How);
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Retrieve a socket option value.
 *
 * @param Parameter Pointer to SOCKET_OPTION_INFO buffer.
 * @return UINT Result code from SocketGetOption.
 */
UINT SysCall_SocketGetOption(UINT Parameter) {
    LPSOCKET_OPTION_INFO Info = (LPSOCKET_OPTION_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_OPTION_INFO) {
        U32 OptionLength = Info->OptionLength;
        UINT Result = SocketGetOption(Info->SocketHandle, Info->Level, Info->OptionName, Info->OptionValue, &OptionLength);
        Info->OptionLength = OptionLength;
        return Result;
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Set a socket option value.
 *
 * @param Parameter Pointer to SOCKET_OPTION_INFO request.
 * @return UINT Result code from SocketSetOption.
 */
UINT SysCall_SocketSetOption(UINT Parameter) {
    LPSOCKET_OPTION_INFO Info = (LPSOCKET_OPTION_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_OPTION_INFO) {
        return SocketSetOption(Info->SocketHandle, Info->Level, Info->OptionName, Info->OptionValue, Info->OptionLength);
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Retrieve the address of the connected peer.
 *
 * @param Parameter Pointer to SOCKET_ACCEPT_INFO buffer.
 * @return UINT Result code from SocketGetPeerName.
 */
UINT SysCall_SocketGetPeerName(UINT Parameter) {
    LPSOCKET_ACCEPT_INFO Info = (LPSOCKET_ACCEPT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_ACCEPT_INFO) {
        return SocketGetPeerName(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressBuffer, Info->AddressLength);
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Retrieve the local address of a socket.
 *
 * @param Parameter Pointer to SOCKET_ACCEPT_INFO buffer.
 * @return UINT Result code from SocketGetSocketName.
 */
UINT SysCall_SocketGetSocketName(UINT Parameter) {
    LPSOCKET_ACCEPT_INFO Info = (LPSOCKET_ACCEPT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_ACCEPT_INFO) {
        return SocketGetSocketName(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressBuffer, Info->AddressLength);
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

UINT SystemCallHandler(U32 Function, UINT Parameter) {
    if (Function >= SYSCALL_Last || SysCallTable[Function].Function == NULL) {
        return 0;
    }

    LPUSERACCOUNT CurrentUser = GetCurrentUser();
    U32 RequiredPrivilege = SysCallTable[Function].Privilege;

    if (CurrentUser == NULL) {
        if (RequiredPrivilege != EXOS_PRIVILEGE_USER) {
            return 0;
        }
    } else {
        if (CurrentUser->Privilege > RequiredPrivilege) {
            return 0;
        }
    }

    return SysCallTable[Function].Function(Parameter);
}
