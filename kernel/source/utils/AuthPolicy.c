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


    Authentication attempt policy helper

\************************************************************************/

#include "utils/AuthPolicy.h"

/************************************************************************/

/**
 * @brief Initialize one authentication policy state.
 * @param Policy Policy storage.
 * @param FailureDelayMS Cooldown applied after one failed attempt.
 * @param LockoutThreshold Consecutive failures before temporary lockout.
 * @return TRUE on success.
 */
BOOL AuthPolicyInit(LPAUTH_POLICY Policy, U32 FailureDelayMS, U32 LockoutThreshold) {
    if (Policy == NULL || FailureDelayMS == 0 || LockoutThreshold == 0) {
        return FALSE;
    }

    if (FailureGateInit(&(Policy->FailureGate), LockoutThreshold) == FALSE) {
        return FALSE;
    }

    if (CooldownInit(&(Policy->FailureCooldown), FailureDelayMS) == FALSE) {
        return FALSE;
    }

    Policy->LockoutEndTime = 0;
    Policy->Initialized = TRUE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Reset one authentication policy to its initial state.
 * @param Policy Policy storage.
 */
void AuthPolicyReset(LPAUTH_POLICY Policy) {
    if (Policy == NULL || Policy->Initialized == FALSE) {
        return;
    }

    FailureGateReset(&(Policy->FailureGate));
    Policy->FailureCooldown.NextAllowedTick = 0;
    Policy->LockoutEndTime = 0;
}

/************************************************************************/

/**
 * @brief Query whether one authentication attempt is allowed at one time.
 * @param Policy Policy storage.
 * @param Now Current system time in milliseconds.
 * @param WaitRemainingOut Receives remaining wait time when blocked.
 * @return TRUE when one attempt may proceed.
 */
BOOL AuthPolicyCanAttempt(LPAUTH_POLICY Policy, UINT Now, UINT* WaitRemainingOut) {
    if (WaitRemainingOut != NULL) {
        *WaitRemainingOut = 0;
    }

    if (Policy == NULL || Policy->Initialized == FALSE) {
        return TRUE;
    }

    if (Policy->LockoutEndTime > Now) {
        if (WaitRemainingOut != NULL) {
            *WaitRemainingOut = Policy->LockoutEndTime - Now;
        }
        return FALSE;
    }

    if (Policy->LockoutEndTime != 0 && FailureGateIsBlocked(&(Policy->FailureGate))) {
        FailureGateReset(&(Policy->FailureGate));
        Policy->LockoutEndTime = 0;
    }

    if (!CooldownReady(&(Policy->FailureCooldown), (U32)Now)) {
        if (WaitRemainingOut != NULL) {
            *WaitRemainingOut = CooldownRemaining(&(Policy->FailureCooldown), (U32)Now);
        }
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Record one failed authentication attempt.
 * @param Policy Policy storage.
 * @param Now Current system time in milliseconds.
 * @param WaitRemainingOut Receives remaining wait time after the failure.
 * @return TRUE when the failure triggered a temporary lockout.
 */
BOOL AuthPolicyRecordFailure(LPAUTH_POLICY Policy, UINT Now, UINT* WaitRemainingOut) {
    BOOL IsLocked;

    if (WaitRemainingOut != NULL) {
        *WaitRemainingOut = 0;
    }

    if (Policy == NULL || Policy->Initialized == FALSE) {
        return FALSE;
    }

    Policy->FailureCooldown.NextAllowedTick = (U32)Now + Policy->FailureCooldown.IntervalMS;
    IsLocked = FailureGateRecordFailure(&(Policy->FailureGate));

    if (IsLocked) {
        Policy->LockoutEndTime = Now + AUTH_POLICY_LOCKOUT_DURATION_MS;
        if (WaitRemainingOut != NULL) {
            *WaitRemainingOut = AUTH_POLICY_LOCKOUT_DURATION_MS;
        }
        return TRUE;
    }

    if (WaitRemainingOut != NULL) {
        *WaitRemainingOut = Policy->FailureCooldown.IntervalMS;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Record one successful authentication attempt.
 * @param Policy Policy storage.
 */
void AuthPolicyRecordSuccess(LPAUTH_POLICY Policy) {
    AuthPolicyReset(Policy);
}
