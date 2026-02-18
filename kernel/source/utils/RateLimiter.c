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

#include "utils/RateLimiter.h"

/************************************************************************/

/**
 * @brief Initialize a rate limiter.
 *
 * @param Limiter Pointer to the rate limiter.
 * @param ImmediateBudget Number of immediate triggers allowed.
 * @param IntervalMS Minimum interval in milliseconds for later triggers.
 * @return TRUE on success, FALSE on invalid parameters.
 */
BOOL RateLimiterInit(LPRATE_LIMITER Limiter, U32 ImmediateBudget, U32 IntervalMS) {
    if (Limiter == NULL) {
        return FALSE;
    }

    Limiter->ImmediateBudget = ImmediateBudget;
    Limiter->ImmediateCount = 0;
    Limiter->SuppressedCount = 0;
    Limiter->Initialized = FALSE;

    if (CooldownInit(&(Limiter->Cooldown), IntervalMS) == FALSE) {
        return FALSE;
    }

    Limiter->Initialized = TRUE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Reset a rate limiter counters while preserving configuration.
 *
 * @param Limiter Pointer to the rate limiter.
 */
void RateLimiterReset(LPRATE_LIMITER Limiter) {
    if (Limiter == NULL) {
        return;
    }

    Limiter->ImmediateCount = 0;
    Limiter->SuppressedCount = 0;
    Limiter->Cooldown.NextAllowedTick = 0;
}

/************************************************************************/

/**
 * @brief Check if the caller can trigger this event now.
 *
 * @param Limiter Pointer to the rate limiter.
 * @param Now Current system time in milliseconds.
 * @param SuppressedOut Optional output count suppressed since last trigger.
 * @return TRUE if event should trigger, FALSE otherwise.
 */
BOOL RateLimiterShouldTrigger(LPRATE_LIMITER Limiter, U32 Now, U32* SuppressedOut) {
    BOOL Trigger;

    if (Limiter == NULL || Limiter->Initialized == FALSE) {
        return TRUE;
    }

    Trigger = FALSE;
    if (Limiter->ImmediateCount < Limiter->ImmediateBudget) {
        Limiter->ImmediateCount++;
        Trigger = TRUE;
    } else if (CooldownTryArm(&(Limiter->Cooldown), Now)) {
        Trigger = TRUE;
    }

    if (Trigger) {
        if (SuppressedOut != NULL) {
            *SuppressedOut = Limiter->SuppressedCount;
        }
        Limiter->SuppressedCount = 0;
    } else {
        Limiter->SuppressedCount++;
    }

    return Trigger;
}

/************************************************************************/
