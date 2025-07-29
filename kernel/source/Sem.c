
// Sem.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#include "Process.h"
#include "Kernel.h"

/***************************************************************************/

SEMAPHORE KernelSemaphore =
{
  ID_SEMAPHORE, 1,
  (LPLISTNODE) &MemorySemaphore,
  NULL,
  NULL,
  NULL,
  0
};

SEMAPHORE MemorySemaphore =
{
  ID_SEMAPHORE, 1,
  (LPLISTNODE) &ScheduleSemaphore,
  (LPLISTNODE) &KernelSemaphore,
  NULL,
  NULL,
  0
};

SEMAPHORE ScheduleSemaphore =
{
  ID_SEMAPHORE, 1,
  (LPLISTNODE) &DesktopSemaphore,
  (LPLISTNODE) &MemorySemaphore,
  NULL,
  NULL,
  0
};

SEMAPHORE DesktopSemaphore =
{
  ID_SEMAPHORE, 1,
  (LPLISTNODE) &ProcessSemaphore,
  (LPLISTNODE) &ScheduleSemaphore,
  NULL,
  NULL,
  0
};

SEMAPHORE ProcessSemaphore =
{
  ID_SEMAPHORE, 1,
  (LPLISTNODE) &TaskSemaphore,
  (LPLISTNODE) &DesktopSemaphore,
  NULL,
  NULL,
  0
};

SEMAPHORE TaskSemaphore =
{
  ID_SEMAPHORE, 1,
  (LPLISTNODE) &FileSystemSemaphore,
  (LPLISTNODE) &ProcessSemaphore,
  NULL,
  NULL,
  0
};

SEMAPHORE FileSystemSemaphore =
{
  ID_SEMAPHORE, 1,
  (LPLISTNODE) &FileSemaphore,
  (LPLISTNODE) &TaskSemaphore,
  NULL,
  NULL,
  0
};

SEMAPHORE FileSemaphore =
{
  ID_SEMAPHORE, 1,
  (LPLISTNODE) &ConsoleSemaphore,
  (LPLISTNODE) &FileSystemSemaphore,
  NULL,
  NULL,
  0
};

SEMAPHORE ConsoleSemaphore =
{
  ID_SEMAPHORE, 1,
  NULL,
  (LPLISTNODE) &FileSemaphore,
  NULL,
  NULL,
  0
};

/***************************************************************************/

void InitSemaphore (LPSEMAPHORE This)
{
  if (This == NULL) return;

  This->ID         = ID_SEMAPHORE;
  This->References = 1;
  This->Next       = NULL;
  This->Prev       = NULL;
  This->Process    = NULL;
  This->Task       = NULL;
  This->Lock       = 0;
}

/***************************************************************************/

LPSEMAPHORE NewSemaphore ()
{
  LPSEMAPHORE This = (LPSEMAPHORE) KernelMemAlloc(sizeof(SEMAPHORE));

  if (This == NULL) return NULL;

  MemorySet(This, 0, sizeof (SEMAPHORE));

  This->ID         = ID_SEMAPHORE;
  This->References = 1;
  This->Process    = NULL;
  This->Task       = NULL;
  This->Lock       = 0;

  return This;
}

/***************************************************************************/

LPSEMAPHORE CreateSemaphore ()
{
  LPSEMAPHORE Semaphore = NewSemaphore();

  if (Semaphore == NULL) return NULL;

  ListAddItem(Kernel.Semaphore, Semaphore);

  return Semaphore;
}

/***************************************************************************/

BOOL DeleteSemaphore (LPSEMAPHORE Semaphore)
{
  //-------------------------------------
  // Check validity of parameters

  if (Semaphore->ID != ID_SEMAPHORE) return 0;

  if (Semaphore->References) Semaphore->References--;

  if (Semaphore->References == 0)
  {
    Semaphore->ID = ID_NONE;
    ListEraseItem(Kernel.Semaphore, Semaphore);
  }

  return 1;
}

/***************************************************************************/

U32 LockSemaphore (LPSEMAPHORE Semaphore, U32 TimeOut)
{
  LPPROCESS Process;
  LPTASK    Task;
  U32       Flags;
  U32       Ret = 0;

  //-------------------------------------
  // Check validity of parameters

  if (Semaphore == NULL) return 0;
  if (Semaphore->ID != ID_SEMAPHORE) return 0;

  SaveFlags(&Flags);
  DisableInterrupts();

  Task    = GetCurrentTask();
  Process = Task->Process;

  if (Semaphore->Task == Task)
  {
    Semaphore->Lock++;
    Ret = Semaphore->Lock;
    goto Out;
  }

  //-------------------------------------
  // Wait for semaphore to be unlocked by its owner task

  while (1)
  {
    DisableInterrupts();

    //-------------------------------------
    // Check if a process did not delete this semaphore

    if (Semaphore->ID != ID_SEMAPHORE)
    {
      Ret = 0;
      goto Out;
    }

    //-------------------------------------
    // Check if the semaphore is not locked anymore

    if (Semaphore->Task == NULL)
    {
      break;
    }

    //-------------------------------------
    // Sleep

    Task->Status     = TASK_STATUS_SLEEPING;
    Task->WakeUpTime = GetSystemTime() + 20;

    EnableInterrupts();

    while (Task->Status == TASK_STATUS_SLEEPING) {}
  }

  DisableInterrupts();

  Semaphore->Process = Process;
  Semaphore->Task    = Task;
  Semaphore->Lock    = 1;

  Ret = Semaphore->Lock;

Out :

  RestoreFlags(&Flags);

  return Ret;
}

/***************************************************************************/

BOOL UnlockSemaphore (LPSEMAPHORE Semaphore)
{
  LPTASK Task = NULL;
  U32    Flags;

  //-------------------------------------
  // Check validity of parameters

  if (Semaphore == NULL) return 0;
  if (Semaphore->ID != ID_SEMAPHORE) return 0;

  SaveFlags(&Flags);
  DisableInterrupts();

  Task = GetCurrentTask();

  if (Semaphore->Task != Task) goto Out_Error;

  if (Semaphore->Lock != 0) Semaphore->Lock--;

  if (Semaphore->Lock == 0)
  {
    Semaphore->Process = NULL;
    Semaphore->Task    = NULL;
  }

  RestoreFlags(&Flags);
  return TRUE;

Out_Error :

  RestoreFlags(&Flags);
  return FALSE;
}

/***************************************************************************/
