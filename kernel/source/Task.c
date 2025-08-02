
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "Address.h"
#include "Kernel.h"
#include "Process.h"

/***************************************************************************/


LIST KernelTaskMessageList = {NULL,           NULL,          NULL, 0,
                              KernelMemAlloc, KernelMemFree, NULL};

TASK KernelTask = {ID_TASK,
                   1,
                   NULL,
                   NULL,
                   EMPTY_MUTEX,
                   &KernelProcess,
                   TASK_STATUS_RUNNING,
                   TASK_PRIORITY_LOWER,
                   NULL,
                   NULL,
                   0,
                   0,
                   SELECTOR_TSS_0,
                   (LINEAR) KernelStack,
                   STK_SIZE,
                   (LINEAR) KernelStack,
                   STK_SIZE,
                   0,
                   0,
                   EMPTY_MUTEX,
                   &KernelTaskMessageList};

/***************************************************************************/

static LPMESSAGE NewMessage() {
    LPMESSAGE This;

    This = (LPMESSAGE)KernelMemAlloc(sizeof(MESSAGE));

    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(MESSAGE));

    This->ID = ID_MESSAGE;
    This->References = 1;

    return This;
}

/***************************************************************************/

void DeleteMessage(LPMESSAGE This) {
    if (This == NULL) return;

    This->ID = ID_NONE;

    KernelMemFree(This);
}

/***************************************************************************/

void MessageDestructor(LPVOID This) { DeleteMessage((LPMESSAGE)This); }

/***************************************************************************/

LPTASK NewTask() {
    LPTASK This = NULL;

#ifdef __DEBUG__
    KernelPrint("Entering NewTask\n");
#endif

    This = (LPTASK)KernelMemAlloc(sizeof(TASK));

    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(TASK));

    This->ID = ID_TASK;
    This->References = 1;
    This->Next = NULL;
    This->Prev = NULL;
    This->Process = NULL;
    This->Status = 0;
    This->Priority = 0;
    This->Function = NULL;
    This->Parameter = 0;
    This->ReturnValue = 0;
    This->Selector = 0;
    This->StackBase = NULL;
    This->StackSize = 0;
    This->SysStackBase = NULL;
    This->SysStackSize = 0;
    This->Time = 0;
    This->WakeUpTime = 0;

    InitMutex(&(This->Mutex));
    InitMutex(&(This->MessageMutex));

    //-------------------------------------
    // Initialize the message queue

    This->Message = NewList(MessageDestructor, KernelMemAlloc, KernelMemFree);

#ifdef __DEBUG__
    KernelPrint("Exiting NewTask\n");
#endif

    return This;
}

/***************************************************************************/

void DeleteTask(LPTASK This) {
    LPLISTNODE Node = NULL;
    LPMUTEX Mutex = NULL;

#ifdef __DEBUG__
    KernelPrint("Entering DeleteTask\n");
#endif

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

#ifdef __DEBUG__
    KernelPrint("Deleting message queue...\n");
#endif

    if (This->Message != NULL) DeleteList(This->Message);

        //-------------------------------------
        // Delete the task's stacks

#ifdef __DEBUG__
    KernelPrint("Deleting stacks...\n");
#endif

    if (This->SysStackBase != NULL) {
        HeapFree_HBHS(KernelProcess.HeapBase, KernelProcess.HeapSize,
                      (LPVOID)This->SysStackBase);
    }

    if (This->Process != NULL) {
        if (This->StackBase != NULL) {
            HeapFree_HBHS(This->Process->HeapBase, This->Process->HeapSize,
                          (LPVOID)This->StackBase);
        }
    }

    //-------------------------------------
    // Free the task structure itself

    KernelMemFree(This);

#ifdef __DEBUG__
    KernelPrint("Exiting DeleteTask\n");
#endif
}

/***************************************************************************/

BOOL InitKernelTask() {
    LINEAR StackPointer = NULL;

    StackPointer = KernelTask.StackBase + KernelTask.StackSize;

    //-------------------------------------
    // Setup the TSS

    MemorySet(TSS, 0, sizeof(TASKSTATESEGMENT));

    TSS[0].SS0 = SELECTOR_KERNEL_DATA;
    TSS[0].ESP0 = StackPointer - (0 * TASK_SYSTEM_STACK_SIZE);
    TSS[0].SS1 = SELECTOR_KERNEL_DATA;
    TSS[0].ESP1 = StackPointer - (1 * TASK_SYSTEM_STACK_SIZE);
    TSS[0].SS2 = SELECTOR_KERNEL_DATA;
    TSS[0].ESP2 = StackPointer - (2 * TASK_SYSTEM_STACK_SIZE);
    TSS[0].CR3 = PA_PGD;
    TSS[0].EIP = (U32)TaskRunner;
    TSS[0].EFlags = EFLAGS_A1 | EFLAGS_IF;
    TSS[0].EAX = 0;
    TSS[0].EBX = 0;
    TSS[0].ESP = StackPointer - (3 * TASK_SYSTEM_STACK_SIZE);
    TSS[0].EBP = StackPointer - (3 * TASK_SYSTEM_STACK_SIZE);
    TSS[0].CS = SELECTOR_KERNEL_CODE;
    TSS[0].DS = SELECTOR_KERNEL_DATA;
    TSS[0].SS = SELECTOR_KERNEL_DATA;
    TSS[0].ES = SELECTOR_KERNEL_DATA;
    TSS[0].FS = SELECTOR_KERNEL_DATA;
    TSS[0].GS = SELECTOR_KERNEL_DATA;
    TSS[0].IOMap = MEMBER_OFFSET(TASKSTATESEGMENT, IOMapBits[0]);

    //-------------------------------------
    // Setup the TSS descriptor

    TTD[0].TSS.Type = GATE_TYPE_386_TSS_AVAIL;
    TTD[0].TSS.Privilege = PRIVILEGE_KERNEL;
    TTD[0].TSS.Present = 0x01;
    TTD[0].TSS.Granularity = GDT_GRANULAR_1B;

    SetTSSDescriptorBase(&(TTD[0].TSS), (U32)(TSS + 0));
    SetTSSDescriptorLimit(&(TTD[0].TSS), sizeof(TASKSTATESEGMENT) - 1);

    //-------------------------------------

    UpdateScheduler();

    return TRUE;
}

/***************************************************************************/

LPTASK CreateTask(LPPROCESS Process, LPTASKINFO Info) {
    LPTASK Task = NULL;
    LINEAR StackPointer = NULL;
    LINEAR SysStackPointer = NULL;
    SELECTOR CodeSelector = 0;
    SELECTOR DataSelector = 0;
    U32 Table = MAX_U32;
    U32 Index = 0;

#ifdef __DEBUG__
    KernelPrint("Entering CreateTask\n");
#endif

    //-------------------------------------
    // Check parameters

    if (Info->Func == NULL) return NULL;

    if (Info->StackSize < TASK_MINIMUM_STACK_SIZE) {
        Info->StackSize = TASK_MINIMUM_STACK_SIZE;
    }

    if (Info->Priority > TASK_PRIORITY_CRITICAL) {
        Info->Priority = TASK_PRIORITY_CRITICAL;
    }

    //-------------------------------------
    // Lock access to kernel data

    LockMutex(MUTEX_KERNEL, INFINITY);

    //-------------------------------------
    // Find a free task

    for (Index = 0; Index < NUM_TASKS; Index++) {
        if (TTD[Index].TSS.Type == 0) {
            Table = Index;
            break;
        }
    }

    if (Table == MAX_U32) goto Out;

    Task = NewTask();

    if (Task == NULL) goto Out;

    //-------------------------------------
    // Setup the task

    Task->Process = Process;
    Task->Priority = Info->Priority;
    Task->Function = Info->Func;
    Task->Parameter = Info->Parameter;
    Task->Table = Table;

    Task->Selector = ((GDT_NUM_BASE_DESCRIPTORS * DESCRIPTOR_SIZE) +
                      (Table * GDT_TASK_DESCRIPTORS_SIZE)) |
                     SELECTOR_GLOBAL | Process->Privilege;

    //-------------------------------------
    // Allocate the system stack

    Task->StackSize = Info->StackSize;
    Task->StackBase = (LINEAR)HeapAlloc_HBHS(
        Process->HeapBase, Process->HeapSize, Task->StackSize);

    //-------------------------------------
    // Allocate the task stack

    Task->SysStackSize = TASK_SYSTEM_STACK_SIZE * 4;
    Task->SysStackBase = (LINEAR)HeapAlloc_HBHS(
        KernelProcess.HeapBase, KernelProcess.HeapSize, Task->SysStackSize);

    if (Task->StackBase == NULL || Task->SysStackBase == NULL) {
        if (Task->StackBase != NULL) {
            HeapFree_HBHS(Process->HeapBase, Process->HeapSize,
                          (LPVOID)Task->StackBase);
        }

        if (Task->SysStackBase != NULL) {
            HeapFree_HBHS(KernelProcess.HeapBase, KernelProcess.HeapSize,
                          (LPVOID)Task->SysStackBase);
        }

        DeleteTask(Task);
        Task = NULL;

        goto Out;
    }

    //-------------------------------------
    // Setup privilege data

    if (Process->Privilege == PRIVILEGE_KERNEL) {
        CodeSelector = SELECTOR_KERNEL_CODE;
        DataSelector = SELECTOR_KERNEL_DATA;
    } else {
        CodeSelector = SELECTOR_USER_CODE;
        DataSelector = SELECTOR_USER_DATA;
    }

    //-------------------------------------
    // Setup the TSS

    MemorySet(TSS + Table, 0, sizeof(TASKSTATESEGMENT));

    StackPointer = Task->StackBase + Task->StackSize;
    SysStackPointer = Task->SysStackBase + Task->SysStackSize;

    TSS[Table].SS0 = SELECTOR_KERNEL_DATA;
    TSS[Table].ESP0 = SysStackPointer - (0 * TASK_SYSTEM_STACK_SIZE);
    TSS[Table].SS1 = SELECTOR_KERNEL_DATA;
    TSS[Table].ESP1 = SysStackPointer - (1 * TASK_SYSTEM_STACK_SIZE);
    TSS[Table].SS2 = SELECTOR_KERNEL_DATA;
    TSS[Table].ESP2 = SysStackPointer - (2 * TASK_SYSTEM_STACK_SIZE);
    TSS[Table].CR3 = Process->PageDirectory;
    TSS[Table].EIP = (U32)TaskRunner;
    TSS[Table].EFlags = EFLAGS_A1 | EFLAGS_IF;
    TSS[Table].EAX = (U32)Task->Parameter;
    TSS[Table].EBX = (U32)Task->Function;
    TSS[Table].ESP = StackPointer;
    TSS[Table].EBP = StackPointer;
    TSS[Table].CS = CodeSelector;
    TSS[Table].DS = DataSelector;
    TSS[Table].SS = DataSelector;
    TSS[Table].ES = DataSelector;
    TSS[Table].FS = DataSelector;
    TSS[Table].GS = DataSelector;
    TSS[Table].IOMap = MEMBER_OFFSET(TASKSTATESEGMENT, IOMapBits[0]);

    //-------------------------------------
    // Setup the TSS descriptor

    TTD[Table].TSS.Type = GATE_TYPE_386_TSS_AVAIL;
    TTD[Table].TSS.Privilege = Process->Privilege;
    TTD[Table].TSS.Present = 0x01;
    TTD[Table].TSS.Granularity = GDT_GRANULAR_1B;

    SetTSSDescriptorBase(&(TTD[Table].TSS), (U32)(TSS + Table));
    SetTSSDescriptorLimit(&(TTD[Table].TSS), sizeof(TASKSTATESEGMENT) - 1);

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

    UnlockMutex(MUTEX_KERNEL);

#ifdef __DEBUG__
    KernelPrint("Exiting CreateTask\n");
#endif

    return Task;
}

/***************************************************************************/

BOOL KillTask(LPTASK Task) {
    if (Task == NULL) return FALSE;
    if (Task == &KernelTask) return FALSE;

#ifdef __DEBUG__
    KernelPrint("Entering KillTask\n");
#endif

    //-------------------------------------
    // Lock access to kernel data

    LockMutex(MUTEX_KERNEL, INFINITY);
    FreezeScheduler();

    //-------------------------------------
    // Remove task from scheduler queue

    RemoveTaskFromQueue(Task);

    Task->References--;
    Task->Status = TASK_STATUS_DEAD;

    //-------------------------------------
    // Free all resources owned by the task

    DeleteTask(Task);

    ListRemove(Kernel.Task, Task);

    //-------------------------------------
    // Free the associated Task State Segment
    // by setting it's type to 0

    TTD[Task->Table].TSS.Type = 0;

Out:

    //-------------------------------------
    // Unlock access to kernel data

    UnfreezeScheduler();
    UnlockMutex(MUTEX_KERNEL);

#ifdef __DEBUG__
    KernelPrint("Exiting KillTask\n");
#endif

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

Out:

    UnlockMutex(MUTEX_KERNEL);

    return OldPriority;
}

/***************************************************************************/

void Sleep(U32 MilliSeconds) {
    LPTASK Task;

    FreezeScheduler();
    Task = GetCurrentTask();
    if (Task == NULL) return;
    Task->Status = TASK_STATUS_SLEEPING;
    Task->WakeUpTime = GetSystemTime() + MilliSeconds;
    UnfreezeScheduler();

    while (Task->Status == TASK_STATUS_SLEEPING) {
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
    LPPROCESS Process;
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

            Window->Function(Message->Target, Message->Message, Message->Param1,
                             Message->Param2);

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
    LPLISTNODE Node = NULL;
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
    STR Temp[32];

    LockMutex(&(Task->Mutex), INFINITY);

    KernelPrint("Address         : %08X\n", Task);
    KernelPrint("References      : %d\n", Task->References);
    KernelPrint("Process         : %08X\n", Task->Process);
    KernelPrint("Status          : %X\n", Task->Status);
    KernelPrint("Priority        : %X\n", Task->Priority);
    KernelPrint("Function        : %08X\n", Task->Function);
    KernelPrint("Parameter       : %08X\n", Task->Parameter);
    KernelPrint("ReturnValue     : %08X\n", Task->ReturnValue);
    KernelPrint("Selector        : %04X\n", Task->Selector);
    KernelPrint("StackBase       : %08X\n", Task->StackBase);
    KernelPrint("StackSize       : %08X\n", Task->StackSize);
    KernelPrint("SysStackBase    : %08X\n", Task->SysStackBase);
    KernelPrint("SysStackSize    : %08X\n", Task->SysStackSize);
    KernelPrint("Time            : %d\n", Task->Time);
    KernelPrint("WakeUpTime      : %d\n", Task->WakeUpTime);
    KernelPrint("Queued messages : %d\n", Task->Message->NumItems);

    UnlockMutex(&(Task->Mutex));
}

/***************************************************************************/
