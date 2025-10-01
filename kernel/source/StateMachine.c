
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


    Generic State Machine Implementation

\************************************************************************/

#include "../include/StateMachine.h"

/************************************************************************/

/**
 * @brief Initialize a state machine
 * @param SM Pointer to the state machine to initialize
 * @param Transitions Array of transition definitions
 * @param TransitionCount Number of transitions in the array
 * @param States Array of state definitions
 * @param StateCount Number of states in the array
 * @param InitialState Initial state to set
 * @param Context Application-specific context pointer
 */
void SM_Initialize(STATE_MACHINE* SM,
                   SM_TRANSITION* Transitions, U32 TransitionCount,
                   SM_STATE_DEFINITION* States, U32 StateCount,
                   SM_STATE InitialState, LPVOID Context)
{
    if (SM == NULL) return;

    SM->CurrentState = SM_INVALID_STATE;
    SM->PreviousState = SM_INVALID_STATE;
    SM->Transitions = Transitions;
    SM->TransitionCount = TransitionCount;
    SM->States = States;
    SM->StateCount = StateCount;
    SM->Context = Context;
    SM->Enabled = TRUE;
    SM->InTransition = FALSE;

    SM_ForceState(SM, InitialState);
}

/************************************************************************/

/**
 * @brief Destroy a state machine and cleanup resources
 * @param SM Pointer to the state machine to destroy
 */
void SM_Destroy(STATE_MACHINE* SM)
{
    if (SM == NULL) return;

    if (SM->CurrentState != SM_INVALID_STATE) {
        SM_STATE_DEFINITION* StateDef = NULL;
        for (U32 i = 0; i < SM->StateCount; i++) {
            if (SM->States[i].State == SM->CurrentState) {
                StateDef = &SM->States[i];
                break;
            }
        }
        if (StateDef && StateDef->OnExit) {
            StateDef->OnExit(SM);
        }
    }

    SM->CurrentState = SM_INVALID_STATE;
    SM->PreviousState = SM_INVALID_STATE;
    SM->Enabled = FALSE;
}

/************************************************************************/

/**
 * @brief Process an event in the state machine
 * @param SM Pointer to the state machine
 * @param Event The event to process
 * @param EventData Optional data associated with the event
 * @return TRUE if a transition occurred, FALSE otherwise
 */
BOOL SM_ProcessEvent(STATE_MACHINE* SM, SM_EVENT Event, LPVOID EventData)
{
    if (SM == NULL || !SM->Enabled || SM->InTransition) return FALSE;

    SM_TRANSITION* ValidTransition = NULL;

    for (U32 i = 0; i < SM->TransitionCount; i++) {
        SM_TRANSITION* Trans = &SM->Transitions[i];

        if (Trans->FromState == SM->CurrentState && Trans->Event == Event) {
            if (Trans->Condition == NULL || Trans->Condition(SM, EventData)) {
                ValidTransition = Trans;
                break;
            }
        }
    }

    if (ValidTransition == NULL) return FALSE;

    SM->InTransition = TRUE;

    SM_STATE_DEFINITION* CurrentStateDef = NULL;
    SM_STATE_DEFINITION* NextStateDef = NULL;

    for (U32 i = 0; i < SM->StateCount; i++) {
        if (SM->States[i].State == SM->CurrentState) {
            CurrentStateDef = &SM->States[i];
        }
        if (SM->States[i].State == ValidTransition->ToState) {
            NextStateDef = &SM->States[i];
        }
    }

    if (CurrentStateDef && CurrentStateDef->OnExit) {
        CurrentStateDef->OnExit(SM);
    }

    if (ValidTransition->Action) {
        ValidTransition->Action(SM, EventData);
    }

    SM->PreviousState = SM->CurrentState;
    SM->CurrentState = ValidTransition->ToState;

    if (NextStateDef && NextStateDef->OnEnter) {
        NextStateDef->OnEnter(SM);
    }

    SM->InTransition = FALSE;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Force the state machine to a specific state
 * @param SM Pointer to the state machine
 * @param NewState The state to transition to
 */
void SM_ForceState(STATE_MACHINE* SM, SM_STATE NewState)
{
    if (SM == NULL) return;

    if (SM->CurrentState != SM_INVALID_STATE) {
        SM_STATE_DEFINITION* CurrentStateDef = NULL;
        for (U32 i = 0; i < SM->StateCount; i++) {
            if (SM->States[i].State == SM->CurrentState) {
                CurrentStateDef = &SM->States[i];
                break;
            }
        }
        if (CurrentStateDef && CurrentStateDef->OnExit) {
            CurrentStateDef->OnExit(SM);
        }
    }

    SM->PreviousState = SM->CurrentState;
    SM->CurrentState = NewState;

    SM_STATE_DEFINITION* NewStateDef = NULL;
    for (U32 i = 0; i < SM->StateCount; i++) {
        if (SM->States[i].State == NewState) {
            NewStateDef = &SM->States[i];
            break;
        }
    }
    if (NewStateDef && NewStateDef->OnEnter) {
        NewStateDef->OnEnter(SM);
    }
}

/************************************************************************/

/**
 * @brief Get the current state of the state machine
 * @param SM Pointer to the state machine
 * @return Current state, or SM_INVALID_STATE if SM is NULL
 */
SM_STATE SM_GetCurrentState(STATE_MACHINE* SM)
{
    return (SM != NULL) ? SM->CurrentState : SM_INVALID_STATE;
}

/************************************************************************/

/**
 * @brief Get the previous state of the state machine
 * @param SM Pointer to the state machine
 * @return Previous state, or SM_INVALID_STATE if SM is NULL
 */
SM_STATE SM_GetPreviousState(STATE_MACHINE* SM)
{
    return (SM != NULL) ? SM->PreviousState : SM_INVALID_STATE;
}

/************************************************************************/

/**
 * @brief Check if the state machine is in a specific state
 * @param SM Pointer to the state machine
 * @param State The state to check
 * @return TRUE if in the specified state, FALSE otherwise
 */
BOOL SM_IsInState(STATE_MACHINE* SM, SM_STATE State)
{
    return (SM != NULL) ? (SM->CurrentState == State) : FALSE;
}

/************************************************************************/

/**
 * @brief Enable the state machine
 * @param SM Pointer to the state machine
 */
void SM_Enable(STATE_MACHINE* SM)
{
    SAFE_USE(SM) {
        SM->Enabled = TRUE;
    }
}

/************************************************************************/

/**
 * @brief Disable the state machine
 * @param SM Pointer to the state machine
 */
void SM_Disable(STATE_MACHINE* SM)
{
    SAFE_USE(SM) {
        SM->Enabled = FALSE;
    }
}

/************************************************************************/

/**
 * @brief Check if the state machine is enabled
 * @param SM Pointer to the state machine
 * @return TRUE if enabled, FALSE otherwise
 */
BOOL SM_IsEnabled(STATE_MACHINE* SM)
{
    return (SM != NULL) ? SM->Enabled : FALSE;
}

/************************************************************************/

/**
 * @brief Update the state machine (calls OnUpdate for current state)
 * @param SM Pointer to the state machine
 */
void SM_Update(STATE_MACHINE* SM)
{
    if (SM == NULL || !SM->Enabled || SM->InTransition) return;

    SM_STATE_DEFINITION* CurrentStateDef = NULL;
    for (U32 i = 0; i < SM->StateCount; i++) {
        if (SM->States[i].State == SM->CurrentState) {
            CurrentStateDef = &SM->States[i];
            break;
        }
    }

    if (CurrentStateDef && CurrentStateDef->OnUpdate) {
        CurrentStateDef->OnUpdate(SM);
    }
}

/************************************************************************/

/**
 * @brief Get the context pointer from the state machine
 * @param SM Pointer to the state machine
 * @return Context pointer, or NULL if SM is NULL
 */
LPVOID SM_GetContext(STATE_MACHINE* SM)
{
    return (SM != NULL) ? SM->Context : NULL;
}

/************************************************************************/

/**
 * @brief Set the context pointer for the state machine
 * @param SM Pointer to the state machine
 * @param Context New context pointer
 */
void SM_SetContext(STATE_MACHINE* SM, LPVOID Context)
{
    SAFE_USE(SM) {
        SM->Context = Context;
    }
}
