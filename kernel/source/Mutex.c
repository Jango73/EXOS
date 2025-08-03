
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Clock.h"
#include "../include/Kernel.h"
#include "../include/Process.h"

/***************************************************************************/

MUTEX KernelMutex = {ID_MUTEX, 1, (LPLISTNODE)&LogMutex, NULL, NULL,                     NULL,     0};
MUTEX LogMutex = {ID_MUTEX, 1, (LPLISTNODE)&MemoryMutex, (LPLISTNODE)&KernelMutex, NULL,                     NULL,     0};
MUTEX MemoryMutex = {ID_MUTEX, 1, (LPLISTNODE)&ScheduleMutex, (LPLISTNODE)&LogMutex, NULL,    NULL,     0};
MUTEX ScheduleMutex = {ID_MUTEX, 1, (LPLISTNODE)&DesktopMutex, (LPLISTNODE)&MemoryMutex, NULL,    NULL,     0};
MUTEX DesktopMutex = {ID_MUTEX, 1, (LPLISTNODE)&ProcessMutex, (LPLISTNODE)&ScheduleMutex, NULL,    NULL,     0};
MUTEX ProcessMutex = {ID_MUTEX, 1, (LPLISTNODE)&TaskMutex, (LPLISTNODE)&DesktopMutex, NULL,    NULL,     0};
MUTEX TaskMutex = {ID_MUTEX, 1, (LPLISTNODE)&FileSystemMutex, (LPLISTNODE)&ProcessMutex, NULL,    NULL,     0};
MUTEX FileSystemMutex = {ID_MUTEX, 1, (LPLISTNODE)&FileMutex, (LPLISTNODE)&TaskMutex, NULL, NULL, 0};
MUTEX FileMutex = {ID_MUTEX, 1, (LPLISTNODE)&ConsoleMutex, (LPLISTNODE)&FileSystemMutex, NULL,    NULL,     0};
MUTEX ConsoleMutex = {ID_MUTEX, 1, NULL, (LPLISTNODE)&FileMutex, NULL, NULL, 0};

/***************************************************************************/

void InitMutex(LPMUTEX This) {
    if (This == NULL) return;

    This->ID = ID_MUTEX;
    This->References = 1;
    This->Next = NULL;
    This->Prev = NULL;
    This->Process = NULL;
    This->Task = NULL;
    This->Lock = 0;
}

/***************************************************************************/

LPMUTEX NewMutex() {
    LPMUTEX This = (LPMUTEX)KernelMemAlloc(sizeof(MUTEX));

    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(MUTEX));

    This->ID = ID_MUTEX;
    This->References = 1;
    This->Process = NULL;
    This->Task = NULL;
    This->Lock = 0;

    return This;
}

/***************************************************************************/

LPMUTEX CreateMutex() {
    LPMUTEX Mutex = NewMutex();

    if (Mutex == NULL) return NULL;

    ListAddItem(Kernel.Mutex, Mutex);

    return Mutex;
}

/***************************************************************************/

BOOL DeleteMutex(LPMUTEX Mutex) {
    //-------------------------------------
    // Check validity of parameters

    if (Mutex == NULL) return 0;
    if (Mutex->ID != ID_MUTEX) return 0;

    if (Mutex->References) Mutex->References--;

    if (Mutex->References == 0) {
        Mutex->ID = ID_NONE;
        ListEraseItem(Kernel.Mutex, Mutex);
    }

    return 1;
}

/***************************************************************************/

U32 LockMutex(LPMUTEX Mutex, U32 TimeOut) {
    UNUSED(TimeOut);

    LPPROCESS Process;
    LPTASK Task;
    U32 Flags;
    U32 Ret = 0;

    //-------------------------------------
    // Check validity of parameters

    if (Mutex == NULL) return 0;
    if (Mutex->ID != ID_MUTEX) return 0;

    SaveFlags(&Flags);
    DisableInterrupts();

    Task = GetCurrentTask();
    Process = Task->Process;

    if (Mutex->Task == Task) {
        Mutex->Lock++;
        Ret = Mutex->Lock;
        goto Out;
    }

    //-------------------------------------
    // Wait for mutex to be unlocked by its owner task

    while (1) {
        DisableInterrupts();

        //-------------------------------------
        // Check if a process did not delete this mutex

        if (Mutex->ID != ID_MUTEX) {
            Ret = 0;
            goto Out;
        }

        //-------------------------------------
        // Check if the mutex is not locked anymore

        if (Mutex->Task == NULL) {
            break;
        }

        //-------------------------------------
        // Sleep

        Task->Status = TASK_STATUS_SLEEPING;
        Task->WakeUpTime = GetSystemTime() + 20;

        EnableInterrupts();

        while (Task->Status == TASK_STATUS_SLEEPING) {
        }
    }

    DisableInterrupts();

    Mutex->Process = Process;
    Mutex->Task = Task;
    Mutex->Lock = 1;

    Ret = Mutex->Lock;

Out:

    RestoreFlags(&Flags);

    return Ret;
}

/***************************************************************************/

BOOL UnlockMutex(LPMUTEX Mutex) {
    LPTASK Task = NULL;
    U32 Flags;

    //-------------------------------------
    // Check validity of parameters

    if (Mutex == NULL) return 0;
    if (Mutex->ID != ID_MUTEX) return 0;

    SaveFlags(&Flags);
    DisableInterrupts();

    Task = GetCurrentTask();

    if (Mutex->Task != Task) goto Out_Error;

    if (Mutex->Lock != 0) Mutex->Lock--;

    if (Mutex->Lock == 0) {
        Mutex->Process = NULL;
        Mutex->Task = NULL;
    }

    RestoreFlags(&Flags);
    return TRUE;

Out_Error:

    RestoreFlags(&Flags);
    return FALSE;
}

/***************************************************************************/
