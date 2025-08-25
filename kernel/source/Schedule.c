
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
// Internal helper for concise selector logging

static void LogSelectorDetails(LPCSTR Prefix, SELECTOR Sel) {
    U32 idx = SELECTOR_INDEX(Sel);
    U32 ti  = SELECTOR_TI(Sel);
    U32 rpl = SELECTOR_RPL(Sel);

    KernelLogText(LOG_DEBUG, TEXT("%s selector=%X  index=%X  TI=%X  RPL=%X"),
                  Prefix, (U32)Sel, idx, ti, rpl);
}

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
    .Freeze = 0,
    .SchedulerTime = 0,
    .TaskTime = 20,
    .NumTasks = 0,
    .Current = NULL,
    .Tasks = {NULL}
};

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

void Scheduler(void) {
    // We're in a interrupt gate
    // No CLI or STI, it is managed by the processor (iretd)

    TaskList.SchedulerTime += 10;

    if (TaskList.Freeze) return;

    if (TaskList.SchedulerTime >= TaskList.TaskTime) {
        TaskList.SchedulerTime = 0;

        LPTASK StartTask = TaskList.Current;

        while (1) {
            RotateQueue();

            TaskList.TaskTime = TaskList.Current->Time;

            switch (TaskList.Current->Status) {
                case TASK_STATUS_RUNNING: {

                    SELECTOR target = TaskList.Current->Selector;
                    SELECTOR current = GetTaskRegister();

                    if (SELECTOR_INDEX(target) == SELECTOR_INDEX(current) && SELECTOR_TI(target) == SELECTOR_TI(current)) {
                        // KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Skip switch: same TSS %X. Returning."), target);
                        return;
                    }

                    //-------------------------------------
                    // Switch to the new current task

                    U32 TableIndex = TaskList.Current->Table;

                    /*
                    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] Switch to task %X"), TaskList.Current);

                    SELECTOR CurrentSelector = GetTaskRegister();
                    SELECTOR TargetSelector = TaskList.Current->Selector;

                    KernelLogText(LOG_DEBUG, TEXT("[Scheduler] EBP=%X  TR=%X"), (U32)GetEBP(), (U32)CurrentSelector);
                    LogSelectorDetails("[Scheduler] Target", TargetSelector);
                    LogSelectorDetails("[Scheduler] Current", CurrentSelector);
                    LogTSSDescriptor(LOG_DEBUG, (const TSSDESCRIPTOR*)&Kernel_i386.TTD[TaskList.Current->Table]);
                    LogTaskStateSegment(LOG_DEBUG, (const TASKSTATESEGMENT*)(Kernel_i386.TSS + TaskList.Current->Table));
                    */

                    KernelLogText(LOG_DEBUG, "[Scheduler] Target task table index = %X", TableIndex);

                    Kernel_i386.TTD[TableIndex].TSS.Type = GATE_TYPE_386_TSS_AVAIL;

                    SELECTOR TargetSelector = TaskList.Current->Selector;
                    LogSelectorDetails("[Scheduler] Target task selector", TargetSelector);

                    KernelLogText(LOG_DEBUG, "[Scheduler] GDTR = %X", GetGDTR());
                    KernelLogText(LOG_DEBUG, "[Scheduler] LDTR = %X", GetLDTR());

                    SwitchToTask((U32)TaskList.Current->Selector);

                    //-------------------------------------
                    // Immediately return when scheduler comes back
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

            if (TaskList.Current == StartTask) {
                return;
            }
        }

        return;
    }
}

/***************************************************************************/

LPPROCESS GetCurrentProcess(void) { return GetCurrentTask()->Process; }

/***************************************************************************/

LPTASK GetCurrentTask(void) {
    return TaskList.Current;
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

/***************************************************************************/
