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


    Debug lock order checker

\************************************************************************/

#include "utils/LockOrderDebug.h"

#include "Log.h"
#include "System.h"
#include "process/Task.h"
#include "process/Schedule.h"

/***************************************************************************/
// Macros

#define LOCK_ORDER_DEBUG_MAX_DEPTH 32

/***************************************************************************/
// Inline functions

static LPTASK LockOrderDebugGetCurrentTask(void) {
    LPTASK Task;

    Task = GetCurrentTask();
    if (Task == NULL || Task->TypeID != KOID_TASK) {
        return NULL;
    }

    return Task;
}

/***************************************************************************/
// External functions

void AcquireLockRole(U32 Role, LPCSTR RoleName) {
#if DEBUG_OUTPUT == 1
    U32 Flags;
    LPTASK Task;
    U32 PreviousRole;

    if (Role == 0) return;

    SaveFlags(&Flags);
    DisableInterrupts();

    Task = LockOrderDebugGetCurrentTask();
    if (Task == NULL) {
        RestoreFlags(&Flags);
        return;
    }

    if (Task->DebugLockOrderDepth > 0) {
        PreviousRole = Task->DebugLockOrderStack[Task->DebugLockOrderDepth - 1];
        if (Role < PreviousRole) {
            WARNING(TEXT("[AcquireLockRole] Lock order inversion task=%p role=%u previous=%u name=%s"),
                  Task,
                  Role,
                  PreviousRole,
                  RoleName != NULL ? RoleName : TEXT("unknown"));
        }
    }

    if (Task->DebugLockOrderDepth >= LOCK_ORDER_DEBUG_MAX_DEPTH) {
        WARNING(TEXT("[AcquireLockRole] Lock depth overflow task=%p role=%u name=%s"),
              Task,
              Role,
              RoleName != NULL ? RoleName : TEXT("unknown"));
        RestoreFlags(&Flags);
        return;
    }

    Task->DebugLockOrderStack[Task->DebugLockOrderDepth] = Role;
    Task->DebugLockOrderDepth++;

    RestoreFlags(&Flags);
#else
    UNUSED(Role);
    UNUSED(RoleName);
#endif
}

/***************************************************************************/

void ReleaseLockRole(U32 Role, LPCSTR RoleName) {
#if DEBUG_OUTPUT == 1
    U32 Flags;
    LPTASK Task;
    U32 PreviousRole;

    if (Role == 0) return;

    SaveFlags(&Flags);
    DisableInterrupts();

    Task = LockOrderDebugGetCurrentTask();
    if (Task == NULL || Task->DebugLockOrderDepth == 0) {
        ERROR(TEXT("[ReleaseLockRole] Lock release without acquire task=%p role=%u name=%s"),
              Task,
              Role,
              RoleName != NULL ? RoleName : TEXT("unknown"));
        RestoreFlags(&Flags);
        return;
    }

    PreviousRole = Task->DebugLockOrderStack[Task->DebugLockOrderDepth - 1];
    if (PreviousRole != Role) {
        ERROR(TEXT("[ReleaseLockRole] Lock release order mismatch task=%p role=%u expected=%u name=%s"),
              Task,
              Role,
              PreviousRole,
              RoleName != NULL ? RoleName : TEXT("unknown"));
        RestoreFlags(&Flags);
        return;
    }

    Task->DebugLockOrderDepth--;

    RestoreFlags(&Flags);
#else
    UNUSED(Role);
    UNUSED(RoleName);
#endif
}

/***************************************************************************/

void LockOrderDebugAcquire(U32 Role, LPCSTR RoleName) { AcquireLockRole(Role, RoleName); }

/***************************************************************************/

void LockOrderDebugRelease(U32 Role, LPCSTR RoleName) { ReleaseLockRole(Role, RoleName); }
