
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


    Mutex

\************************************************************************/

#ifndef MUTEX_H_INCLUDED
#define MUTEX_H_INCLUDED

/************************************************************************/

#include "List.h"

/************************************************************************/

typedef struct tag_TASK TASK, *LPTASK;
typedef struct tag_PROCESS PROCESS, *LPPROCESS;

/************************************************************************/
// The mutex structure

struct tag_MUTEX {
    LISTNODE_FIELDS         // Standard EXOS object fields
    LPPROCESS Owner;        // Process that created this mutex
    LPPROCESS Process;      // Process that has locked this mutex.
    LPTASK Task;            // Task that has locked this mutex.
    UINT Lock;              // Lock count of this mutex.
};

typedef struct tag_MUTEX MUTEX, *LPMUTEX;

// Macro to initialize a mutex

#define EMPTY_MUTEX { \
    .TypeID = KOID_MUTEX, \
    .References = 1, \
    .OwnerProcess = NULL, \
    .Next = NULL, \
    .Prev = NULL, \
    .Process = NULL, \
    .Task = NULL, \
    .Lock = 0 \
}

/************************************************************************/
// Mutex shortcuts

#define MUTEX_KERNEL (&KernelMutex)
#define MUTEX_MEMORY (&MemoryMutex)
#define MUTEX_SCHEDULE (&ScheduleMutex)
#define MUTEX_DESKTOP (&DesktopMutex)
#define MUTEX_PROCESS (&ProcessMutex)
#define MUTEX_TASK (&TaskMutex)
#define MUTEX_FILESYSTEM (&FileSystemMutex)
#define MUTEX_FILE (&FileMutex)
#define MUTEX_CONSOLE (&ConsoleMutex)
#define MUTEX_ACCOUNTS (&UserAccountMutex)
#define MUTEX_SESSION (&SessionMutex)

/************************************************************************/
// Global mutex

extern MUTEX KernelMutex;
extern MUTEX LogMutex;
extern MUTEX MemoryMutex;
extern MUTEX ScheduleMutex;
extern MUTEX DesktopMutex;
extern MUTEX ProcessMutex;
extern MUTEX TaskMutex;
extern MUTEX FileSystemMutex;
extern MUTEX FileMutex;
extern MUTEX ConsoleMutex;
extern MUTEX UserAccountMutex;
extern MUTEX SessionMutex;

/************************************************************************/
// Mutex API

void InitMutex(LPMUTEX Mutex);
LPMUTEX CreateMutex(void);
BOOL DeleteMutex(LPMUTEX Mutex);
UINT LockMutex(LPMUTEX Mutex, UINT Timeout);
BOOL UnlockMutex(LPMUTEX Mutex);

#endif  // MUTEX_H_INCLUDED
