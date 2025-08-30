
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


    Task

\************************************************************************/
#ifndef TASK_H_INCLUDED
#define TASK_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "I386.h"
#include "List.h"
#include "Mutex.h"

#define TASK_TYPE_KERNEL_MAIN 0
#define TASK_TYPE_KERNEL_OTHER 1
#define TASK_TYPE_USER 2

/************************************************************************/
// The Task structure

struct tag_TASK {
    LISTNODE_FIELDS     // Standard EXOS object fields
        MUTEX Mutex;    // This structure's mutex
    LPPROCESS Process;  // Process that owns this task
    U32 Type;           // Type of task
    U32 Status;         // Current status of this task
    U32 Priority;       // Current priority of this task
    TASKFUNC Function;  // Start address of this task
    LPVOID Parameter;   // Parameter passed to the function
    U32 ReturnValue;
    INTERRUPTFRAME Context;  // Saved context for software switching
    LINEAR StackBase;        // This task's stack in the heap
    U32 StackSize;           // This task's stack size
    LINEAR SysStackBase;
    U32 SysStackSize;
    U32 Time;  // Time allocated to this task
    U32 WakeUpTime;
    MUTEX MessageMutex;  // Mutex to access message queue
    LPLIST Message;      // This task's message queue
};

typedef struct tag_TASK TASK, *LPTASK;

/************************************************************************/
#endif
