
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

/************************************************************************/

static DEFERRED_WORK_ITEM g_WorkItems[DEFERRED_WORK_MAX_ITEMS];
static LPKERNEL_EVENT g_DeferredEvent = NULL;
static BOOL g_PollingMode = FALSE;
static BOOL g_DispatcherStarted = FALSE;

/************************************************************************/

#define DEFERRED_WORK_VER_MAJOR 1
#define DEFERRED_WORK_VER_MINOR 0

static UINT DeferredWorkDriverCommands(UINT Function, UINT Parameter);

DRIVER DeferredWorkDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_OTHER,
    .VersionMajor = DEFERRED_WORK_VER_MAJOR,
    .VersionMinor = DEFERRED_WORK_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "DeferredWork",
    .Flags = DRIVER_FLAG_CRITICAL,
    .Command = DeferredWorkDriverCommands};

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
                return DF_ERROR_SUCCESS;
            }

            if (InitializeDeferredWork()) {
                return DF_ERROR_SUCCESS;
            }

            return DF_ERROR_UNEXPECT;

        case DF_UNLOAD:
            if ((DeferredWorkDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_ERROR_SUCCESS;
            }

            DeferredWorkDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_ERROR_SUCCESS;

        case DF_GETVERSION:
            return MAKE_VERSION(DEFERRED_WORK_VER_MAJOR, DEFERRED_WORK_VER_MINOR);
    }

    return DF_ERROR_NOTIMPL;
}

/************************************************************************/

static void ProcessPendingWork(void);
static void ProcessPollCallbacks(void);
static U32 DeferredWorkDispatcherTask(LPVOID Param);
static BOOL DeferredWorkDispatch(LPVOID Param);

/************************************************************************/

BOOL InitializeDeferredWork(void) {
    if (g_DispatcherStarted) {
        return TRUE;
    }

    Kernel.DeferredWorkWaitTimeoutMS = DEFERRED_WORK_WAIT_TIMEOUT_MS;
    Kernel.DeferredWorkPollDelayMS = DEFERRED_WORK_POLL_DELAY_MS;

    LPCSTR WaitTimeoutValue = GetConfigurationValue(TEXT(CONFIG_GENERAL_DEFERRED_WORK_WAIT_TIMEOUT_MS));
    if (STRING_EMPTY(WaitTimeoutValue) == FALSE) {
        Kernel.DeferredWorkWaitTimeoutMS = StringToU32(WaitTimeoutValue);
    }

    LPCSTR PollDelayValue = GetConfigurationValue(TEXT(CONFIG_GENERAL_DEFERRED_WORK_POLL_DELAY_MS));
    if (STRING_EMPTY(PollDelayValue) == FALSE) {
        Kernel.DeferredWorkPollDelayMS = StringToU32(PollDelayValue);
    }

    MemorySet(g_WorkItems, 0, sizeof(g_WorkItems));

    g_DeferredEvent = CreateKernelEvent();
    if (g_DeferredEvent == NULL) {
        ERROR(TEXT("[InitializeDeferredWork] Failed to create deferred event"));
        return FALSE;
    }

    DEBUG(TEXT("[InitializeDeferredWork] Deferred event created at %p"), g_DeferredEvent);

    LPCSTR ModeValue = GetConfigurationValue(TEXT(CONFIG_GENERAL_POLLING));
    if (STRING_EMPTY(ModeValue) == FALSE) {
        U32 Numeric = StringToU32(ModeValue);
        if (Numeric != 0) {
            g_PollingMode = TRUE;
        } else if (StringCompareNC(ModeValue, TEXT("true")) == 0) {
            g_PollingMode = TRUE;
        }
    }

    if (g_PollingMode == TRUE) {
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
        DeleteKernelEvent(g_DeferredEvent);
        g_DeferredEvent = NULL;
        return FALSE;
    }

    g_DispatcherStarted = TRUE;
    DEBUG(TEXT("[InitializeDeferredWork] Dispatcher task started"));
    return TRUE;
}

/************************************************************************/

void ShutdownDeferredWork(void) {
    g_DispatcherStarted = FALSE;
    g_PollingMode = FALSE;
    SAFE_USE(g_DeferredEvent) {
        ResetKernelEvent(g_DeferredEvent);
    }
}

/************************************************************************/

U32 DeferredWorkRegister(const DEFERRED_WORK_REGISTRATION *Registration) {
    if (Registration == NULL) {
        return DEFERRED_WORK_INVALID_HANDLE;
    }

    if (Registration->WorkCallback == NULL && Registration->PollCallback == NULL) {
        return DEFERRED_WORK_INVALID_HANDLE;
    }

    for (U32 Index = 0; Index < DEFERRED_WORK_MAX_ITEMS; Index++) {
        if (!g_WorkItems[Index].InUse) {
            g_WorkItems[Index].InUse = TRUE;
            g_WorkItems[Index].WorkCallback = Registration->WorkCallback;
            g_WorkItems[Index].PollCallback = Registration->PollCallback;
            g_WorkItems[Index].Context = Registration->Context;
            g_WorkItems[Index].PendingCount = 0;
            MemorySet(g_WorkItems[Index].Name, 0, sizeof(g_WorkItems[Index].Name));
            if (Registration->Name) {
                StringCopyLimit(g_WorkItems[Index].Name, Registration->Name, sizeof(g_WorkItems[Index].Name));
            }

            DEBUG(TEXT("[DeferredWorkRegister] Registered work item %u (%s)"),
                  Index,
                  g_WorkItems[Index].Name);
            return Index;
        }
    }

    ERROR(TEXT("[DeferredWorkRegister] No free deferred work slots"));
    return DEFERRED_WORK_INVALID_HANDLE;
}

/************************************************************************/

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

void DeferredWorkUnregister(U32 Handle) {
    if (Handle >= DEFERRED_WORK_MAX_ITEMS) {
        return;
    }

    g_WorkItems[Handle].InUse = FALSE;
    g_WorkItems[Handle].WorkCallback = NULL;
    g_WorkItems[Handle].PollCallback = NULL;
    g_WorkItems[Handle].Context = NULL;
    g_WorkItems[Handle].PendingCount = 0;
    MemorySet(g_WorkItems[Handle].Name, 0, sizeof(g_WorkItems[Handle].Name));

    DEBUG(TEXT("[DeferredWorkUnregister] Unregistered work item %u"), Handle);
}

/************************************************************************/

void DeferredWorkSignal(U32 Handle) {
    if (Handle >= DEFERRED_WORK_MAX_ITEMS) {
        return;
    }

    LPDEFERRED_WORK_ITEM Item = &g_WorkItems[Handle];
    if (!Item->InUse || Item->WorkCallback == NULL) {
        return;
    }

    UINT Flags;
    SaveFlags(&Flags);
    DisableInterrupts();
    Item->PendingCount++;
    RestoreFlags(&Flags);

    SAFE_USE(g_DeferredEvent) {
        SignalKernelEvent(g_DeferredEvent);
    }
}

/************************************************************************/

BOOL DeferredWorkIsPollingMode(void) {
    return g_PollingMode;
}

/************************************************************************/

static void ProcessPendingWork(void) {
    BOOL WorkFound;

    do {
        WorkFound = FALSE;

        for (U32 Index = 0; Index < DEFERRED_WORK_MAX_ITEMS; Index++) {
            LPDEFERRED_WORK_ITEM Item = &g_WorkItems[Index];
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
        if (g_WorkItems[Index].InUse && g_WorkItems[Index].PendingCount > 0U) {
            PendingLeft = TRUE;
            break;
        }
    }

    if (!PendingLeft) {
        SAFE_USE(g_DeferredEvent) {
            ResetKernelEvent(g_DeferredEvent);
        }
    }

    RestoreFlags(&Flags);
}

/************************************************************************/

static void ProcessPollCallbacks(void) {
    for (U32 Index = 0; Index < DEFERRED_WORK_MAX_ITEMS; Index++) {
        LPDEFERRED_WORK_ITEM Item = &g_WorkItems[Index];
        if (!Item->InUse || Item->PollCallback == NULL) {
            continue;
        }

        Item->PollCallback(Item->Context);
    }
}

/************************************************************************/

static U32 DeferredWorkDispatcherTask(LPVOID Param) {
    UNUSED(Param);

    WAITINFO WaitInfo;
    MemorySet(&WaitInfo, 0, sizeof(WAITINFO));
    WaitInfo.Header.Size = sizeof(WAITINFO);
    WaitInfo.Header.Version = EXOS_ABI_VERSION;
    WaitInfo.Header.Flags = 0;
    WaitInfo.Count = 1;
    WaitInfo.Objects[0] = (HANDLE)g_DeferredEvent;
    WaitInfo.MilliSeconds = Kernel.DeferredWorkWaitTimeoutMS;

    FOREVER {
        if (DeferredWorkIsPollingMode()) {
            ProcessPollCallbacks();
            Sleep(Kernel.DeferredWorkPollDelayMS);
            continue;
        }

        WaitInfo.MilliSeconds = Kernel.DeferredWorkWaitTimeoutMS;
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
