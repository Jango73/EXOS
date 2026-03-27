
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


    Process control

\************************************************************************/

#ifndef PROCESS_CONTROL_H_INCLUDED
#define PROCESS_CONTROL_H_INCLUDED

/************************************************************************/

#include "process/Process.h"

/************************************************************************/

#define ETM_INTERRUPT 0x20000001
#define ETM_PROCESS_KILL 0x20000002
#define ETM_PROCESS_TOGGLE_PAUSE 0x20000003

#define PROCESS_CONTROL_FLAG_INTERRUPT_PENDING 0x00000001

/************************************************************************/

BOOL ProcessControlIsMessage(U32 Message);
BOOL ProcessControlHandleMessage(LPPROCESS Process, U32 Message, U32 Param1, U32 Param2);

void ProcessControlRequestInterrupt(LPPROCESS Process);
BOOL ProcessControlIsInterruptRequested(LPPROCESS Process);
BOOL ProcessControlConsumeInterrupt(LPPROCESS Process);
BOOL ProcessControlCheckpoint(LPPROCESS Process);

BOOL ProcessControlSetPaused(LPPROCESS Process, BOOL Paused);
BOOL ProcessControlTogglePaused(LPPROCESS Process);
BOOL ProcessControlIsProcessPaused(LPPROCESS Process);
BOOL GetProcessSchedulerState(LPPROCESS Process, LPPROCESS_SCHEDULER_STATE State);

/************************************************************************/

#endif
