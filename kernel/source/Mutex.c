
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


    Mutex

\************************************************************************/

#include "Clock.h"
#include "BootStageMarker.h"
#include "Kernel.h"
#include "Log.h"
#include "process/Process.h"

/***************************************************************************/

#ifndef BOOT_STAGE_MARKERS
    #define BOOT_STAGE_MARKERS 0
#endif

/***************************************************************************/

MUTEX KernelMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&LogMutex, .Prev = NULL, .Owner = NULL, .Process = NULL, .Task = NULL, .Lock = 0};
MUTEX LogMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&MemoryMutex, .Prev = (LPLISTNODE)&KernelMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .Lock = 0};
MUTEX MemoryMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&ScheduleMutex, .Prev = (LPLISTNODE)&LogMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .Lock = 0};
MUTEX ScheduleMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&DesktopMutex, .Prev = (LPLISTNODE)&MemoryMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .Lock = 0};
MUTEX DesktopMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&ProcessMutex, .Prev = (LPLISTNODE)&ScheduleMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .Lock = 0};
MUTEX ProcessMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&TaskMutex, .Prev = (LPLISTNODE)&DesktopMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .Lock = 0};
MUTEX TaskMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&FileSystemMutex, .Prev = (LPLISTNODE)&ProcessMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .Lock = 0};
MUTEX FileSystemMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&FileMutex, .Prev = (LPLISTNODE)&TaskMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .Lock = 0};
MUTEX FileMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&ConsoleMutex, .Prev = (LPLISTNODE)&FileSystemMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .Lock = 0};
MUTEX ConsoleMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&UserAccountMutex, .Prev = (LPLISTNODE)&FileMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .Lock = 0};
MUTEX UserAccountMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = (LPLISTNODE)&SessionMutex, .Prev = (LPLISTNODE)&ConsoleMutex, .Owner = NULL, .Process = NULL, .Task = NULL, .Lock = 0};
MUTEX SessionMutex = {.TypeID = KOID_MUTEX, .References = 1, .Next = NULL, .Prev = (LPLISTNODE)&UserAccountMutex, .Owner = NULL, .Process = NULL, NULL, .Lock = 0};

/***************************************************************************/

/**
 * @brief Initializes a mutex structure.
 *
 * @param This Pointer to the mutex to initialize.
 */
void InitMutex(LPMUTEX This) {
    if (This == NULL) return;

    // LISTNODE_FIELDS already initialized if created with CreateKernelObject
    // Only initialize ID, References, Next, Prev if not already set
    if (This->TypeID == 0) {
        This->TypeID = KOID_MUTEX;
        This->References = 1;
        This->OwnerProcess = GetCurrentProcess();
        This->Next = NULL;
        This->Prev = NULL;
        This->Parent = NULL;
    }

    This->Owner = NULL;
    This->Process = NULL;
    This->Task = NULL;
    This->Lock = 0;
}

/***************************************************************************/

/**
 * @brief Creates a new mutex and adds it to the kernel mutex list.
 *
 * @return Pointer to the new mutex, or NULL on failure.
 */
LPMUTEX CreateMutex(void) {
    LPMUTEX Mutex = (LPMUTEX)CreateKernelObject(sizeof(MUTEX), KOID_MUTEX);

    SAFE_USE(Mutex) {
        Mutex->Owner = NULL;
        Mutex->Process = NULL;
        Mutex->Task = NULL;
        Mutex->Lock = 0;

        LPLIST MutexList = GetMutexList();
        ListAddItem(MutexList, Mutex);

        return Mutex;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Deletes a mutex by decrementing its reference count.
 *
 * @param Mutex Pointer to the mutex to delete.
 * @return TRUE on success, FALSE on failure.
 */
BOOL DeleteMutex(LPMUTEX Mutex) {
    SAFE_USE_VALID_ID(Mutex, KOID_MUTEX) {
        ReleaseKernelObject(Mutex);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Acquires a mutex lock, blocking until available.
 *
 * If the mutex is already owned by the calling task, increments the lock count.
 * Otherwise, waits until the mutex becomes available and then acquires it.
 *
 * @param Mutex Pointer to the mutex to lock.
 * @param TimeOut Timeout value (currently unused).
 * @return Lock count on success, 0 on failure.
 */
UINT LockMutex(LPMUTEX Mutex, UINT TimeOut) {
    UNUSED(TimeOut);
    LPPROCESS Process;
    LPTASK Task;
    UINT Flags;
    UINT Ret = 0;

#if BOOT_STAGE_MARKERS == 1
    BootStageMarkerFromConsole(81, 255, 0, 192);
#endif

    SaveFlags(&Flags);
    DisableInterrupts();

#if BOOT_STAGE_MARKERS == 1
    BootStageMarkerFromConsole(82, 255, 64, 192);
#endif

    //-------------------------------------
    // Check validity of parameters

    SAFE_USE_ID(Mutex, KOID_MUTEX) {
#if BOOT_STAGE_MARKERS == 1
        BootStageMarkerFromConsole(83, 255, 128, 192);
#endif
        // Have at leat two tasks
        LPLIST TaskList = GetTaskList();
        SAFE_USE_ID_2(TaskList->First, TaskList->First->Next, KOID_TASK) {
#if BOOT_STAGE_MARKERS == 1
            BootStageMarkerFromConsole(84, 255, 192, 192);
#endif
            Task = GetCurrentTask();

            SAFE_USE_VALID_ID(Task, KOID_TASK) {
#if BOOT_STAGE_MARKERS == 1
                BootStageMarkerFromConsole(85, 255, 255, 192);
#endif
                Process = Task->Process;

                SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
                    if (Mutex->Task == Task) {
#if BOOT_STAGE_MARKERS == 1
                        BootStageMarkerFromConsole(86, 192, 255, 192);
#endif
                        Mutex->Lock++;
                        Ret = Mutex->Lock;
                    } else {
#if BOOT_STAGE_MARKERS == 1
                        BootStageMarkerFromConsole(87, 128, 255, 192);
#endif
                        //-------------------------------------
                        // Wait for mutex to be unlocked by its owner task

                        UINT StartWaitTime = GetSystemTime();
                        UINT LastDebugTime = StartWaitTime;

                        FOREVER {
#if BOOT_STAGE_MARKERS == 1
                            BootStageMarkerFromConsole(88, 64, 255, 192);
#endif
                            //-------------------------------------
                            // Check if a process deleted this mutex

                            if (Mutex->TypeID != KOID_MUTEX) {
#if BOOT_STAGE_MARKERS == 1
                                BootStageMarkerFromConsole(89, 0, 255, 192);
#endif
                                RestoreFlags(&Flags);
                                return 0;
                            }

                            //-------------------------------------
                            // Check if the mutex is not locked anymore

                            if (Mutex->Task == NULL) {
#if BOOT_STAGE_MARKERS == 1
                                BootStageMarkerFromConsole(90, 0, 192, 255);
#endif
                                break;
                            }

                            //-------------------------------------
                            // Periodic debug output every 2 seconds

                            UINT CurrentTime = GetSystemTime();
                            if (CurrentTime - LastDebugTime >= 2000) {
                                DEBUG("[LockMutex] Task %p (%s) waiting for mutex %p owned by task %p (%s) for %u ms",
                                      Task, Task->Name,
                                      Mutex, Mutex->Task, Mutex->Task->Name,
                                      (U32)(CurrentTime - StartWaitTime));
                                LastDebugTime = CurrentTime;
                            }

                            //-------------------------------------
                            // Sleep with proper interrupt handling

#if BOOT_STAGE_MARKERS == 1
                            BootStageMarkerFromConsole(91, 0, 128, 255);
#endif
                            SetTaskStatusDirect(Task, TASK_STATUS_SLEEPING);
                            Task->WakeUpTime = GetSystemTime() + 20;

                            // Keep interrupts disabled during critical section
                            while (Task->Status == TASK_STATUS_SLEEPING) {
#if BOOT_STAGE_MARKERS == 1
                                BootStageMarkerFromConsole(92, 0, 64, 255);
#endif
                                IdleCPU();            // IdleCPU enables interrupts
                                DisableInterrupts();  // Disable immediately after
                            }
#if BOOT_STAGE_MARKERS == 1
                            BootStageMarkerFromConsole(93, 0, 0, 255);
#endif
                            // Continue loop with interrupts already disabled
                        }

                        Mutex->Process = Process;
                        Mutex->Task = Task;
                        Mutex->Lock = 1;

#if BOOT_STAGE_MARKERS == 1
                        BootStageMarkerFromConsole(94, 64, 0, 255);
#endif
                        Ret = Mutex->Lock;
                    }
                }
            }
        }
        else {
            // Consider mutex free if no task valid
#if BOOT_STAGE_MARKERS == 1
            BootStageMarkerFromConsole(95, 128, 0, 255);
#endif
            Ret = 1;
        }
    }

#if BOOT_STAGE_MARKERS == 1
    BootStageMarkerFromConsole(96, 192, 0, 255);
#endif
    RestoreFlags(&Flags);
    return Ret;
}

/***************************************************************************/

/**
 * @brief Releases a mutex lock.
 *
 * Decrements the lock count and releases the mutex if count reaches zero.
 * Only the task that owns the mutex can unlock it.
 *
 * @param Mutex Pointer to the mutex to unlock.
 * @return TRUE on success, FALSE if the calling task doesn't own the mutex.
 */
BOOL UnlockMutex(LPMUTEX Mutex) {
    LPTASK Task = NULL;
    U32 Flags;

    //-------------------------------------
    // Check validity of parameters

    if (Mutex == NULL) return 0;
    if (Mutex->TypeID != KOID_MUTEX) return 0;

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
