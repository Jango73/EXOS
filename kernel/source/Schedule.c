
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Base.h"
#include "../include/Clock.h"
#include "../include/Kernel.h"
#include "../include/Process.h"
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

static TASKLIST TaskList = {0, 0, 20, 1, &KernelTask, {&KernelTask}};

/***************************************************************************/

void UpdateScheduler() {
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

    FreezeScheduler();

    //-------------------------------------
    // Check validity of parameters

    if (NewTask == NULL) goto Out_Error;
    if (NewTask->ID != ID_TASK) goto Out_Error;

    //-------------------------------------
    // Check if task queue is full

    if (TaskList.NumTasks == NUM_TASKS) goto Out_Error;

    //-------------------------------------
    // Check if task already in task queue

    for (Index = 0; Index < TaskList.NumTasks; Index++) {
        if (TaskList.Tasks[Index] == NewTask) goto Out_Error;
    }

    //-------------------------------------
    // Add task to queue

    TaskList.Tasks[TaskList.NumTasks] = NewTask;
    TaskList.NumTasks++;

    UpdateScheduler();

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

static void RotateQueue() {
    U32 Index = 0;

    TaskList.Current = TaskList.Tasks[0];

    for (Index = 1; Index < TaskList.NumTasks; Index++) {
        TaskList.Tasks[Index - 1] = TaskList.Tasks[Index];
    }

    TaskList.Tasks[TaskList.NumTasks - 1] = TaskList.Current;
}

/***************************************************************************/

void Scheduler() {
    TaskList.SchedulerTime += 10;

    if (TaskList.Freeze) return;

    if (TaskList.SchedulerTime >= TaskList.TaskTime) {
        DisableInterrupts();

        TaskList.SchedulerTime = 0;

        while (1) {
            RotateQueue();

            TaskList.TaskTime = TaskList.Current->Time;

            switch (TaskList.Current->Status) {
                case TASK_STATUS_RUNNING: {
                    /*
                          switch (CharSequence)
                          {
                        case 0 : *((LPSTR)0xB8000) = '/'; break;
                        case 1 : *((LPSTR)0xB8000) = '\\'; break;
                          }
                          CharSequence = 1 - CharSequence;
                    */

                    /*
                          if (TaskList.Current == &KernelTask)
                          {
                        STR Temp [8];
                        LPLISTNODE Node;

                        Index = 0;
                        for (Node = Kernel.Task->First; Node; Node =
                       Node->Next)
                        {
                          U32ToString(((LPTASK)Node)->Status, Temp);
                          *((LPSTR) 0xB8000 + Index) = Temp[0];
                          Index += 2;
                        }
                          }
                    */

                    /*
                          if (TaskList.Current == &KernelTask)
                          {
                        LPLISTNODE Node;
                        LPTASK Task;
                        U32 Value;
                        for (Node = Kernel.Task->First, Index = 0; Node;
                       Node = Node->Next)
                        {
                          Task = (LPTASK) Node;

                          switch (Task->Status)
                          {
                            case TASK_STATUS_DEAD     : Value =
                       0x00FFFFFF; break; case TASK_STATUS_RUNNING  : Value =
                       0x0000FF00; break; case TASK_STATUS_SLEEPING : Value =
                       0x000000FF; break; case TASK_STATUS_WAITING  : Value =
                       0x00FF0000; break;
                          }

                          *((U8*)0xA0000+Index+0) = Value >> 0;
                          *((U8*)0xA0000+Index+1) = Value >> 8;
                          *((U8*)0xA0000+Index+2) = Value >> 16;
                          *((U8*)0xA0000+Index+3) = Value >> 0;
                          *((U8*)0xA0000+Index+4) = Value >> 8;
                          *((U8*)0xA0000+Index+5) = Value >> 16;
                          Index += 9;
                        }
                          }
                    */

                    //-------------------------------------
                    // Set the TSS descriptor "not busy" before jumping to it

                    TTD[TaskList.Current->Table].TSS.Type = GATE_TYPE_386_TSS_AVAIL;

                    //-------------------------------------
                    // Switch to the new current task

                    SwitchToTask(TaskList.Current->Selector);

                    //-------------------------------------
                    // Immediately return when scheduler comes back

                    EnableInterrupts();
                    return;
                } break;

                case TASK_STATUS_SLEEPING: {
                    if (GetSystemTime() >= TaskList.Current->WakeUpTime) {
                        //-------------------------------------
                        // Wake-up the task

                        TaskList.Current->Status = TASK_STATUS_RUNNING;
                    }
                } break;
            }
        }

        EnableInterrupts();
        return;
    }
}

/***************************************************************************/

LPPROCESS GetCurrentProcess() { return GetCurrentTask()->Process; }

/***************************************************************************/

LPTASK GetCurrentTask() { return TaskList.Current; }

/***************************************************************************/

BOOL FreezeScheduler() {
    LockMutex(MUTEX_SCHEDULE, INFINITY);
    TaskList.Freeze++;
    UnlockMutex(MUTEX_SCHEDULE);
    return TRUE;
}

/***************************************************************************/

BOOL UnfreezeScheduler() {
    LockMutex(MUTEX_SCHEDULE, INFINITY);
    if (TaskList.Freeze) TaskList.Freeze--;
    UnlockMutex(MUTEX_SCHEDULE);
    return TRUE;
}

/***************************************************************************/
