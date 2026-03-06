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


    Failure backoff helper

\************************************************************************/

#ifndef FAILUREBACKOFF_H_INCLUDED
#define FAILUREBACKOFF_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "utils/Cooldown.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

typedef struct tag_FAILURE_BACKOFF {
    COOLDOWN Cooldown;
    U32 ConsecutiveFailures;
    U32 FailuresBeforeBackoff;
    U32 BackoffStepMS;
    U32 BackoffMaxMS;
    BOOL Initialized;
} FAILURE_BACKOFF, *LPFAILURE_BACKOFF;

/************************************************************************/

BOOL FailureBackoffInit(LPFAILURE_BACKOFF Backoff,
                        U32 FailuresBeforeBackoff,
                        U32 BackoffStepMS,
                        U32 BackoffMaxMS);
void FailureBackoffReset(LPFAILURE_BACKOFF Backoff);
BOOL FailureBackoffCanAttempt(LPFAILURE_BACKOFF Backoff, U32 Now, U32* RemainingMSOut);
void FailureBackoffOnFailure(LPFAILURE_BACKOFF Backoff, U32 Now, U32* AppliedBackoffMSOut);

/************************************************************************/

#pragma pack(pop)

#endif

