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


    Deadlock monitor

\************************************************************************/

#include "utils/DeadlockMonitor.h"

#include "Clock.h"
#include "console/Console.h"
#include "Log.h"
#include "Memory.h"
#include "Mutex.h"
#include "process/Task.h"
#include "utils/RateLimiter.h"

/************************************************************************/

#define DEADLOCK_MONITOR_MAX_CHAIN_DEPTH 32

/************************************************************************/

static RATE_LIMITER DATA_SECTION DeadlockMonitorCycleLogLimiter = {0};
static BOOL DATA_SECTION DeadlockMonitorCycleLogLimiterInitialized = FALSE;

/************************************************************************/

/**
 * @brief Ensure the cycle log limiter is initialized once.
 *
 * @return TRUE when the limiter can be used.
 */
static BOOL DeadlockMonitorEnsureCycleLogLimiter(void) {
    if (DeadlockMonitorCycleLogLimiterInitialized != FALSE) {
        return TRUE;
    }

    if (RateLimiterInit(&DeadlockMonitorCycleLogLimiter, 2, 1000) == FALSE) {
        return FALSE;
    }

    DeadlockMonitorCycleLogLimiterInitialized = TRUE;
    return TRUE;
}

/**
 * @brief Validate one mutex pointer for deadlock analysis.
 *
 * @param Mutex Mutex pointer to validate.
 * @return Valid mutex pointer, or NULL when invalid.
 */
static LPMUTEX DeadlockMonitorGetValidMutex(LPMUTEX Mutex) {
    SAFE_USE_VALID_ID(Mutex, KOID_MUTEX) {
        return Mutex;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Validate one task pointer for deadlock analysis.
 *
 * @param Task Task pointer to validate.
 * @return Valid task pointer, or NULL when invalid.
 */
static LPTASK DeadlockMonitorGetValidTask(LPTASK Task) {
    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        return Task;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Clear mutex wait tracking for one task when it matches the target mutex.
 *
 * @param Task Task whose wait state is updated.
 * @param Mutex Mutex to clear, or NULL to clear unconditionally.
 */
static void DeadlockMonitorClearWaitState(LPTASK Task, LPMUTEX Mutex) {
    Task = DeadlockMonitorGetValidTask(Task);
    if (Task == NULL) {
        return;
    }

    if (Mutex != NULL && Task->WaitingMutex != Mutex) {
        return;
    }

    Task->WaitingMutex = NULL;
    Task->WaitingSince = 0;
}

/************************************************************************/

/**
 * @brief Log one confirmed mutex deadlock chain.
 *
 * @param WaiterTask Task that started the wait.
 * @param Mutex Initial mutex waited by the task.
 */
static void DeadlockMonitorLogCycle(LPTASK WaiterTask, LPMUTEX Mutex) {
    UINT Depth;
    U32 Now;
    U32 Suppressed = 0;
    LPTASK OwnerTask;
    LPTASK CurrentTask;
    LPMUTEX CurrentMutex;

    WaiterTask = DeadlockMonitorGetValidTask(WaiterTask);
    Mutex = DeadlockMonitorGetValidMutex(Mutex);
    if (WaiterTask == NULL || Mutex == NULL) {
        return;
    }

    OwnerTask = DeadlockMonitorGetValidTask(Mutex->Task);
    if (OwnerTask == NULL) {
        return;
    }

    Now = GetSystemTime();
    if (DeadlockMonitorEnsureCycleLogLimiter() != FALSE) {
        if (RateLimiterShouldTrigger(&DeadlockMonitorCycleLogLimiter, Now, &Suppressed) == FALSE) {
            return;
        }
    }

    ERROR(TEXT("[DeadlockMonitorLogCycle] Mutex deadlock detected waiter=%p (%s) mutex=%p owner=%p (%s) suppressed=%u"),
          WaiterTask,
          WaiterTask->Name[0] != STR_NULL ? WaiterTask->Name : TEXT("Unnamed"),
          Mutex,
          OwnerTask,
          OwnerTask->Name[0] != STR_NULL ? OwnerTask->Name : TEXT("Unnamed"),
          Suppressed);

    CurrentTask = WaiterTask;
    CurrentMutex = Mutex;

    for (Depth = 0; Depth < DEADLOCK_MONITOR_MAX_CHAIN_DEPTH; Depth++) {
        OwnerTask = DeadlockMonitorGetValidTask(CurrentMutex->Task);
        if (OwnerTask == NULL) {
            DEBUG(TEXT("[DeadlockMonitorLogCycle] Chain[%u] task=%p (%s) waits for mutex=%p with no valid owner"),
                  Depth,
                  CurrentTask,
                  CurrentTask->Name[0] != STR_NULL ? CurrentTask->Name : TEXT("Unnamed"),
                  CurrentMutex);
            return;
        }

        DEBUG(TEXT("[DeadlockMonitorLogCycle] Chain[%u] task=%p (%s) waits for mutex=%p owned by task=%p (%s)"),
              Depth,
              CurrentTask,
              CurrentTask->Name[0] != STR_NULL ? CurrentTask->Name : TEXT("Unnamed"),
              CurrentMutex,
              OwnerTask,
              OwnerTask->Name[0] != STR_NULL ? OwnerTask->Name : TEXT("Unnamed"));

        if (OwnerTask == WaiterTask) {
            return;
        }

        CurrentTask = OwnerTask;
        CurrentMutex = DeadlockMonitorGetValidMutex(CurrentTask->WaitingMutex);
        if (CurrentMutex == NULL) {
            DEBUG(TEXT("[DeadlockMonitorLogCycle] Chain[%u] task=%p (%s) has no waited mutex"),
                  Depth + 1,
                  CurrentTask,
                  CurrentTask->Name[0] != STR_NULL ? CurrentTask->Name : TEXT("Unnamed"));
            return;
        }
    }

    DEBUG(TEXT("[DeadlockMonitorLogCycle] Chain truncated at depth=%u"), DEADLOCK_MONITOR_MAX_CHAIN_DEPTH);

#if DEBUG_OUTPUT == 1
    ConsolePanic(TEXT("Mutex deadlock detected"));
#endif
}

/************************************************************************/

/**
 * @brief Record that one task starts waiting on one mutex.
 *
 * @param Task Waiting task.
 * @param Mutex Target mutex.
 */
void DeadlockMonitorOnWaitStart(LPTASK Task, LPMUTEX Mutex) {
    Task = DeadlockMonitorGetValidTask(Task);
    Mutex = DeadlockMonitorGetValidMutex(Mutex);
    if (Task == NULL || Mutex == NULL) {
        return;
    }

    Task->WaitingMutex = Mutex;
    Task->WaitingSince = GetSystemTime();

    if (DeadlockMonitorWouldCreateCycle(Task, Mutex) != FALSE) {
        DeadlockMonitorLogCycle(Task, Mutex);
    }
}

/************************************************************************/

/**
 * @brief Record that one mutex wait was canceled or ended without acquisition.
 *
 * @param Task Waiting task.
 * @param Mutex Waited mutex.
 */
void DeadlockMonitorOnWaitCancel(LPTASK Task, LPMUTEX Mutex) {
    DeadlockMonitorClearWaitState(Task, Mutex);
}

/************************************************************************/

/**
 * @brief Record that one task acquired one mutex after a wait.
 *
 * @param Task Owner task.
 * @param Mutex Acquired mutex.
 */
void DeadlockMonitorOnAcquire(LPTASK Task, LPMUTEX Mutex) {
    DeadlockMonitorClearWaitState(Task, Mutex);
}

/************************************************************************/

/**
 * @brief Record that one task released one mutex.
 *
 * @param Task Releasing task.
 * @param Mutex Released mutex.
 * @param NextOwner Next owner if known.
 */
void DeadlockMonitorOnRelease(LPTASK Task, LPMUTEX Mutex, LPTASK NextOwner) {
    UNUSED(Task);
    UNUSED(Mutex);
    UNUSED(NextOwner);
}

/************************************************************************/

/**
 * @brief Check whether a blocking wait would create a mutex wait cycle.
 *
 * @param Task Waiting task.
 * @param Mutex Target mutex already owned by another task.
 * @return TRUE if a cycle is found, FALSE otherwise.
 */
BOOL DeadlockMonitorWouldCreateCycle(LPTASK Task, LPMUTEX Mutex) {
    UINT Depth;
    LPTASK WaiterTask;
    LPTASK CurrentOwner;
    LPMUTEX CurrentWaitedMutex;

    WaiterTask = DeadlockMonitorGetValidTask(Task);
    Mutex = DeadlockMonitorGetValidMutex(Mutex);
    if (WaiterTask == NULL || Mutex == NULL) {
        return FALSE;
    }

    CurrentOwner = DeadlockMonitorGetValidTask(Mutex->Task);
    if (CurrentOwner == NULL || CurrentOwner == WaiterTask) {
        return FALSE;
    }

    for (Depth = 0; Depth < DEADLOCK_MONITOR_MAX_CHAIN_DEPTH; Depth++) {
        if (CurrentOwner == WaiterTask) {
            return TRUE;
        }

        CurrentWaitedMutex = DeadlockMonitorGetValidMutex(CurrentOwner->WaitingMutex);
        if (CurrentWaitedMutex == NULL) {
            return FALSE;
        }

        CurrentOwner = DeadlockMonitorGetValidTask(CurrentWaitedMutex->Task);
        if (CurrentOwner == NULL) {
            return FALSE;
        }
    }

    return FALSE;
}

/************************************************************************/
