
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

#ifndef SCHEDULE_H_INCLUDED
#define SCHEDULE_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "I386.h"

typedef struct tag_TASK TASK, *LPTASK;
typedef struct tag_PROCESS PROCESS, *LPPROCESS;

/***************************************************************************/

// Adds a task to the scheduler's queue
BOOL AddTaskToQueue(LPTASK NewTask);

// Removes a task from scheduler's queue
BOOL RemoveTaskFromQueue(LPTASK);

// Runs the scheduler to activate the next task (preemptive)
void Scheduler(void);

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
