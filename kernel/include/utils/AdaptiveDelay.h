
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


    Adaptive Delay System with Exponential Backoff

\************************************************************************/

#ifndef ADAPTIVE_DELAY_H_INCLUDED
#define ADAPTIVE_DELAY_H_INCLUDED

/************************************************************************/

#include "../Base.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/
// Constants

#define ADAPTIVE_DELAY_MIN_TICKS        10      // Minimum delay: 10 ticks
#define ADAPTIVE_DELAY_MAX_TICKS        1000    // Maximum delay: 1000 ticks
#define ADAPTIVE_DELAY_BACKOFF_FACTOR   2       // Exponential backoff factor
#define ADAPTIVE_DELAY_MAX_ATTEMPTS     10      // Maximum retry attempts

/************************************************************************/
// Structures

typedef struct tag_ADAPTIVE_DELAY_STATE {
    U32 CurrentDelay;       // Current delay in ticks
    U32 AttemptCount;       // Number of attempts made
    U32 MinDelay;           // Minimum delay
    U32 MaxDelay;           // Maximum delay
    U32 BackoffFactor;      // Exponential backoff factor
    U32 MaxAttempts;        // Maximum number of attempts
    BOOL IsActive;          // Whether delay is currently active
} ADAPTIVE_DELAY_STATE, *LPADAPTIVE_DELAY_STATE;

/************************************************************************/
// Functions

void AdaptiveDelay_Initialize(LPADAPTIVE_DELAY_STATE State);
void AdaptiveDelay_Reset(LPADAPTIVE_DELAY_STATE State);
U32 AdaptiveDelay_GetNextDelay(LPADAPTIVE_DELAY_STATE State);
BOOL AdaptiveDelay_ShouldContinue(LPADAPTIVE_DELAY_STATE State);
void AdaptiveDelay_OnSuccess(LPADAPTIVE_DELAY_STATE State);
void AdaptiveDelay_OnFailure(LPADAPTIVE_DELAY_STATE State);

#pragma pack(pop)

#endif  // ADAPTIVE_DELAY_H_INCLUDED
