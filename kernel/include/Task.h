
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


    Task manager

\************************************************************************/

#ifndef TASK_H_INCLUDED
#define TASK_H_INCLUDED

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#include "Base.h"
#include "arch/i386/I386.h"
#include "List.h"
#include "Mutex.h"
#include "User.h"

/************************************************************************/

#define TASK_TYPE_NONE 0
#define TASK_TYPE_KERNEL_MAIN 1
#define TASK_TYPE_KERNEL_OTHER 2
#define TASK_TYPE_USER_MAIN 3
#define TASK_TYPE_USER_OTHER 4

/************************************************************************/
// The Task structure

struct tag_TASK {
    LISTNODE_FIELDS           // Standard EXOS object fields
        MUTEX Mutex;          // This structure's mutex
    LPPROCESS Process;        // Process that owns this task
    STR Name[MAX_USER_NAME];  // Task name for debugging
    U32 Type;                 // Type of task
    U32 Status;               // Current status of this task
    U32 Priority;             // Current priority of this task
    TASKFUNC Function;        // Start address of this task
    LPVOID Parameter;         // Parameter passed to the function
    U32 ExitCode;             // This task's exit code
    U32 Flags;                // Task creation flags
    INTERRUPT_FRAME Context;   // Saved context for software switching
    LINEAR StackBase;         // This task's stack in the heap
    UINT StackSize;           // This task's stack size
    LINEAR SysStackBase;
    UINT SysStackSize;
    U32 WakeUpTime;           // System time at which to wake up the task
    MUTEX MessageMutex;       // Mutex to access message queue
    LPLIST Message;           // This task's message queue
};

typedef struct tag_TASK TASK, *LPTASK;

/************************************************************************/

BOOL InitKernelTask(void);
LPTASK CreateTask(LPPROCESS, LPTASKINFO);
BOOL KillTask(LPTASK Task);
BOOL SetTaskExitCode(LPTASK Task, U32 Code);
void DeleteDeadTasksAndProcesses(void);
U32 SetTaskPriority(LPTASK, U32);
void Sleep(U32);
U32 GetTaskStatus(LPTASK Task);
void SetTaskStatus(LPTASK Task, U32 Status);
void SetTaskWakeUpTime(LPTASK Task, U32 WakeupTime);
U32 ComputeTaskQuantumTime(U32 Priority);
BOOL PostMessage(HANDLE, U32, U32, U32);
U32 SendMessage(HANDLE, U32, U32, U32);
BOOL GetMessage(LPMESSAGEINFO);
BOOL DispatchMessage(LPMESSAGEINFO);
void DumpTask(LPTASK);

/************************************************************************/

#pragma pack(pop)

#endif  // TASK_H_INCLUDED
