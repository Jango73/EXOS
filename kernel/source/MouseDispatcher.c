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


    Mouse dispatcher

\************************************************************************/

#include "MouseDispatcher.h"

#include "Arch.h"
#include "Clock.h"
#include "Mouse.h"
#include "process/Task.h"
#include "User.h"
#include "utils/Cooldown.h"

/************************************************************************/

#define MOUSE_MOVE_COOLDOWN_MS 10

/************************************************************************/

typedef struct tag_MOUSE_DISPATCH_STATE {
    BOOL Initialized;
    MUTEX Mutex;
    COOLDOWN MoveCooldown;
    I32 PosX;
    I32 PosY;
    U32 Buttons;
} MOUSE_DISPATCH_STATE, *LPMOUSE_DISPATCH_STATE;

static MOUSE_DISPATCH_STATE g_MouseDispatch = {
    .Initialized = FALSE,
    .Mutex = EMPTY_MUTEX,
    .MoveCooldown = {0, 0, FALSE},
    .PosX = 0,
    .PosY = 0,
    .Buttons = 0};

/************************************************************************/

/**
 * @brief Initialize mouse dispatch state and cooldown.
 *
 * @return TRUE on success, FALSE if the structure could not be initialized.
 */
BOOL InitializeMouseDispatcher(void) {
    if (g_MouseDispatch.Initialized) {
        return TRUE;
    }

    InitMutex(&(g_MouseDispatch.Mutex));

    if (CooldownInit(&(g_MouseDispatch.MoveCooldown), MOUSE_MOVE_COOLDOWN_MS) == FALSE) {
        return FALSE;
    }

    g_MouseDispatch.PosX = 0;
    g_MouseDispatch.PosY = 0;
    g_MouseDispatch.Buttons = 0;
    g_MouseDispatch.Initialized = TRUE;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Process a raw mouse delta and broadcast throttled events.
 *
 * The first movement after any idle period is dispatched immediately.
 * Subsequent movement broadcasts are spaced by at least
 * MOUSE_MOVE_COOLDOWN_MS. Button transitions are always broadcast
 * immediately.
 *
 * @param DeltaX Signed X delta.
 * @param DeltaY Signed Y delta.
 * @param Buttons Current button bitmask (MB_*).
 */
void MouseDispatcherOnInput(I32 DeltaX, I32 DeltaY, U32 Buttons) {
    if (g_MouseDispatch.Initialized == FALSE) {
        return;
    }

    UINT Flags;
    I32 PosX = 0;
    I32 PosY = 0;
    U32 PreviousButtons;
    U32 DownButtons = 0;
    U32 UpButtons = 0;
    BOOL SendMove = FALSE;
    U32 Now = GetSystemTime();

    SaveFlags(&Flags);
    DisableInterrupts();

    g_MouseDispatch.PosX += DeltaX;
    g_MouseDispatch.PosY += DeltaY;

    PreviousButtons = g_MouseDispatch.Buttons;
    g_MouseDispatch.Buttons = Buttons;

    DownButtons = (~PreviousButtons) & Buttons;
    UpButtons = PreviousButtons & (~Buttons);

    if ((DeltaX != 0 || DeltaY != 0) && CooldownTryArm(&(g_MouseDispatch.MoveCooldown), Now)) {
        SendMove = TRUE;
        PosX = g_MouseDispatch.PosX;
        PosY = g_MouseDispatch.PosY;
    }

    RestoreFlags(&Flags);

    if (DownButtons) {
        EnqueueInputMessage(EWM_MOUSEDOWN, DownButtons, 0);
    }

    if (UpButtons) {
        EnqueueInputMessage(EWM_MOUSEUP, UpButtons, 0);
    }

    if (SendMove) {
        EnqueueInputMessage(EWM_MOUSEMOVE, UNSIGNED(PosX), UNSIGNED(PosY));
    }
}

/************************************************************************/
