
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\************************************************************************/

#include "../include/Address.h"
#include "../include/Clock.h"
#include "../include/I386.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Process.h"

/************************************************************************\

    GDTR (6 bytes)
    ┌───────────────────────────────────────────────────────────────┐
    │ base = LA_GDT = 0xFF401000   •   limit ≈ 0x1FFF (8 KiB - 1)   │
    └───────────────────────────────────────────────────────────────┘
                    │
                    ▼
    GDT @ LA_GDT = 0xFF401000   (each entry = 8 bytes)
    ┌──────┬───────────┬──────────────┬─────────────────────────────┐
    │Idx   │ Selector  │ VA (LA_GDT + │ Role / Notes                │
    │      │ (hex)     │   idx*8)     │                             │
    ├──────┼───────────┼──────────────┼─────────────────────────────┤
    │0     │ 0x00      │ +0x00        │ NULL                        │
    │1     │ 0x08      │ +0x08        │ Kernel Code                 │
    │2     │ 0x10      │ +0x10        │ Kernel Data                 │
    │3     │ 0x18      │ +0x18        │ User   Code                 │
    │4     │ 0x20      │ +0x20        │ User   Data                 │
    │5     │ 0x28      │ +0x28        │ (free / system)             │
    │6     │ 0x30      │ +0x30        │ (free / system)             │
    │7     │ 0x38      │ +0x38        │ (free / system)             │
    │------┴───────────┴──────────────┴─────────────────────────────│
    │  Per-task region starts at:                                   │
    │    LA_GDT_TASK = LA_GDT + GDT_NUM_BASE_DESCRIPTORS*8          │
    │               = 0xFF401000 + 8*8 = 0xFF401040                 │
    ├──────┬───────────┬──────────────┬─────────────────────────────┤
    │8     │ 0x40      │ +0x40        │ Task 0 • TSS descriptor     │
    │9     │ 0x48      │ +0x48        │ Task 0 • LDT descriptor (*) │
    │10    │ 0x50      │ +0x50        │ Task 1 • TSS descriptor     │
    │11    │ 0x58      │ +0x58        │ Task 1 • LDT descriptor (*) │
    │12    │ 0x60      │ +0x60        │ Task 2 • TSS descriptor     │
    │13    │ 0x68      │ +0x68        │ Task 2 • LDT descriptor (*) │
    │…     │ …         │ …            │ …                           │
    └──────┴───────────┴──────────────┴─────────────────────────────┘
    (*) LDT slot reserved (P=0).
    Per-task slot size in the GDT = 2 entries = 16 bytes (TSS then LDT).

    TTD (per-task descriptor array in C)
    LA_GDT_TASK = 0xFF401040  →  points inside the GDT
    ┌───────────────────────────────────────────────────────────────┐
    │ TTD[0] @ LA_GDT_TASK + 0x00 : { TSS desc, LDT desc }          │
    │ TTD[1] @ LA_GDT_TASK + 0x10 : { TSS desc, LDT desc }          │
    │ TTD[2] @ LA_GDT_TASK + 0x20 : { TSS desc, LDT desc }          │
    │ …                                                             │
    └───────────────────────────────────────────────────────────────┘
    (Each TTD slot = sizeof(TASKTSSDESCRIPTOR) = 16 bytes = 2 GDT entries.)

    TSS memory (system area)
    LA_SYSTEM = 0xFF400000
    ┌───────────────────────────────────────────────────────────────┐
    │ TSS[0] @ LA_SYSTEM + 0x8000 = 0xFF408000  (256 bytes)         │
    │ TSS[1] @ LA_SYSTEM + 0x8100 = 0xFF408100  (256 bytes)         │
    │ TSS[2] @ LA_SYSTEM + 0x8200 = 0xFF408200  (256 bytes)         │
    │ …                                                             │
    └───────────────────────────────────────────────────────────────┘
    (TSS[i] = 0xFF408000 + i*0x100 ; limit = 0xFF (byte granularity).)

\************************************************************************/

/************************************************************************/

LIST KernelTaskMessageList = {
    .First = NULL,
    .Last = NULL,
    .Current = NULL,
    .NumItems = 0,
    .MemAllocFunc = KernelMemAlloc,
    .MemFreeFunc = KernelMemFree,
    .Destructor = NULL};

TASK KernelTask = {
    .ID = ID_TASK,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Mutex = EMPTY_MUTEX,
    .Process = &KernelProcess,
    .Status = TASK_STATUS_RUNNING,
    .Priority = TASK_PRIORITY_LOWER,
    .Function = NULL,
    .Parameter = NULL,
    .ReturnValue = 0,
    .Table = 0,
    .Selector = 0,
    .StackBase = 0,
    .StackSize = 0,
    .SysStackBase = 0,
    .SysStackSize = 0,
    .Time = 0,
    .WakeUpTime = 0,
    .MessageMutex = EMPTY_MUTEX,
    .Message = &KernelTaskMessageList};

/************************************************************************/

static LPMESSAGE NewMessage(void) {
    LPMESSAGE This;

    This = (LPMESSAGE)KernelMemAlloc(sizeof(MESSAGE));

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

    KernelMemFree(This);
}

/************************************************************************/

void MessageDestructor(LPVOID This) { DeleteMessage((LPMESSAGE)This); }

/************************************************************************/

LPTASK NewTask(void) {
    LPTASK This = NULL;

    KernelLogText(LOG_DEBUG, TEXT("[NewTask] Enter"));

    This = (LPTASK)KernelMemAlloc(sizeof(TASK));

    if (This == NULL) {
        KernelLogText(LOG_ERROR, TEXT("[NewTask] Could not allocate memory for task"));
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
    KernelLogText(LOG_DEBUG, TEXT("[NewTask] KernelMemAlloc = %X"), (LINEAR)KernelMemAlloc);
    KernelLogText(LOG_DEBUG, TEXT("[NewTask] KernelMemFree = %X"), (LINEAR)KernelMemFree);
    KernelLogText(LOG_DEBUG, TEXT("[NewTask] EBP = %X"), (LINEAR)GetEBP());

    This->Message = NewList(MessageDestructor, KernelMemAlloc, KernelMemFree);

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

    KernelMemFree(This);

    KernelLogText(LOG_DEBUG, TEXT("[DeleteTask] Exit"));
}

/***************************************************************************/

BOOL InitKernelTask(void) {
    LINEAR StackPointer = NULL;

    KernelLogText(LOG_VERBOSE, TEXT("[InitKernelTask]"));

    KernelTask.StackBase = (LINEAR)LA_KERNEL_STACK + N_4KB;
    KernelTask.StackSize = STK_SIZE - N_4KB;
    KernelTask.SysStackBase = (LINEAR)LA_KERNEL_STACK;
    KernelTask.SysStackSize = N_4KB;

    StackPointer = KernelTask.StackBase + KernelTask.StackSize;

    KernelTask.Selector =
    ( (GDT_NUM_BASE_DESCRIPTORS + (KernelTask.Table * GDT_TASK_DESCRIPTOR_ENTRIES)) << 3 )
    | SELECTOR_GLOBAL
    | PRIVILEGE_KERNEL;

    //-------------------------------------
    // Setup the TSS

    KernelLogText(LOG_VERBOSE, TEXT("[InitKernelTask] Clearing TSS"));

    MemorySet(Kernel_i386.TSS, 0, sizeof(TASKSTATESEGMENT));

    Kernel_i386.TSS[0].SS0 = SELECTOR_KERNEL_DATA;
    Kernel_i386.TSS[0].ESP0 = StackPointer - (0 * TASK_SYSTEM_STACK_SIZE);
    Kernel_i386.TSS[0].SS1 = SELECTOR_KERNEL_DATA;
    Kernel_i386.TSS[0].ESP1 = StackPointer - (1 * TASK_SYSTEM_STACK_SIZE);
    Kernel_i386.TSS[0].SS2 = SELECTOR_KERNEL_DATA;
    Kernel_i386.TSS[0].ESP2 = StackPointer - (2 * TASK_SYSTEM_STACK_SIZE);
    Kernel_i386.TSS[0].CR3 = KernelStartup.SI_Phys_PGD;
    Kernel_i386.TSS[0].EIP = (U32)TaskRunner;
    Kernel_i386.TSS[0].EFlags = EFLAGS_A1;
    Kernel_i386.TSS[0].EAX = 0;
    Kernel_i386.TSS[0].EBX = 0;
    Kernel_i386.TSS[0].ESP = StackPointer - (3 * TASK_SYSTEM_STACK_SIZE);
    Kernel_i386.TSS[0].EBP = StackPointer - (3 * TASK_SYSTEM_STACK_SIZE);
    Kernel_i386.TSS[0].CS = SELECTOR_KERNEL_CODE;
    Kernel_i386.TSS[0].DS = SELECTOR_KERNEL_DATA;
    Kernel_i386.TSS[0].SS = SELECTOR_KERNEL_DATA;
    Kernel_i386.TSS[0].ES = SELECTOR_KERNEL_DATA;
    Kernel_i386.TSS[0].FS = SELECTOR_KERNEL_DATA;
    Kernel_i386.TSS[0].GS = SELECTOR_KERNEL_DATA;
    Kernel_i386.TSS[0].IOMap = MEMBER_OFFSET(TASKSTATESEGMENT, IOMapBits[0]);

    //-------------------------------------
    // Setup the TSS descriptor

    Kernel_i386.TTD[0].TSS.Type = GATE_TYPE_386_TSS_AVAIL;
    Kernel_i386.TTD[0].TSS.Privilege = PRIVILEGE_KERNEL;
    Kernel_i386.TTD[0].TSS.Present = 0x01;
    Kernel_i386.TTD[0].TSS.Granularity = GDT_GRANULAR_1B;

    SetTSSDescriptorBase(&(Kernel_i386.TTD[0].TSS), (U32)(Kernel_i386.TSS + 0));
    SetTSSDescriptorLimit(&(Kernel_i386.TTD[0].TSS), sizeof(TASKSTATESEGMENT) - 1);

    //-------------------------------------

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

    if (IsValidMemory((LINEAR) Info->Func) == FALSE) {
        KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Function is not in mapped memory. Aborting."), Info->Func);
        return NULL;
    }

    //-------------------------------------
    // Lock access to kernel data

    LockMutex(MUTEX_KERNEL, INFINITY);

    //-------------------------------------
    // Find a free task

    for (Index = 0; Index < NUM_TASKS; Index++) {
        if (Kernel_i386.TTD[Index].TSS.Type == 0) {
            Table = Index;
            break;
        }
    }

    if (Table == MAX_U32) goto Out;

    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Task table index = %X"), Table);

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
    Task->Table = Table;

    /*
    U16 TaskIndex = GDT_NUM_BASE_DESCRIPTORS + Table * GDT_TASK_DESCRIPTOR_ENTRIES;
    Task->Selector = (TaskIndex << 3) | SELECTOR_GLOBAL | Process->Privilege;
    */

    Task->Selector =
    (
        (GDT_NUM_BASE_DESCRIPTORS * DESCRIPTOR_SIZE) +
        (Table * GDT_TASK_DESCRIPTORS_SIZE)
    ) | SELECTOR_GLOBAL | Process->Privilege;

    //-------------------------------------
    // Allocate the system stack

    Task->StackSize = Info->StackSize;
    Task->StackBase = (LINEAR)HeapAlloc_HBHS(Process->HeapBase, Process->HeapSize, Task->StackSize);

    //-------------------------------------
    // Allocate the task stack

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
    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] System stack (%X bytes) allocated at %X"), Task->SysStackSize, Task->SysStackBase);

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

    //-------------------------------------
    // Setup the TSS

    MemorySet(Kernel_i386.TSS + Table, 0, sizeof(TASKSTATESEGMENT));

    StackPointer = Task->StackBase + Task->StackSize;
    SysStackPointer = Task->SysStackBase + Task->SysStackSize;

    Kernel_i386.TSS[Table].SS0 = SELECTOR_KERNEL_DATA;
    Kernel_i386.TSS[Table].ESP0 = SysStackPointer - (0 * TASK_SYSTEM_STACK_SIZE);
    Kernel_i386.TSS[Table].SS1 = SELECTOR_KERNEL_DATA;
    Kernel_i386.TSS[Table].ESP1 = SysStackPointer - (1 * TASK_SYSTEM_STACK_SIZE);
    Kernel_i386.TSS[Table].SS2 = SELECTOR_KERNEL_DATA;
    Kernel_i386.TSS[Table].ESP2 = SysStackPointer - (2 * TASK_SYSTEM_STACK_SIZE);
    Kernel_i386.TSS[Table].CR3 = Process->PageDirectory;
    Kernel_i386.TSS[Table].EIP = (U32)TaskRunner;
    Kernel_i386.TSS[Table].EFlags = EFLAGS_A1;
    Kernel_i386.TSS[Table].EAX = (U32)Task->Parameter;
    Kernel_i386.TSS[Table].EBX = (U32)Task->Function;
    Kernel_i386.TSS[Table].ESP = StackPointer;
    Kernel_i386.TSS[Table].EBP = StackPointer;
    Kernel_i386.TSS[Table].CS = CodeSelector;
    Kernel_i386.TSS[Table].DS = DataSelector;
    Kernel_i386.TSS[Table].SS = DataSelector;
    Kernel_i386.TSS[Table].ES = DataSelector;
    Kernel_i386.TSS[Table].FS = DataSelector;
    Kernel_i386.TSS[Table].GS = DataSelector;
    Kernel_i386.TSS[Table].IOMap = MEMBER_OFFSET(TASKSTATESEGMENT, IOMapBits[0]);

    //-------------------------------------
    // Setup the TSS descriptor

    Kernel_i386.TTD[Table].TSS.Type = GATE_TYPE_386_TSS_AVAIL;
    Kernel_i386.TTD[Table].TSS.Privilege = Process->Privilege;
    Kernel_i386.TTD[Table].TSS.Present = 0x01;
    Kernel_i386.TTD[Table].TSS.Granularity = GDT_GRANULAR_1B;

    SetTSSDescriptorBase(&(Kernel_i386.TTD[Table].TSS), (U32)(Kernel_i386.TSS + Table));
    SetTSSDescriptorLimit(&(Kernel_i386.TTD[Table].TSS), sizeof(TASKSTATESEGMENT) - 1);

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

    KernelLogText(LOG_DEBUG, TEXT("[CreateTask] Exit"));

    return Task;
}

/***************************************************************************/

BOOL KillTask(LPTASK Task) {
    if (Task == NULL) return FALSE;
    if (Task == &KernelTask) return FALSE;

    KernelLogText(LOG_DEBUG, TEXT("[KillTask] Enter"));
    KernelLogText(LOG_DEBUG, TEXT("Process : %X"), Task->Process);
    KernelLogText(LOG_DEBUG, TEXT("Task : %X"), Task);
    KernelLogText(
        LOG_DEBUG, TEXT("Message : %X"), Task->Message->First ? ((LPMESSAGE)Task->Message->First)->Message : 0);

    // Lock access to kernel data
    LockMutex(MUTEX_KERNEL, INFINITY);
    FreezeScheduler();

    U32 table = Task->Table;

    // Remove task from scheduler queue
    RemoveTaskFromQueue(Task);

    Task->References = 0;
    Task->Status = TASK_STATUS_DEAD;

    // Remove from global kernel task list BEFORE freeing
    ListRemove(Kernel.Task, Task);

    // Free the associated Task State Segment by setting its type to 0
    Kernel_i386.TTD[table].TSS.Type = 0;

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
    if (Task == NULL) return;
    Task->Status = TASK_STATUS_SLEEPING;
    Task->WakeUpTime = GetSystemTime() + MilliSeconds;
    UnfreezeScheduler();

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

    KernelLogText(LOG_VERBOSE, TEXT("Address         : %08X\n"), Task);
    KernelLogText(LOG_VERBOSE, TEXT("References      : %d\n"), Task->References);
    KernelLogText(LOG_VERBOSE, TEXT("Process         : %08X\n"), Task->Process);
    KernelLogText(LOG_VERBOSE, TEXT("Status          : %X\n"), Task->Status);
    KernelLogText(LOG_VERBOSE, TEXT("Priority        : %X\n"), Task->Priority);
    KernelLogText(LOG_VERBOSE, TEXT("Function        : %08X\n"), Task->Function);
    KernelLogText(LOG_VERBOSE, TEXT("Parameter       : %08X\n"), Task->Parameter);
    KernelLogText(LOG_VERBOSE, TEXT("ReturnValue     : %08X\n"), Task->ReturnValue);
    KernelLogText(LOG_VERBOSE, TEXT("Selector        : %04X\n"), Task->Selector);
    KernelLogText(LOG_VERBOSE, TEXT("StackBase       : %08X\n"), Task->StackBase);
    KernelLogText(LOG_VERBOSE, TEXT("StackSize       : %08X\n"), Task->StackSize);
    KernelLogText(LOG_VERBOSE, TEXT("SysStackBase    : %08X\n"), Task->SysStackBase);
    KernelLogText(LOG_VERBOSE, TEXT("SysStackSize    : %08X\n"), Task->SysStackSize);
    KernelLogText(LOG_VERBOSE, TEXT("Time            : %d\n"), Task->Time);
    KernelLogText(LOG_VERBOSE, TEXT("WakeUpTime      : %d\n"), Task->WakeUpTime);
    KernelLogText(LOG_VERBOSE, TEXT("Queued messages : %d\n"), Task->Message->NumItems);

    UnlockMutex(&(Task->Mutex));
}

/***************************************************************************/
