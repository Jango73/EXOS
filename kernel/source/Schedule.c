
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
#include "../include/Process.h"
#include "../include/System.h"
#include "../include/Task.h"

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

// Calculate quantum time based on priority (once per task)
static inline U32 CalculateQuantumTime(U32 Priority) {
    U32 Time = (Priority & 0xFF) * 2;
    return (Time < 20) ? 20 : Time;
}

/***************************************************************************/

// Count how many tasks are ready to run
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

// Find next runnable task starting from given index
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

BOOL AddTaskToQueue(LPTASK NewTask) {
#ifdef ENABLE_CRITICAL_DEBUG_LOGS
    KernelLogText(LOG_DEBUG, TEXT("[AddTaskToQueue] NewTask = %X"), NewTask);
#endif

    FreezeScheduler();

    // Check validity of parameters
    if (NewTask == NULL || NewTask->ID != ID_TASK) {
        UnfreezeScheduler();
        return FALSE;
    }

    // Check if task queue is full
    if (TaskList.NumTasks >= NUM_TASKS) {
        KernelLogText(LOG_ERROR, TEXT("[AddTaskToQueue] Cannot add %X, too many tasks"), NewTask);
        UnfreezeScheduler();
        return FALSE;
    }

    // Check if task already in task queue
    for (U32 Index = 0; Index < TaskList.NumTasks; Index++) {
        if (TaskList.Tasks[Index] == NewTask) {
            UnfreezeScheduler();
            return TRUE;  // Already present, success
        }
    }

    // Add task to queue
#ifdef ENABLE_CRITICAL_DEBUG_LOGS
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
    return TRUE;
}

/***************************************************************************/

BOOL RemoveTaskFromQueue(LPTASK OldTask) {
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
            return TRUE;
        }
    }

    UnfreezeScheduler();
    return FALSE;
}

/***************************************************************************/

LPINTERRUPTFRAME Scheduler(LPINTERRUPTFRAME Frame) {
#ifdef ENABLE_CRITICAL_DEBUG_LOGS
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Enter"));
#endif

    TaskList.SchedulerTime += 10;

    // Save current task context if we have one
    if (TaskList.NumTasks > 0 && TaskList.CurrentIndex < TaskList.NumTasks && Frame) {
        LPTASK CurrentTask = TaskList.Tasks[TaskList.CurrentIndex];
        if (CurrentTask) {
            MemoryCopy(&(CurrentTask->Context), Frame, sizeof(INTERRUPTFRAME));
        }
    }

    // If scheduler is frozen, don't switch
    if (TaskList.Freeze) {
#ifdef ENABLE_CRITICAL_DEBUG_LOGS
        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] TaskList frozen: Returning NULL"));
#endif
        return NULL;
    }

    // No tasks to schedule
    if (TaskList.NumTasks == 0) {
        return NULL;
    }

    // Check if current task quantum has expired
    BOOL QuantumExpired = (TaskList.SchedulerTime >= TaskList.TaskTime);
    LPTASK CurrentTask = (TaskList.CurrentIndex < TaskList.NumTasks) ? TaskList.Tasks[TaskList.CurrentIndex] : NULL;
    
    // Update sleeping tasks status
    U32 RunnableCount = CountRunnableTasks();
    
    // No runnable tasks - system idle
    if (RunnableCount == 0) {
#ifdef ENABLE_CRITICAL_DEBUG_LOGS
        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] No runnable tasks"));
#endif
        return NULL;
    }

    // If current task is still running and quantum not expired, keep it
    if (CurrentTask && CurrentTask->Status == TASK_STATUS_RUNNING && !QuantumExpired) {
#ifdef ENABLE_CRITICAL_DEBUG_LOGS
        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Current task continues"));
#endif
        return NULL;
    }

    // Time to switch - find next runnable task
    U32 NextIndex = FindNextRunnableTask((TaskList.CurrentIndex + 1) % TaskList.NumTasks);
    
    if (NextIndex >= TaskList.NumTasks) {
        // Should not happen if RunnableCount > 0, but safety check
#ifdef ENABLE_CRITICAL_DEBUG_LOGS
        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] No next task found"));
#endif
        return NULL;
    }

    LPTASK NextTask = TaskList.Tasks[NextIndex];
    TaskList.CurrentIndex = NextIndex;
    TaskList.SchedulerTime = 0;
    TaskList.TaskTime = NextTask->Time;

#ifdef ENABLE_CRITICAL_DEBUG_LOGS
    LogTask(LOG_DEBUG, NextTask);
#endif

    // Switch page directory if different process
    if (NextTask->Process && CurrentTask && 
        NextTask->Process->PageDirectory != CurrentTask->Process->PageDirectory) {
#ifdef ENABLE_CRITICAL_DEBUG_LOGS
        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Load CR3 = %X"), NextTask->Process->PageDirectory);
#endif
        LoadPageDirectory(NextTask->Process->PageDirectory);
    }

    // Set up system stack for new task
    U32 NextSysStackTop = NextTask->SysStackBase + NextTask->SysStackSize;
#ifdef ENABLE_CRITICAL_DEBUG_LOGS
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Set ESP0 = %X"), NextSysStackTop);
#endif
    Kernel_i386.TSS->ESP0 = NextSysStackTop;

    // Prepare interrupt frame for task switch
    LPINTERRUPTFRAME NextFrame = (LPINTERRUPTFRAME)(NextSysStackTop - sizeof(INTERRUPTFRAME));
    MemoryCopy(NextFrame, &(NextTask->Context), sizeof(INTERRUPTFRAME));

    return NextFrame;
}

/***************************************************************************/

LPPROCESS GetCurrentProcess(void) {
    LPTASK Task = GetCurrentTask();
    SAFE_USE(Task) { return Task->Process; }
    return &KernelProcess;
}

/***************************************************************************/

LPTASK GetCurrentTask(void) {
    if (TaskList.NumTasks == 0 || TaskList.CurrentIndex >= TaskList.NumTasks) {
        return NULL;
    }
    return TaskList.Tasks[TaskList.CurrentIndex];
}

/***************************************************************************/

BOOL FreezeScheduler(void) {
    LockMutex(MUTEX_SCHEDULE, INFINITY);
    TaskList.Freeze++;
    UnlockMutex(MUTEX_SCHEDULE);
    return TRUE;
}

/***************************************************************************/

BOOL UnfreezeScheduler(void) {
    LockMutex(MUTEX_SCHEDULE, INFINITY);
    if (TaskList.Freeze) TaskList.Freeze--;
    UnlockMutex(MUTEX_SCHEDULE);
    return TRUE;
}
