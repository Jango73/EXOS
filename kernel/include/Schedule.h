
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

void UpdateScheduler();
BOOL AddTaskToQueue(LPTASK);
BOOL RemoveTaskFromQueue(LPTASK);
void Scheduler();
LPTASK GetCurrentTask();
LPPROCESS GetCurrentProcess();
BOOL FreezeScheduler();
BOOL UnfreezeScheduler();

/***************************************************************************/

#endif
