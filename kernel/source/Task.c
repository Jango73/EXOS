
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\************************************************************************/

#include "../include/Clock.h"
#include "../include/I386.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Process.h"

/************************************************************************/

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

void DeleteMessage(LPMESSAGE This) {
    if (This == NULL) return;

    This->ID = ID_NONE;

    HeapFree(This);
}

/************************************************************************/

void MessageDestructor(LPVOID This) { DeleteMessage((LPMESSAGE)This); }

/************************************************************************/

LPTASK NewTask(void) {
    LPTASK This = NULL;

    KernelLogText(LOG_DEBUG, TEXT("[NewTask] Enter"));

    This = (LPTASK)HeapAlloc(sizeof(TASK));

    if (This == NULL) {
        KernelLogText(LOG_ERROR, TEXT("[NewTask] Could not allocate memory for task"));
        return NULL;
    }

    if (IsValidMemory((LINEAR)This) == FALSE) {
        KernelLogText(LOG_ERROR, TEXT("[NewTask] Allocated task is not a valid pointer"));
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

    return This;
}

/***************************************************************************/

void DeleteTask(LPTASK This) {
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
        HeapFree_HBHS(KernelProcess.HeapBase, KernelProcess.HeapSize, (LPVOID)This->SysStackBase);
    }

    if (This->Process != NULL) {
        if (This->StackBase != NULL) {
            HeapFree_HBHS(This->Process->HeapBase, This->Process->HeapSize, (LPVOID)This->StackBase);
        }
    }

    //-------------------------------------
    // Free the task structure itself

    HeapFree(This);

    KernelLogText(LOG_DEBUG, TEXT("[DeleteTask] Exit"));
}

/***************************************************************************/

LPTASK CreateTask(LPPROCESS Process, LPTASKINFO Info) {
    LPTASK Task = NULL;
    LINEAR StackPointer = NULL;
    LINEAR SysStackPointer = NULL;
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

    if (Info->Func == NULL) return NULL;

    if (Info->StackSize < TASK_MINIMUM_STACK_SIZE) {
        Info->StackSize = TASK_MINIMUM_STACK_SIZE;
    }

    if (Info->Priority > TASK_PRIORITY_CRITICAL) {
        Info->Priority = TASK_PRIORITY_CRITICAL;
    }

    if (IsValidMemory((LINEAR)Info->Func) == FALSE) {
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Function is not in mapped memory. Aborting."), Info->Func);
        return NULL;
    }

    //-------------------------------------
    // Lock access to kernel data & to the process

    LockMutex(MUTEX_KERNEL, INFINITY);
    LockMutex(&(Process->Mutex), INFINITY);

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

    //-------------------------------------
    // Allocate the stack

    Task->StackSize = Info->StackSize;
    Task->StackBase = (LINEAR)HeapAlloc_HBHS(Process->HeapBase, Process->HeapSize, Task->StackSize);

    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Calling process heap base %X, size %X"), Process->HeapBase, Process->HeapSize);

    //-------------------------------------
    // Allocate the task stack

    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Kernel process heap base %X, size %X"), KernelProcess.HeapBase, KernelProcess.HeapSize);

    Task->SysStackSize = TASK_SYSTEM_STACK_SIZE * 3;
    Task->SysStackBase = (LINEAR)HeapAlloc_HBHS(KernelProcess.HeapBase, KernelProcess.HeapSize, Task->SysStackSize);

    if (Task->StackBase == NULL || Task->SysStackBase == NULL) {
        if (Task->StackBase != NULL) {
            HeapFree_HBHS(Process->HeapBase, Process->HeapSize, (LPVOID)Task->StackBase);
        }

        if (Task->SysStackBase != NULL) {
            HeapFree_HBHS(KernelProcess.HeapBase, KernelProcess.HeapSize, (LPVOID)Task->SysStackBase);
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

    StackPointer = Task->StackBase + Task->StackSize;
    SysStackPointer = Task->SysStackBase + Task->SysStackSize;
    Task->SysStackTop = SysStackPointer;

    MemorySet(&(Task->Context), 0, sizeof(TRAPFRAME));
    Task->Context.EIP = (U32)TaskRunner;
    Task->Context.EAX = (U32)Task->Parameter;
    Task->Context.EBX = (U32)Task->Function;
    Task->Context.ESP = StackPointer;
    Task->Context.EBP = StackPointer;
    Task->Context.CS = CodeSelector;
    Task->Context.DS = DataSelector;
    Task->Context.ES = DataSelector;
    Task->Context.FS = DataSelector;
    Task->Context.GS = DataSelector;
    Task->Context.SS = DataSelector;
    Task->Context.UserESP = StackPointer;
    Task->Context.EFlags = EFLAGS_IF | EFLAGS_A1;

    if (Info->Flags & TASK_CREATE_MAIN) {
        Kernel_i386.TSS->ESP0 = Task->SysStackTop;
    }

    //-------------------------------------
    // Set the task's status as running

    Task->Status = TASK_STATUS_RUNNING;

    ListAddItem(Kernel.Task, Task);

    //-------------------------------------
    // Add task to the scheduler's queue

    if ((Info->Flags & TASK_CREATE_SUSPENDED) == 0) {
        AddTaskToQueue(Task);
    }

Out:

    UnlockMutex(&(Process->Mutex));
    UnlockMutex(MUTEX_KERNEL);

    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Exit"));

    return Task;
}

/***************************************************************************/

BOOL KillTask(LPTASK Task) {
    if (Task == NULL) return FALSE;
    if (Task->Type == TASK_TYPE_KERNEL_MAIN) return FALSE;

    KernelLogText(LOG_DEBUG, TEXT("[KillTask] Enter"));
    KernelLogText(LOG_DEBUG, TEXT("Process : %X"), Task->Process);
    KernelLogText(LOG_DEBUG, TEXT("Task : %X"), Task);
    KernelLogText(
        LOG_DEBUG, TEXT("Message : %X"), Task->Message->First ? ((LPMESSAGE)Task->Message->First)->Message : 0);

    // Lock access to kernel data
    LockMutex(MUTEX_KERNEL, INFINITY);
    FreezeScheduler();

    // Remove task from scheduler queue
    RemoveTaskFromQueue(Task);

    Task->References = 0;
    Task->Status = TASK_STATUS_DEAD;

    // Remove from global kernel task list BEFORE freeing
    ListRemove(Kernel.Task, Task);

    // Finally, free all resources owned by the task
    DeleteTask(Task);

    // Unlock access to kernel data
    UnfreezeScheduler();
    UnlockMutex(MUTEX_KERNEL);

    KernelLogText(LOG_DEBUG, TEXT("[KillTask] Exit"));

    return TRUE;
}

/***************************************************************************/

U32 SetTaskPriority(LPTASK Task, U32 Priority) {
    U32 OldPriority = 0;

    if (Task == NULL) return OldPriority;

    LockMutex(MUTEX_KERNEL, INFINITY);

    OldPriority = Task->Priority;
    Task->Priority = Priority;

    UpdateScheduler();

    UnlockMutex(MUTEX_KERNEL);

    return OldPriority;
}

/***************************************************************************/

void Sleep(U32 MilliSeconds) {
    LPTASK Task;

    FreezeScheduler();
    Task = GetCurrentTask();
    if (Task == NULL) { UnfreezeScheduler(); return; }

    Task->Status    = TASK_STATUS_SLEEPING;
    Task->WakeUpTime = GetSystemTime() + MilliSeconds;
    UnfreezeScheduler();

    // Block here until the timer IRQ moves us back to RUNNING
    while (Task->Status == TASK_STATUS_SLEEPING) {
        IdleCPU();
    }
}

/***************************************************************************/

U32 GetTaskStatus(LPTASK Task) {
    U32 Status = 0;

    if (Task == NULL) return Status;

    LockMutex(&(Task->Mutex), INFINITY);

    Status = Task->Status;

    UnlockMutex(&(Task->Mutex));

    return Status;
}

/***************************************************************************/

void SetTaskStatus(LPTASK Task, U32 Status) {
    UNUSED(Status);

    LockMutex(&(Task->Mutex), INFINITY);
    FreezeScheduler();

    Task->Status = TASK_STATUS_RUNNING;

    UnfreezeScheduler();
    UnlockMutex(&(Task->Mutex));
}

/***************************************************************************/

void AddTaskMessage(LPTASK Task, LPMESSAGE Message) {
    LockMutex(&(Task->Mutex), INFINITY);
    LockMutex(&(Task->MessageMutex), INFINITY);

    ListAddItem(Task->Message, Message);

    UnlockMutex(&(Task->MessageMutex));
    UnlockMutex(&(Task->Mutex));
}

/***************************************************************************/

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

/***************************************************************************/

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

/***************************************************************************/

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

/***************************************************************************/

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

/***************************************************************************/

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

/***************************************************************************/

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

/***************************************************************************/

void DumpTask(LPTASK Task) {
    LockMutex(&(Task->Mutex), INFINITY);

    KernelLogText(LOG_VERBOSE, TEXT("Address         : %X\n"), Task);
    KernelLogText(LOG_VERBOSE, TEXT("References      : %d\n"), Task->References);
    KernelLogText(LOG_VERBOSE, TEXT("Process         : %X\n"), Task->Process);
    KernelLogText(LOG_VERBOSE, TEXT("Status          : %X\n"), Task->Status);
    KernelLogText(LOG_VERBOSE, TEXT("Priority        : %X\n"), Task->Priority);
    KernelLogText(LOG_VERBOSE, TEXT("Function        : %X\n"), Task->Function);
    KernelLogText(LOG_VERBOSE, TEXT("Parameter       : %X\n"), Task->Parameter);
    KernelLogText(LOG_VERBOSE, TEXT("ReturnValue     : %X\n"), Task->ReturnValue);
    KernelLogText(LOG_VERBOSE, TEXT("StackBase       : %X\n"), Task->StackBase);
    KernelLogText(LOG_VERBOSE, TEXT("StackSize       : %X\n"), Task->StackSize);
    KernelLogText(LOG_VERBOSE, TEXT("SysStackBase    : %X\n"), Task->SysStackBase);
    KernelLogText(LOG_VERBOSE, TEXT("SysStackSize    : %X\n"), Task->SysStackSize);
    KernelLogText(LOG_VERBOSE, TEXT("Time            : %d\n"), Task->Time);
    KernelLogText(LOG_VERBOSE, TEXT("WakeUpTime      : %d\n"), Task->WakeUpTime);
    KernelLogText(LOG_VERBOSE, TEXT("Queued messages : %d\n"), Task->Message->NumItems);

    UnlockMutex(&(Task->Mutex));
}

/***************************************************************************/
