/************************************************************************\

    EXOS Kernel

    Deferred work dispatcher infrastructure

\************************************************************************/

#ifndef DEFERREDWORK_H_INCLUDED
#define DEFERREDWORK_H_INCLUDED

/***************************************************************************/

#include "Base.h"

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

#endif // DEFERREDWORK_H_INCLUDED
