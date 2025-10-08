
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


    Schedule

\************************************************************************/

#include "Base.h"
#include "Clock.h"
#include "ID.h"
#include "Kernel.h"
#include "List.h"
#include "Log.h"
#include "Memory.h"
#include "Process.h"
#include "Stack.h"
#include "System.h"
#include "Task.h"

/***************************************************************************/

typedef struct tag_TASKLIST {
    volatile U32 Freeze;
    volatile U32 SchedulerTime;
    volatile UINT NumTasks;
    volatile UINT CurrentIndex;  // Index of current task instead of pointer
    LPTASK Tasks[NUM_TASKS];
} TASKLIST, *LPTASKLIST;

/***************************************************************************/

static TASKLIST TaskList = {.Freeze = 0, .SchedulerTime = 0, .NumTasks = 0, .CurrentIndex = 0, .Tasks = {NULL}};

/***************************************************************************/

/**
 * @brief Wakes up tasks whose sleep time has expired.
 *
 * Centralized function to check and wake sleeping tasks. Should be called
 * periodically by the scheduler or timer interrupt.
 */
static void WakeUpExpiredTasks(void) {
    U32 CurrentTime = GetSystemTime();

    for (UINT Index = 0; Index < TaskList.NumTasks; Index++) {
        LPTASK Task = TaskList.Tasks[Index];

        if (GetTaskStatus(Task) == TASK_STATUS_SLEEPING && CurrentTime >= Task->WakeUpTime) {
            SetTaskStatus(Task, TASK_STATUS_RUNNING);
        }
    }
}

/***************************************************************************/

/**
 * @brief Removes dead tasks from the scheduler queue during context switches.
 *
 * Called when switching TO a task that is not dead. Removes all dead tasks
 * from the queue to prevent them from being scheduled again. This is done
 * during context switches to avoid silent CurrentIndex adjustments.
 *
 * @param ExceptTask Task pointer we're switching to (don't remove it)
 * @return New index of ExceptTask after removals
 */
static UINT RemoveDeadTasksFromQueue(LPTASK ExceptTask) {
    UINT NewExceptIndex = INFINITY;

    // Search backwards to handle array compaction safely
    for (I32 Index = (I32)(TaskList.NumTasks - 1); Index >= 0; Index--) {
        LPTASK Task = TaskList.Tasks[Index];

        if (GetTaskStatus(Task) == TASK_STATUS_DEAD && Task != ExceptTask) {
#if SCHEDULING_DEBUG_OUTPUT == 1
            KernelLogText(
                LOG_DEBUG, TEXT("[RemoveDeadTasksFromQueue] Removing dead task %s at index %d"), Task->Name, Index);
#endif

            // Shift remaining tasks down
            for (UINT ShiftIndex = (UINT)Index; ShiftIndex < TaskList.NumTasks - 1; ShiftIndex++) {
                TaskList.Tasks[ShiftIndex] = TaskList.Tasks[ShiftIndex + 1];
            }

            TaskList.NumTasks--;
            TaskList.Tasks[TaskList.NumTasks] = NULL;  // Clear last slot
        }
    }

    // Find new index of ExceptTask
    for (UINT Index = 0; Index < TaskList.NumTasks; Index++) {
        if (TaskList.Tasks[Index] == ExceptTask) {
            NewExceptIndex = Index;
            break;
        }
    }

    return NewExceptIndex;
}

/***************************************************************************/

/**
 * @brief Counts the number of tasks that are ready to run.
 *
 * Also wakes up any sleeping tasks whose wake-up time has expired.
 *
 * @return Number of runnable tasks
 */
static UINT CountRunnableTasks(void) {
    UINT RunnableCount = 0;

    for (UINT Index = 0; Index < TaskList.NumTasks; Index++) {
        LPTASK Task = TaskList.Tasks[Index];

        U32 Status = GetTaskStatus(Task);
        if (Status == TASK_STATUS_READY || Status == TASK_STATUS_RUNNING) {
            RunnableCount++;
        }
    }

    return RunnableCount;
}

/***************************************************************************/

/**
 * @brief Finds the next runnable task starting from a given index.
 *
 * Performs round-robin search through the task list, skipping dead and sleeping tasks.
 * Returns the index of the next runnable task.
 *
 * @param StartIndex Index to start searching from
 * @return Index of next runnable task, or INFINITY if none found
 */
UINT FindNextRunnableTask(UINT StartIndex) {
    for (UINT Attempts = 0; Attempts < TaskList.NumTasks; Attempts++) {
        UINT Index = (StartIndex + Attempts) % TaskList.NumTasks;
        LPTASK Task = TaskList.Tasks[Index];

        // Skip dead tasks - they will be removed during context switch
        U32 Status = GetTaskStatus(Task);
        if (Status == TASK_STATUS_READY || Status == TASK_STATUS_RUNNING) {
            return Index;
        }
    }

    return INFINITY;  // No runnable task found
}

/************************************************************************/

/**
 * @brief Adds a task to the scheduler's execution queue.
 *
 * Validates the task, checks for duplicates and capacity, then adds the task
 * to the scheduling queue. Calculates and assigns the task's quantum time
 * based on its priority. If this is the first task, makes it current.
 *
 * @param NewTask Pointer to task to add to scheduler queue
 * @return TRUE if task added successfully, FALSE on error or capacity exceeded
 */
BOOL AddTaskToQueue(LPTASK NewTask) {
    TRACED_FUNCTION;

#if SCHEDULING_DEBUG_OUTPUT == 1
    DEBUG(TEXT("[AddTaskToQueue] NewTask = %x"), NewTask);
#endif

    FreezeScheduler();

    // Check validity of parameters
    SAFE_USE_VALID_ID(NewTask, KOID_TASK) {
        // Check if task queue is full
        if (TaskList.NumTasks >= NUM_TASKS) {
            ERROR(TEXT("[AddTaskToQueue] Cannot add %x, too many tasks"), NewTask);
            UnfreezeScheduler();

            TRACED_EPILOGUE("AddTaskToQueue");
            return FALSE;
        }

        // Check if task already in task queue
        for (UINT Index = 0; Index < TaskList.NumTasks; Index++) {
            if (TaskList.Tasks[Index] == NewTask) {
                UnfreezeScheduler();

                TRACED_EPILOGUE("AddTaskToQueue");
                return TRUE;  // Already present, success
            }
        }

        // Add task to queue
#if SCHEDULING_DEBUG_OUTPUT == 1
        DEBUG(TEXT("[AddTaskToQueue] Adding %X"), NewTask);
#endif

        TaskList.Tasks[TaskList.NumTasks] = NewTask;

        // Set quantum time for this task
        SetTaskWakeUpTime(NewTask, ComputeTaskQuantumTime(NewTask->Priority));

        // If this is the first task, make it current
        if (TaskList.NumTasks == 0) {
            TaskList.CurrentIndex = 0;
        }

        TaskList.NumTasks++;

        UnfreezeScheduler();

        TRACED_EPILOGUE("AddTaskToQueue");
        return TRUE;
    }

    UnfreezeScheduler();
    TRACED_EPILOGUE("AddTaskToQueue");
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Removes a task from the scheduler's execution queue.
 *
 * Searches for the task in the queue and removes it, compacting the array.
 * Adjusts the current task index appropriately to maintain scheduling order.
 *
 * @param OldTask Pointer to task to remove from scheduler queue
 * @return TRUE if task removed successfully, FALSE if not found
 */
BOOL RemoveTaskFromQueue(LPTASK OldTask) {
    TRACED_FUNCTION;

    FreezeScheduler();

    for (UINT Index = 0; Index < TaskList.NumTasks; Index++) {
        if (TaskList.Tasks[Index] == OldTask) {
            // If removing current task, adjust current index
            if (Index == TaskList.CurrentIndex) {
                // Find next runnable task or wrap around
                if (TaskList.NumTasks > 1) {
                    TaskList.CurrentIndex = FindNextRunnableTask((Index + 1) % TaskList.NumTasks);

                    if (TaskList.CurrentIndex >= TaskList.NumTasks - 1) {
                        TaskList.CurrentIndex = 0;  // Wrap to beginning
                    }
                } else {
                    TaskList.CurrentIndex = 0;
                }
            } else if (Index < TaskList.CurrentIndex) {
                TaskList.CurrentIndex--;  // Adjust index after removal
            }

            // Shift remaining tasks
            for (UINT ShiftIndex = Index; ShiftIndex < TaskList.NumTasks - 1; ShiftIndex++) {
                TaskList.Tasks[ShiftIndex] = TaskList.Tasks[ShiftIndex + 1];
            }

            TaskList.NumTasks--;
            TaskList.Tasks[TaskList.NumTasks] = NULL;  // Clear last slot

            UnfreezeScheduler();

            TRACED_EPILOGUE("RemoveTaskFromQueue");
            return TRUE;
        }
    }

    UnfreezeScheduler();

    TRACED_EPILOGUE("RemoveTaskFromQueue");
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Returns the process that owns the currently executing task.
 *
 * @return Pointer to current process, or KernelProcess if no current task
 */
LPPROCESS GetCurrentProcess(void) {
    LPTASK Task = GetCurrentTask();
    SAFE_USE(Task) { return Task->Process; }
    return &KernelProcess;
}

/***************************************************************************/

/**
 * @brief Returns the currently executing task.
 *
 * @return Pointer to current task, or NULL if no tasks are scheduled
 */
LPTASK GetCurrentTask(void) {
    LPTASK Task = NULL;

    FreezeScheduler();
    if (TaskList.NumTasks == 0 || TaskList.CurrentIndex >= TaskList.NumTasks) {
    } else {
        Task = TaskList.Tasks[TaskList.CurrentIndex];
    }
    UnfreezeScheduler();

    return Task;
}

/***************************************************************************/

/**
 * @brief Temporarily disables task switching.
 *
 * Increments the freeze counter to prevent the scheduler from switching tasks.
 * Used for atomic operations that must not be interrupted by task switches.
 *
 * @return Always TRUE
 */
BOOL FreezeScheduler(void) {
    U32 Flags;
    SaveFlags(&Flags);
    DisableInterrupts();
    TaskList.Freeze++;
    RestoreFlags(&Flags);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Re-enables task switching.
 *
 * Decrements the freeze counter. Task switching is only enabled when the
 * counter reaches zero, allowing nested freeze/unfreeze calls.
 *
 * @return Always TRUE
 */
BOOL UnfreezeScheduler(void) {
    U32 Flags;
    SaveFlags(&Flags);
    DisableInterrupts();
    if (TaskList.Freeze) TaskList.Freeze--;
    RestoreFlags(&Flags);
    return TRUE;
}

/************************************************************************/

void SwitchToNextTask(LPTASK CurrentTask, LPTASK NextTask) {
#if SCHEDULING_DEBUG_OUTPUT == 1
    DEBUG(TEXT("[SwitchToNextTask] Enter %x"), NextTask);
#endif

    if (NextTask->Status > TASK_STATUS_DEAD) {
        KernelLogText(
            LOG_ERROR, TEXT("[SwitchToNextTask_3] MEMORY CORRUPTION: Task status %x is out of range"),
            NextTask->Status);
        return;
    }

    // SAFE_USE_VALID_ID_2(CurrentTask, NextTask, KOID_TASK) {
        // __asm__ __volatile__("xchg %%bx,%%bx" : : );     // A breakpoint
        SwitchToNextTask_2(CurrentTask, NextTask);
    // }

#if SCHEDULING_DEBUG_OUTPUT == 1
    DEBUG(TEXT("[SwitchToNextTask] Exit for task %x"), CurrentTask);
#endif
}

/************************************************************************/

void SwitchToNextTask_3(register LPTASK CurrentTask, register LPTASK NextTask) {

    // Set up system stack for new task
    LINEAR NextSysStackTop = NextTask->SysStackBase + NextTask->SysStackSize;

    Kernel_i386.TSS->SS0 = SELECTOR_KERNEL_DATA;
    Kernel_i386.TSS->ESP0 = NextSysStackTop - STACK_SAFETY_MARGIN;

    GetFS(CurrentTask->Context.Registers.FS);
    GetGS(CurrentTask->Context.Registers.GS);

    SaveFPU((LPVOID)&(CurrentTask->Context.FPURegisters));

    // Load CR3 only if different than current
    LoadPageDirectory(NextTask->Process->PageDirectory);

    SetDS(NextTask->Context.Registers.DS);
    SetES(NextTask->Context.Registers.ES);
    SetFS(NextTask->Context.Registers.FS);
    SetGS(NextTask->Context.Registers.GS);

    RestoreFPU(&(NextTask->Context.FPURegisters));

    U32 CurrentTaskStatus = GetTaskStatus(NextTask);

    // First time run for the task
    if (CurrentTaskStatus == TASK_STATUS_READY) {
        SetTaskStatus(NextTask, TASK_STATUS_RUNNING);

        if (NextTask->Process->Privilege == PRIVILEGE_KERNEL) {
            LINEAR ESP = NextTask->StackBase + NextTask->StackSize - STACK_SAFETY_MARGIN;
            SetupStackForKernelMode(NextTask, ESP);
            JumpToReadyTask(NextTask, ESP);
        } else {
            LINEAR ESP = NextTask->SysStackBase + NextTask->SysStackSize - STACK_SAFETY_MARGIN;
            SetupStackForUserMode(NextTask, ESP, NextTask->StackBase + NextTask->StackSize - STACK_SAFETY_MARGIN);
            JumpToReadyTask(NextTask, ESP);
        }
    }

    // Returning normally to next task
}

/************************************************************************/

/**
 * @brief Main scheduler function called by timer interrupt.
 *
 * This is the core scheduling function that implements preemptive multitasking.
 * It saves the current task context, checks for stack overflows, manages task
 * quantums, wakes sleeping tasks, and selects the next task to run. Performs
 * context switching by setting up ESP0 for Ring 3 tasks and returning the
 * interrupt frame of the next task.
 *
 */
void Scheduler(void) {
#if SCHEDULING_DEBUG_OUTPUT == 1
    U32 Flags;
    SaveFlags(&Flags);
    DEBUG(TEXT("[Scheduler] Enter : INT = %x, IF = %x"), Frame->IntNo, Flags & 0x200);
#endif

    // If scheduler is frozen, don't switch (atomic read - safe in interrupt context)
    if (TaskList.Freeze) {
#if SCHEDULING_DEBUG_OUTPUT == 1
        DEBUG(TEXT("[Scheduler] TaskList frozen: Returning NULL"));
#endif
        return;
    }

    TaskList.SchedulerTime += 10;

    // Check for stack overflow - kill dangerous tasks immediately
    /*
    if (!CheckStack()) {
        LPTASK DangerousTask = GetCurrentTask();

        if (DangerousTask) {

            ERROR(TEXT("[Scheduler] Killing task due to overflow : %X"), DangerousTask);

            // Mark task as dead - will be removed during next context switch
            DangerousTask->Status = TASK_STATUS_DEAD;
        }
    }
    */

    // No tasks to schedule
    if (TaskList.NumTasks == 0) {
        return;
    }

    // Check if current task quantum has expired
    LPTASK CurrentTask = (TaskList.CurrentIndex < TaskList.NumTasks) ? TaskList.Tasks[TaskList.CurrentIndex] : NULL;
    BOOL QuantumExpired = CurrentTask && GetSystemTime() >= CurrentTask->WakeUpTime;

    // Wake up expired sleeping tasks first
    WakeUpExpiredTasks();

    // Update sleeping tasks status
    UINT RunnableCount = CountRunnableTasks();

    // No runnable tasks - system idle
    if (RunnableCount == 0) {
#if SCHEDULING_DEBUG_OUTPUT == 1
        DEBUG(TEXT("[Scheduler] No runnable tasks"));
#endif

        return;
    }

    // If current task is still running and quantum not expired, keep it
    if (CurrentTask && CurrentTask->Status == TASK_STATUS_RUNNING && !QuantumExpired) {
#if SCHEDULING_DEBUG_OUTPUT == 1
        DEBUG(TEXT("[Scheduler] Current task continues"));
#endif

        return;
    }

    // Time to switch - find next runnable task
    UINT NextIndex = FindNextRunnableTask((TaskList.CurrentIndex + 1) % TaskList.NumTasks);

    if (TaskList.CurrentIndex != NextIndex) {
        // Get task pointers BEFORE any queue manipulation
        LPTASK CurrentTask = (TaskList.CurrentIndex < TaskList.NumTasks) ? TaskList.Tasks[TaskList.CurrentIndex] : NULL;
        LPTASK NextTask = TaskList.Tasks[NextIndex];

#if SCHEDULING_DEBUG_OUTPUT == 1
        KernelLogText(
            LOG_DEBUG, TEXT("[Scheduler] Switch between task index %u (%s @ %s) and %u (%s @ %s)"),
            TaskList.CurrentIndex, CurrentTask ? CurrentTask->Name : TEXT("NULL"),
            CurrentTask ? CurrentTask->Process->FileName : TEXT("NULL"), NextIndex, NextTask->Name,
            NextTask->Process->FileName);
#endif

        if (NextIndex >= TaskList.NumTasks) {
            // Should not happen if RunnableCount > 0, but safety check
#if SCHEDULING_DEBUG_OUTPUT == 1
            DEBUG(TEXT("[Scheduler] No next task found"));
#endif

            return;
        }

        // Remove dead tasks from queue now that we're switching TO a non-dead task
        // This prevents silent CurrentIndex adjustments and phantom task changes
        if (NextTask->Status != TASK_STATUS_DEAD) {
            NextIndex = RemoveDeadTasksFromQueue(NextTask);

            if (NextIndex == INFINITY) {
                // NextTask was somehow removed - this should not happen
                ERROR(TEXT("[Scheduler] NextTask was removed during cleanup!"));
                return;
            }
        }

        TaskList.CurrentIndex = NextIndex;
        TaskList.SchedulerTime = 0;

        if (CurrentTask && CurrentTask->Process != NextTask->Process &&
            CurrentTask->Process->Privilege != NextTask->Process->Privilege) {
#if SCHEDULING_DEBUG_OUTPUT == 1
            DEBUG(TEXT("[Scheduler] Different ring switch :"));
            LogFrame(CurrentTask, &CurrentTask->Context);
            LogFrame(NextTask, &NextTask->Context);
#endif
        }

        SwitchToNextTask(CurrentTask, NextTask);
    }
}

/************************************************************************/

static BOOL MatchObject(LPVOID Data, LPVOID Context) {
    LPOBJECT_TERMINATION_STATE State = (LPOBJECT_TERMINATION_STATE)Data;
    LPOBJECT KernelObject = (LPOBJECT)Context;

    if (State == NULL) {
        return FALSE;
    }

    SAFE_USE_VALID(KernelObject) {
        return U64_EQUAL(State->ID, KernelObject->ID);
    }

    return FALSE;
}

/************************************************************************/

static BOOL IsObjectSignaled(LPVOID Object) {
    BOOL IsSignaled = FALSE;

    LockMutex(MUTEX_KERNEL, INFINITY);

    // First check termination cache
    LPOBJECT_TERMINATION_STATE TermState = (LPOBJECT_TERMINATION_STATE)CacheFind(
        &Kernel.ObjectTerminationCache,
        MatchObject,
        Object
    );

    SAFE_USE(TermState) {
        DEBUG(TEXT("[IsObjectSignaled] Object %x found in termination cache - marking as signaled"), Object);
        UnlockMutex(MUTEX_KERNEL);
        return TRUE;
    }

    UnlockMutex(MUTEX_KERNEL);

    return IsSignaled;
}

/************************************************************************/

static U32 GetObjectExitCode(LPVOID Object) {

    LockMutex(MUTEX_KERNEL, INFINITY);

    // First check termination cache
    LPOBJECT_TERMINATION_STATE TermState = (LPOBJECT_TERMINATION_STATE)CacheFind(
        &Kernel.ObjectTerminationCache,
        MatchObject,
        Object
    );

    SAFE_USE(TermState) {
        DEBUG(TEXT("[GetObjectExitCode] Object %x found in termination cache, ExitCode=%u"), Object, TermState->ExitCode);
        UnlockMutex(MUTEX_KERNEL);
        return TermState->ExitCode;
    }

    UnlockMutex(MUTEX_KERNEL);

    return MAX_U32;
}

/************************************************************************/

U32 Wait(LPWAITINFO WaitInfo) {
    UINT Index;
    U32 StartTime;
    LPTASK CurrentTask;

    if (WaitInfo == NULL || WaitInfo->Count == 0 || WaitInfo->Count > WAITINFO_MAX_OBJECTS) {
        return WAIT_INVALID_PARAMETER;
    }

    CurrentTask = GetCurrentTask();
    if (CurrentTask == NULL) {
        return WAIT_INVALID_PARAMETER;
    }

    StartTime = GetSystemTime();
    U32 LastDebugTime = StartTime;

    FOREVER {
        UINT SignaledCount = 0;

        // Count signaled objects
        for (Index = 0; Index < WaitInfo->Count; Index++) {
            if (IsObjectSignaled((LPVOID)WaitInfo->Objects[Index])) {
                SignaledCount++;
            }
        }

        if (WaitInfo->Flags & WAIT_FLAG_ALL) {
            if (SignaledCount == WaitInfo->Count) {
                for (Index = 0; Index < WaitInfo->Count; Index++) {
                    WaitInfo->ExitCodes[Index] = GetObjectExitCode((LPVOID)WaitInfo->Objects[Index]);
                }
                return WAIT_OBJECT_0;
            }
        } else {
            if (SignaledCount > 0) {
                for (Index = 0; Index < WaitInfo->Count; Index++) {
                    if (IsObjectSignaled((LPVOID)WaitInfo->Objects[Index])) {
                        WaitInfo->ExitCodes[Index] = GetObjectExitCode((LPVOID)WaitInfo->Objects[Index]);
                        return WAIT_OBJECT_0 + Index;
                    }
                }
            }
        }

        U32 CurrentTime = GetSystemTime();

        if (WaitInfo->MilliSeconds != INFINITY) {
            if (CurrentTime - StartTime >= WaitInfo->MilliSeconds) {
                return WAIT_TIMEOUT;
            }
        }

        // Periodic debug output every 2 seconds
        if (CurrentTime - LastDebugTime >= 2000) {
            DEBUG(TEXT("[Wait] Task %x waiting for %u objects for %u ms"),
                  CurrentTask, WaitInfo->Count, CurrentTime - StartTime);
            LastDebugTime = CurrentTime;
        }

        SetTaskStatus(CurrentTask, TASK_STATUS_SLEEPING);
        Sleep(50);
        SetTaskStatus(CurrentTask, TASK_STATUS_RUNNING);
    }

    return WAIT_TIMEOUT;
}
