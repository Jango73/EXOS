
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
