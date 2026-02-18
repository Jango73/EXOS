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


    Rate limiter helper

\************************************************************************/

#ifndef RATELIMITER_H_INCLUDED
#define RATELIMITER_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "utils/Cooldown.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

typedef struct tag_RATE_LIMITER {
    U32 ImmediateBudget;
    U32 ImmediateCount;
    U32 SuppressedCount;
    COOLDOWN Cooldown;
    BOOL Initialized;
} RATE_LIMITER, *LPRATE_LIMITER;

/************************************************************************/

BOOL RateLimiterInit(LPRATE_LIMITER Limiter, U32 ImmediateBudget, U32 IntervalMS);
void RateLimiterReset(LPRATE_LIMITER Limiter);
BOOL RateLimiterShouldTrigger(LPRATE_LIMITER Limiter, U32 Now, U32* SuppressedOut);

/************************************************************************/

#pragma pack(pop)

#endif
