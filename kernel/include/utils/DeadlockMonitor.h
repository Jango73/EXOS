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


    Deadlock monitor

\************************************************************************/

#ifndef DEADLOCKMONITOR_H_INCLUDED
#define DEADLOCKMONITOR_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

typedef struct tag_TASK TASK, *LPTASK;
typedef struct tag_MUTEX MUTEX, *LPMUTEX;

/************************************************************************/

void DeadlockMonitorOnWaitStart(LPTASK Task, LPMUTEX Mutex);
void DeadlockMonitorOnWaitCancel(LPTASK Task, LPMUTEX Mutex);
void DeadlockMonitorOnAcquire(LPTASK Task, LPMUTEX Mutex);
void DeadlockMonitorOnRelease(LPTASK Task, LPMUTEX Mutex, LPTASK NextOwner);
BOOL DeadlockMonitorWouldCreateCycle(LPTASK Task, LPMUTEX Mutex);

/************************************************************************/

#endif
