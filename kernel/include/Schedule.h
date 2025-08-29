
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef SCHEDULE_H_INCLUDED
#define SCHEDULE_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "I386.h"

typedef struct tag_TASK TASK, *LPTASK;
typedef struct tag_PROCESS PROCESS, *LPPROCESS;

/***************************************************************************/

// Updates the scheduler
void UpdateScheduler(void);

// Adds a task to the scheduler's queue
BOOL AddTaskToQueue(LPTASK NewTask);

// Removes a task from scheduler's queue
BOOL RemoveTaskFromQueue(LPTASK);

// Runs the scheduler to activate the next task (preemptive)
LPTRAPFRAME Scheduler(LPTRAPFRAME Frame);

// Returns the currently running task
LPTASK GetCurrentTask(void);

// Returns the currently running process
LPPROCESS GetCurrentProcess(void);

// Freezes the scheduler
BOOL FreezeScheduler(void);

// Unfreezes the scheduler
BOOL UnfreezeScheduler(void);

/***************************************************************************/

#endif
