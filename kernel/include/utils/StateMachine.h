
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


    Generic State Machine Infrastructure

\************************************************************************/

#ifndef STATEMACHINE_H_INCLUDED
#define STATEMACHINE_H_INCLUDED

#include "Base.h"

/************************************************************************/

typedef U32 SM_STATE;
typedef U32 SM_EVENT;

#define SM_INVALID_STATE 0xFFFFFFFF
#define SM_INVALID_EVENT 0xFFFFFFFF

/************************************************************************/

typedef struct tag_STATE_MACHINE STATE_MACHINE;

typedef struct {
    SM_STATE FromState;
    SM_EVENT Event;
    SM_STATE ToState;
    BOOL (*Condition)(STATE_MACHINE* SM, LPVOID EventData);
    void (*Action)(STATE_MACHINE* SM, LPVOID EventData);
} SM_TRANSITION;

typedef struct tag_SM_STATE_DEFINITION {
    SM_STATE State;
    void (*OnEnter)(STATE_MACHINE* SM);
    void (*OnExit)(STATE_MACHINE* SM);
    void (*OnUpdate)(STATE_MACHINE* SM);
} SM_STATE_DEFINITION;

typedef struct tag_STATE_MACHINE {
    SM_STATE CurrentState;
    SM_STATE PreviousState;

    SM_TRANSITION* Transitions;
    UINT TransitionCount;

    SM_STATE_DEFINITION* States;
    UINT StateCount;

    LPVOID Context;

    BOOL Enabled;
    BOOL InTransition;
} STATE_MACHINE;

/************************************************************************/

void SM_Initialize(STATE_MACHINE* SM,
                   SM_TRANSITION* Transitions, U32 TransitionCount,
                   SM_STATE_DEFINITION* States, U32 StateCount,
                   SM_STATE InitialState, LPVOID Context);

void SM_Destroy(STATE_MACHINE* SM);

BOOL SM_ProcessEvent(STATE_MACHINE* SM, SM_EVENT Event, LPVOID EventData);

void SM_ForceState(STATE_MACHINE* SM, SM_STATE NewState);

SM_STATE SM_GetCurrentState(STATE_MACHINE* SM);
SM_STATE SM_GetPreviousState(STATE_MACHINE* SM);

BOOL SM_IsInState(STATE_MACHINE* SM, SM_STATE State);

void SM_Enable(STATE_MACHINE* SM);
void SM_Disable(STATE_MACHINE* SM);
BOOL SM_IsEnabled(STATE_MACHINE* SM);

void SM_Update(STATE_MACHINE* SM);

LPVOID SM_GetContext(STATE_MACHINE* SM);
void SM_SetContext(STATE_MACHINE* SM, LPVOID Context);

/************************************************************************/

#endif /* STATEMACHINE_H_INCLUDED */
