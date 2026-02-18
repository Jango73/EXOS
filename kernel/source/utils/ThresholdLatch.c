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

#include "utils/ThresholdLatch.h"

/************************************************************************/

/**
 * @brief Initialize a threshold latch.
 *
 * @param Latch Pointer to the latch to initialize.
 * @param Name Identifier for the threshold condition.
 * @param ThresholdMS Threshold in milliseconds.
 * @param StartTick Start time in milliseconds.
 * @return TRUE on success, FALSE on invalid parameters.
 */
BOOL ThresholdLatchInit(LPTHRESHOLD_LATCH Latch, LPCSTR Name, U32 ThresholdMS, U32 StartTick) {
    if (Latch == NULL) {
        return FALSE;
    }

    Latch->Name = Name;
    Latch->ThresholdMS = ThresholdMS;
    Latch->StartTick = StartTick;
    Latch->Triggered = FALSE;
    Latch->Initialized = TRUE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Update the threshold without resetting the latch.
 *
 * @param Latch Pointer to the latch.
 * @param ThresholdMS New threshold in milliseconds.
 */
void ThresholdLatchSetThreshold(LPTHRESHOLD_LATCH Latch, U32 ThresholdMS) {
    if (Latch == NULL) {
        return;
    }

    Latch->ThresholdMS = ThresholdMS;
}

/************************************************************************/

/**
 * @brief Reset the latch start time and clear the trigger.
 *
 * @param Latch Pointer to the latch.
 * @param StartTick New start time in milliseconds.
 */
void ThresholdLatchReset(LPTHRESHOLD_LATCH Latch, U32 StartTick) {
    if (Latch == NULL) {
        return;
    }

    Latch->StartTick = StartTick;
    Latch->Triggered = FALSE;
    Latch->Initialized = TRUE;
}

/************************************************************************/

/**
 * @brief Check whether the threshold has been exceeded.
 *
 * Returns TRUE once when the threshold is crossed. Subsequent calls
 * return FALSE until the latch is reset.
 *
 * @param Latch Pointer to the latch.
 * @param Now Current time in milliseconds.
 * @return TRUE once when the threshold is exceeded, otherwise FALSE.
 */
BOOL ThresholdLatchCheck(LPTHRESHOLD_LATCH Latch, U32 Now) {
    U32 Elapsed;

    if (Latch == NULL || Latch->Initialized == FALSE) {
        return FALSE;
    }

    if (Latch->Triggered == TRUE) {
        return FALSE;
    }

    if (Now < Latch->StartTick) {
        return FALSE;
    }

    Elapsed = Now - Latch->StartTick;
    if (Elapsed < Latch->ThresholdMS) {
        return FALSE;
    }

    Latch->Triggered = TRUE;
    return TRUE;
}

/************************************************************************/
