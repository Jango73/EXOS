
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


    Cooldown helper

\************************************************************************/

#include "utils/Cooldown.h"

/************************************************************************/

/**
 * @brief Initialize a cooldown structure with the specified interval.
 *
 * @param Cooldown Pointer to the cooldown structure to initialize.
 * @param IntervalMS Minimum interval in milliseconds between two firings.
 * @return TRUE on success, FALSE if parameters are invalid.
 */
BOOL CooldownInit(LPCOOLDOWN Cooldown, U32 IntervalMS) {
    if (Cooldown == NULL) {
        return FALSE;
    }

    Cooldown->IntervalMS = IntervalMS;
    Cooldown->NextAllowedTick = 0;
    Cooldown->Initialized = TRUE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Update the cooldown interval without altering the schedule.
 *
 * @param Cooldown Pointer to the cooldown structure to update.
 * @param IntervalMS New interval in milliseconds.
 */
void CooldownSetInterval(LPCOOLDOWN Cooldown, U32 IntervalMS) {
    if (Cooldown == NULL) {
        return;
    }

    Cooldown->IntervalMS = IntervalMS;
}

/************************************************************************/

/**
 * @brief Attempt to arm the cooldown if it has expired.
 *
 * When the cooldown is expired (Now >= NextAllowedTick), this call arms it
 * by setting NextAllowedTick to Now + IntervalMS and returns TRUE.
 *
 * @param Cooldown Pointer to the cooldown structure.
 * @param Now Current system time in milliseconds.
 * @return TRUE if the cooldown was armed and the caller may proceed.
 */
BOOL CooldownTryArm(LPCOOLDOWN Cooldown, U32 Now) {
    if (Cooldown == NULL || Cooldown->Initialized == FALSE) {
        return FALSE;
    }

    if (Cooldown->NextAllowedTick > Now) {
        return FALSE;
    }

    Cooldown->NextAllowedTick = Now + Cooldown->IntervalMS;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Check whether the cooldown has expired.
 *
 * @param Cooldown Pointer to the cooldown structure.
 * @param Now Current system time in milliseconds.
 * @return TRUE if the cooldown is ready, FALSE otherwise.
 */
BOOL CooldownReady(LPCOOLDOWN Cooldown, U32 Now) {
    if (Cooldown == NULL || Cooldown->Initialized == FALSE) {
        return FALSE;
    }

    return (Cooldown->NextAllowedTick <= Now);
}

/************************************************************************/

/**
 * @brief Compute the remaining time before the cooldown expires.
 *
 * @param Cooldown Pointer to the cooldown structure.
 * @param Now Current system time in milliseconds.
 * @return Remaining time in milliseconds, or 0 when ready/invalid.
 */
U32 CooldownRemaining(LPCOOLDOWN Cooldown, U32 Now) {
    if (Cooldown == NULL || Cooldown->Initialized == FALSE) {
        return 0;
    }

    if (Cooldown->NextAllowedTick <= Now) {
        return 0;
    }

    return Cooldown->NextAllowedTick - Now;
}

/************************************************************************/
