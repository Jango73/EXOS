
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


    Deferred work dispatcher infrastructure

\************************************************************************/

#ifndef DEFERREDWORK_H_INCLUDED
#define DEFERREDWORK_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Driver.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

typedef void (*DEFERRED_WORK_CALLBACK)(LPVOID Context);
typedef void (*DEFERRED_WORK_POLL_CALLBACK)(LPVOID Context);

typedef struct tag_DEFERRED_WORK_REGISTRATION {
    DEFERRED_WORK_CALLBACK WorkCallback;
    DEFERRED_WORK_POLL_CALLBACK PollCallback;
    LPVOID Context;
    LPCSTR Name;
} DEFERRED_WORK_REGISTRATION, *LPDEFERRED_WORK_REGISTRATION;

#define DEFERRED_WORK_INVALID_HANDLE 0xFFFFFFFFU

/***************************************************************************/

BOOL InitializeDeferredWork(void);
void ShutdownDeferredWork(void);
U32 DeferredWorkRegister(const DEFERRED_WORK_REGISTRATION *Registration);
U32 DeferredWorkRegisterPollOnly(DEFERRED_WORK_POLL_CALLBACK PollCallback, LPVOID Context, LPCSTR Name);
void DeferredWorkUnregister(U32 Handle);
void DeferredWorkSignal(U32 Handle);
BOOL DeferredWorkIsPollingMode(void);
void DeferredWorkUpdateMode(void);

/***************************************************************************/

#pragma pack(pop)

#endif // DEFERREDWORK_H_INCLUDED
