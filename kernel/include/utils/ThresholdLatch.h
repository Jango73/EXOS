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


    Threshold latch helper

\************************************************************************/

#ifndef THRESHOLDLATCH_H_INCLUDED
#define THRESHOLDLATCH_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

typedef struct tag_THRESHOLD_LATCH {
    LPCSTR Name;
    U32 ThresholdMS;
    U32 StartTick;
    BOOL Triggered;
    BOOL Initialized;
} THRESHOLD_LATCH, *LPTHRESHOLD_LATCH;

/************************************************************************/

BOOL ThresholdLatchInit(LPTHRESHOLD_LATCH Latch, LPCSTR Name, U32 ThresholdMS, U32 StartTick);
void ThresholdLatchSetThreshold(LPTHRESHOLD_LATCH Latch, U32 ThresholdMS);
void ThresholdLatchReset(LPTHRESHOLD_LATCH Latch, U32 StartTick);
BOOL ThresholdLatchCheck(LPTHRESHOLD_LATCH Latch, U32 Now);

/************************************************************************/

#pragma pack(pop)

#endif
