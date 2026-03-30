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


    Process ownership access helpers

\************************************************************************/

#ifndef PROCESSACCESS_H_INCLUDED
#define PROCESSACCESS_H_INCLUDED

/************************************************************************/

#include "Base.h"

typedef struct tag_PROCESS PROCESS, *LPPROCESS;
typedef struct tag_TASK TASK, *LPTASK;

/************************************************************************/

#define SAFE_USE_VALID_ID_ACCESSIBLE(a, i, c, o) \
    if ((a) != NULL && IsValidMemory((LINEAR)(a)) && ((a)->TypeID == (i)) && \
        ProcessAccessCanTargetObject((c), (LPVOID)(a), (o)))

#define SAFE_USE_VALID_ID_CURRENT_PROCESS_ACCESSIBLE(a, i, o) \
    SAFE_USE_VALID_ID_ACCESSIBLE((a), (i), GetCurrentProcess(), (o))

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

BOOL ProcessAccessGetEffectiveUserID(LPPROCESS Process, U64* UserIDOut);
BOOL ProcessAccessIsKernelProcess(LPPROCESS Process);
BOOL ProcessAccessIsAdministratorProcess(LPPROCESS Process);
BOOL ProcessAccessIsSameUser(LPPROCESS Caller, LPPROCESS Target);
BOOL ProcessAccessCanTargetProcess(LPPROCESS Caller, LPPROCESS Target, BOOL AllowAdminOverride);
BOOL ProcessAccessCanTargetTask(LPPROCESS Caller, LPTASK TargetTask, BOOL AllowAdminOverride);
BOOL ProcessAccessCanTargetObject(LPPROCESS Caller, LPVOID Object, BOOL AllowAdminOverride);
BOOL ProcessAccessCanCurrentProcessTargetObject(LPVOID Object, BOOL AllowAdminOverride);

/************************************************************************/

#pragma pack(pop)

#endif
