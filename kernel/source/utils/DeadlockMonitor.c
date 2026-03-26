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
#include "Memory.h"
#include "Mutex.h"
#include "process/Task.h"

/************************************************************************/

#define DEADLOCK_MONITOR_MAX_CHAIN_DEPTH 32

/************************************************************************/

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
