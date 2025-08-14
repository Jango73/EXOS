
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef SCHEDULE_H_INCLUDED
#define SCHEDULE_H_INCLUDED

/***************************************************************************/

#include "Base.h"

typedef struct tag_TASK TASK, *LPTASK;
typedef struct tag_PROCESS PROCESS, *LPPROCESS;

/***************************************************************************/

// Updates the scheduler
void UpdateScheduler();

// Adds a task to the scheduler's queue
BOOL AddTaskToQueue(LPTASK NewTask);

// Removes a task from scheduler's queue
BOOL RemoveTaskFromQueue(LPTASK);

// Runs the scheduler to activate the next task (collborative)
void Scheduler();

// Returns the currently running task
LPTASK GetCurrentTask();

// Returns the currently running process
LPPROCESS GetCurrentProcess();

// Freezes the scheduler
BOOL FreezeScheduler();

// Unfreezes the scheduler
BOOL UnfreezeScheduler();

/***************************************************************************/

#endif
