
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

#include "../include/Base.h"
#include "../include/Clock.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Memory.h"
#include "../include/Process.h"
#include "../include/System.h"
#include "../include/Task.h"
#include "../include/Stack.h"
#include "../include/StackTrace.h"

/***************************************************************************/

typedef struct tag_TASKLIST {
    U32 Freeze;
    U32 SchedulerTime;
    U32 TaskTime;
    U32 NumTasks;
    U32 CurrentIndex;        // Index of current task instead of pointer
    LPTASK Tasks[NUM_TASKS];
} TASKLIST, *LPTASKLIST;

/***************************************************************************/

static TASKLIST TaskList = {
    .Freeze = 0, .SchedulerTime = 0, .TaskTime = 20, .NumTasks = 0, .CurrentIndex = 0, .Tasks = {NULL}};

/***************************************************************************/

/**
 * @brief Calculates the time quantum for a task based on its priority.
 *
 * Higher priority tasks get longer time slices. Minimum quantum is 20ms.
 *
 * @param Priority Task priority value
 * @return Time quantum in milliseconds
 */
static inline U32 CalculateQuantumTime(U32 Priority) {
    U32 Time = (Priority & 0xFF) * 2;
    return (Time < 20) ? 20 : Time;
}

/***************************************************************************/

/**
 * @brief Counts the number of tasks that are ready to run.
 *
 * Also wakes up any sleeping tasks whose wake-up time has expired.
 *
 * @return Number of runnable tasks
 */
static U32 CountRunnableTasks(void) {
    U32 RunnableCount = 0;

    for (U32 Index = 0; Index < TaskList.NumTasks; Index++) {
        LPTASK Task = TaskList.Tasks[Index];

        if (Task->Status == TASK_STATUS_RUNNING) {
            RunnableCount++;
        } else if (Task->Status == TASK_STATUS_SLEEPING) {
            // Check if sleep time has expired
            if (GetSystemTime() >= Task->WakeUpTime) {
                Task->Status = TASK_STATUS_RUNNING;
                RunnableCount++;
            }
        }
    }

    return RunnableCount;
}

/***************************************************************************/

/**
 * @brief Finds the next runnable task starting from a given index.
 *
 * Performs round-robin search through the task list, waking up sleeping tasks
 * whose time has expired. Returns the index of the next runnable task.
 *
 * @param StartIndex Index to start searching from
 * @return Index of next runnable task, or MAX_U32 if none found
 */
static U32 FindNextRunnableTask(U32 StartIndex) {
    for (U32 Attempts = 0; Attempts < TaskList.NumTasks; Attempts++) {
        U32 Index = (StartIndex + Attempts) % TaskList.NumTasks;
        LPTASK Task = TaskList.Tasks[Index];
        
        if (Task->Status == TASK_STATUS_RUNNING) {
            return Index;
        }
        
        // Wake up sleeping tasks if needed
        if (Task->Status == TASK_STATUS_SLEEPING && GetSystemTime() >= Task->WakeUpTime) {
            Task->Status = TASK_STATUS_RUNNING;
            return Index;
        }
    }

    return TaskList.NumTasks;  // No runnable task found
}

/***************************************************************************/

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
    KernelLogText(LOG_DEBUG, TEXT("[AddTaskToQueue] NewTask = %X"), NewTask);
#endif

    FreezeScheduler();

    // Check validity of parameters
    if (NewTask == NULL || NewTask->ID != ID_TASK) {
        UnfreezeScheduler();

        TRACED_EPILOGUE("AddTaskToQueue");
        return FALSE;
    }

    // Check if task queue is full
    if (TaskList.NumTasks >= NUM_TASKS) {
        KernelLogText(LOG_ERROR, TEXT("[AddTaskToQueue] Cannot add %X, too many tasks"), NewTask);
        UnfreezeScheduler();

        TRACED_EPILOGUE("AddTaskToQueue");
        return FALSE;
    }

    // Check if task already in task queue
    for (U32 Index = 0; Index < TaskList.NumTasks; Index++) {
        if (TaskList.Tasks[Index] == NewTask) {
            UnfreezeScheduler();

            TRACED_EPILOGUE("AddTaskToQueue");
            return TRUE;  // Already present, success
        }
    }

    // Add task to queue
#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[AddTaskToQueue] Adding %X"), NewTask);
#endif

    TaskList.Tasks[TaskList.NumTasks] = NewTask;
    
    // Set quantum time for this task
    NewTask->Time = CalculateQuantumTime(NewTask->Priority);
    
    // If this is the first task, make it current
    if (TaskList.NumTasks == 0) {
        TaskList.CurrentIndex = 0;
    }

    TaskList.NumTasks++;

    UnfreezeScheduler();

    TRACED_EPILOGUE("AddTaskToQueue");
    return TRUE;
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

    for (U32 Index = 0; Index < TaskList.NumTasks; Index++) {
        if (TaskList.Tasks[Index] == OldTask) {
            // If removing current task, adjust current index
            if (Index == TaskList.CurrentIndex) {
                // Find next runnable task or wrap around
                if (TaskList.NumTasks > 1) {
                    TaskList.CurrentIndex = FindNextRunnableTask((Index + 1) % (TaskList.NumTasks - 1));

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
            for (U32 ShiftIndex = Index; ShiftIndex < TaskList.NumTasks - 1; ShiftIndex++) {
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
 * @brief Main scheduler function called by timer interrupt.
 *
 * This is the core scheduling function that implements preemptive multitasking.
 * It saves the current task context, checks for stack overflows, manages task
 * quantums, wakes sleeping tasks, and selects the next task to run. Performs
 * context switching by setting up ESP0 for Ring 3 tasks and returning the
 * interrupt frame of the next task.
 *
 * @param Frame Current interrupt frame containing CPU state
 * @return Interrupt frame of next task to execute, or NULL to continue current
 */
LPINTERRUPTFRAME Scheduler(LPINTERRUPTFRAME Frame) {
    TRACED_FUNCTION;

#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Enter"));
#endif

    // If scheduler is frozen, don't switch (atomic read - safe in interrupt context)
    if (TaskList.Freeze) {
#if SCHEDULING_DEBUG_OUTPUT == 1
        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] TaskList frozen: Returning NULL"));
#endif
        TRACED_EPILOGUE("Scheduler");
        return NULL;
    }

    TaskList.SchedulerTime += 10;

    // Check for stack overflow - kill dangerous tasks immediately
    if (!CheckStack()) {
        LPTASK DangerousTask = GetCurrentTask();

        if (DangerousTask) {

            KernelLogText(LOG_ERROR, TEXT("[Scheduler] Killing task due to overflow : %X"), DangerousTask);

            // Mark task as ended and remove it from the scheduler
            DangerousTask->Status = TASK_STATUS_DEAD;
            KillTask(DangerousTask);
        }
    }

    // Save current task context if we have one
    if (TaskList.NumTasks > 0 && TaskList.CurrentIndex < TaskList.NumTasks && Frame) {
        LPTASK CurrentTask = TaskList.Tasks[TaskList.CurrentIndex];

        if (CurrentTask) {
            // For the main kernel task, preserve the ESP if it was already switched
            U32 OriginalESP = CurrentTask->Context.Registers.ESP;
            MemoryCopy(&(CurrentTask->Context), Frame, sizeof(INTERRUPTFRAME));
        }
    }

    // No tasks to schedule
    if (TaskList.NumTasks == 0) {
        TRACED_EPILOGUE("Scheduler");
        return NULL;
    }

    // Check if current task quantum has expired
    BOOL QuantumExpired = (TaskList.SchedulerTime >= TaskList.TaskTime);
    LPTASK CurrentTask = (TaskList.CurrentIndex < TaskList.NumTasks) ? TaskList.Tasks[TaskList.CurrentIndex] : NULL;
    
    // Update sleeping tasks status
    U32 RunnableCount = CountRunnableTasks();
    
    // No runnable tasks - system idle
    if (RunnableCount == 0) {
#if SCHEDULING_DEBUG_OUTPUT == 1
        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] No runnable tasks"));
#endif

        TRACED_EPILOGUE("Scheduler");
        return NULL;
    }

    // If current task is still running and quantum not expired, keep it
    if (CurrentTask && CurrentTask->Status == TASK_STATUS_RUNNING && !QuantumExpired) {
#if SCHEDULING_DEBUG_OUTPUT == 1
        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Current task continues"));
#endif

        TRACED_EPILOGUE("Scheduler");
        return NULL;
    }

    // Time to switch - find next runnable task
    U32 NextIndex = FindNextRunnableTask((TaskList.CurrentIndex + 1) % TaskList.NumTasks);
    
    if (NextIndex >= TaskList.NumTasks) {
        // Should not happen if RunnableCount > 0, but safety check
#if SCHEDULING_DEBUG_OUTPUT == 1
        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] No next task found"));
#endif

        TRACED_EPILOGUE("Scheduler");
        return NULL;
    }

    LPTASK NextTask = TaskList.Tasks[NextIndex];
    TaskList.CurrentIndex = NextIndex;
    TaskList.SchedulerTime = 0;
    TaskList.TaskTime = NextTask->Time;

#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Switching to"));
    LogTask(LOG_DEBUG, NextTask);
#endif

    // Switch page directory if different process
    if (NextTask->Process && CurrentTask && 
        NextTask->Process->PageDirectory != CurrentTask->Process->PageDirectory) {
#if SCHEDULING_DEBUG_OUTPUT == 1
        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Load CR3 = %X"), NextTask->Process->PageDirectory);
#endif
        LoadPageDirectory(NextTask->Process->PageDirectory);
    }

    // Set up system stack for new task
    U32 NextSysStackTop = NextTask->SysStackBase + NextTask->SysStackSize;
#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] NextTask = %X"), NextTask);
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] NextTask->SysStackBase = %X"), NextTask->SysStackBase);  
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] NextTask->SysStackSize = %X"), NextTask->SysStackSize);
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Calculated ESP0 = %X"), NextSysStackTop);
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Set ESP0 = %X"), NextSysStackTop);
#endif
    Kernel_i386.TSS->ESP0 = NextSysStackTop;

#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Returning next frame to the stub"));
    // DumpFrame(&(NextTask->Context));
#endif

    TRACED_EPILOGUE("Scheduler");
    return &(NextTask->Context);
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
    if (TaskList.NumTasks == 0 || TaskList.CurrentIndex >= TaskList.NumTasks) {
        return NULL;
    }
    return TaskList.Tasks[TaskList.CurrentIndex];
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
    LockMutex(MUTEX_SCHEDULE, INFINITY);
    TaskList.Freeze++;
    UnlockMutex(MUTEX_SCHEDULE);
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
    LockMutex(MUTEX_SCHEDULE, INFINITY);
    if (TaskList.Freeze) TaskList.Freeze--;
    UnlockMutex(MUTEX_SCHEDULE);
    return TRUE;
}
