
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

#include "system/DeferredWork.h"

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

#define DEFERRED_WORK_MAX_ITEMS 16
#define DEFERRED_WORK_WAIT_TIMEOUT_MS 50
#define DEFERRED_WORK_POLL_DELAY_MS 5

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

static void ProcessPendingWork(void);
static void ProcessPollCallbacks(void);
static U32 DeferredWorkDispatcherTask(LPVOID Param);

/************************************************************************/

BOOL InitializeDeferredWork(void) {
    if (g_DispatcherStarted) {
        return TRUE;
    }

    MemorySet(g_WorkItems, 0, sizeof(g_WorkItems));

    g_DeferredEvent = CreateKernelEvent();
    if (g_DeferredEvent == NULL) {
        ERROR(TEXT("[InitializeDeferredWork] Failed to create deferred event"));
        return FALSE;
    }

    DEBUG(TEXT("[InitializeDeferredWork] Deferred event created at %p"), g_DeferredEvent);

    DeferredWorkUpdateMode();

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
    if (Registration == NULL || Registration->WorkCallback == NULL) {
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
    if (!Item->InUse) {
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

void DeferredWorkUpdateMode(void) {
    LPCSTR ModeValue = GetConfigurationValue(TEXT("General.Polling"));
    g_PollingMode = (ModeValue != NULL && STRINGS_EQUAL(ModeValue, TEXT("1"))) ? TRUE : FALSE;
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
    WaitInfo.MilliSeconds = DEFERRED_WORK_WAIT_TIMEOUT_MS;

    FOREVER {
        DeferredWorkUpdateMode();

        if (DeferredWorkIsPollingMode()) {
            ProcessPollCallbacks();
            Sleep(DEFERRED_WORK_POLL_DELAY_MS);
            continue;
        }

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
