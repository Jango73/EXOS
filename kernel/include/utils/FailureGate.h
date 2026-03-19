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


    Failure gate helper

\************************************************************************/

#ifndef FAILUREGATE_H_INCLUDED
#define FAILUREGATE_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

typedef struct tag_FAILURE_GATE {
    U32 FailureThreshold;
    U32 ConsecutiveFailures;
    BOOL Blocked;
    BOOL Initialized;
} FAILURE_GATE, *LPFAILURE_GATE;

/************************************************************************/

BOOL FailureGateInit(LPFAILURE_GATE Gate, U32 FailureThreshold);
void FailureGateReset(LPFAILURE_GATE Gate);
BOOL FailureGateRecordFailure(LPFAILURE_GATE Gate);
void FailureGateRecordSuccess(LPFAILURE_GATE Gate);
BOOL FailureGateIsBlocked(LPFAILURE_GATE Gate);

/************************************************************************/

#pragma pack(pop)

#endif
