
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


    Generic Hysteresis Module

\************************************************************************/

#ifndef HYSTERESIS_H_INCLUDED
#define HYSTERESIS_H_INCLUDED

#include "../include/Base.h"

/************************************************************************/
// Hysteresis structure

typedef struct tag_HYSTERESIS {
    U32 LowThreshold;       // Low threshold value
    U32 HighThreshold;      // High threshold value
    U32 CurrentValue;       // Current monitored value
    BOOL State;             // Current hysteresis state (FALSE=low, TRUE=high)
    BOOL TransitionPending; // Transition event pending flag
} HYSTERESIS, *LPHYSTERESIS;

/************************************************************************/

void Hysteresis_Initialize(LPHYSTERESIS This, U32 LowThreshold, U32 HighThreshold, U32 InitialValue);
BOOL Hysteresis_Update(LPHYSTERESIS This, U32 NewValue);
BOOL Hysteresis_GetState(LPHYSTERESIS This);
BOOL Hysteresis_IsTransitionPending(LPHYSTERESIS This);
void Hysteresis_ClearTransition(LPHYSTERESIS This);
U32 Hysteresis_GetValue(LPHYSTERESIS This);
void Hysteresis_Reset(LPHYSTERESIS This, U32 NewValue);

/************************************************************************/

#endif  // HYSTERESIS_H_INCLUDED
