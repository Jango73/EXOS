
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

#include "Clock.h"
#include "Console.h"
#include "Arch.h"
#include "Kernel.h"
#include "Log.h"
#include "package/PackageFS.h"
#include "package/PackageNamespace.h"
#include "process/Process.h"
#include "process/Schedule.h"
#include "process/Task-Messaging.h"
#include "CoreString.h"
#include "utils/Helpers.h"

/************************************************************************/

static UINT DATA_SECTION TaskMinimumTaskStackSize = TASK_MINIMUM_TASK_STACK_SIZE_DEFAULT;
static UINT DATA_SECTION TaskMinimumSystemStackSize = TASK_MINIMUM_SYSTEM_STACK_SIZE_DEFAULT;
static BOOL DATA_SECTION TaskStackConfigInitialized = FALSE;

/************************************************************************/

static void TaskInitializeStackConfig(void) {
    if (TaskStackConfigInitialized) {
        return;
    }

    TaskStackConfigInitialized = TRUE;

    LPCSTR configValue = GetConfigurationValue(TEXT(CONFIG_TASK_MINIMUM_TASK_STACK_SIZE));

    if (STRING_EMPTY(configValue) == FALSE) {
        UINT parsedValue = StringToU32(configValue);

        if (parsedValue >= TASK_MINIMUM_TASK_STACK_SIZE_DEFAULT) {
            TaskMinimumTaskStackSize = parsedValue;
        } else {
            WARNING(TEXT("[TaskInitializeStackConfig] MinimumTaskStackSize='%s' resolves to %u which is below minimum %u, using default"),
                    configValue, parsedValue, TASK_MINIMUM_TASK_STACK_SIZE_DEFAULT);
        }
    }

    configValue = GetConfigurationValue(TEXT(CONFIG_TASK_MINIMUM_SYSTEM_STACK_SIZE));

    if (STRING_EMPTY(configValue) == FALSE) {
        UINT parsedValue = StringToU32(configValue);

        if (parsedValue >= TASK_MINIMUM_SYSTEM_STACK_SIZE_DEFAULT) {
            TaskMinimumSystemStackSize = parsedValue;
        } else {
            WARNING(TEXT("[TaskInitializeStackConfig] MinimumSystemStackSize='%s' resolves to %u which is below minimum %u, using default"),
                    configValue, parsedValue, TASK_MINIMUM_SYSTEM_STACK_SIZE_DEFAULT);
        }
    }
}

/************************************************************************/

UINT TaskGetMinimumTaskStackSize(void) {
    TaskInitializeStackConfig();

    return TaskMinimumTaskStackSize;
}

/************************************************************************/

UINT TaskGetMinimumSystemStackSize(void) {
    TaskInitializeStackConfig();

    return TaskMinimumSystemStackSize;
}

/************************************************************************/

/**
 * @brief Allocates and initializes a new task structure.
 *
 * Creates a new task object with default values, initializes mutexes,
 * and sets up the message queue. The task ID is set to KOID_TASK for validation.
 * Memory is validated before use to detect corruption.
 *
 * @return Pointer to newly allocated task, or NULL on allocation failure
 */
LPTASK NewTask(void) {
    TRACED_FUNCTION;

    LPTASK This = NULL;


    This = (LPTASK)CreateKernelObject(sizeof(TASK), KOID_TASK);

    if (This == NULL) {
        ERROR(TEXT("[NewTask] Could not allocate memory for task"));

        TRACED_EPILOGUE("NewTask");
        return NULL;
    }

    if (IsValidMemory((LINEAR)This) == FALSE) {
        ERROR(TEXT("[NewTask] Allocated task is not a valid pointer"));

        TRACED_EPILOGUE("NewTask");
        return NULL;
    }


    // Initialize task-specific fields (LISTNODE_FIELDS already initialized by CreateKernelObject)
    InitMutex(&(This->Mutex));
    This->Type = TASK_TYPE_NONE;
    This->Status = TASK_STATUS_READY;
    MemorySet(&(This->MessageQueue), 0, sizeof(MESSAGEQUEUE));


    //-------------------------------------
    // Initialize the message queue


    TRACED_EPILOGUE("NewTask");
    return This;
}

/**
 * @brief Releases all mutexes owned by the specified task.
 *
 * Iterates through the global kernel mutex list and clears any mutex that is
 * currently owned by the provided task. The function expects MUTEX_KERNEL to
 * be locked by the caller to guarantee list consistency.
 *
 * @param Task Pointer to the task whose mutexes should be released.
 */
static void ReleaseTaskMutexes(LPTASK Task) {
    LPLISTNODE Node = NULL;
    LPMUTEX Mutex = NULL;

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        LPLIST MutexList = GetMutexList();
        for (Node = MutexList->First; Node; Node = Node->Next) {
            Mutex = (LPMUTEX)Node;

            if (Mutex->TypeID == KOID_MUTEX && Mutex->Task == Task) {
                Mutex->Process = NULL;
                Mutex->Task = NULL;
                Mutex->Lock = 0;
            }
        }
    }
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


    //-------------------------------------
    // Check validity of parameters

    SAFE_USE_VALID_ID(This, KOID_TASK) {
        // Lock kernel mutex for the entire operation
        SAFE_USE(This->Process) {
        }

        LockMutex(MUTEX_KERNEL, INFINITY);

        //-------------------------------------
        // Unlock all mutexs locked by this task

        ReleaseTaskMutexes(This);

        //-------------------------------------
        // Delete the task's message queue


        DeleteMessageQueue(&(This->MessageQueue));

        //-------------------------------------
        // Delete the task's stacks


        SAFE_USE(This->Arch.SystemStack.Base) {
            FreeRegion(This->Arch.SystemStack.Base, This->Arch.SystemStack.Size);
        }

#if defined(__EXOS_ARCH_X86_64__)
        SAFE_USE(This->Arch.Ist1Stack.Base) {
            FreeRegion(This->Arch.Ist1Stack.Base, This->Arch.Ist1Stack.Size);
        }
#endif

        SAFE_USE(This->Process) {
            SAFE_USE(This->Arch.Stack.Base) {
                FreeRegion(This->Arch.Stack.Base, This->Arch.Stack.Size);
            }
        }

        //-------------------------------------
        // Decrement process task count and check if process should be deleted

        LPLIST ProcessList = GetProcessList();
        LPLIST TaskList = GetTaskList();

        if (This->Process != NULL && This->Process != &KernelProcess) {
            LockMutex(MUTEX_PROCESS, INFINITY);
            This->Process->TaskCount--;


            if (This->Process->TaskCount == 0) {

                // Set process exit code to last task's exit code
                This->Process->ExitCode = This->ExitCode;

                SetProcessStatus(This->Process, PROCESS_STATUS_DEAD);

                // Apply child process policy
                if (This->Process->Flags & PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH) {

                    // Find and kill all child processes
                    LPPROCESS Current = (LPPROCESS)ProcessList->First;

                    while (Current != NULL) {
                        LPPROCESS Next = (LPPROCESS)Current->Next;

                        SAFE_USE_VALID_ID(Current, KOID_PROCESS) {
                            if (Current->OwnerProcess == This->Process) {

                                // Kill all tasks of the child process
                                LPTASK ChildTask = (LPTASK)TaskList->First;

                                while (ChildTask != NULL) {
                                    LPTASK NextChildTask = (LPTASK)ChildTask->Next;

                                    SAFE_USE_VALID_ID(ChildTask, KOID_TASK) {
                                        if (ChildTask->Process == Current) {
                                            KillTask(ChildTask);
                                        }
                                    }
                                    ChildTask = NextChildTask;
                                }

                                // Mark child process as DEAD
                                SetProcessStatus(Current, PROCESS_STATUS_DEAD);
                            }
                        }
                        Current = Next;
                    }
                } else {

                    // Detach all child processes from parent
                    LPPROCESS Current = (LPPROCESS)ProcessList->First;

                    while (Current != NULL) {
                        LPPROCESS Next = (LPPROCESS)Current->Next;

                        SAFE_USE_VALID_ID(Current, KOID_PROCESS) {
                            if (Current->OwnerProcess == This->Process) {
                                Current->OwnerProcess = NULL;
                            }
                        }
                        Current = Next;
                    }
                }
            }

            UnlockMutex(MUTEX_PROCESS);
        }

        //-------------------------------------
        // Release the task structure using reference counting

        ReleaseKernelObject(This);

        // Unlock kernel mutex
        UnlockMutex(MUTEX_KERNEL);

    }

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


    SAFE_USE(Info) {
    }

    //-------------------------------------
    // Check parameters

    if (Info->Func == NULL) {
        TRACED_EPILOGUE("CreateTask");
        return NULL;
    }

    if (Info->StackSize < TASK_MINIMUM_TASK_STACK_SIZE) {
        Info->StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
    }

    if (Info->Priority > TASK_PRIORITY_CRITICAL) {
        Info->Priority = TASK_PRIORITY_CRITICAL;
    }

    if (IsValidMemory((LINEAR)Info->Func) == FALSE) {

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
        ERROR(TEXT("[CreateTask] NewTask failed"));
        goto Out;
    }


    //-------------------------------------
    // Setup the task

    Task->Process = Process;
    Task->Priority = Info->Priority;
    Task->Function = Info->Func;
    Task->Parameter = Info->Parameter;

    // Increment process task count
    SAFE_USE(Process) {
        LockMutex(MUTEX_PROCESS, INFINITY);
        Process->TaskCount++;
        UnlockMutex(MUTEX_PROCESS);
    }

    Task->Type = (Process->Privilege == CPU_PRIVILEGE_KERNEL) ?
        TASK_TYPE_KERNEL_OTHER :
        Process->TaskCount == 0 ? TASK_TYPE_USER_MAIN : TASK_TYPE_USER_OTHER;

    SetTaskWakeUpTime(Task, ComputeTaskQuantumTime(Task->Priority));

    // Copy task name for debugging
    if (Info->Name[0] != STR_NULL) {
        StringCopy(Task->Name, Info->Name);
    } else {
        StringCopy(Task->Name, TEXT("Unnamed"));
    }

    //-------------------------------------
    // Allocate the stacks


    if (SetupTask(Task, Process, Info) == FALSE) {
        DeleteTask(Task);
        Task = NULL;

        ERROR(TEXT("[CreateTask] Architecture-specific task setup failed"));
        goto Out;
    }

    // Save flags for scheduler
    Task->Flags = Info->Flags;

    LPLIST TaskList = GetTaskList();
    ListAddItem(TaskList, Task);

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
    SAFE_USE_VALID_ID(Task, KOID_TASK) {

        if (Task->Type == TASK_TYPE_KERNEL_MAIN) {
            ERROR(TEXT("[KillTask] Can't kill kernel task"));
            ConsolePanic(TEXT("Can't kill kernel task"));
            return FALSE;
        }

        UINT FirstMessage = 0;
        SAFE_USE_2(Task->MessageQueue.Messages, Task->MessageQueue.Messages->First) {
            FirstMessage = ((LPMESSAGE)Task->MessageQueue.Messages->First)->Message;
        }

        // Lock access to kernel data
        LockMutex(MUTEX_KERNEL, INFINITY);

        //-------------------------------------
        // Release all mutexes locked by this task

        ReleaseTaskMutexes(Task);

        SetTaskStatus(Task, TASK_STATUS_DEAD);

        // Dead task remains in scheduler queue until context switch
        // RemoveTaskFromQueue will be called during actual task switching
        // The task will be killed by the monitor a bit later

        // Unlock access to kernel data
        UnlockMutex(MUTEX_KERNEL);


        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

BOOL SetTaskExitCode(LPTASK Task, UINT Code) {
    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        LockMutex(MUTEX_KERNEL, INFINITY);

        Task->ExitCode = Code;

        if (Task->Type == TASK_TYPE_USER_MAIN) {
            SAFE_USE_VALID_ID(Task->Process, KOID_PROCESS) {
                Task->Process->ExitCode = Code;
            }
        }

        UnlockMutex(MUTEX_KERNEL);

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Removes and deallocates all tasks and processes marked as DEAD.
 *
 * Iterates through the global task list and deletes any tasks that have been
 * marked as dead by KillTask(). Also iterates through the process list and
 * deletes any processes marked as DEAD. This function is called periodically
 * by the kernel monitor thread to clean up terminated tasks and processes.
 *
 * @note This function locks MUTEX_KERNEL and MUTEX_PROCESS during operation
 */
void DeleteDeadTasksAndProcesses(void) {
    LPTASK Task, NextTask;
    LPPROCESS Process, NextProcess;

    // DEBUG(TEXT("[DeleteDeadTasksAndProcesses]"));

    // Lock access to kernel data
    LockMutex(MUTEX_KERNEL, INFINITY);

    LPLIST TaskList = GetTaskList();
    Task = (LPTASK)TaskList->First;

    while (Task != NULL) {
        SAFE_USE_VALID_ID(Task, KOID_TASK) {
            NextTask = (LPTASK)Task->Next;

            if (Task->Status == TASK_STATUS_DEAD) {

                // DeleteTask will handle removing from list and cleanup
                DeleteTask(Task);

            }

            Task = NextTask;
        }
        else {
            ConsolePanic(TEXT("Corrupt task in task list : %p"), Task);
        }
    }

    // Now handle DEAD processes - keep MUTEX_KERNEL locked to preserve lock order
    LockMutex(MUTEX_PROCESS, INFINITY);

    LPLIST ProcessList = GetProcessList();
    Process = (LPPROCESS)ProcessList->First;

    while (Process != NULL) {
        SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
            NextProcess = (LPPROCESS)Process->Next;

            if (Process->Status == PROCESS_STATUS_DEAD) {
                PackageNamespaceUnbindCurrentProcessPackageView();
                if (Process->PackageFileSystem != NULL) {
                    if (!PackageFSUnmount(Process->PackageFileSystem)) {
                        WARNING(TEXT("[DeleteDeadTasksAndProcesses] PackageFS unmount failed process=%s fs=%p"),
                            Process->FileName,
                            Process->PackageFileSystem);
                    }
                    Process->PackageFileSystem = NULL;
                }

                ReleaseProcessKernelObjects(Process);

                // DeleteProcessCommit will handle removing from list and cleanup
                DeleteProcessCommit(Process);

            }

            Process = NextProcess;
        }
        else {
            ConsolePanic(TEXT("Corrupt process in process list : %p"), Process);
        }
    }

    UnlockMutex(MUTEX_PROCESS);
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

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        LockMutex(MUTEX_KERNEL, INFINITY);

        OldPriority = Task->Priority;
        Task->Priority = Priority;

        UnlockMutex(MUTEX_KERNEL);
    }

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
    // DEBUG(TEXT("[Sleep] Enter : IF = %x"), Flags & 0x200);

    // Lock the task mutex
    LockMutex(MUTEX_TASK, INFINITY);

    Task = GetCurrentTask();

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        if (Task->Status == TASK_STATUS_DEAD) {
            UnlockMutex(MUTEX_TASK);
            DeadCPU();
        }

        SetTaskStatus(Task, TASK_STATUS_SLEEPING);
        SetTaskWakeUpTime(Task, MilliSeconds);

        UnlockMutex(MUTEX_TASK);

        // Block here until scheduler wakes us up
        while (GetTaskStatus(Task) == TASK_STATUS_SLEEPING) {
            if (Task->TypeID != KOID_TASK) {
                return;
            }

            if (Task->Status == TASK_STATUS_DEAD) {
                return;
            }

            IdleCPU();
            DisableInterrupts();
        }

        // DEBUG(TEXT("[Sleep] Exit %x (%s)"), Task, Task->Name);
        return;
    }

    UnlockMutex(MUTEX_TASK);
    RestoreFlags(&Flags);

    // DEBUG(TEXT("[Sleep] Exit"));
    return;
}

/************************************************************************/

/**
 * @brief Suspends the current task even when the scheduler is frozen.
 *
 * Uses a timed idle loop when task switching is disabled.
 *
 * @param MilliSeconds Number of milliseconds to sleep
 */
void SleepWithSchedulerFrozenSupport(U32 MilliSeconds) {
    if (MilliSeconds == 0) {
        return;
    }

    if (IsSchedulerFrozen()) {
        UINT StartTime = GetSystemTime();

        while ((UINT)(GetSystemTime() - StartTime) < MilliSeconds) {
            IdleCPU();
        }

        return;
    }

    Sleep(MilliSeconds);
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

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        LockMutex(&(Task->Mutex), INFINITY);

        Status = Task->Status;

        UnlockMutex(&(Task->Mutex));
    }

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
    FINE_DEBUG(TEXT("[SetTaskStatus] Enter"));

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        U32 OldStatus = Task->Status;
        UNUSED(OldStatus);

        LockMutex(&(Task->Mutex), INFINITY);
        FreezeScheduler();

        Task->Status = Status;

        if (Task->Status == TASK_STATUS_DEAD) {
            // Store termination state in cache before task is destroyed
            StoreObjectTerminationState(Task, Task->ExitCode);
        }

        FINE_DEBUG(TEXT("[SetTaskStatus] Task %p (%s): %u -> %u"), Task, Task->Name, OldStatus, Status);

        UnfreezeScheduler();
        UnlockMutex(&(Task->Mutex));
    }

    FINE_DEBUG(TEXT("[SetTaskStatus] Exit"));
}

/************************************************************************/

/**
 * @brief Sets a task status to the specified value.
 *
 * @param Task Pointer to task to modify
 * @param Status New status value to set
 */
void SetTaskStatusDirect(LPTASK Task, U32 Status) {
    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        U32 OldStatus = Task->Status;
        UNUSED(OldStatus);

        Task->Status = Status;

        FINE_DEBUG(TEXT("[SetTaskStatusDirect] Task %p (%s): %u -> %u"), Task, Task->Name, OldStatus, Status);
    }
}

/************************************************************************/

/**
 * @brief Sets the wake-up time for a task in a thread-safe manner.
 *
 * @param Task Pointer to task to modify
 * @param WakeupTime Wake-up time in milliseconds
 */
void SetTaskWakeUpTime(LPTASK Task, UINT WakeupTime) {
    if (Task == NULL) return;

    LockMutex(&(Task->Mutex), INFINITY);

    if (WakeupTime == INFINITY) {
        // INFINITY is treated as a sentinel meaning "sleep indefinitely"
        Task->WakeUpTime = INFINITY;
    } else {
        UINT CurrentTime = GetSystemTime();
        UINT Quantum = GetMinimumQuantum();
        UINT BaseTime = CurrentTime + Quantum;

        if (BaseTime < CurrentTime) {
            // Overflow occurred while adding the quantum, saturate to sentinel
            Task->WakeUpTime = INFINITY;
        } else {
            UINT TargetTime = BaseTime + WakeupTime;

            // If addition overflows, keep the task asleep indefinitely
            Task->WakeUpTime = (TargetTime < BaseTime) ? INFINITY : TargetTime;
        }
    }

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
    UINT MinimumQuantum = GetMinimumQuantum();
    UINT MaximumQuantum = GetMaximumQuantum();
    if (Time < MinimumQuantum) Time = MinimumQuantum;
    if (Time > MaximumQuantum) Time = MaximumQuantum;
    return Time;
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

    VERBOSE(TEXT("Address         : %p"), Task);
    VERBOSE(TEXT("Task Name       : %s"), Task->Name);
    VERBOSE(TEXT("References      : %u"), Task->References);
    VERBOSE(TEXT("Process         : %p"), Task->Process);
    VERBOSE(TEXT("Status          : %u"), Task->Status);
    VERBOSE(TEXT("Priority        : %u"), Task->Priority);
    VERBOSE(TEXT("Function        : %p"), Task->Function);
    VERBOSE(TEXT("Parameter       : %p"), Task->Parameter);
    VERBOSE(TEXT("ExitCode        : %u"), (U32)Task->ExitCode);
    VERBOSE(TEXT("StackBase       : %p"), Task->Arch.Stack.Base);
    VERBOSE(TEXT("StackSize       : %u"), Task->Arch.Stack.Size);
    VERBOSE(TEXT("SysStackBase    : %p"), Task->Arch.SystemStack.Base);
    VERBOSE(TEXT("SysStackSize    : %u"), Task->Arch.SystemStack.Size);
#if defined(__EXOS_ARCH_X86_64__)
    VERBOSE(TEXT("IST1StackBase   : %p"), Task->Arch.Ist1Stack.Base);
    VERBOSE(TEXT("IST1StackSize   : %u"), Task->Arch.Ist1Stack.Size);
#endif
    VERBOSE(TEXT("WakeUpTime      : %u"), (U32)Task->WakeUpTime);
    UINT PendingMessages = 0;

    if (Task->MessageQueue.Messages != NULL) {
        PendingMessages = Task->MessageQueue.Messages->NumItems;
    }

    VERBOSE(TEXT("Queued messages : %u"), PendingMessages);

    UnlockMutex(&(Task->Mutex));
}
