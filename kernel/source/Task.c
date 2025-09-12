
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

#include "../include/Clock.h"
#include "../include/I386.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Process.h"
#include "../include/Stack.h"
#include "../include/StackTrace.h"

/************************************************************************/

/**
 * @brief Allocates and initializes a new message structure.
 *
 * Creates a new message object with default values and reference count of 1.
 * The message ID is set to ID_MESSAGE for validation purposes.
 *
 * @return Pointer to newly allocated message, or NULL on allocation failure
 */
static LPMESSAGE NewMessage(void) {
    LPMESSAGE This;

    This = (LPMESSAGE)HeapAlloc(sizeof(MESSAGE));

    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(MESSAGE));

    This->ID = ID_MESSAGE;
    This->References = 1;

    return This;
}

/************************************************************************/

/**
 * @brief Deallocates a message structure.
 *
 * Clears the message ID and frees the memory allocated for the message.
 * The ID is set to ID_NONE to prevent use-after-free bugs.
 *
 * @param This Pointer to message to delete, ignored if NULL
 */
void DeleteMessage(LPMESSAGE This) {
    if (This == NULL) return;

    This->ID = ID_NONE;

    HeapFree(This);
}

/************************************************************************/

/**
 * @brief Destructor function for message objects in lists.
 *
 * Generic destructor callback that casts the void pointer to LPMESSAGE
 * and calls DeleteMessage. Used by list structures for automatic cleanup.
 *
 * @param This Generic pointer to message object to destroy
 */
void MessageDestructor(LPVOID This) { DeleteMessage((LPMESSAGE)This); }

/************************************************************************/

/**
 * @brief Allocates and initializes a new task structure.
 *
 * Creates a new task object with default values, initializes mutexes,
 * and sets up the message queue. The task ID is set to ID_TASK for validation.
 * Memory is validated before use to detect corruption.
 *
 * @return Pointer to newly allocated task, or NULL on allocation failure
 */
LPTASK NewTask(void) {
    TRACED_FUNCTION;

    LPTASK This = NULL;

    KernelLogText(LOG_DEBUG, TEXT("[NewTask] Enter"));

    This = (LPTASK)HeapAlloc(sizeof(TASK));

    if (This == NULL) {
        KernelLogText(LOG_ERROR, TEXT("[NewTask] Could not allocate memory for task"));

        TRACED_EPILOGUE("NewTask");
        return NULL;
    }

    if (IsValidMemory((LINEAR)This) == FALSE) {
        KernelLogText(LOG_ERROR, TEXT("[NewTask] Allocated task is not a valid pointer"));

        TRACED_EPILOGUE("NewTask");
        return NULL;
    }

    KernelLogText(LOG_DEBUG, TEXT("[NewTask] Task pointer = %X"), (LINEAR)This);

    *This = (TASK){.ID = ID_TASK, .References = 1, .Mutex = EMPTY_MUTEX, .MessageMutex = EMPTY_MUTEX, .Type = TASK_TYPE_NONE, .Status = TASK_STATUS_READY};

    KernelLogText(LOG_DEBUG, TEXT("[NewTask] Task initialized: Address=%X, Status=%X, TASK_STATUS_READY=%X"), (U32)This, This->Status, TASK_STATUS_READY);

    InitMutex(&(This->Mutex));
    InitMutex(&(This->MessageMutex));

    //-------------------------------------
    // Initialize the message queue

    KernelLogText(LOG_DEBUG, TEXT("[NewTask] Initialize task message queue"));
    KernelLogText(LOG_DEBUG, TEXT("[NewTask] MessageDestructor = %X"), (LINEAR)MessageDestructor);
    KernelLogText(LOG_DEBUG, TEXT("[NewTask] HeapAlloc = %X"), (LINEAR)HeapAlloc);
    KernelLogText(LOG_DEBUG, TEXT("[NewTask] HeapFree = %X"), (LINEAR)HeapFree);
    KernelLogText(LOG_DEBUG, TEXT("[NewTask] EBP = %X"), (LINEAR)GetEBP());

    This->Message = NewList(MessageDestructor, HeapAlloc, HeapFree);

    KernelLogText(LOG_DEBUG, TEXT("[NewTask] Exit"));

    TRACED_EPILOGUE("NewTask");
    return This;
}

/************************************************************************/

/**
 * @brief Deallocates a task structure and all associated resources.
 *
 * Unlocks all mutexes locked by this task, deletes the message queue,
 * frees stack memory, and deallocates the task structure itself.
 * Validates task ID before proceeding.
 *
 * @param This Pointer to task to delete, ignored if NULL or invalid
 */
void DeleteTask(LPTASK This) {
    TRACED_FUNCTION;

    LPLISTNODE Node = NULL;
    LPMUTEX Mutex = NULL;

    KernelLogText(LOG_DEBUG, TEXT("[DeleteTask] Enter"));

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return;
    if (This->ID != ID_TASK) return;

    //-------------------------------------
    // Unlock all mutexs locked by this task

    for (Node = Kernel.Mutex->First; Node; Node = Node->Next) {
        Mutex = (LPMUTEX)Node;

        if (Mutex->ID == ID_MUTEX && Mutex->Task == This) {
            Mutex->Task = NULL;
            Mutex->Lock = 0;
        }
    }

    //-------------------------------------
    // Delete the task's message queue

    KernelLogText(LOG_DEBUG, TEXT("[DeleteTask] Deleting message queue"));

    if (This->Message != NULL) DeleteList(This->Message);

    //-------------------------------------
    // Delete the task's stacks

    KernelLogText(LOG_DEBUG, TEXT("[DeleteTask] Deleting stacks"));

    if (This->SysStackBase != NULL) {
        KernelLogText(LOG_DEBUG, TEXT("[DeleteTask] Freeing SysStack: base=%X, size=%X"), This->SysStackBase, This->SysStackSize);
        FreeRegion(This->SysStackBase, This->SysStackSize);
    }

    if (This->Process != NULL) {
        if (This->StackBase != NULL) {
            KernelLogText(LOG_DEBUG, TEXT("[DeleteTask] Freeing Stack: base=%X, size=%X"), This->StackBase, This->StackSize);
            FreeRegion(This->StackBase, This->StackSize);
        }
    }

    //-------------------------------------
    // Remove from global kernel task list before freeing
    
    LockMutex(MUTEX_KERNEL, INFINITY);

    ListRemove(Kernel.Task, This);

    UnlockMutex(MUTEX_KERNEL);

    //-------------------------------------
    // Decrement process task count and check if process should be deleted
    if (This->Process != NULL && This->Process != &KernelProcess) {
        LockMutex(MUTEX_PROCESS, INFINITY);
        This->Process->TaskCount--;
        KernelLogText(LOG_DEBUG, TEXT("[DeleteTask] Process %s TaskCount decremented to %d"), This->Process->FileName, This->Process->TaskCount);
        
        if (This->Process->TaskCount == 0) {
            KernelLogText(LOG_DEBUG, TEXT("[DeleteTask] Process %s has no more tasks, deleting process"), This->Process->FileName);
            LPPROCESS ProcessToDelete = This->Process;
            UnlockMutex(MUTEX_PROCESS);
            DeleteProcess(ProcessToDelete);
        } else {
            UnlockMutex(MUTEX_PROCESS);
        }
    }

    //-------------------------------------
    // Free the task structure itself

    HeapFree(This);

    KernelLogText(LOG_DEBUG, TEXT("[DeleteTask] Exit"));

    TRACED_EPILOGUE("DeleteTask");
}

/************************************************************************/

/**
 * @brief Creates a new task with specified parameters and adds it to the scheduler.
 *
 * This function allocates memory for stack and system stack, sets up the task
 * context with appropriate privilege level, initializes register values, and
 * adds the task to both the kernel task list and scheduler queue. For main
 * kernel tasks, it performs stack switching from boot stack to allocated stack.
 *
 * @param Process Pointer to process that will own this task
 * @param Info Task creation parameters including function, stack size, priority
 * @return Pointer to created task, or NULL on failure
 */
LPTASK CreateTask(LPPROCESS Process, LPTASKINFO Info) {
    TRACED_FUNCTION;

    LPTASK Task = NULL;
    LINEAR StackTop = NULL;
    LINEAR SysStackTop = NULL;
    SELECTOR CodeSelector = 0;
    SELECTOR DataSelector = 0;

    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Enter"));
    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Process : %X"), Process);
    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Info : %X"), Info);

    if (Info != NULL) {
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Func : %X"), Info->Func);
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Parameter : %X"), Info->Parameter);
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Flags : %X"), Info->Flags);
    }

    //-------------------------------------
    // Check parameters

    if (Info->Func == NULL) {
        TRACED_EPILOGUE("CreateTask");
        return NULL;
    }

    if (Info->StackSize < TASK_MINIMUM_STACK_SIZE) {
        Info->StackSize = TASK_MINIMUM_STACK_SIZE;
    }

    if (Info->Priority > TASK_PRIORITY_CRITICAL) {
        Info->Priority = TASK_PRIORITY_CRITICAL;
    }

    if (IsValidMemory((LINEAR)Info->Func) == FALSE) {
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Function is not in mapped memory. Aborting."), Info->Func);

        TRACED_EPILOGUE("CreateTask");
        return NULL;
    }

    //-------------------------------------
    // Lock access to kernel data & to the process

    LockMutex(MUTEX_KERNEL, INFINITY);
    LockMutex(MUTEX_MEMORY, INFINITY);

    if (Process != &KernelProcess) {
        LockMutex(&(Process->Mutex), INFINITY);
        LockMutex(&(Process->HeapMutex), INFINITY);
    }

    //-------------------------------------
    // Instantiate a task

    Task = NewTask();

    if (Task == NULL) {
        KernelLogText(LOG_ERROR, TEXT("[CreateTask] NewTask failed"));
        goto Out;
    }

    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Task allocated at %X"), Task);

    //-------------------------------------
    // Setup the task

    Task->Process = Process;
    Task->Priority = Info->Priority;
    Task->Function = Info->Func;
    Task->Parameter = Info->Parameter;
    
    // Increment process task count
    if (Process != NULL) {
        LockMutex(MUTEX_PROCESS, INFINITY);
        Process->TaskCount++;
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Process %s TaskCount incremented to %d"), Process->FileName, Process->TaskCount);
        UnlockMutex(MUTEX_PROCESS);
    }
    Task->Type = (Process->Privilege == PRIVILEGE_KERNEL) ? TASK_TYPE_KERNEL_OTHER : TASK_TYPE_USER;

    SetTaskWakeUpTime(Task, ComputeTaskQuantumTime(Task->Priority));
    
    // Copy task name for debugging
    if (Info->Name[0] != STR_NULL) {
        StringCopy(Task->Name, Info->Name);
    } else {
        StringCopy(Task->Name, TEXT("Unnamed"));
    }

    //-------------------------------------
    // Allocate the stacks

    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Allocating stack..."));
    KernelLogText(
        LOG_DEBUG, TEXT("[CreateTask] Calling process heap base %X, size %X"), Process->HeapBase, Process->HeapSize);
    KernelLogText(
        LOG_DEBUG, TEXT("[CreateTask] Kernel process heap base %X, size %X"), KernelProcess.HeapBase,
        KernelProcess.HeapSize);
    KernelLogText(
        LOG_DEBUG, TEXT("[CreateTask] Process == KernelProcess ? %s"), (Process == &KernelProcess) ? "YES" : "NO");

    LINEAR BaseVMA = VMA_KERNEL;

    if (Process->Privilege == PRIVILEGE_USER) {
        BaseVMA = VMA_USER;
    }

    Task->StackSize = Info->StackSize;
    Task->SysStackSize = TASK_SYSTEM_STACK_SIZE * 4;

    Task->StackBase = AllocRegion(BaseVMA, 0, Task->StackSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE | ALLOC_PAGES_AT_OR_OVER);
    Task->SysStackBase = AllocKernelRegion(0, Task->SysStackSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE);
    
    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] BaseVMA=%X, Requested StackBase at BaseVMA"), BaseVMA);
    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Actually got StackBase=%X"), Task->StackBase);

    if (Task->StackBase == NULL || Task->SysStackBase == NULL) {
        if (Task->StackBase != NULL) {
            FreeRegion(Task->StackBase, Task->StackSize);
        }

        if (Task->SysStackBase != NULL) {
            FreeRegion(Task->SysStackBase, Task->SysStackSize);
        }

        DeleteTask(Task);
        Task = NULL;

        KernelLogText(LOG_ERROR, TEXT("[CreateTask] Stack or system stack allocation failed"));
        goto Out;
    }

    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Stack (%X bytes) allocated at %X"), Task->StackSize, Task->StackBase);
    KernelLogText(
        LOG_DEBUG, TEXT("[CreateTask] System stack (%X bytes) allocated at %X"), Task->SysStackSize,
        Task->SysStackBase);

    //-------------------------------------
    // Clear stacks

    MemorySet((void*)(Task->StackBase), 0, Task->StackSize);
    MemorySet((void*)(Task->SysStackBase), 0, Task->SysStackSize);

    //-------------------------------------
    // Setup privilege data

    if (Process->Privilege == PRIVILEGE_KERNEL) {
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Setting kernel privilege (ring 0)"));

        CodeSelector = SELECTOR_KERNEL_CODE;
        DataSelector = SELECTOR_KERNEL_DATA;
    } else {
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Setting user privilege (ring 3)"));

        CodeSelector = SELECTOR_USER_CODE;
        DataSelector = SELECTOR_USER_DATA;
    }

    StackTop = Task->StackBase + Task->StackSize;
    SysStackTop = Task->SysStackBase + Task->SysStackSize;

    MemorySet(&(Task->Context), 0, sizeof(INTERRUPTFRAME));

    Task->Context.Registers.EAX = (U32)Task->Parameter;
    Task->Context.Registers.EBX = (U32)Task->Function;
    Task->Context.Registers.ECX = 0;
    Task->Context.Registers.EDX = 0;

    Task->Context.Registers.CS = CodeSelector;
    Task->Context.Registers.DS = DataSelector;
    Task->Context.Registers.ES = DataSelector;
    Task->Context.Registers.FS = DataSelector;
    Task->Context.Registers.GS = DataSelector;
    Task->Context.Registers.SS = DataSelector;
    Task->Context.Registers.EFlags = EFLAGS_IF | EFLAGS_A1;
    Task->Context.Registers.CR3 = Process->PageDirectory;
    Task->Context.Registers.CR4 = GetCR4();
    Task->Context.Registers.EIP = VMA_TASK_RUNNER;

    if (Process->Privilege == PRIVILEGE_KERNEL) {
        Task->Context.Registers.ESP = StackTop - STACK_SAFETY_MARGIN;
        Task->Context.Registers.EBP = StackTop - STACK_SAFETY_MARGIN;
    } else {
        Task->Context.Registers.ESP = SysStackTop - STACK_SAFETY_MARGIN;
        Task->Context.Registers.EBP = SysStackTop - STACK_SAFETY_MARGIN;
    }

    if (Info->Flags & TASK_CREATE_MAIN_KERNEL) {
        Task->Status = TASK_STATUS_RUNNING;

        Kernel_i386.TSS->ESP0 = SysStackTop - STACK_SAFETY_MARGIN;

        LINEAR BootStackTop = KernelStartup.StackTop;
        LINEAR ESP = GetESP();
        U32 StackUsed = (BootStackTop - ESP) + 256;

        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] BootStackTop = %X"), BootStackTop);
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] StackTop = %X"), StackTop);
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] StackUsed = %X"), StackUsed);
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Switching to new stack..."));

        if (SwitchStack(StackTop, BootStackTop, StackUsed)) {
            Task->Context.Registers.ESP = 0;  // Not used for main task
            Task->Context.Registers.EBP = GetEBP();
            KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Main task stack switched successfully"));
        } else {
            KernelLogText(LOG_ERROR, TEXT("[CreateTask] Stack switch failed"));
        }
    }

    // Save flags for scheduler
    Task->Flags = Info->Flags;

    ListAddItem(Kernel.Task, Task);

    //-------------------------------------
    // Add task to the scheduler's queue

    if ((Info->Flags & TASK_CREATE_SUSPENDED) == 0) {
        AddTaskToQueue(Task);
    }

Out:

    if (Process != &KernelProcess) {
        UnlockMutex(&(Process->HeapMutex));
        UnlockMutex(&(Process->Mutex));
    }

    UnlockMutex(MUTEX_MEMORY);
    UnlockMutex(MUTEX_KERNEL);

    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Exit"));

    return Task;
}

/************************************************************************/

/**
 * @brief Terminates a task and frees all associated resources.
 *
 * Removes the task from the scheduler queue, marks it as dead, removes it
 * from the kernel task list.
 * Cannot be used to kill the main kernel task (will halt system).
 *
 * @param Task Pointer to task to kill
 * @return TRUE if task was successfully killed, FALSE if invalid or main task
 */
BOOL KillTask(LPTASK Task) {
    SAFE_USE_VALID_ID(Task, ID_TASK) {
        KernelLogText(LOG_DEBUG, TEXT("[KillTask] Enter"));

        if (Task->Type == TASK_TYPE_KERNEL_MAIN) {
            KernelLogText(LOG_ERROR, TEXT("[KillTask] Can't kill kernel task, halting"));
            DO_THE_SLEEPING_BEAUTY;
            return FALSE;
        }

        KernelLogText(LOG_DEBUG, TEXT("Process : %X"), Task->Process);
        KernelLogText(LOG_DEBUG, TEXT("Task : %X, func = %X"), Task, Task->Function);
        KernelLogText(
            LOG_DEBUG, TEXT("Message : %X"), Task->Message->First ? ((LPMESSAGE)Task->Message->First)->Message : 0);

        // Lock access to kernel data
        LockMutex(MUTEX_KERNEL, INFINITY);

        Task->References = 0;
        SetTaskStatus(Task, TASK_STATUS_DEAD);

        // Dead task remains in scheduler queue until context switch
        // This prevents silent CurrentIndex adjustments and phantom task changes
        // RemoveTaskFromQueue will be called during actual task switching

        // Don't remove from global kernel task list - DeleteDeadTasks will do it
        // The task will be killed by the monitor a bit later
        // Don't touch it for now

        // Unlock access to kernel data
        UnlockMutex(MUTEX_KERNEL);

        KernelLogText(LOG_DEBUG, TEXT("[KillTask] Exit"));

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Removes and deallocates all tasks marked as TASK_STATUS_DEAD.
 *
 * Iterates through the global task list and deletes any tasks that have been
 * marked as dead by KillTask(). This function is called periodically by the
 * kernel monitor thread to clean up terminated tasks.
 *
 * @note This function locks MUTEX_KERNEL during operation
 */
void DeleteDeadTasks(void) {
    LPTASK Task, NextTask;

    // KernelLogText(LOG_VERBOSE, TEXT("[DeleteDeadTasks]"));

    // Lock access to kernel data
    LockMutex(MUTEX_KERNEL, INFINITY);

    Task = (LPTASK)Kernel.Task->First;

    while (Task != NULL) {
        NextTask = (LPTASK)Task->Next;

        if (Task->Status == TASK_STATUS_DEAD) {
            KernelLogText(LOG_DEBUG, TEXT("[DeleteDeadTasks] About to delete task %x"), (U32)Task);

            // DeleteTask will handle removing from list and cleanup
            DeleteTask(Task);

            KernelLogText(LOG_DEBUG, TEXT("Deleted task %x"), (U32)Task);
        }

        Task = NextTask;
    }

    // Unlock access to kernel data
    UnlockMutex(MUTEX_KERNEL);
}

/************************************************************************/

/**
 * @brief Changes the priority of a task.
 *
 * @param Task Pointer to task to modify
 * @param Priority New priority value
 * @return Previous priority value, or 0 if task is NULL
 */
U32 SetTaskPriority(LPTASK Task, U32 Priority) {
    U32 OldPriority = 0;

    if (Task == NULL) return OldPriority;

    LockMutex(MUTEX_KERNEL, INFINITY);

    OldPriority = Task->Priority;
    Task->Priority = Priority;

    UnlockMutex(MUTEX_KERNEL);

    return OldPriority;
}

/************************************************************************/

/**
 * @brief Suspends the current task for a specified duration.
 *
 * Puts the current task to sleep for the specified number of milliseconds.
 * The task status is set to SLEEPING and a wake-up time is calculated.
 * The task will remain suspended until the timer interrupt moves it back to RUNNING.
 *
 * @param MilliSeconds Number of milliseconds to sleep
 */
void Sleep(U32 MilliSeconds) {
    LPTASK Task;

    U32 Flags;
    SaveFlags(&Flags);
    DisableInterrupts();
    // KernelLogText(LOG_DEBUG, TEXT("[Sleep] Enter : IF = %x"), Flags & 0x200);

    // Lock the task mutex
    LockMutex(MUTEX_TASK, INFINITY);

    Task = GetCurrentTask();

    SAFE_USE_VALID_ID(Task, ID_TASK) {
        if (Task->Status == TASK_STATUS_DEAD) {
            UnlockMutex(MUTEX_TASK);
            return;
        }

        SetTaskStatus(Task, TASK_STATUS_SLEEPING);
        SetTaskWakeUpTime(Task, MilliSeconds);

        UnlockMutex(MUTEX_TASK);

        // Block here until scheduler wakes us up
        while (GetTaskStatus(Task) == TASK_STATUS_SLEEPING) {
            if (Task->ID != ID_TASK) {
                return;
            }

            if (Task->Status == TASK_STATUS_DEAD) {
                return;
            }

            IdleCPU();
            DisableInterrupts();
        }

        // KernelLogText(LOG_DEBUG, TEXT("[Sleep] Exit %x (%s)"), Task, Task->Name);
        return;
    }

    UnlockMutex(MUTEX_TASK);
    RestoreFlags(&Flags);

    // KernelLogText(LOG_DEBUG, TEXT("[Sleep] Exit"));
    return;
}

/************************************************************************/

/**
 * @brief Retrieves the current status of a task.
 *
 * @param Task Pointer to task to query
 * @return Task status value, or 0 if task is NULL
 */
U32 GetTaskStatus(LPTASK Task) {
    U32 Status = 0;

    if (Task == NULL) return Status;

    LockMutex(&(Task->Mutex), INFINITY);

    Status = Task->Status;

    UnlockMutex(&(Task->Mutex));

    return Status;
}

/************************************************************************/

/**
 * @brief Sets a task status to the specified value.
 *
 * @param Task Pointer to task to modify
 * @param Status New status value to set
 */
void SetTaskStatus(LPTASK Task, U32 Status) {
    if (Task == NULL) return;

    U32 OldStatus = Task->Status;
    
    LockMutex(&(Task->Mutex), INFINITY);
    FreezeScheduler();

    Task->Status = Status;
    
#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[SetTaskStatus] Task %x (%s): %X -> %x"), Task, Task->Name, OldStatus, Status);
#endif

    UnfreezeScheduler();
    UnlockMutex(&(Task->Mutex));
}

/************************************************************************/

/**
 * @brief Sets the wake-up time for a task in a thread-safe manner.
 *
 * @param Task Pointer to task to modify
 * @param WakeupTime Wake-up time in milliseconds
 */
void SetTaskWakeUpTime(LPTASK Task, U32 WakeupTime) {
    if (Task == NULL) return;

    LockMutex(&(Task->Mutex), INFINITY);

    Task->WakeUpTime = GetSystemTime() + Kernel.MinimumQuantum + WakeupTime;

    UnlockMutex(&(Task->Mutex));
}

/***************************************************************************/

/**
 * @brief Calculates the time quantum for a task based on its priority.
 *
 * Higher priority tasks get longer time slices. Minimum quantum is 20ms.
 *
 * @param Priority Task priority value
 * @return Time quantum in milliseconds
 */
U32 ComputeTaskQuantumTime(U32 Priority) {
    U32 Time = (Priority & 0xFF) * 2;
    return (Time < Kernel.MinimumQuantum) ? Kernel.MinimumQuantum : Time;
}

/************************************************************************/

/**
 * @brief Adds a message to a task's message queue in a thread-safe manner.
 *
 * Adds the specified message to the task's message queue. This function
 * locks both the task's mutex and message mutex to ensure thread safety.
 * The message will be processed when the task calls GetMessage().
 *
 * @param Task Pointer to the target task
 * @param Message Pointer to the message to add to the queue
 *
 * @note This function acquires task and message mutexes
 */
void AddTaskMessage(LPTASK Task, LPMESSAGE Message) {
    LockMutex(&(Task->Mutex), INFINITY);
    LockMutex(&(Task->MessageMutex), INFINITY);

    ListAddItem(Task->Message, Message);

    UnlockMutex(&(Task->MessageMutex));
    UnlockMutex(&(Task->Mutex));
}

/************************************************************************/

/**
 * @brief Posts a message asynchronously to a task or window.
 *
 * Sends a message to the specified target without waiting for completion.
 * The target can be a task handle or window handle. For windows, the message
 * is queued to the window's owning task. If the target task is waiting for
 * messages (TASK_STATUS_WAITMESSAGE), it will be awakened.
 *
 * @param Target Handle to the target task or window
 * @param Msg Message identifier
 * @param Param1 First message parameter
 * @param Param2 Second message parameter
 * @return TRUE if message was posted successfully, FALSE on error
 *
 * @note This function locks MUTEX_TASK and MUTEX_DESKTOP during operation
 * @note For EWM_DRAW messages, duplicates are consolidated to prevent flooding
 */
BOOL PostMessage(HANDLE Target, U32 Msg, U32 Param1, U32 Param2) {
    LPLISTNODE Node;
    LPMESSAGE Message;
    LPTASK Task;
    LPDESKTOP Desktop;
    LPWINDOW Win;

    //-------------------------------------
    // Check validity of parameters

    if (Target == NULL) return FALSE;

    //-------------------------------------
    // Lock access to resources

    LockMutex(MUTEX_TASK, INFINITY);
    LockMutex(MUTEX_DESKTOP, INFINITY);

    //-------------------------------------
    // Check if the target is a task

    for (Node = Kernel.Task->First; Node; Node = Node->Next) {
        Task = (LPTASK)Node;

        if (Task == (LPTASK)Target) {
            Message = NewMessage();
            if (Message == NULL) goto Out_Error;

            GetLocalTime(&(Message->Time));

            Message->Target = Target;
            Message->Message = Msg;
            Message->Param1 = Param1;
            Message->Param2 = Param2;

            AddTaskMessage(Task, Message);

            //-------------------------------------
            // Notify the task if it is waiting for messages

            if (GetTaskStatus(Task) == TASK_STATUS_WAITMESSAGE) {
                SetTaskStatus(Task, TASK_STATUS_RUNNING);
            }

            goto Out_Success;
        }
    }

    //-------------------------------------
    // Check if the target is a desktop

    /*
      for (Node = Kernel.Desktop->First; Node; Node = Node->Next)
      {
    Desktop = (LPDESKTOP) Node;

    if (Desktop == (LPDESKTOP) Target)
    {
      Message = NewMessage();
      if (Message == NULL) goto Out_Error;

      GetLocalTime(&(Message->Time));

      Message->Target  = Target;
      Message->Message = Msg;
      Message->Param1  = Param1;
      Message->Param2  = Param2;

      AddTaskMessage(Desktop->Task, Message);

      //-------------------------------------
      // Notify the task if it is waiting for messages

      if (GetTaskStatus(Desktop->Task) == TASK_STATUS_WAITMESSAGE)
      {
        SetTaskStatus(Desktop->Task, TASK_STATUS_RUNNING);
      }

      goto Out_Success;
    }
      }
    */

    //-------------------------------------
    // Check if the target is a window

    Desktop = GetCurrentProcess()->Desktop;

    if (Desktop == NULL) goto Out_Error;
    if (Desktop->ID != ID_DESKTOP) goto Out_Error;

    //-------------------------------------
    // Lock access to the desktop

    LockMutex(&(Desktop->Mutex), INFINITY);

    //-------------------------------------
    // Find the window in the desktop

    Win = FindWindow(Desktop->Window, (LPWINDOW)Target);

    //-------------------------------------
    // Unlock access to the desktop

    UnlockMutex(&(Desktop->Mutex));

    //-------------------------------------
    // Post message to window if found

    if (Win != NULL) {
        //-------------------------------------
        // If the message is EWM_DRAW, do not post it if
        // window already has one. Instead, put the existing
        // one at the end of the queue

        if (Msg == EWM_DRAW) {
            LockMutex(&(Win->Task->Mutex), INFINITY);
            LockMutex(&(Win->Task->MessageMutex), INFINITY);

            for (Node = Win->Task->Message->First; Node; Node = Node->Next) {
                Message = (LPMESSAGE)Node;
                if (Message->Target == (HANDLE)Win && Message->Message == Msg) {
                    ListRemove(Win->Task->Message, Message);

                    GetLocalTime(&(Message->Time));

                    Message->Param1 = Param1;
                    Message->Param2 = Param2;

                    ListAddItem(Win->Task->Message, Message);

                    UnlockMutex(&(Win->Task->MessageMutex));
                    UnlockMutex(&(Win->Task->Mutex));

                    goto Out_Success;
                }
            }
        }

        UnlockMutex(&(Win->Task->MessageMutex));
        UnlockMutex(&(Win->Task->Mutex));

        //-------------------------------------
        // Add the message to the task's queue

        Message = NewMessage();
        if (Message == NULL) goto Out_Error;

        GetLocalTime(&(Message->Time));

        Message->Target = Target;
        Message->Message = Msg;
        Message->Param1 = Param1;
        Message->Param2 = Param2;

        AddTaskMessage(Win->Task, Message);

        //-------------------------------------
        // Notify the task if it is waiting for messages

        if (GetTaskStatus(Win->Task) == TASK_STATUS_WAITMESSAGE) {
            SetTaskStatus(Win->Task, TASK_STATUS_RUNNING);
        }

        goto Out_Success;
    }

Out_Error:

    UnlockMutex(MUTEX_DESKTOP);
    UnlockMutex(MUTEX_TASK);
    return FALSE;

Out_Success:

    UnlockMutex(MUTEX_DESKTOP);
    UnlockMutex(MUTEX_TASK);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Sends a message synchronously to a window and waits for the result.
 *
 * Directly calls the window's message handler function and returns the result.
 * Unlike PostMessage(), this function waits for the window to process the
 * message before returning. Only works with window targets, not task targets.
 *
 * @param Target Handle to the target window
 * @param Msg Message identifier
 * @param Param1 First message parameter
 * @param Param2 Second message parameter
 * @return Result value returned by the window's message handler, or 0 on error
 *
 * @note This function locks the desktop and window mutexes during operation
 * @note The target must be a valid window in the current process's desktop
 */
U32 SendMessage(HANDLE Target, U32 Msg, U32 Param1, U32 Param2) {
    LPDESKTOP Desktop = NULL;
    LPWINDOW Window = NULL;
    U32 Result = 0;

    //-------------------------------------
    // Check if the target is a window

    Desktop = GetCurrentProcess()->Desktop;

    if (Desktop == NULL) return 0;
    if (Desktop->ID != ID_DESKTOP) return 0;

    //-------------------------------------
    // Lock access to the desktop

    LockMutex(&(Desktop->Mutex), INFINITY);

    //-------------------------------------
    // Find the window in the desktop

    Window = FindWindow(Desktop->Window, (LPWINDOW)Target);

    //-------------------------------------
    // Unlock access to the desktop

    UnlockMutex(&(Desktop->Mutex));

    //-------------------------------------
    // Send message to window if found

    if (Window != NULL && Window->ID == ID_WINDOW) {
        if (Window->Function != NULL) {
            LockMutex(&(Window->Mutex), INFINITY);
            Result = Window->Function(Target, Msg, Param1, Param2);
            UnlockMutex(&(Window->Mutex));
        }
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Blocks the specified task until a message arrives in its queue.
 *
 * Sets the task status to TASK_STATUS_WAITMESSAGE and yields CPU cycles
 * until another thread posts a message to the task's queue. The task will
 * remain blocked until PostMessage() or another message-sending function
 * changes its status back to TASK_STATUS_RUNNING.
 *
 * @param Task Pointer to the task that should wait for messages
 *
 * @note This function freezes the scheduler temporarily during status change
 * @note Uses IdleCPU() to yield processor time while waiting
 */
void WaitForMessage(LPTASK Task) {
    //-------------------------------------
    // Change the task's status

    SetTaskStatus(Task, TASK_STATUS_WAITMESSAGE);
    SetTaskWakeUpTime(Task, MAX_U16);

    //-------------------------------------
    // The following loop is to make sure that
    // the task will not return immediately.
    // During the loop, the task does not get any
    // CPU cycles.

    while (GetTaskStatus(Task) == TASK_STATUS_WAITMESSAGE) {
        IdleCPU();
    }
}

/************************************************************************/

/**
 * @brief Retrieves the next message from the current task's message queue.
 *
 * If no messages are available, the task will wait until a message arrives.
 * Messages can be filtered by target or retrieved in FIFO order.
 *
 * @param Message Pointer to message info structure to fill
 * @return TRUE if message retrieved successfully, FALSE on ETM_QUIT or error
 */
BOOL GetMessage(LPMESSAGEINFO Message) {
    LPTASK Task;
    LPMESSAGE CurrentMessage;
    LPLISTNODE Node;

    //-------------------------------------
    // Check validity of parameters

    if (Message == NULL) return FALSE;

    Task = GetCurrentTask();

    LockMutex(&(Task->Mutex), INFINITY);
    LockMutex(&(Task->MessageMutex), INFINITY);

    if (Task->Message->NumItems == 0) {
        UnlockMutex(&(Task->Mutex));
        UnlockMutex(&(Task->MessageMutex));

        WaitForMessage(Task);

        LockMutex(&(Task->Mutex), INFINITY);
        LockMutex(&(Task->MessageMutex), INFINITY);
    }

    if (Message->Target == NULL) {
        CurrentMessage = (LPMESSAGE)Task->Message->First;

        //-------------------------------------
        // Copy the message to the user structure

        Message->Target = CurrentMessage->Target;
        Message->Time = CurrentMessage->Time;
        Message->Message = CurrentMessage->Message;
        Message->Param1 = CurrentMessage->Param1;
        Message->Param2 = CurrentMessage->Param2;

        //-------------------------------------
        // Remove the message from the task's message queue

        ListEraseItem(Task->Message, CurrentMessage);

        if (Message->Message == ETM_QUIT) goto Out_Error;

        goto Out_Success;
    } else {
        for (Node = Task->Message->First; Node; Node = Node->Next) {
            CurrentMessage = (LPMESSAGE)Node;

            if (CurrentMessage->Target == Message->Target) {
                //-------------------------------------
                // Copy the message to the user structure

                Message->Target = CurrentMessage->Target;
                Message->Time = CurrentMessage->Time;
                Message->Message = CurrentMessage->Message;
                Message->Param1 = CurrentMessage->Param1;
                Message->Param2 = CurrentMessage->Param2;

                //-------------------------------------
                // Remove the message from the task's message queue

                ListEraseItem(Task->Message, CurrentMessage);

                if (Message->Message == ETM_QUIT) goto Out_Error;

                goto Out_Success;
            }
        }
    }

Out_Success:

    UnlockMutex(&(Task->Mutex));
    UnlockMutex(&(Task->MessageMutex));
    return TRUE;

Out_Error:

    UnlockMutex(&(Task->Mutex));
    UnlockMutex(&(Task->MessageMutex));
    return FALSE;
}

/************************************************************************/

/**
 * @brief Dispatches a message to a specific window or its children.
 *
 * Attempts to deliver a message to the specified window. If the window handle
 * matches the message target, the window's message handler is called directly.
 * Otherwise, the function recursively searches through the window's children
 * to find the correct target window.
 *
 * @param Message Pointer to the message information structure
 * @param Window Pointer to the window to check (and its children)
 * @return TRUE if message was delivered successfully, FALSE otherwise
 *
 * @note This function locks the window mutex during message delivery
 * @note Recursively searches child windows if target doesn't match
 */
static BOOL DispatchMessageToWindow(LPMESSAGEINFO Message, LPWINDOW Window) {
    LPLISTNODE Node = NULL;
    BOOL Result = FALSE;

    //-------------------------------------
    // Check validity of parameters

    if (Message == NULL) return FALSE;
    if (Message->Target == NULL) return FALSE;

    if (Window == NULL) return FALSE;
    if (Window->ID != ID_WINDOW) return FALSE;

    //-------------------------------------
    // Lock access to the window

    LockMutex(&(Window->Mutex), INFINITY);

    if (Message->Target == (HANDLE)Window) {
        if (Window->Function != NULL) {
            // Call the window function with the parameters

            Window->Function(Message->Target, Message->Message, Message->Param1, Message->Param2);

            Result = TRUE;
        }
    } else {
        for (Node = Window->Children->First; Node; Node = Node->Next) {
            Result = DispatchMessageToWindow(Message, (LPWINDOW)Node);

            if (Result == TRUE) break;
        }
    }

    //-------------------------------------
    // Unlock access to the window

    UnlockMutex(&(Window->Mutex));

    return Result;
}

/************************************************************************/

/**
 * @brief Dispatches a message to its target window within the current desktop.
 *
 * Routes a message to the appropriate window in the current process's desktop.
 * The function validates the current process and desktop, then uses
 * DispatchMessageToWindow() to find and deliver the message to the correct
 * window target.
 *
 * @param Message Pointer to the message information structure containing
 *                target handle and message parameters
 * @return TRUE if message was dispatched successfully, FALSE on error
 *
 * @note This function locks MUTEX_TASK and the desktop mutex during operation
 * @note Only works within the context of the current process's desktop
 */
BOOL DispatchMessage(LPMESSAGEINFO Message) {
    LPPROCESS Process = NULL;
    LPDESKTOP Desktop = NULL;
    // LPLISTNODE Node = NULL;
    BOOL Result = FALSE;

    //-------------------------------------
    // Check validity of parameters

    if (Message == NULL) return FALSE;
    if (Message->Target == NULL) return FALSE;

    //-------------------------------------
    // Lock access to resources

    LockMutex(MUTEX_TASK, INFINITY);

    //-------------------------------------
    // Check if the target is a task

    /*
      for (Node = Kernel.Task->First; Node; Node = Node->Next)
      {
      }
    */

    //-------------------------------------
    // Check if the target is a window

    Process = GetCurrentProcess();
    if (Process == NULL) goto Out;
    if (Process->ID != ID_PROCESS) goto Out;

    Desktop = Process->Desktop;
    if (Desktop == NULL) goto Out;
    if (Desktop->ID != ID_DESKTOP) goto Out;

    LockMutex(&(Desktop->Mutex), INFINITY);

    Result = DispatchMessageToWindow(Message, Desktop->Window);

    UnlockMutex(&(Desktop->Mutex));

Out:

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(MUTEX_TASK);

    return Result;
}

/************************************************************************/

/**
 * @brief Outputs detailed task information to the debug log.
 *
 * Prints all task fields including addresses, status, priority, stack info,
 * timing information, and message queue status for debugging purposes.
 *
 * @param Task Pointer to task to dump
 */
void DumpTask(LPTASK Task) {
    LockMutex(&(Task->Mutex), INFINITY);

    KernelLogText(LOG_VERBOSE, TEXT("Address         : %X"), Task);
    KernelLogText(LOG_VERBOSE, TEXT("Task Name       : %s"), Task->Name);
    KernelLogText(LOG_VERBOSE, TEXT("References      : %d"), Task->References);
    KernelLogText(LOG_VERBOSE, TEXT("Process         : %X"), Task->Process);
    KernelLogText(LOG_VERBOSE, TEXT("Status          : %X"), Task->Status);
    KernelLogText(LOG_VERBOSE, TEXT("Priority        : %X"), Task->Priority);
    KernelLogText(LOG_VERBOSE, TEXT("Function        : %X"), Task->Function);
    KernelLogText(LOG_VERBOSE, TEXT("Parameter       : %X"), Task->Parameter);
    KernelLogText(LOG_VERBOSE, TEXT("ReturnValue     : %X"), Task->ReturnValue);
    KernelLogText(LOG_VERBOSE, TEXT("StackBase       : %X"), Task->StackBase);
    KernelLogText(LOG_VERBOSE, TEXT("StackSize       : %X"), Task->StackSize);
    KernelLogText(LOG_VERBOSE, TEXT("SysStackBase    : %X"), Task->SysStackBase);
    KernelLogText(LOG_VERBOSE, TEXT("SysStackSize    : %X"), Task->SysStackSize);
    KernelLogText(LOG_VERBOSE, TEXT("WakeUpTime      : %d"), Task->WakeUpTime);
    KernelLogText(LOG_VERBOSE, TEXT("Queued messages : %d"), Task->Message->NumItems);

    UnlockMutex(&(Task->Mutex));
}

/************************************************************************/
