/************************************************************************\

    EXOS Kernel

    Kernel event primitive used to bridge interrupts and tasks

\************************************************************************/

#ifndef KERNELEVENT_H_INCLUDED
#define KERNELEVENT_H_INCLUDED

#include "List.h"
#include "ID.h"

/************************************************************************/

typedef struct tag_KERNEL_EVENT {
    LISTNODE_FIELDS
    volatile BOOL Signaled;
    volatile U32 SignalCount;
} KERNEL_EVENT, *LPKERNEL_EVENT;

/************************************************************************/

LPKERNEL_EVENT CreateKernelEvent(void);
BOOL DeleteKernelEvent(LPKERNEL_EVENT Event);
void SignalKernelEvent(LPKERNEL_EVENT Event);
void ResetKernelEvent(LPKERNEL_EVENT Event);
BOOL KernelEventIsSignaled(LPKERNEL_EVENT Event);
U32 KernelEventGetSignalCount(LPKERNEL_EVENT Event);

#endif // KERNELEVENT_H_INCLUDED
