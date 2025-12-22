
/************************************************************************\

    EXOS Sample program - Terminal Tactics
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


    Terminal Tactics - Terminal Strategy Game

\************************************************************************/

#include "tt-commands.h"
#include "tt-map.h"
#include "tt-render.h"
#include "tt-entities.h"

/************************************************************************/

static const char* OrderAckPhrases[] = {
    "Yes, sir",
    "Acknowledged",
    "Moving on",
    "On my way",
    "Roger that",
    "I'll be there in no time, sir"
};

/************************************************************************/

static const char* EscortAckPhrases[] = {
    "Yes sir, escorting %s",
    "Roger, guarding %s",
    "Acknowledged, covering %s",
    "Sir, protecting %s",
    "I'm staying with %s"
};

/************************************************************************/

void CenterViewportOn(I32 x, I32 y) {
    if (App.GameState == NULL) return;
    if (App.GameState->MapWidth <= 0 || App.GameState->MapHeight <= 0) return;

    I32 desiredX = WrapCoord(0, x - VIEWPORT_WIDTH / 2, App.GameState->MapWidth);
    I32 desiredY = WrapCoord(0, y - VIEWPORT_HEIGHT / 2, App.GameState->MapHeight);

    App.GameState->ViewportPos.X = desiredX;
    App.GameState->ViewportPos.Y = desiredY;
    App.Render.BorderDrawn = FALSE;
}

/************************************************************************/

void CancelUnitCommand(void) {
    if (App.GameState == NULL) return;
    App.GameState->IsCommandMode = FALSE;
    App.GameState->CommandType = COMMAND_NONE;
}

/************************************************************************/

void MoveCommandCursor(I32 dx, I32 dy) {
    if (App.GameState == NULL) return;
    if (App.GameState->MapWidth <= 0 || App.GameState->MapHeight <= 0) return;

    App.GameState->CommandX = WrapCoord(App.GameState->CommandX, dx, App.GameState->MapWidth);
    App.GameState->CommandY = WrapCoord(App.GameState->CommandY, dy, App.GameState->MapHeight);

    /* Keep cursor visible, wrapping around map edges */
    if (!GetScreenPosition(App.GameState->CommandX, App.GameState->CommandY, 1, 1, NULL, NULL)) {
        CenterViewportOn(App.GameState->CommandX, App.GameState->CommandY);
    }
}

/************************************************************************/

void StartUnitCommand(I32 commandType) {
    if (App.GameState == NULL) return;
    if (App.GameState->SelectedUnit == NULL) return;

    const UNIT_TYPE* unitType = GetUnitTypeById(App.GameState->SelectedUnit->TypeId);
    if (unitType == NULL) return;

    App.GameState->IsCommandMode = TRUE;
    App.GameState->CommandType = commandType;
    App.GameState->CommandX = App.GameState->SelectedUnit->X;
    App.GameState->CommandY = App.GameState->SelectedUnit->Y;
    if (!GetScreenPosition(App.GameState->SelectedUnit->X, App.GameState->SelectedUnit->Y,
                           unitType->Width, unitType->Height, NULL, NULL)) {
        CenterViewportOn(App.GameState->CommandX, App.GameState->CommandY);
    }
}

/************************************************************************/

void ConfirmUnitCommand(void) {
    if (App.GameState == NULL) return;
    if (!App.GameState->IsCommandMode) return;

    UNIT* unit = App.GameState->SelectedUnit;
    if (unit != NULL) {
        if (App.GameState->CommandType == COMMAND_ESCORT) {
            UNIT* target = FindUnitAtCell(App.GameState->CommandX, App.GameState->CommandY, unit->Team);
            if (target != NULL && target != unit) {
                const UNIT_TYPE* targetType = GetUnitTypeById(target->TypeId);
                const char* targetName = (targetType != NULL) ? targetType->Name : "unit";
                U32 idx = RandomIndex((U32)(sizeof(EscortAckPhrases) / sizeof(EscortAckPhrases[0])));
                char msg[SCREEN_WIDTH + 1];

                SetUnitStateEscort(unit, target->Team, target->Id);
                snprintf(msg, sizeof(msg), EscortAckPhrases[idx], targetName);
                SetStatus(msg);
            } else {
                SetStatus("No friendly unit to escort");
            }
        } else {
            SetUnitStateIdle(unit);
            SetUnitMoveTarget(unit, App.GameState->CommandX, App.GameState->CommandY);
            if (unit->IsMoving) {
                U32 idx = RandomIndex((U32)(sizeof(OrderAckPhrases) / sizeof(OrderAckPhrases[0])));
                SetStatus(OrderAckPhrases[idx]);
            }
        }
    }

    App.GameState->IsCommandMode = FALSE;
    App.GameState->CommandType = COMMAND_NONE;
}
