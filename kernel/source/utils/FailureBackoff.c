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

#include "utils/FailureBackoff.h"

/************************************************************************/

static U32 FailureBackoffClamp(U32 Value, U32 Maximum);

/************************************************************************/

/**
 * @brief Initialize one failure-backoff state object.
 * @param Backoff Backoff state pointer.
 * @param FailuresBeforeBackoff Failure count required before arming cooldown.
 * @param BackoffStepMS Backoff growth step in milliseconds.
 * @param BackoffMaxMS Maximum cooldown in milliseconds.
 * @return TRUE on success.
 */
BOOL FailureBackoffInit(LPFAILURE_BACKOFF Backoff,
                        U32 FailuresBeforeBackoff,
                        U32 BackoffStepMS,
                        U32 BackoffMaxMS) {
    if (Backoff == NULL || FailuresBeforeBackoff == 0 || BackoffStepMS == 0 || BackoffMaxMS == 0) {
        return FALSE;
    }

    if (!CooldownInit(&Backoff->Cooldown, BackoffStepMS)) {
        return FALSE;
    }

    Backoff->ConsecutiveFailures = 0;
    Backoff->FailuresBeforeBackoff = FailuresBeforeBackoff;
    Backoff->BackoffStepMS = BackoffStepMS;
    Backoff->BackoffMaxMS = BackoffMaxMS;
    Backoff->Initialized = TRUE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Reset one failure-backoff state after successful progress.
 * @param Backoff Backoff state pointer.
 */
void FailureBackoffReset(LPFAILURE_BACKOFF Backoff) {
    if (Backoff == NULL || Backoff->Initialized == FALSE) {
        return;
    }

    Backoff->ConsecutiveFailures = 0;
    Backoff->Cooldown.IntervalMS = Backoff->BackoffStepMS;
    Backoff->Cooldown.NextAllowedTick = 0;
}

/************************************************************************/

/**
 * @brief Check whether one operation attempt is allowed now.
 * @param Backoff Backoff state pointer.
 * @param Now Current time in milliseconds.
 * @param RemainingMSOut Remaining cooldown delay when blocked.
 * @return TRUE when caller may attempt now.
 */
BOOL FailureBackoffCanAttempt(LPFAILURE_BACKOFF Backoff, U32 Now, U32* RemainingMSOut) {
    if (RemainingMSOut != NULL) {
        *RemainingMSOut = 0;
    }

    if (Backoff == NULL || Backoff->Initialized == FALSE) {
        return FALSE;
    }

    if (Backoff->ConsecutiveFailures < Backoff->FailuresBeforeBackoff) {
        return TRUE;
    }

    if (CooldownReady(&Backoff->Cooldown, Now)) {
        return TRUE;
    }

    if (RemainingMSOut != NULL) {
        *RemainingMSOut = CooldownRemaining(&Backoff->Cooldown, Now);
    }
    return FALSE;
}

/************************************************************************/

/**
 * @brief Record one failure and arm/extend cooldown when threshold is met.
 * @param Backoff Backoff state pointer.
 * @param Now Current time in milliseconds.
 * @param AppliedBackoffMSOut Applied cooldown interval.
 */
void FailureBackoffOnFailure(LPFAILURE_BACKOFF Backoff, U32 Now, U32* AppliedBackoffMSOut) {
    U32 Stage = 0;
    U32 Interval = 0;
    U32 MaxStages;

    if (AppliedBackoffMSOut != NULL) {
        *AppliedBackoffMSOut = 0;
    }

    if (Backoff == NULL || Backoff->Initialized == FALSE) {
        return;
    }

    if (Backoff->ConsecutiveFailures < MAX_U32) {
        Backoff->ConsecutiveFailures++;
    }

    if (Backoff->ConsecutiveFailures < Backoff->FailuresBeforeBackoff) {
        return;
    }

    Stage = (Backoff->ConsecutiveFailures - Backoff->FailuresBeforeBackoff) + 1;
    MaxStages = Backoff->BackoffMaxMS / Backoff->BackoffStepMS;
    if (MaxStages == 0 || Stage > MaxStages) {
        Interval = Backoff->BackoffMaxMS;
    } else {
        Interval = FailureBackoffClamp(Stage * Backoff->BackoffStepMS, Backoff->BackoffMaxMS);
    }
    if (Interval == 0) {
        Interval = Backoff->BackoffStepMS;
    }

    CooldownSetInterval(&Backoff->Cooldown, Interval);
    Backoff->Cooldown.NextAllowedTick = Now;
    (void)CooldownTryArm(&Backoff->Cooldown, Now);

    if (AppliedBackoffMSOut != NULL) {
        *AppliedBackoffMSOut = Interval;
    }
}

/************************************************************************/

/**
 * @brief Clamp one value to a maximum.
 * @param Value Input value.
 * @param Maximum Maximum accepted value.
 * @return Clamped value.
 */
static U32 FailureBackoffClamp(U32 Value, U32 Maximum) {
    if (Value > Maximum) {
        return Maximum;
    }
    return Value;
}

/************************************************************************/
