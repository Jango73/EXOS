
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Base.h"
#include "../include/Clock.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Process.h"
#include "../include/System.h"
#include "../include/Task.h"

/***************************************************************************/

// #define SCHEDULER_LOGS_ENABLED 1

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

    #ifdef SCHEDULER_LOGS_ENABLED
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

    KernelLogText(LOG_DEBUG, TEXT("[AddTaskToQueue] Adding %X"), NewTask);

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

/***************************************************************************/

static void RotateQueue(void) {
    if (TaskList.NumTasks > 1) {
        TaskList.Current = TaskList.Tasks[0];

        for (U32 Index = 1; Index < TaskList.NumTasks; Index++) {
            TaskList.Tasks[Index - 1] = TaskList.Tasks[Index];
        }

        TaskList.Tasks[TaskList.NumTasks - 1] = TaskList.Current;
    }
}

/***************************************************************************/

LPTRAPFRAME Scheduler(LPTRAPFRAME Frame) {
    #ifdef SCHEDULER_LOGS_ENABLED
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Enter"));
    #endif

    TaskList.SchedulerTime += 10;

    if (TaskList.Current && Frame) {
        Frame->EFlags |= EFLAGS_IF;
        MemoryCopy(&(TaskList.Current->Context), Frame, sizeof(TRAPFRAME));
    }

    if (TaskList.Freeze) {
        #ifdef SCHEDULER_LOGS_ENABLED
        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Returning NULL"));
        #endif

        return NULL;
    }

    #ifdef SCHEDULER_LOGS_ENABLED
    KernelLogText(LOG_DEBUG, TEXT(
            "[Scheduler] Current frame"
            " DS : %X, ES : %X, FS %X, GS %X\n"
            ", EAX : %X, EBX : %X, ECX %X, EDX %X\n"
            ", ESI : %X, EDI : %X, EBP %X, ESP %X\n"
        ),
        Frame->DS, Frame->ES, Frame->FS, Frame->GS,
        Frame->ESI, Frame->EDI, Frame->EBP, Frame->ESP,
        Frame->EAX, Frame->EBX, Frame->ECX, Frame->EDX
        );
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
                    if (Next != Current) {
                        if (Next->Process && Current &&
                            Next->Process->PageDirectory != Current->Process->PageDirectory) {

                            #ifdef SCHEDULER_LOGS_ENABLED
                            KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Load CR3 = %X"), Next->Process->PageDirectory);
                            #endif

                            LoadPageDirectory(Next->Process->PageDirectory);
                        }

                        #ifdef SCHEDULER_LOGS_ENABLED
                        KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Set ESP0 = %X"), Next->SysStackTop);
                        #endif

                        Kernel_i386.TSS->ESP0 = Next->SysStackTop;

                        LPTRAPFRAME NextFrame =
                            (LPTRAPFRAME)(Next->SysStackTop - sizeof(TRAPFRAME));
                        Next->Context.EFlags |= EFLAGS_IF;
                        MemoryCopy(NextFrame, &(Next->Context), sizeof(TRAPFRAME));

                        TaskList.Current = Next;
                        return NextFrame;  // Switch to this stack
                    }

                    #ifdef SCHEDULER_LOGS_ENABLED
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
                #ifdef SCHEDULER_LOGS_ENABLED
                KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Returning NULL"));
                #endif

                return NULL;
            }
        }
    }

    #ifdef SCHEDULER_LOGS_ENABLED
    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Returning NULL"));
    #endif

    return NULL;
}

/***************************************************************************/

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
