
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


    Kernel event implementation for ISR-to-task signaling

\************************************************************************/

#include "KernelEvent.h"

#include "Arch.h"
#include "Kernel.h"

/************************************************************************/

LPKERNEL_EVENT CreateKernelEvent(void) {
    LPKERNEL_EVENT Event = (LPKERNEL_EVENT)CreateKernelObject(sizeof(KERNEL_EVENT), KOID_KERNELEVENT);

    SAFE_USE(Event) {
        Event->Signaled = FALSE;
        Event->SignalCount = 0;

        LPLIST EventList = GetEventList();
        SAFE_USE(EventList) {
            ListAddItem(EventList, Event);
        }

        return Event;
    }

    return NULL;
}

/************************************************************************/

BOOL DeleteKernelEvent(LPKERNEL_EVENT Event) {
    SAFE_USE_VALID_ID(Event, KOID_KERNELEVENT) {
        ReleaseKernelObject(Event);
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

void SignalKernelEvent(LPKERNEL_EVENT Event) {
    SAFE_USE_VALID_ID(Event, KOID_KERNELEVENT) {
        UINT Flags;

        SaveFlags(&Flags);
        DisableInterrupts();

        Event->SignalCount++;
        Event->Signaled = TRUE;

        RestoreFlags(&Flags);
    }
}

/************************************************************************/

void ResetKernelEvent(LPKERNEL_EVENT Event) {
    SAFE_USE_VALID_ID(Event, KOID_KERNELEVENT) {
        UINT Flags;

        SaveFlags(&Flags);
        DisableInterrupts();

        Event->Signaled = FALSE;

        RestoreFlags(&Flags);
    }
}

/************************************************************************/

BOOL KernelEventIsSignaled(LPKERNEL_EVENT Event) {
    SAFE_USE_VALID_ID(Event, KOID_KERNELEVENT) {
        return Event->Signaled ? TRUE : FALSE;
    }

    return FALSE;
}

/************************************************************************/

U32 KernelEventGetSignalCount(LPKERNEL_EVENT Event) {
    SAFE_USE_VALID_ID(Event, KOID_KERNELEVENT) {
        return Event->SignalCount;
    }

    return 0;
}
