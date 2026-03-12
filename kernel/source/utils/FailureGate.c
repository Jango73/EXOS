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

#include "utils/FailureGate.h"

/************************************************************************/

/**
 * @brief Initialize a failure gate.
 * @param Gate Gate storage.
 * @param FailureThreshold Number of consecutive failures before blocking.
 * @return TRUE on success.
 */
BOOL FailureGateInit(LPFAILURE_GATE Gate, U32 FailureThreshold) {
    if (Gate == NULL || FailureThreshold == 0) {
        return FALSE;
    }

    Gate->FailureThreshold = FailureThreshold;
    Gate->ConsecutiveFailures = 0;
    Gate->Blocked = FALSE;
    Gate->Initialized = TRUE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Clear failure count and unblock the gate.
 * @param Gate Gate storage.
 */
void FailureGateReset(LPFAILURE_GATE Gate) {
    if (Gate == NULL) {
        return;
    }

    Gate->ConsecutiveFailures = 0;
    Gate->Blocked = FALSE;
}

/************************************************************************/

/**
 * @brief Record one failure and update blocked state.
 * @param Gate Gate storage.
 * @return TRUE when the gate is blocked after the update.
 */
BOOL FailureGateRecordFailure(LPFAILURE_GATE Gate) {
    if (Gate == NULL || Gate->Initialized == FALSE) {
        return FALSE;
    }

    if (Gate->Blocked == TRUE) {
        return TRUE;
    }

    if (Gate->ConsecutiveFailures < MAX_U32) {
        Gate->ConsecutiveFailures++;
    }

    if (Gate->ConsecutiveFailures >= Gate->FailureThreshold) {
        Gate->Blocked = TRUE;
    }

    return Gate->Blocked;
}

/************************************************************************/

/**
 * @brief Record a success and clear the gate state.
 * @param Gate Gate storage.
 */
void FailureGateRecordSuccess(LPFAILURE_GATE Gate) {
    FailureGateReset(Gate);
}

/************************************************************************/

/**
 * @brief Check whether the gate is blocked.
 * @param Gate Gate storage.
 * @return TRUE when blocked.
 */
BOOL FailureGateIsBlocked(LPFAILURE_GATE Gate) {
    if (Gate == NULL || Gate->Initialized == FALSE) {
        return FALSE;
    }

    return Gate->Blocked;
}

/************************************************************************/
