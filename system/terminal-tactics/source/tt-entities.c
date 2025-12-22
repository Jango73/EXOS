
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

#include "tt-entities.h"
#include "tt-path.h"
#include "tt-game.h"

/************************************************************************/

/* External helpers */
extern void RecalculateEnergy(void);

/************************************************************************/

const BUILDING_TYPE* GetBuildingTypeById(I32 typeId) {
    for (I32 i = 0; i < BUILDING_TYPE_COUNT; i++) {
        if (BuildingTypes[i].Id == typeId) return &BuildingTypes[i];
    }
    return NULL;
}

/************************************************************************/

const UNIT_TYPE* GetUnitTypeById(I32 typeId) {
    for (I32 i = 0; i < UNIT_TYPE_COUNT; i++) {
        if (UnitTypes[i].Id == typeId) return &UnitTypes[i];
    }
    return NULL;
}

/************************************************************************/

BOOL IsValidTeam(I32 team) {
    if (App.GameState == NULL) return FALSE;
    if (team < 0 || team >= App.GameState->TeamCount) return FALSE;
    if (team >= MAX_TEAMS) return FALSE;
    return TRUE;
}

/************************************************************************/

BUILDING** GetTeamBuildingHead(I32 team) {
    if (!IsValidTeam(team)) return NULL;
    return &App.GameState->TeamData[team].Buildings;
}

/************************************************************************/

UNIT** GetTeamUnitHead(I32 team) {
    if (!IsValidTeam(team)) return NULL;
    return &App.GameState->TeamData[team].Units;
}

/************************************************************************/

TEAM_RESOURCES* GetTeamResources(I32 team) {
    if (!IsValidTeam(team)) return NULL;
    return &App.GameState->TeamData[team].Resources;
}

/************************************************************************/

BOOL TeamHasConstructionYard(I32 team) {
    BUILDING* building = IsValidTeam(team) ? App.GameState->TeamData[team].Buildings : NULL;
    while (building != NULL) {
        if (building->TypeId == BUILDING_TYPE_CONSTRUCTION_YARD) {
            return TRUE;
        }
        building = building->Next;
    }
    return FALSE;
}

/************************************************************************/

BOOL IsTeamEliminated(I32 team) {
    TEAM_RESOURCES* res;

    if (!IsValidTeam(team)) return FALSE;
    if (!TeamHasConstructionYard(team)) return TRUE;

    res = GetTeamResources(team);
    if (res == NULL) return FALSE;
    if (res->Plasma > 0) return FALSE;

    if (FindTeamUnit(team, UNIT_TYPE_DRILLER) != NULL) return FALSE;
    return TRUE;
}

/************************************************************************/

BUILDING* FindTeamBuilding(I32 team, I32 typeId) {
    BUILDING* building = IsValidTeam(team) ? App.GameState->TeamData[team].Buildings : NULL;
    while (building != NULL) {
        if (building->TypeId == typeId) return building;
        building = building->Next;
    }
    return NULL;
}

/************************************************************************/

UNIT* FindTeamUnit(I32 team, I32 typeId) {
    UNIT* unit = IsValidTeam(team) ? App.GameState->TeamData[team].Units : NULL;
    while (unit != NULL) {
        if (unit->TypeId == typeId) return unit;
        unit = unit->Next;
    }
    return NULL;
}

/************************************************************************/

/// @brief Find a building by unique id within a team.
BUILDING* FindBuildingById(I32 team, I32 buildingId) {
    BUILDING* building = IsValidTeam(team) ? App.GameState->TeamData[team].Buildings : NULL;
    while (building != NULL) {
        if (building->Id == buildingId) return building;
        building = building->Next;
    }
    return NULL;
}

/************************************************************************/

/// @brief Find a unit by unique id within a team.
UNIT* FindUnitById(I32 team, I32 unitId) {
    UNIT* unit = IsValidTeam(team) ? App.GameState->TeamData[team].Units : NULL;
    while (unit != NULL) {
        if (unit->Id == unitId) return unit;
        unit = unit->Next;
    }
    return NULL;
}

/************************************************************************/

/// @brief Check if a map cell lies inside a unit footprint.
static BOOL IsPointInsideUnit(const UNIT* unit, const UNIT_TYPE* type, I32 x, I32 y) {
    if (unit == NULL || type == NULL || App.GameState == NULL) return FALSE;
    if (App.GameState->MapWidth <= 0 || App.GameState->MapHeight <= 0) return FALSE;

    I32 mapW = App.GameState->MapWidth;
    I32 mapH = App.GameState->MapHeight;

    for (I32 dy = 0; dy < type->Height; dy++) {
        for (I32 dx = 0; dx < type->Width; dx++) {
            I32 px = WrapCoord(unit->X, dx, mapW);
            I32 py = WrapCoord(unit->Y, dy, mapH);
            if (px == x && py == y) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/************************************************************************/

/// @brief Find the unit occupying a map cell, optionally filtering by team.
UNIT* FindUnitAtCell(I32 x, I32 y, I32 teamFilter) {
    if (App.GameState == NULL) return NULL;

    I32 teamCount = GetTeamCountSafe();
    if (teamCount <= 0) return NULL;

    for (I32 team = 0; team < teamCount; team++) {
        if (teamFilter >= 0 && team != teamFilter) continue;
        UNIT* unit = App.GameState->TeamData[team].Units;
        while (unit != NULL) {
            const UNIT_TYPE* type = GetUnitTypeById(unit->TypeId);
            if (type != NULL && IsPointInsideUnit(unit, type, x, y)) {
                return unit;
            }
            unit = unit->Next;
        }
    }
    return NULL;
}

/************************************************************************/

/// @brief Wrap a target coordinate to the nearest path on a torus map.
static I32 WrapNearest(I32 origin, I32 target, I32 size) {
    I32 delta = target - origin;
    if (size > 0) {
        if (delta > size / 2) delta -= size;
        else if (delta < -size / 2) delta += size;
    }
    return WrapCoord(origin, delta, size);
}

/************************************************************************/

/// @brief Issue a move order to a unit and reset its path.
void SetUnitMoveTarget(UNIT* unit, I32 targetX, I32 targetY) {
    if (App.GameState == NULL || unit == NULL) return;
    if (App.GameState->MapWidth <= 0 || App.GameState->MapHeight <= 0) return;

    unit->TargetX = WrapNearest(unit->X, targetX, App.GameState->MapWidth);
    unit->TargetY = WrapNearest(unit->Y, targetY, App.GameState->MapHeight);
    unit->IsMoving = (unit->TargetX != unit->X || unit->TargetY != unit->Y);
    ClearUnitPath(unit);
    if (unit->IsMoving) {
        unit->MoveProgress = 0;
    }
}

/************************************************************************/

/// @brief Reset a unit to Idle state and clear its autonomous orders.
void SetUnitStateIdle(UNIT* unit) {
    if (unit == NULL) return;
    unit->State = UNIT_STATE_IDLE;
    unit->EscortUnitId = 0;
    unit->EscortUnitTeam = -1;
    unit->StateTargetX = UNIT_STATE_TARGET_NONE;
    unit->StateTargetY = UNIT_STATE_TARGET_NONE;
    unit->LastStateUpdateTime = 0;
    unit->IsMoving = FALSE;
    unit->MoveProgress = 0;
    ClearUnitPath(unit);
}

/************************************************************************/

/// @brief Set a unit to escort a target unit.
void SetUnitStateEscort(UNIT* unit, I32 targetTeam, I32 targetUnitId) {
    if (unit == NULL) return;
    unit->State = UNIT_STATE_ESCORT;
    unit->EscortUnitId = targetUnitId;
    unit->EscortUnitTeam = targetTeam;
    unit->StateTargetX = UNIT_STATE_TARGET_NONE;
    unit->StateTargetY = UNIT_STATE_TARGET_NONE;
    unit->LastStateUpdateTime = 0;
    unit->IsMoving = FALSE;
    unit->MoveProgress = 0;
    ClearUnitPath(unit);
}

/************************************************************************/

/// @brief Set a unit to explore toward a target location.
void SetUnitStateExplore(UNIT* unit, I32 targetX, I32 targetY) {
    if (unit == NULL) return;
    unit->State = UNIT_STATE_EXPLORE;
    unit->EscortUnitId = 0;
    unit->EscortUnitTeam = -1;
    unit->StateTargetX = targetX;
    unit->StateTargetY = targetY;
    unit->LastStateUpdateTime = 0;
    unit->IsMoving = FALSE;
    unit->MoveProgress = 0;
    ClearUnitPath(unit);
}

/************************************************************************/

BUILDING* CreateBuilding(I32 typeId, I32 team, I32 x, I32 y) {
    const BUILDING_TYPE* type = GetBuildingTypeById(typeId);
    BUILDING* node;

    if (type == NULL || !IsValidTeam(team)) return NULL;

    node = (BUILDING*)malloc(sizeof(BUILDING));
    if (node == NULL) return NULL;

    node->Id = (App.GameState != NULL) ? App.GameState->NextBuildingId++ : 0;
    node->TypeId = type->Id;
    node->X = x;
    node->Y = y;
    node->Hp = type->MaxHp;
    node->Team = team;
    node->Level = 1;
    node->BuildTimeRemaining = 0;
    node->UnderConstruction = FALSE;
    node->BuildQueueCount = 0;
    node->UnitQueueCount = 0;
    node->LastDamageTime = 0;
    node->Next = NULL;

    SetBuildingOccupancy(node, TRUE);
    return node;
}

/************************************************************************/

UNIT* CreateUnit(I32 typeId, I32 team, I32 x, I32 y) {
    const UNIT_TYPE* type = GetUnitTypeById(typeId);
    UNIT* node;
    I32 maxUnits;

    if (type == NULL || !IsValidTeam(team)) return NULL;
    if (App.GameState == NULL) return NULL;

    maxUnits = GetMaxUnitsForMap(App.GameState->MapWidth, App.GameState->MapHeight);
    {
        U32 maxUnitsU = (maxUnits > 0) ? (U32)maxUnits : 0;
        if (CountUnitsAllTeams() >= maxUnitsU) {
            return NULL;
        }
    }

    node = (UNIT*)malloc(sizeof(UNIT));
    if (node == NULL) return NULL;

    node->Id = (App.GameState != NULL) ? App.GameState->NextUnitId++ : 0;
    node->TypeId = type->Id;
    node->X = x;
    node->Y = y;
    node->Hp = type->MaxHp;
    node->Team = team;
    node->State = UNIT_STATE_IDLE;
    node->EscortUnitId = 0;
    node->EscortUnitTeam = -1;
    node->StateTargetX = UNIT_STATE_TARGET_NONE;
    node->StateTargetY = UNIT_STATE_TARGET_NONE;
    node->IsMoving = FALSE;
    node->TargetX = x;
    node->TargetY = y;
    node->IsSelected = FALSE;
    node->LastAttackTime = 0;
    node->LastDamageTime = 0;
    node->LastHarvestTime = 0;
    node->LastStateUpdateTime = 0;
    node->MoveProgress = 0;
    node->PathHead = NULL;
    node->PathTail = NULL;
    node->PathTargetX = x;
    node->PathTargetY = y;
    node->Next = NULL;

    SetUnitOccupancy(node, TRUE);
    return node;
}

/************************************************************************/

void RemoveTeamEntities(I32 team) {
    if (!IsValidTeam(team)) return;

    BUILDING* building = App.GameState->TeamData[team].Buildings;
    while (building != NULL) {
        BUILDING* next = building->Next;
        SetBuildingOccupancy(building, FALSE);
        free(building);
        building = next;
    }
    App.GameState->TeamData[team].Buildings = NULL;

    UNIT* unit = App.GameState->TeamData[team].Units;
    while (unit != NULL) {
        UNIT* next = unit->Next;
        SetUnitOccupancy(unit, FALSE);
        ClearUnitPath(unit);
        free(unit);
        unit = next;
    }
    App.GameState->TeamData[team].Units = NULL;
    App.GameState->FogDirty = TRUE;
}

/************************************************************************/

void RemoveUnitFromTeamList(I32 team, UNIT* target) {
    UNIT** head;
    UNIT* current;
    UNIT* prev = NULL;

    if (target == NULL) return;
    head = GetTeamUnitHead(team);
    if (head == NULL) return;

    current = *head;
    while (current != NULL) {
        if (current == target) {
            if (prev == NULL) {
                *head = current->Next;
            } else {
                prev->Next = current->Next;
            }
            SetUnitOccupancy(current, FALSE);
            if (App.GameState->SelectedUnit == current) {
                App.GameState->SelectedUnit = NULL;
            }
            ClearUnitPath(current);
            free(current);
            App.GameState->FogDirty = TRUE;
            return;
        }
        prev = current;
        current = current->Next;
    }
}

/************************************************************************/

void RemoveBuildingFromTeamList(I32 team, BUILDING* target) {
    BUILDING** head;
    BUILDING* current;
    BUILDING* prev = NULL;

    if (target == NULL) return;
    head = GetTeamBuildingHead(team);
    if (head == NULL) return;

    current = *head;
    while (current != NULL) {
        if (current == target) {
            if (prev == NULL) {
                *head = current->Next;
            } else {
                prev->Next = current->Next;
            }
            SetBuildingOccupancy(current, FALSE);
            if (App.GameState->SelectedBuilding == current) {
                App.GameState->SelectedBuilding = NULL;
            }
            free(current);
            App.GameState->FogDirty = TRUE;
            RecalculateEnergy();
            return;
        }
        prev = current;
        current = current->Next;
    }
}
