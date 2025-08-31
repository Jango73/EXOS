
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
    LPTASK Current;
    LPTASK Tasks[NUM_TASKS];
} TASKLIST, *LPTASKLIST;

/***************************************************************************/

static TASKLIST TaskList = {
    .Freeze = 0, .SchedulerTime = 0, .TaskTime = 20, .NumTasks = 0, .Current = NULL, .Tasks = {NULL}};

/***************************************************************************/

void UpdateScheduler(void) {
    U32 Index = 0;

    for (Index = 0; Index < TaskList.NumTasks; Index++) {
        // TaskList.Tasks[Index]->Time = TaskList.Tasks[Index]->Priority & 0xFF;

        TaskList.Tasks[Index]->Time = TaskList.Tasks[Index]->Priority;
        TaskList.Tasks[Index]->Time &= 0xFF;
        TaskList.Tasks[Index]->Time *= 2;

        if (TaskList.Tasks[Index]->Time < 20) {
            TaskList.Tasks[Index]->Time = 20;
        }
    }
}

/***************************************************************************/

BOOL AddTaskToQueue(LPTASK NewTask) {
    U32 Index = 0;

#ifdef ENABLE_CRITICAL_DEBUG_LOGS
    KernelLogText(LOG_DEBUG, TEXT("[AddTaskToQueue] NewTask = %X"), NewTask);
#endif

    FreezeScheduler();

    //-------------------------------------
    // Check validity of parameters

    if (NewTask == NULL) goto Out_Error;
    if (NewTask->ID != ID_TASK) goto Out_Error;

    //-------------------------------------
    // Check if task queue is full

    if (TaskList.NumTasks == NUM_TASKS) {
        KernelLogText(LOG_ERROR, TEXT("[AddTaskToQueue] Cannot add %X, too much tasks"), NewTask);
        goto Out_Error;
    }

    //-------------------------------------
    // Check if task already in task queue

    for (Index = 0; Index < TaskList.NumTasks; Index++) {
        if (TaskList.Tasks[Index] == NewTask) goto Out_Success;
    }

    //-------------------------------------
    // Add task to queue

#ifdef ENABLE_CRITICAL_DEBUG_LOGS
    KernelLogText(LOG_DEBUG, TEXT("[AddTaskToQueue] Adding %X"), NewTask);
#endif

    TaskList.Tasks[TaskList.NumTasks] = NewTask;

    if (TaskList.NumTasks == 0) {
        TaskList.Current = NewTask;
    }

    TaskList.NumTasks++;

    UpdateScheduler();

Out_Success:

    UnfreezeScheduler();
    return TRUE;

Out_Error:

    UnfreezeScheduler();
    return FALSE;
}

/***************************************************************************/

BOOL RemoveTaskFromQueue(LPTASK OldTask) {
    U32 Index = 0;

    FreezeScheduler();

    for (Index = 0; Index < TaskList.NumTasks; Index++) {
        if (TaskList.Tasks[Index] == OldTask) {
            for (Index++; Index < TaskList.NumTasks; Index++) {
                TaskList.Tasks[Index - 1] = TaskList.Tasks[Index];
            }

            TaskList.NumTasks--;

            UnfreezeScheduler();
            return TRUE;
        }
    }

    UnfreezeScheduler();
    return FALSE;
}

/************************************************************************/

static void RotateQueue(void) {
    if (TaskList.NumTasks > 1) {
        TaskList.Current = TaskList.Tasks[0];

        for (U32 Index = 1; Index < TaskList.NumTasks; Index++) {
            TaskList.Tasks[Index - 1] = TaskList.Tasks[Index];
        }

        TaskList.Tasks[TaskList.NumTasks - 1] = TaskList.Current;
    }
}

/************************************************************************/

LPINTERRUPTFRAME Scheduler(LPINTERRUPTFRAME Frame) {
#ifdef ENABLE_CRITICAL_DEBUG_LOGS
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Enter"));
#endif

    TaskList.SchedulerTime += 10;

    if (TaskList.Current && Frame) {
        MemoryCopy(&(TaskList.Current->Context), Frame, sizeof(INTERRUPTFRAME));
    }

    if (TaskList.Freeze) {
#ifdef ENABLE_CRITICAL_DEBUG_LOGS
        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] TaskList frozen : Returning NULL"));
#endif

        return NULL;
    }

#ifdef ENABLE_CRITICAL_DEBUG_LOGS
    KernelLogText(
        LOG_DEBUG,
        TEXT("[Scheduler] Incoming frame (current) :\n"
             " EIP : %X, SS : %X\n"
             " DS : %X, ES : %X, FS %X, GS %X\n"
             ", ESI : %X, EDI : %X, EBP %X, ESP %X\n"
             ", EAX : %X, EBX : %X, ECX %X, EDX %X"),
        Frame->Registers.EIP, Frame->Registers.SS, Frame->Registers.DS, Frame->Registers.ES, Frame->Registers.FS,
        Frame->Registers.GS, Frame->Registers.ESI, Frame->Registers.EDI, Frame->Registers.EBP, Frame->Registers.ESP,
        Frame->Registers.EAX, Frame->Registers.EBX, Frame->Registers.ECX, Frame->Registers.EDX);
#endif

    if (TaskList.SchedulerTime >= TaskList.TaskTime) {
        TaskList.SchedulerTime = 0;

        LPTASK Current = TaskList.Current;

        while (1) {
            RotateQueue();

            LPTASK Next = TaskList.Current;
            TaskList.TaskTime = Next->Time;

            switch (Next->Status) {
                case TASK_STATUS_RUNNING: {
                    if (Next != NULL && Next != Current) {
                        if (Next->Process && Current &&
                            Next->Process->PageDirectory != Current->Process->PageDirectory) {
#ifdef ENABLE_CRITICAL_DEBUG_LOGS
                            KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Load CR3 = %X"), Next->Process->PageDirectory);
#endif

                            LoadPageDirectory(Next->Process->PageDirectory);
                        }

#ifdef ENABLE_CRITICAL_DEBUG_LOGS
                        LogTask(LOG_DEBUG, Next);
#endif

                        U32 NextSysStackTop = Next->SysStackBase + Next->SysStackSize;

#ifdef ENABLE_CRITICAL_DEBUG_LOGS
                        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Set ESP0 = %X"), NextSysStackTop);
#endif

                        Kernel_i386.TSS->ESP0 = NextSysStackTop;

                        LPINTERRUPTFRAME NextFrame = (LPINTERRUPTFRAME)(NextSysStackTop - sizeof(INTERRUPTFRAME));
                        MemoryCopy(NextFrame, &(Next->Context), sizeof(INTERRUPTFRAME));

                        TaskList.Current = Next;
                        return NextFrame;  // Switch to this stack
                    }

#ifdef ENABLE_CRITICAL_DEBUG_LOGS
                    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Returning NULL"));
#endif

                    return NULL;
                } break;

                case TASK_STATUS_SLEEPING: {
                    if (GetSystemTime() >= Next->WakeUpTime) {
                        Next->Status = TASK_STATUS_RUNNING;
                    }
                } break;
            }

            if (Next == Current) {
#ifdef ENABLE_CRITICAL_DEBUG_LOGS
                KernelLogText(LOG_DEBUG, TEXT("[Scheduler] No task to switch to, returning NULL"));
#endif

                return NULL;
            }
        }
    }

#ifdef ENABLE_CRITICAL_DEBUG_LOGS
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Not sheduling time yet, returning NULL"));
#endif

    return NULL;
}

/************************************************************************/

LPPROCESS GetCurrentProcess(void) {
    LPTASK Task = GetCurrentTask();
    SAFE_USE(Task) { return Task->Process; }
    return &KernelProcess;
}

/***************************************************************************/

LPTASK GetCurrentTask(void) { return TaskList.Current; }

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

/***************************************************************************/
