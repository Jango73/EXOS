
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef TASK_H_INCLUDED
#define TASK_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Mutex.h"

/***************************************************************************/

// The Task structure

struct tag_TASK {
    LISTNODE_FIELDS     // Standard EXOS object fields
        MUTEX Mutex;    // This structure's mutex
    LPPROCESS Process;  // Process that owns this task
    U32 Status;         // Current status of this task
    U32 Priority;       // Current priority of this task
    TASKFUNC Function;  // Start address of this task
    LPVOID Parameter;   // Parameter passed to the function
    U32 ReturnValue;
    U32 Table;         // Index in the TSS tables
    U32 Selector;      // GDT selector for this task
    LINEAR StackBase;  // This task's stack in the heap
    U32 StackSize;     // This task's stack size
    LINEAR SysStackBase;
    U32 SysStackSize;
    U32 Time;  // Time allocated to this task
    U32 WakeUpTime;
    U32 Age;          // Aging counter for priority boost
    MUTEX MessageMutex;  // Mutex to access message queue
    LPLIST Message;      // This task's message queue
};

typedef struct tag_TASK TASK, *LPTASK;

/***************************************************************************/

#endif
