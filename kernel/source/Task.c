
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

    *This = (TASK){.ID = ID_TASK, .References = 1, .Mutex = EMPTY_MUTEX, .MessageMutex = EMPTY_MUTEX};

    InitMutex(&(This->Mutex));
    InitMutex(&(This->MessageMutex));

    //-------------------------------------
    // Initialize the message queue

    KernelLogText(LOG_DEBUG, TEXT("[NewTask] Initialize task message queue"));
    KernelLogText(LOG_DEBUG, TEXT("[NewTask] MessageDestructor = %X"), (LINEAR)MessageDestructor);
    KernelLogText(LOG_DEBUG, TEXT("[NewTask] HeapAlloc = %X"), (LINEAR)HeapAlloc);
    KernelLogText(LOG_DEBUG, TEXT("[NewTask] HeapFree = %X"), (LINEAR)HeapFree);
    KernelLogText(LOG_DEBUG, TEXT("[NewTask] EBP = %X"), (LINEAR)GetEBP());

    This->Type = TASK_TYPE_KERNEL_OTHER;
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
        FreeRegion(This->SysStackBase, This->SysStackSize);
    }

    if (This->Process != NULL) {
        if (This->StackBase != NULL) {
            FreeRegion(This->StackBase, This->StackSize);
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
    Task->Type = (Process->Privilege == PRIVILEGE_KERNEL) ? TASK_TYPE_KERNEL_OTHER : TASK_TYPE_USER;
    
    // Copy task name for debugging
    if (Info->Name[0] != STR_NULL) {
        StringCopy(Task->Name, Info->Name);
    } else {
        StringCopy(Task->Name, TEXT("Unnamed"));
    }

    //-------------------------------------
    // Allocate the stack

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

    Task->StackBase = AllocRegion(BaseVMA, 0, Task->StackSize, ALLOC_PAGES_COMMIT | ALLOC_PAGES_AT_OR_OVER);
    Task->SysStackBase = AllocKernelRegion(0, Task->SysStackSize, ALLOC_PAGES_COMMIT);
    
    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] CRITICAL: BaseVMA=%X, Requested StackBase at BaseVMA"), BaseVMA);
    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] CRITICAL: Actually got StackBase=%X"), Task->StackBase);

    if (Task->StackBase == NULL || Task->SysStackBase == NULL) {
        if (Task->StackBase != NULL) {
            FreeRegion(Task->StackBase, Task->StackSize);
        }

        if (Task->SysStackBase != NULL) {
            FreeRegion(Task->SysStackBase, Task->StackSize);
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

    // Set EIP appropriately: VMA_TASK_RUNNER for kernel tasks, actual entry point for user tasks
    if (Process->Privilege == PRIVILEGE_KERNEL) {
        Task->Context.Registers.EIP = VMA_TASK_RUNNER;
    } else {
        // For user processes, EIP should be the actual entry point
        Task->Context.Registers.EIP = (U32)Task->Function;
    }
    Task->Context.Registers.EAX = (U32)Task->Parameter;
    Task->Context.Registers.EBX = (U32)Task->Function;
    Task->Context.Registers.ECX = 0;
    Task->Context.Registers.EDX = 0;
    
    Task->Context.Registers.ESP = StackTop;
    Task->Context.Registers.EBP = StackTop;
    Task->Context.Registers.CS = CodeSelector;
    Task->Context.Registers.DS = DataSelector;
    Task->Context.Registers.ES = DataSelector;
    Task->Context.Registers.FS = DataSelector;
    Task->Context.Registers.GS = DataSelector;
    Task->Context.Registers.SS = DataSelector;
    Task->Context.Registers.EFlags = EFLAGS_IF | EFLAGS_A1;

    // Debug logs for user tasks
    if (Process->Privilege != PRIVILEGE_KERNEL) {
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] USER TASK DEBUG:"));
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask]   EIP = 0x%X"), Task->Context.Registers.EIP);
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask]   EBX (Function) = 0x%X"), Task->Context.Registers.EBX);
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask]   ESP = 0x%X"), Task->Context.Registers.ESP);
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask]   CS = 0x%X, DS = 0x%X"), Task->Context.Registers.CS, Task->Context.Registers.DS);
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask]   StackBase = 0x%X, StackSize = 0x%X"), Task->StackBase, Task->StackSize);
    }

    if (Info->Flags & TASK_CREATE_MAIN_KERNEL) {
        Kernel_i386.TSS->ESP0 = SysStackTop;

        LINEAR BootStackTop = KernelStartup.StackTop;
        LINEAR ESP = GetESP();
        U32 StackUsed = (BootStackTop - ESP) + 256;

        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] BootStackTop = %X"), BootStackTop);
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] StackTop = %X"), StackTop);
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] StackUsed = %X"), StackUsed);
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Switching to new stack..."));

        // Use the new SwitchStack function
        if (SwitchStack(StackTop, BootStackTop, StackUsed)) {
            Task->Context.Registers.ESP = 0;  // Not used for main task
            Task->Context.Registers.EBP = GetEBP();
            KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Main task stack switched successfully"));
        } else {
            KernelLogText(LOG_ERROR, TEXT("[CreateTask] Stack switch failed"));
        }
    }

    //-------------------------------------
    // Set the task's status as running

    Task->Status = TASK_STATUS_RUNNING;
    Task->Flags = Info->Flags; // Save flags for scheduler

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
 * from the kernel task list, and calls DeleteTask to free resources.
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

        // Remove task from scheduler queue
        RemoveTaskFromQueue(Task);

        // Lock access to kernel data
        LockMutex(MUTEX_KERNEL, INFINITY);

        Task->References = 0;
        Task->Status = TASK_STATUS_DEAD;

        // Remove from global kernel task list BEFORE freeing
        ListRemove(Kernel.Task, Task);

        // Unlock access to kernel data
        UnlockMutex(MUTEX_KERNEL);

        // Finally, free all resources owned by the task
        DeleteTask(Task);

        KernelLogText(LOG_DEBUG, TEXT("[KillTask] Exit"));

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

void DeleteDeadTasks(void) {
    LPTASK Task, NextTask;
    
    // Lock access to kernel data
    LockMutex(MUTEX_KERNEL, INFINITY);
    
    Task = (LPTASK)Kernel.Task->First;
    
    while (Task != NULL) {
        NextTask = (LPTASK)Task->Next;
        
        if (Task->Status == TASK_STATUS_DEAD) {
            RemoveTaskFromQueue(Task);
            ListRemove(Kernel.Task, Task);
            DeleteTask(Task);

            KernelLogText(LOG_VERBOSE, TEXT("Deleted task %x"), (U32)Task);
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

    FreezeScheduler();
    Task = GetCurrentTask();
    if (Task == NULL) {
        UnfreezeScheduler();
        return;
    }

    Task->Status = TASK_STATUS_SLEEPING;
    Task->WakeUpTime = GetSystemTime() + MilliSeconds;
    UnfreezeScheduler();

    // Block here until the timer IRQ moves us back to RUNNING
    while (Task->Status == TASK_STATUS_SLEEPING) {
        IdleCPU();
    }
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
 * @brief Sets a task status to RUNNING (ignores Status parameter).
 *
 * @param Task Pointer to task to modify
 * @param Status Unused parameter (always sets to RUNNING)
 */
void SetTaskStatus(LPTASK Task, U32 Status) {
    UNUSED(Status);

    LockMutex(&(Task->Mutex), INFINITY);
    FreezeScheduler();

    Task->Status = TASK_STATUS_RUNNING;

    UnfreezeScheduler();
    UnlockMutex(&(Task->Mutex));
}

/************************************************************************/

void AddTaskMessage(LPTASK Task, LPMESSAGE Message) {
    LockMutex(&(Task->Mutex), INFINITY);
    LockMutex(&(Task->MessageMutex), INFINITY);

    ListAddItem(Task->Message, Message);

    UnlockMutex(&(Task->MessageMutex));
    UnlockMutex(&(Task->Mutex));
}

/************************************************************************/

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

void WaitForMessage(LPTASK Task) {
    //-------------------------------------
    // Change the task's status

    FreezeScheduler();

    Task->Status = TASK_STATUS_WAITMESSAGE;
    Task->WakeUpTime = INFINITY;

    UnfreezeScheduler();

    //-------------------------------------
    // The following loop is to make sure that
    // the task will not return immediately.
    // During the loop, the task does not get any
    // CPU cycles.

    while (Task->Status == TASK_STATUS_WAITMESSAGE) {
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
    KernelLogText(LOG_VERBOSE, TEXT("Time            : %d"), Task->Time);
    KernelLogText(LOG_VERBOSE, TEXT("WakeUpTime      : %d"), Task->WakeUpTime);
    KernelLogText(LOG_VERBOSE, TEXT("Queued messages : %d"), Task->Message->NumItems);

    UnlockMutex(&(Task->Mutex));
}

/************************************************************************/
