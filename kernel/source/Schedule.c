// Schedule.c

/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Base.h"
#include "../include/Kernel.h"
#include "../include/Process.h"
#include "../include/Task.h"
#include "../include/List.h"

/***************************************************************************/

#define MAX_PRIORITY_LEVELS 5
#define PRIORITY_STEP 0x04
#define AGE_THRESHOLD 5
#define MAX_PRIORITY TASK_PRIORITY_HIGHEST

/***************************************************************************/

static U32 PriorityToIndex(U32 Priority) {
    if (Priority >= TASK_PRIORITY_CRITICAL) return MAX_PRIORITY_LEVELS - 1;
    return Priority >> 2;
}

static void UpdateTaskTime(LPTASK Task) {
    Task->Time = Task->Priority & 0xFF;
    Task->Time *= 2;
    if (Task->Time < 20) {
        Task->Time = 20;
    }
}

/***************************************************************************/

typedef struct tag_TASKLIST {
    U32 Freeze;
    U32 SchedulerTime;
    U32 TaskTime;
    LPTASK Current;
    LPLIST RunQueues[MAX_PRIORITY_LEVELS];
    LPLIST Sleeping;
} TASKLIST, *LPTASKLIST;

/***************************************************************************/

static LIST RunQueueStorage[MAX_PRIORITY_LEVELS] = {
    [0] = {
        .First = NULL,
        .Last = NULL,
        .Current = NULL,
        .NumItems = 0,
        .MemAllocFunc = KernelMemAlloc,
        .MemFreeFunc = KernelMemFree,
        .Destructor = NULL
    },
    [1] = {
        .First = (LPLISTNODE)&KernelTask,
        .Last = (LPLISTNODE)&KernelTask,
        .Current = NULL,
        .NumItems = 1,
        .MemAllocFunc = KernelMemAlloc,
        .MemFreeFunc = KernelMemFree,
        .Destructor = NULL
    },
    [2] = {
        .First = NULL,
        .Last = NULL,
        .Current = NULL,
        .NumItems = 0,
        .MemAllocFunc = KernelMemAlloc,
        .MemFreeFunc = KernelMemFree,
        .Destructor = NULL
    },
    [3] = {
        .First = NULL,
        .Last = NULL,
        .Current = NULL,
        .NumItems = 0,
        .MemAllocFunc = KernelMemAlloc,
        .MemFreeFunc = KernelMemFree,
        .Destructor = NULL
    },
    [4] = {
        .First = NULL,
        .Last = NULL,
        .Current = NULL,
        .NumItems = 0,
        .MemAllocFunc = KernelMemAlloc,
        .MemFreeFunc = KernelMemFree,
        .Destructor = NULL
    }
};

static LIST SleepingStorage = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL
};

static TASKLIST TaskList = {
    .Freeze = 0,
    .SchedulerTime = 0,
    .TaskTime = 20,
    .Current = &KernelTask,
    .RunQueues = {&RunQueueStorage[0], &RunQueueStorage[1],
                  &RunQueueStorage[2], &RunQueueStorage[3],
                  &RunQueueStorage[4]},
    .Sleeping = &SleepingStorage
};

/***************************************************************************/

static void AddTaskToRunQueue(LPTASK Task) {
    U32 Index = PriorityToIndex(Task->Priority);
    ListAddTail(TaskList.RunQueues[Index], Task);
    UpdateTaskTime(Task);
    Task->Age = 0;
}

static void RemoveTaskFromRunQueue(LPTASK Task) {
    U32 Index = PriorityToIndex(Task->Priority);
    ListRemove(TaskList.RunQueues[Index], Task);
}

/***************************************************************************/

void WakeSleepingTasks() {
    LPLISTNODE Node = TaskList.Sleeping->First;

    while (Node) {
        LPTASK Task = (LPTASK)Node;
        Node = Node->Next;

        if (GetSystemTime() >= Task->WakeUpTime) {
            ListRemove(TaskList.Sleeping, Task);
            Task->Status = TASK_STATUS_RUNNING;
            AddTaskToRunQueue(Task);
        }
    }
}

/***************************************************************************/

static void AgeRunnableTasks(LPTASK Selected) {
    for (U32 i = 0; i < MAX_PRIORITY_LEVELS; i++) {
        LPLISTNODE Node = TaskList.RunQueues[i]->First;
        while (Node) {
            LPTASK Task = (LPTASK)Node;
            Node = Node->Next;

            if (Task == Selected) continue;

            Task->Age++;
            if (Task->Age >= AGE_THRESHOLD && Task->Priority < MAX_PRIORITY) {
                ListRemove(TaskList.RunQueues[i], Task);
                Task->Priority += PRIORITY_STEP;
                Task->Age = 0;
                AddTaskToRunQueue(Task);
            }
        }
    }
}

/***************************************************************************/

void UpdateScheduler() {
    for (U32 i = 0; i < MAX_PRIORITY_LEVELS; i++) {
        for (LPLISTNODE Node = TaskList.RunQueues[i]->First; Node;
             Node = Node->Next) {
            UpdateTaskTime((LPTASK)Node);
        }
    }
}

/***************************************************************************/

BOOL AddTaskToQueue(LPTASK NewTask) {
    if (NewTask == NULL) return FALSE;
    if (NewTask->ID != ID_TASK) return FALSE;

    FreezeScheduler();

    if (NewTask->Status == TASK_STATUS_SLEEPING) {
        ListAddTail(TaskList.Sleeping, NewTask);
    } else {
        AddTaskToRunQueue(NewTask);
    }

    UnfreezeScheduler();
    return TRUE;
}

/***************************************************************************/

BOOL RemoveTaskFromQueue(LPTASK OldTask) {
    if (OldTask == NULL) return FALSE;

    FreezeScheduler();
    BOOL Result = FALSE;

    if (OldTask->Status == TASK_STATUS_SLEEPING) {
        if (ListRemove(TaskList.Sleeping, OldTask)) Result = TRUE;
    } else {
        U32 Index = PriorityToIndex(OldTask->Priority);
        if (ListRemove(TaskList.RunQueues[Index], OldTask)) Result = TRUE;
    }

    UnfreezeScheduler();
    return Result;
}

/***************************************************************************/

void Scheduler() {
    TaskList.SchedulerTime += 10;

    if (TaskList.Freeze) return;

    WakeSleepingTasks();

    if (TaskList.SchedulerTime >= TaskList.TaskTime) {
        DisableInterrupts();
        TaskList.SchedulerTime = 0;

        while (1) {
            LPTASK Next = NULL;
            U32 QueueIndex;

            for (QueueIndex = MAX_PRIORITY_LEVELS; QueueIndex > 0; QueueIndex--) {
                U32 Index = QueueIndex - 1;
                if (TaskList.RunQueues[Index]->First) {
                    Next = (LPTASK)TaskList.RunQueues[Index]->First;
                    ListRemove(TaskList.RunQueues[Index], Next);
                    break;
                }
            }

            if (Next == NULL) {
                EnableInterrupts();
                return;
            }

            if (Next->Status == TASK_STATUS_RUNNING) {
                ListAddTail(TaskList.RunQueues[PriorityToIndex(Next->Priority)],
                            Next);

                TaskList.Current = Next;
                TaskList.TaskTime = Next->Time;
                Next->Age = 0;

                AgeRunnableTasks(Next);

                TTD[TaskList.Current->Table].TSS.Type =
                    GATE_TYPE_386_TSS_AVAIL;
                SwitchToTask(TaskList.Current->Selector);

                EnableInterrupts();
                return;
            } else if (Next->Status == TASK_STATUS_SLEEPING) {
                ListAddTail(TaskList.Sleeping, Next);
            }
        }
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

