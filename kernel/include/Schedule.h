
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
#include "arch/i386/I386.h"

typedef struct tag_TASK TASK, *LPTASK;
typedef struct tag_PROCESS PROCESS, *LPPROCESS;
typedef struct tag_WAITINFO WAITINFO, *LPWAITINFO;

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

// Waits for one or more kernel objects to become signaled
U32 Wait(LPWAITINFO WaitInfo);

/************************************************************************/

#define SetupStackForKernelMode(Task, StackTop)           \
    (StackTop) -= 3;                                      \
    ((U32*)StackTop)[2] = Task->Context.Registers.EFlags; \
    ((U32*)StackTop)[1] = Task->Context.Registers.CS;     \
    ((U32*)StackTop)[0] = Task->Context.Registers.EIP;

/************************************************************************/

#define SetupStackForUserMode(Task, StackTop, UserESP)    \
    (StackTop) -= 5;                                      \
    ((U32*)StackTop)[4] = Task->Context.Registers.SS;     \
    ((U32*)StackTop)[3] = UserESP;                        \
    ((U32*)StackTop)[2] = Task->Context.Registers.EFlags; \
    ((U32*)StackTop)[1] = Task->Context.Registers.CS;     \
    ((U32*)StackTop)[0] = Task->Context.Registers.EIP;

/************************************************************************/

#define SwitchToNextTask_2(prev, next)                                                                 \
    do {                                                                                               \
        __asm__ __volatile__(                                                                          \
            "pusha\n\t"                                                                                \
            "movl %%esp,%0\n\t"                                                                        \
            "movl %2,%%esp\n\t"                                                                        \
            "movl $1f,%1\n\t"                                                                          \
            "pushl %5\n\t"                                                                             \
            "pushl %4\n\t"                                                                             \
            "call SwitchToNextTask_3\n"                                                                \
            "1:\t"                                                                                     \
            "add $8, %%esp\n\t"                                                                        \
            "popa\n\t"                                                                                 \
            : "=m"(prev->Context.Registers.ESP), "=m"(prev->Context.Registers.EIP)                     \
            : "m"(next->Context.Registers.ESP), "m"(next->Context.Registers.EIP), "r"(prev), "r"(next) \
            : "memory");                                                                               \
    } while (0)

/************************************************************************/

#define JumpToReadyTask(Task, StackPointer)                                                         \
    __asm__ __volatile__(                                                                           \
        "finit\n\t"                                                                                 \
        "mov %0, %%eax\n\t"                                                                         \
        "mov %1, %%ebx\n\t"                                                                         \
        "mov %2, %%esp\n\t"                                                                         \
        "iret"                                                                                      \
        :                                                                                           \
        : "m"((Task)->Context.Registers.EAX), "m"((Task)->Context.Registers.EBX), "m"(StackPointer) \
        : "eax", "ebx", "memory");

/***************************************************************************/

#endif
