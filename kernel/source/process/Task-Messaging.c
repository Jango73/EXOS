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


    Task messaging

\************************************************************************/

#include "Clock.h"
#include "Arch.h"
#include "Kernel.h"
#include "Log.h"
#include "process/Process.h"
#include "process/Task-Messaging.h"
#include "CoreString.h"
#include "utils/Helpers.h"

/************************************************************************/

static BOOL AddTaskMessage(LPTASK Task, LPMESSAGE Message);
static BOOL CopyMessageFromQueueLocked(LPMESSAGEQUEUE Queue, LPMESSAGEINFO Message, BOOL Remove);
static BOOL AddProcessMessage(LPPROCESS Process, LPMESSAGE Message);
static BOOL FetchProcessMessage(LPPROCESS Process, LPMESSAGEINFO Message, BOOL Remove);
static BOOL FetchTaskMessage(LPTASK Task, LPMESSAGEINFO Message, BOOL Remove);

/************************************************************************/

/**
 * @brief Allocates and initializes a new message structure.
 *
 * Creates a new message object with default values and reference count of 1.
 * The message ID is set to KOID_MESSAGE for validation purposes.
 *
 * @return Pointer to newly allocated message, or NULL on allocation failure
 */
static LPMESSAGE NewMessage(void) {
    LPMESSAGE This;

    This = (LPMESSAGE)KernelHeapAlloc(sizeof(MESSAGE));

    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(MESSAGE));

    This->TypeID = KOID_MESSAGE;
    This->References = 1;

    return This;
}

/************************************************************************/

/**
 * @brief Deallocates a message structure.
 *
 * Clears the message ID and frees the memory allocated for the message.
 * The ID is set to KOID_NONE to prevent use-after-free bugs.
 *
 * @param This Pointer to message to delete, ignored if NULL
 */
void DeleteMessage(LPMESSAGE This) {
    SAFE_USE(This) {
        This->TypeID = KOID_NONE;

        KernelHeapFree(This);
    }
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
 * @brief Initializes a message queue structure.
 *
 * Sets default flags, initializes the mutex, and allocates the underlying
 * message list using the message destructor. The queue is ready for use
 * after successful initialization.
 *
 * @param Queue Pointer to the queue to initialize
 * @return TRUE on success, FALSE on invalid pointer or allocation failure
 */
BOOL InitMessageQueue(LPMESSAGEQUEUE Queue) {
    if (Queue == NULL) return FALSE;

    InitMutex(&(Queue->Mutex));
    Queue->Capacity = 0;
    Queue->Flags = 0;
    Queue->Waiting = FALSE;

    Queue->Messages = NewList(MessageDestructor, KernelHeapAlloc, KernelHeapFree);

    return Queue->Messages != NULL;
}

/************************************************************************/

/**
 * @brief Destroys a message queue and resets its fields.
 *
 * Frees the underlying message list and clears bookkeeping fields. The
 * function ignores NULL queues and NULL internal lists.
 *
 * @param Queue Pointer to the queue to destroy
 */
void DeleteMessageQueue(LPMESSAGEQUEUE Queue) {
    SAFE_USE(Queue) {
        SAFE_USE(Queue->Messages) DeleteList(Queue->Messages);
        Queue->Messages = NULL;
        Queue->Capacity = 0;
        Queue->Flags = 0;
        Queue->Waiting = FALSE;
    }
}

/************************************************************************/

BOOL EnsureTaskMessageQueue(LPTASK Task, BOOL CreateIfMissing) {
    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        if (Task->MessageQueue.Messages == NULL) {
            if (CreateIfMissing == FALSE) return FALSE;

            if (InitMessageQueue(&(Task->MessageQueue)) == FALSE) {
                ERROR(TEXT("[EnsureTaskMessageQueue] Failed to initialize queue for task %p"), Task);
                return FALSE;
            }
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

BOOL EnsureProcessMessageQueue(LPPROCESS Process, BOOL CreateIfMissing) {
    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->MessageQueue.Messages == NULL) {
            if (CreateIfMissing == FALSE) {
                return FALSE;
            }

            if (InitMessageQueue(&(Process->MessageQueue)) == FALSE) {
                ERROR(TEXT("[EnsureProcessMessageQueue] Failed to initialize queue for process %p"), Process);
                return FALSE;
            }
            Process->MessageQueue.Capacity = TASK_MESSAGE_QUEUE_MAX_MESSAGES;
        }

        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

static BOOL FetchProcessMessage(LPPROCESS Process, LPMESSAGEINFO Message, BOOL Remove) {
    if (EnsureProcessMessageQueue(Process, TRUE) == FALSE) {
        return FALSE;
    }

    LockMutex(&(Process->MessageQueue.Mutex), INFINITY);

    BOOL Result = CopyMessageFromQueueLocked(&(Process->MessageQueue), Message, Remove);

    UnlockMutex(&(Process->MessageQueue.Mutex));

    return Result;
}

/************************************************************************/

static BOOL FetchTaskMessage(LPTASK Task, LPMESSAGEINFO Message, BOOL Remove) {
    if (EnsureTaskMessageQueue(Task, TRUE) == FALSE) {
        return FALSE;
    }

    LockMutex(&(Task->Mutex), INFINITY);
    LockMutex(&(Task->MessageQueue.Mutex), INFINITY);

    BOOL Result = CopyMessageFromQueueLocked(&(Task->MessageQueue), Message, Remove);

    UnlockMutex(&(Task->MessageQueue.Mutex));
    UnlockMutex(&(Task->Mutex));

    return Result;
}

/************************************************************************/

/**
 * @brief Peek the next message from the current task's queue without removing it.
 *
 * Non-blocking; returns FALSE if no message is available or parameters are invalid.
 *
 * @param Message Pointer to message info structure to fill
 * @return TRUE if a message was found, FALSE otherwise
 */
BOOL PeekMessage(LPMESSAGEINFO Message) {
    LPTASK Task;
    LPPROCESS TaskProcessPtr = NULL;
    LPPROCESS Process = NULL;

    if (Message == NULL) return FALSE;

    Task = GetCurrentTask();
    SAFE_USE_VALID_ID(Task, KOID_TASK) { TaskProcessPtr = Task->Process; }
    DEBUG(TEXT("[PeekMessage] Task=%p Process=%p FocusedProcess=%p"), Task, TaskProcessPtr, GetFocusedProcess());

    Process = TaskProcessPtr;

    if (EnsureTaskMessageQueue(Task, TRUE) == FALSE) return FALSE;

    if (FetchProcessMessage(Process, Message, FALSE) == TRUE) {
        return TRUE;
    }

    return FetchTaskMessage(Task, Message, FALSE);
}

/************************************************************************/

/**
 * @brief Copy a message from a locked queue, optionally removing it.
 * @param Queue Message queue (caller must lock Queue->Mutex).
 * @param Message Target structure to fill.
 * @param Remove TRUE to erase the message from the queue.
 * @return TRUE if a message was found.
 */
static BOOL CopyMessageFromQueueLocked(LPMESSAGEQUEUE Queue, LPMESSAGEINFO Message, BOOL Remove) {
    LPLISTNODE Node = NULL;
    LPMESSAGE CurrentMessage = NULL;

    if (Queue == NULL || Queue->Messages == NULL || Message == NULL) {
        return FALSE;
    }

    if (Queue->Messages->NumItems == 0) {
        return FALSE;
    }

    if (Message->Target == NULL) {
        CurrentMessage = (LPMESSAGE)Queue->Messages->First;
    } else {
        for (Node = Queue->Messages->First; Node; Node = Node->Next) {
            LPMESSAGE Candidate = (LPMESSAGE)Node;
            if (Candidate->Target == Message->Target) {
                CurrentMessage = Candidate;
                break;
            }
        }
    }

    if (CurrentMessage == NULL) {
        return FALSE;
    }

    Message->Target = CurrentMessage->Target;
    Message->Time = CurrentMessage->Time;
    Message->Message = CurrentMessage->Message;
    Message->Param1 = CurrentMessage->Param1;
    Message->Param2 = CurrentMessage->Param2;

    if (Remove) {
        ListEraseItem(Queue->Messages, CurrentMessage);
    }

    return TRUE;
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
static BOOL AddTaskMessage(LPTASK Task, LPMESSAGE Message) {
    if (Task == NULL || Task->TypeID != KOID_TASK) {
        DeleteMessage(Message);
        return FALSE;
    }

    if (Task->MessageQueue.Messages == NULL) {
        DeleteMessage(Message);
        return FALSE;
    }

    LockMutex(&(Task->Mutex), INFINITY);
    LockMutex(&(Task->MessageQueue.Mutex), INFINITY);

    if (Task->MessageQueue.Messages->NumItems >= TASK_MESSAGE_QUEUE_MAX_MESSAGES) {
        WARNING(TEXT("[AddTaskMessage] Queue full for task %p, dropping message %u"), Task, Message->Message);
        UnlockMutex(&(Task->MessageQueue.Mutex));
        UnlockMutex(&(Task->Mutex));
        DeleteMessage(Message);
        return FALSE;
    }

    ListAddItem(Task->MessageQueue.Messages, Message);

    if (Task->MessageQueue.Waiting && GetTaskStatus(Task) == TASK_STATUS_WAITMESSAGE) {
        Task->MessageQueue.Waiting = FALSE;
        SetTaskStatus(Task, TASK_STATUS_RUNNING);
    }

    UnlockMutex(&(Task->MessageQueue.Mutex));
    UnlockMutex(&(Task->Mutex));

    return TRUE;
}

/************************************************************************/

static BOOL AddProcessMessage(LPPROCESS Process, LPMESSAGE Message) {
    if (Process == NULL || Process->TypeID != KOID_PROCESS) {
        DeleteMessage(Message);
        return FALSE;
    }

    if (Process->MessageQueue.Messages == NULL) {
        DeleteMessage(Message);
        return FALSE;
    }

    LockMutex(&(Process->Mutex), INFINITY);
    LockMutex(&(Process->MessageQueue.Mutex), INFINITY);

    if (Process->MessageQueue.Messages->NumItems >= TASK_MESSAGE_QUEUE_MAX_MESSAGES) {
        WARNING(TEXT("[AddProcessMessage] Queue full for process %p, dropping message %u"), Process, Message->Message);
        UnlockMutex(&(Process->MessageQueue.Mutex));
        UnlockMutex(&(Process->Mutex));
        DeleteMessage(Message);
        return FALSE;
    }

    ListAddItem(Process->MessageQueue.Messages, Message);

    UnlockMutex(&(Process->MessageQueue.Mutex));
    UnlockMutex(&(Process->Mutex));

    LockMutex(MUTEX_TASK, INFINITY);
    LPLIST TaskList = GetTaskList();
    for (LPLISTNODE Node = TaskList != NULL ? TaskList->First : NULL; Node; Node = Node->Next) {
        LPTASK Task = (LPTASK)Node;

        SAFE_USE_VALID_ID(Task, KOID_TASK) {
            if (Task->Process == Process && GetTaskStatus(Task) == TASK_STATUS_WAITMESSAGE) {
                SetTaskStatus(Task, TASK_STATUS_RUNNING);
            }
        }
    }
    UnlockMutex(MUTEX_TASK);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Enqueue an input message.
 *
 * Routes keyboard/mouse events to the focused window's task queue when a focused window exists,
 * otherwise to the focused process' message queue (created on-demand for the kernel process or
 * when the process has explicitly initialized its queue via PeekMessage/GetMessage).
 * If no suitable queue exists, the message is dropped.
 *
 * @param Msg Message identifier.
 * @param Param1 First message parameter.
 * @param Param2 Second message parameter.
 * @return TRUE on success, FALSE if no queue is available.
 */
BOOL EnqueueInputMessage(U32 Msg, U32 Param1, U32 Param2) {
    LPDESKTOP Desktop = GetFocusedDesktop();
    LPPROCESS Process = GetFocusedProcess();
    LPWINDOW FocusedWindow = NULL;
    LPTASK TargetTask = NULL;

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) { FocusedWindow = Desktop->Focus; }

    // Only route to a focused window if it belongs to the focused process.
    SAFE_USE_VALID_ID(FocusedWindow, KOID_WINDOW) {
        SAFE_USE_VALID_ID(FocusedWindow->Task, KOID_TASK) {
            if (FocusedWindow->Task->Process == Process) {
                TargetTask = FocusedWindow->Task;
            }
        }
    }

    LPMESSAGE Message = NewMessage();
    if (Message == NULL) {
        return FALSE;
    }

    GetLocalTime(&(Message->Time));
    Message->Target = (TargetTask != NULL && FocusedWindow != NULL) ? (HANDLE)FocusedWindow : NULL;
    Message->Message = Msg;
    Message->Param1 = Param1;
    Message->Param2 = Param2;

    if (TargetTask != NULL) {
        if (AddTaskMessage(TargetTask, Message) == TRUE) {
            return TRUE;
        }

        // AddTaskMessage deletes Message on failure, so recreate for fallback
        Message = NewMessage();
        if (Message == NULL) {
            return FALSE;
        }

        GetLocalTime(&(Message->Time));
        Message->Target = NULL;
        Message->Message = Msg;
        Message->Param1 = Param1;
        Message->Param2 = Param2;
    }

    SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
        if (Process->MessageQueue.Messages != NULL) {
            return AddProcessMessage(Process, Message);
        }
    }

    DeleteMessage(Message);
    return FALSE;
}

/************************************************************************/

/**
 * @brief Broadcast a message to all user processes with message queues.
 * @param Msg Message identifier.
 * @param Param1 First message parameter.
 * @param Param2 Second message parameter.
 * @return TRUE when at least one message was posted.
 */
BOOL BroadcastProcessMessage(U32 Msg, U32 Param1, U32 Param2) {
    BOOL Sent = FALSE;
    LPLIST ProcessList = GetProcessList();

    if (ProcessList == NULL) return FALSE;

    LockMutex(MUTEX_TASK, INFINITY);

    for (LPLISTNODE Node = ProcessList->First; Node; Node = Node->Next) {
        LPPROCESS Process = (LPPROCESS)Node;

        SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
            if (Process == &KernelProcess) {
                continue;
            }

            if (Process->MessageQueue.Messages == NULL) {
                continue;
            }

            LPMESSAGE Message = NewMessage();
            if (Message == NULL) {
                continue;
            }

            GetLocalTime(&(Message->Time));
            Message->Target = NULL;
            Message->Message = Msg;
            Message->Param1 = Param1;
            Message->Param2 = Param2;

            if (AddProcessMessage(Process, Message) == TRUE) {
                Sent = TRUE;
            }
        }
    }

    UnlockMutex(MUTEX_TASK);

    return Sent;
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

    //-------------------------------------
    // Lock access to resources

    LockMutex(MUTEX_TASK, INFINITY);
    LockMutex(MUTEX_DESKTOP, INFINITY);

    //-------------------------------------
    // Null target means current task

    if (Target == NULL) {
        Task = GetCurrentTask();

        SAFE_USE_VALID_ID(Task, KOID_TASK) {
            if (Task->MessageQueue.Messages == NULL) {
                goto Out_Error;
            }

            Message = NewMessage();
            if (Message == NULL) goto Out_Error;

            GetLocalTime(&(Message->Time));

            Message->Target = Target;
            Message->Message = Msg;
            Message->Param1 = Param1;
            Message->Param2 = Param2;

            if (AddTaskMessage(Task, Message) == FALSE) goto Out_Error;

            if (GetTaskStatus(Task) == TASK_STATUS_WAITMESSAGE) {
                SetTaskStatus(Task, TASK_STATUS_RUNNING);
            }

            goto Out_Success;
        }

        goto Out_Error;
    }

    //-------------------------------------
    // Check if the target is a task

    LPLIST TaskList = GetTaskList();
    for (Node = TaskList != NULL ? TaskList->First : NULL; Node; Node = Node->Next) {
        Task = (LPTASK)Node;

        if (Task == (LPTASK)Target) {
            if (Task->MessageQueue.Messages == NULL) {
                goto Out_Error;
            }

            Message = NewMessage();
            if (Message == NULL) goto Out_Error;

            GetLocalTime(&(Message->Time));

            Message->Target = Target;
            Message->Message = Msg;
            Message->Param1 = Param1;
            Message->Param2 = Param2;

            if (AddTaskMessage(Task, Message) == FALSE) goto Out_Error;

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
      LPLIST DesktopList = GetDesktopList();
      for (Node = DesktopList != NULL ? DesktopList->First : NULL; Node; Node = Node->Next)
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

    Desktop = NULL;
    Win = (LPWINDOW)Target;

    SAFE_USE_VALID_ID(Win, KOID_WINDOW) {
        SAFE_USE_VALID_ID(Win->Task, KOID_TASK) {
            SAFE_USE_VALID_ID(Win->Task->Process, KOID_PROCESS) {
                Desktop = Win->Task->Process->Desktop;
            }
        }
    }

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        LockMutex(&(Desktop->Mutex), INFINITY);
        Win = FindWindow(Desktop->Window, (LPWINDOW)Target);
        UnlockMutex(&(Desktop->Mutex));
    } else {
        Win = NULL;
    }

    SAFE_USE_VALID_ID(Win, KOID_WINDOW) {
        if (Win->Task->MessageQueue.Messages == NULL) {
            goto Out_Error;
        }

        //-------------------------------------
        // If the message is EWM_DRAW, do not post it if
        // window already has one. Instead, put the existing
        // one at the end of the queue

        if (Msg == EWM_DRAW) {
            LockMutex(&(Win->Task->Mutex), INFINITY);
            LockMutex(&(Win->Task->MessageQueue.Mutex), INFINITY);

            for (Node = Win->Task->MessageQueue.Messages->First; Node; Node = Node->Next) {
                Message = (LPMESSAGE)Node;
                if (Message->Target == (HANDLE)Win && Message->Message == Msg) {
                    ListRemove(Win->Task->MessageQueue.Messages, Message);

                    GetLocalTime(&(Message->Time));

                    Message->Param1 = Param1;
                    Message->Param2 = Param2;

                    ListAddItem(Win->Task->MessageQueue.Messages, Message);

                    UnlockMutex(&(Win->Task->MessageQueue.Mutex));
                    UnlockMutex(&(Win->Task->Mutex));

                    goto Out_Success;
                }
            }
        }

        UnlockMutex(&(Win->Task->MessageQueue.Mutex));
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

        if (AddTaskMessage(Win->Task, Message) == FALSE) goto Out_Error;


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
    if (Desktop->TypeID != KOID_DESKTOP) return 0;

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

    if (Window != NULL && Window->TypeID == KOID_WINDOW) {
        SAFE_USE(Window->Function) {
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

    if (EnsureTaskMessageQueue(Task, TRUE) == TRUE) {
        Task->MessageQueue.Waiting = TRUE;
    }

    SetTaskStatus(Task, TASK_STATUS_WAITMESSAGE);
    SetTaskWakeUpTime(Task, MAX_U16);

    //-------------------------------------
    // The following loop is to make sure that
    // the task will not return immediately.
    // During the loop, the task does not get any
    // CPU cycles.

    while (GetTaskStatus(Task) == TASK_STATUS_WAITMESSAGE) {
        SAFE_USE_VALID_ID(Task->Process, KOID_PROCESS) {
            if (EnsureProcessMessageQueue(Task->Process, TRUE) == TRUE) {
                LockMutex(&(Task->Process->MessageQueue.Mutex), INFINITY);

                if (Task->Process->MessageQueue.Messages != NULL &&
                    Task->Process->MessageQueue.Messages->NumItems > 0) {
                    UnlockMutex(&(Task->Process->MessageQueue.Mutex));
                    SetTaskStatus(Task, TASK_STATUS_RUNNING);
                    break;
                }

                UnlockMutex(&(Task->Process->MessageQueue.Mutex));
            }
        }

        IdleCPU();
    }

    if (EnsureTaskMessageQueue(Task, TRUE) == TRUE) {
        Task->MessageQueue.Waiting = FALSE;
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
    LPPROCESS TaskProcessPtr = NULL;
    LPPROCESS Process = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (Message == NULL) return FALSE;

    Task = GetCurrentTask();
    SAFE_USE_VALID_ID(Task, KOID_TASK) { TaskProcessPtr = Task->Process; }

    if (EnsureTaskMessageQueue(Task, TRUE) == FALSE) return FALSE;
    Process = TaskProcessPtr;

    FOREVER {
        if (FetchProcessMessage(Process, Message, TRUE) == TRUE) {
            return Message->Message != ETM_QUIT;
        }

        if (FetchTaskMessage(Task, Message, TRUE) == TRUE) {
            return Message->Message != ETM_QUIT;
        }

        WaitForMessage(Task);
    }
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
    if (Message->Target == 0) return FALSE;

    if (Window == NULL) return FALSE;
    if (Window->TypeID != KOID_WINDOW) return FALSE;

    //-------------------------------------
    // Lock access to the window

    LockMutex(&(Window->Mutex), INFINITY);

    if (EnsureKernelPointer((LINEAR)Message->Target) == (LINEAR)Window) {
        SAFE_USE(Window->Function) {
            // Call the window function with the parameters

            HANDLE WindowParam = EnsureHandle((LINEAR)Window);

            Window->Function(WindowParam, Message->Message, Message->Param1, Message->Param2);

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
      LPLIST TaskList = GetTaskList();
      for (Node = TaskList != NULL ? TaskList->First : NULL; Node; Node = Node->Next)
      {
      }
    */

    //-------------------------------------
    // Check if the target is a window

    Process = GetCurrentProcess();
    if (Process == NULL) goto Out;
    if (Process->TypeID != KOID_PROCESS) goto Out;

    Desktop = Process->Desktop;
    if (Desktop == NULL) goto Out;
    if (Desktop->TypeID != KOID_DESKTOP) goto Out;

    LockMutex(&(Desktop->Mutex), INFINITY);

    Result = DispatchMessageToWindow(Message, Desktop->Window);

    UnlockMutex(&(Desktop->Mutex));

Out:

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(MUTEX_TASK);

    return Result;
}
/**
 * @brief Ensure both task and process message queues exist.
 * @param Task Task whose queues must be initialized.
 * @param CreateIfMissing TRUE to create queues when absent.
 * @return TRUE if both queues are ready or created, FALSE otherwise.
 */
BOOL EnsureAllMessageQueues(LPTASK Task, BOOL CreateIfMissing) {
    if (EnsureTaskMessageQueue(Task, CreateIfMissing) == FALSE) {
        return FALSE;
    }

    SAFE_USE_VALID_ID(Task, KOID_TASK) {
        return EnsureProcessMessageQueue(Task->Process, CreateIfMissing);
    }

    return FALSE;
}

/************************************************************************/
