
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


    Deferred work dispatcher infrastructure

\************************************************************************/

#include "DeferredWork.h"

#include "KernelEvent.h"
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "process/Process.h"
#include "process/Schedule.h"
#include "process/Task.h"
#include "System.h"
#include "User.h"
#include "CoreString.h"
#include "utils/Helpers.h"

/************************************************************************/

typedef struct tag_DEFERRED_WORK_ITEM {
    BOOL InUse;
    DEFERRED_WORK_CALLBACK WorkCallback;
    DEFERRED_WORK_POLL_CALLBACK PollCallback;
    LPVOID Context;
    volatile U32 PendingCount;
    STR Name[32];
} DEFERRED_WORK_ITEM, *LPDEFERRED_WORK_ITEM;

typedef struct tag_DEFERRED_WORK_CONTEXT {
    DEFERRED_WORK_ITEM WorkItems[DEFERRED_WORK_MAX_ITEMS];
    LPKERNEL_EVENT DeferredEvent;
    BOOL PollingMode;
    BOOL DispatcherStarted;
} DEFERRED_WORK_CONTEXT;

/************************************************************************/

static DEFERRED_WORK_CONTEXT DATA_SECTION g_DeferredWork = {
    .DeferredEvent = NULL,
    .PollingMode = FALSE,
    .DispatcherStarted = FALSE};

/************************************************************************/

static UINT DeferredWorkDriverCommands(UINT Function, UINT Parameter);
static void ProcessPendingWork(void);
static void ProcessPollCallbacks(void);
static U32 DeferredWorkDispatcherTask(LPVOID Param);

/************************************************************************/

#define DEFERRED_WORK_VER_MAJOR 1
#define DEFERRED_WORK_VER_MINOR 0

DRIVER DATA_SECTION DeferredWorkDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_INIT,
    .VersionMajor = DEFERRED_WORK_VER_MAJOR,
    .VersionMinor = DEFERRED_WORK_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "DeferredWork",
    .Alias = "deferred_work",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = DeferredWorkDriverCommands};

/************************************************************************/

/**
 * @brief Retrieves the deferred work driver descriptor.
 * @return Pointer to the deferred work driver.
 */
LPDRIVER DeferredWorkGetDriver(void) {
    return &DeferredWorkDriver;
}

/************************************************************************/

/**
 * @brief Initializes deferred work dispatcher task and event.
 *
 * Reads configuration for timeouts/polling, creates dispatcher task, and
 * prepares internal slots for deferred work items.
 *
 * @return TRUE on success, FALSE on allocation or task creation failure.
 */
BOOL InitializeDeferredWork(void) {
    if (g_DeferredWork.DispatcherStarted) {
        return TRUE;
    }

    SetDeferredWorkWaitTimeout(DEFERRED_WORK_WAIT_TIMEOUT_MS);
    SetDeferredWorkPollDelay(DEFERRED_WORK_POLL_DELAY_MS);

    LPCSTR WaitTimeoutValue = GetConfigurationValue(TEXT(CONFIG_GENERAL_DEFERRED_WORK_WAIT_TIMEOUT_MS));
    if (STRING_EMPTY(WaitTimeoutValue) == FALSE) {
        SetDeferredWorkWaitTimeout(StringToU32(WaitTimeoutValue));
    }

    LPCSTR PollDelayValue = GetConfigurationValue(TEXT(CONFIG_GENERAL_DEFERRED_WORK_POLL_DELAY_MS));
    if (STRING_EMPTY(PollDelayValue) == FALSE) {
        SetDeferredWorkPollDelay(StringToU32(PollDelayValue));
    }

    MemorySet(g_DeferredWork.WorkItems, 0, sizeof(g_DeferredWork.WorkItems));

    g_DeferredWork.DeferredEvent = CreateKernelEvent();
    if (g_DeferredWork.DeferredEvent == NULL) {
        ERROR(TEXT("[InitializeDeferredWork] Failed to create deferred event"));
        return FALSE;
    }

    DEBUG(TEXT("[InitializeDeferredWork] Deferred event created at %p"), g_DeferredWork.DeferredEvent);

    LPCSTR ModeValue = GetConfigurationValue(TEXT(CONFIG_GENERAL_POLLING));
    if (STRING_EMPTY(ModeValue) == FALSE) {
        U32 Numeric = StringToU32(ModeValue);
        if (Numeric != 0) {
            g_DeferredWork.PollingMode = TRUE;
        } else if (StringCompareNC(ModeValue, TEXT("true")) == 0) {
            g_DeferredWork.PollingMode = TRUE;
        }
    }

    if (g_DeferredWork.PollingMode == TRUE) {
        ConsolePrint(TEXT("WARNING : Devices in polling mode.\n"));
    }

    TASKINFO TaskInfo;
    MemorySet(&TaskInfo, 0, sizeof(TaskInfo));
    TaskInfo.Header.Size = sizeof(TASKINFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Func = DeferredWorkDispatcherTask;
    TaskInfo.Parameter = NULL;
    TaskInfo.StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
    TaskInfo.Priority = TASK_PRIORITY_LOWER;
    TaskInfo.Flags = 0;
    StringCopy(TaskInfo.Name, TEXT("DeferredWork"));

    if (CreateTask(&KernelProcess, &TaskInfo) == NULL) {
        ERROR(TEXT("[InitializeDeferredWork] Failed to create dispatcher task"));
        DeleteKernelEvent(g_DeferredWork.DeferredEvent);
        g_DeferredWork.DeferredEvent = NULL;
        return FALSE;
    }

    g_DeferredWork.DispatcherStarted = TRUE;
    DEBUG(TEXT("[InitializeDeferredWork] Dispatcher task started"));
    return TRUE;
}

/************************************************************************/

/**
 * @brief Shuts down deferred work dispatcher state.
 *
 * Clears dispatcher flags and resets the deferred event when available.
 */
void ShutdownDeferredWork(void) {
    g_DeferredWork.DispatcherStarted = FALSE;
    g_DeferredWork.PollingMode = FALSE;
    SAFE_USE(g_DeferredWork.DeferredEvent) {
        ResetKernelEvent(g_DeferredWork.DeferredEvent);
    }
}

/************************************************************************/

/**
 * @brief Registers a deferred work item with callbacks and context.
 *
 * @param Registration Registration information defining callbacks and context.
 *
 * @return Handle to the registered work item or DEFERRED_WORK_INVALID_HANDLE.
 */
U32 DeferredWorkRegister(const DEFERRED_WORK_REGISTRATION *Registration) {
    if (Registration == NULL) {
        return DEFERRED_WORK_INVALID_HANDLE;
    }

    if (Registration->WorkCallback == NULL && Registration->PollCallback == NULL) {
        return DEFERRED_WORK_INVALID_HANDLE;
    }

    for (U32 Index = 0; Index < DEFERRED_WORK_MAX_ITEMS; Index++) {
        if (!g_DeferredWork.WorkItems[Index].InUse) {
            g_DeferredWork.WorkItems[Index].InUse = TRUE;
            g_DeferredWork.WorkItems[Index].WorkCallback = Registration->WorkCallback;
            g_DeferredWork.WorkItems[Index].PollCallback = Registration->PollCallback;
            g_DeferredWork.WorkItems[Index].Context = Registration->Context;
            g_DeferredWork.WorkItems[Index].PendingCount = 0;
            MemorySet(g_DeferredWork.WorkItems[Index].Name, 0, sizeof(g_DeferredWork.WorkItems[Index].Name));
            if (Registration->Name) {
                StringCopyLimit(g_DeferredWork.WorkItems[Index].Name,
                                Registration->Name,
                                sizeof(g_DeferredWork.WorkItems[Index].Name));
            }

            DEBUG(TEXT("[DeferredWorkRegister] Registered work item %u (%s)"),
                  Index,
                  g_DeferredWork.WorkItems[Index].Name);
            return Index;
        }
    }

    ERROR(TEXT("[DeferredWorkRegister] No free deferred work slots"));
    return DEFERRED_WORK_INVALID_HANDLE;
}

/************************************************************************/

/**
 * @brief Registers a polling-only deferred work item.
 *
 * @param PollCallback Callback invoked during polling.
 * @param Context User-provided context passed to callback.
 * @param Name Debug name for the registration.
 *
 * @return Handle to the registered work item or DEFERRED_WORK_INVALID_HANDLE.
 */
U32 DeferredWorkRegisterPollOnly(DEFERRED_WORK_POLL_CALLBACK PollCallback, LPVOID Context, LPCSTR Name) {
    DEFERRED_WORK_REGISTRATION Registration;
    MemorySet(&Registration, 0, sizeof(Registration));
    Registration.WorkCallback = NULL;
    Registration.PollCallback = PollCallback;
    Registration.Context = Context;
    Registration.Name = Name;
    return DeferredWorkRegister(&Registration);
}

/************************************************************************/

/**
 * @brief Unregisters a deferred work item and clears its slot.
 *
 * @param Handle Deferred work handle to remove.
 */
void DeferredWorkUnregister(U32 Handle) {
    if (Handle >= DEFERRED_WORK_MAX_ITEMS) {
        return;
    }

    g_DeferredWork.WorkItems[Handle].InUse = FALSE;
    g_DeferredWork.WorkItems[Handle].WorkCallback = NULL;
    g_DeferredWork.WorkItems[Handle].PollCallback = NULL;
    g_DeferredWork.WorkItems[Handle].Context = NULL;
    g_DeferredWork.WorkItems[Handle].PendingCount = 0;
    MemorySet(g_DeferredWork.WorkItems[Handle].Name, 0, sizeof(g_DeferredWork.WorkItems[Handle].Name));

    DEBUG(TEXT("[DeferredWorkUnregister] Unregistered work item %u"), Handle);
}

/************************************************************************/

/**
 * @brief Signals a deferred work item to run its work callback.
 *
 * @param Handle Deferred work handle to signal.
 */
void DeferredWorkSignal(U32 Handle) {
    if (Handle >= DEFERRED_WORK_MAX_ITEMS) {
        return;
    }

    LPDEFERRED_WORK_ITEM Item = &g_DeferredWork.WorkItems[Handle];
    if (!Item->InUse || Item->WorkCallback == NULL) {
        return;
    }

    UINT Flags;
    SaveFlags(&Flags);
    DisableInterrupts();
    Item->PendingCount++;
    RestoreFlags(&Flags);

    SAFE_USE(g_DeferredWork.DeferredEvent) {
        SignalKernelEvent(g_DeferredWork.DeferredEvent);
    }
}

/************************************************************************/

/**
 * @brief Indicates if deferred work dispatching uses polling mode.
 *
 * @return TRUE when polling mode is enabled, FALSE otherwise.
 */
BOOL DeferredWorkIsPollingMode(void) {
    return g_DeferredWork.PollingMode;
}

/************************************************************************/

/**
 * @brief Processes pending deferred work callbacks until the queue drains.
 *
 * Resets the deferred event when no pending items remain.
 */
static void ProcessPendingWork(void) {
    BOOL WorkFound;

    do {
        WorkFound = FALSE;

        for (U32 Index = 0; Index < DEFERRED_WORK_MAX_ITEMS; Index++) {
            LPDEFERRED_WORK_ITEM Item = &g_DeferredWork.WorkItems[Index];
            if (!Item->InUse || Item->WorkCallback == NULL) {
                continue;
            }

            U32 Pending = 0;
            UINT Flags;
            SaveFlags(&Flags);
            DisableInterrupts();
            if (Item->PendingCount > 0U) {
                Pending = Item->PendingCount;
                Item->PendingCount = 0U;
            }
            RestoreFlags(&Flags);

            while (Pending > 0U) {
                Item->WorkCallback(Item->Context);
                Pending--;
                WorkFound = TRUE;
            }
        }
    } while (WorkFound);

    UINT Flags;
    SaveFlags(&Flags);
    DisableInterrupts();

    BOOL PendingLeft = FALSE;
    for (U32 Index = 0; Index < DEFERRED_WORK_MAX_ITEMS; Index++) {
        if (g_DeferredWork.WorkItems[Index].InUse && g_DeferredWork.WorkItems[Index].PendingCount > 0U) {
            PendingLeft = TRUE;
            break;
        }
    }

    if (!PendingLeft) {
        SAFE_USE(g_DeferredWork.DeferredEvent) {
            ResetKernelEvent(g_DeferredWork.DeferredEvent);
        }
    }

    RestoreFlags(&Flags);
}

/************************************************************************/

/**
 * @brief Runs all registered polling callbacks.
 */
static void ProcessPollCallbacks(void) {
    for (U32 Index = 0; Index < DEFERRED_WORK_MAX_ITEMS; Index++) {
        LPDEFERRED_WORK_ITEM Item = &g_DeferredWork.WorkItems[Index];
        if (!Item->InUse || Item->PollCallback == NULL) {
            continue;
        }

        Item->PollCallback(Item->Context);
    }
}

/************************************************************************/

/**
 * @brief Task entry point that dispatches deferred work based on mode.
 *
 * @param Param Unused task parameter.
 *
 * @return Always 0 when the task exits.
 */
static U32 DeferredWorkDispatcherTask(LPVOID Param) {
    UNUSED(Param);

    WAITINFO WaitInfo;
    MemorySet(&WaitInfo, 0, sizeof(WAITINFO));
    WaitInfo.Header.Size = sizeof(WAITINFO);
    WaitInfo.Header.Version = EXOS_ABI_VERSION;
    WaitInfo.Header.Flags = 0;
    WaitInfo.Count = 1;
    WaitInfo.Objects[0] = (HANDLE)g_DeferredWork.DeferredEvent;
    WaitInfo.MilliSeconds = GetDeferredWorkWaitTimeout();

    FOREVER {
        if (DeferredWorkIsPollingMode()) {
            ProcessPollCallbacks();
            Sleep(GetDeferredWorkPollDelay());
            continue;
        }

        WaitInfo.MilliSeconds = GetDeferredWorkWaitTimeout();
        U32 WaitResult = Wait(&WaitInfo);
        if (WaitResult == WAIT_TIMEOUT) {
            ProcessPollCallbacks();
            continue;
        }

        if (WaitResult != WAIT_OBJECT_0) {
            WARNING(TEXT("[DeferredWorkDispatcherTask] Unexpected wait result %u"), WaitResult);
            continue;
        }

        ProcessPendingWork();
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Driver command handler for deferred work initialization.
 *
 * DF_LOAD starts deferred work components once; DF_UNLOAD only clears readiness.
 */
static UINT DeferredWorkDriverCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD:
            if ((DeferredWorkDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            if (InitializeDeferredWork()) {
                DeferredWorkDriver.Flags |= DRIVER_FLAG_READY;
                return DF_RETURN_SUCCESS;
            }

            return DF_RETURN_UNEXPECTED;

        case DF_UNLOAD:
            if ((DeferredWorkDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            DeferredWorkDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(DEFERRED_WORK_VER_MAJOR, DEFERRED_WORK_VER_MINOR);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
