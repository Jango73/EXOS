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

#include "tt-ai-internal.h"
#include "tt-entities.h"
#include "tt-game.h"
#include "tt-map.h"

/************************************************************************/

/// @brief Apply the driller escort behavior when needed.
BOOL ActionUpdateDrillerEscort(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (Context->Driller == NULL) return FALSE;

    return AssignDrillerEscorts(Context->Team, Context->Driller, Context->DesiredEscortForce);
}

/************************************************************************/

/// @brief Queue a factory so the AI can reach the driller target.
BOOL ActionQueueFactoryForDrillers(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    return AiQueueBuildingForTeam(Context->Team, BUILDING_TYPE_FACTORY);
}

/************************************************************************/

/// @brief Queue a barracks.
BOOL ActionQueueBarracks(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    return AiQueueBuildingForTeam(Context->Team, BUILDING_TYPE_BARRACKS);
}

/************************************************************************/

/// @brief Queue a power plant.
BOOL ActionQueuePowerPlant(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    return AiQueueBuildingForTeam(Context->Team, BUILDING_TYPE_POWER_PLANT);
}

/************************************************************************/

/// @brief Queue a tech center.
BOOL ActionQueueTechCenter(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    return AiQueueBuildingForTeam(Context->Team, BUILDING_TYPE_TECH_CENTER);
}

/************************************************************************/

/// @brief Queue a factory as a fallback.
BOOL ActionQueueFactory(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    return AiQueueBuildingForTeam(Context->Team, BUILDING_TYPE_FACTORY);
}

/************************************************************************/

/// @brief Queue a fortress wall or turret when a placement exists.
BOOL ActionQueueFortress(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (Context->FortressTypeId < 0) return FALSE;
    return AiQueueBuildingForTeam(Context->Team, Context->FortressTypeId);
}

/************************************************************************/

/// @brief Produce a driller at the factory.
BOOL ActionProduceDriller(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (Context->Factory == NULL) return FALSE;
    return AiProduceUnit(Context->Team, UNIT_TYPE_DRILLER, Context->Factory);
}

/************************************************************************/

/// @brief Produce a scout at the barracks.
BOOL ActionProduceScout(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (Context->Barracks == NULL) return FALSE;
    return AiProduceUnit(Context->Team, UNIT_TYPE_SCOUT, Context->Barracks);
}

/************************************************************************/

/// @brief Assign a scout exploration order.
BOOL ActionOrderScoutExplore(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (Context->ScoutToOrder == NULL) return FALSE;

    I32 TargetX;
    I32 TargetY;
    if (!PickExplorationTarget(Context->Team, &TargetX, &TargetY)) return FALSE;

    SetUnitStateExplore(Context->ScoutToOrder, TargetX, TargetY);
    LogTeamAction(Context->Team, "SetExplore", (U32)Context->ScoutToOrder->Id,
                  (U32)TargetX, (U32)TargetY, "Scout", "");
    return TRUE;
}

/************************************************************************/

/// @brief Produce the next infantry unit from barracks.
BOOL ActionProduceBarracksUnit(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (Context->Barracks == NULL) return FALSE;

    I32 UnitTypeId = SelectBarracksUnitType(Context->Team, Context->Mindset,
                                            Context->InfantryTarget, Context->Barracks);
    if (UnitTypeId >= 0) {
        return AiProduceUnit(Context->Team, UnitTypeId, Context->Barracks);
    }
    return FALSE;
}

/************************************************************************/

/// @brief Produce the next vehicle unit from factory.
BOOL ActionProduceFactoryUnit(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (Context->Factory == NULL) return FALSE;

    I32 UnitTypeId = SelectFactoryUnitType(Context->Team, Context->Mindset,
                                           Context->VehicleTarget, Context->Factory);
    if (UnitTypeId >= 0) {
        return AiProduceUnit(Context->Team, UnitTypeId, Context->Factory);
    }
    return FALSE;
}

/************************************************************************/

/// @brief Issue aggressive move orders toward the selected cluster.
BOOL ActionAggressiveOrders(AI_CONTEXT* Context) {
    BOOL issued = FALSE;
    if (Context == NULL) return FALSE;
    if (!Context->HasAttackTarget) return FALSE;

    UNIT* Unit = App.GameState->TeamData[Context->Team].Units;
    while (Unit != NULL) {
        const UNIT_TYPE* UnitType = GetUnitTypeById(Unit->TypeId);
        if (UnitType != NULL && UnitType->Damage > 0 &&
            UnitType->Id != UNIT_TYPE_SCOUT && UnitType->Id != UNIT_TYPE_DRILLER &&
            Unit->State == UNIT_STATE_IDLE && !Unit->IsMoving) {
            if (Unit->TargetX == Context->AttackTargetX && Unit->TargetY == Context->AttackTargetY) {
                Unit = Unit->Next;
                continue;
            }
            SetUnitMoveTarget(Unit, Context->AttackTargetX, Context->AttackTargetY);
            LogTeamAction(Context->Team, "SetMoveTarget", (U32)Unit->Id,
                          (U32)Context->AttackTargetX, (U32)Context->AttackTargetY, "", "");
            issued = TRUE;
        }
        Unit = Unit->Next;
    }
    return issued;
}

/************************************************************************/

/// @brief Shuffle idle combat units around the base to relieve congestion.
BOOL ActionShuffleBaseUnits(AI_CONTEXT* Context) {
    I32 mapW;
    I32 mapH;
    I32 centerX;
    I32 centerY;
    I32 moved = 0;
    const BUILDING_TYPE* yardType;
    UNIT* unit;

    if (Context == NULL || App.GameState == NULL) return FALSE;
    if (Context->Yard == NULL) return FALSE;

    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    yardType = GetBuildingTypeById(Context->Yard->TypeId);
    centerX = Context->Yard->X;
    centerY = Context->Yard->Y;
    if (yardType != NULL) {
        centerX = Context->Yard->X + yardType->Width / 2;
        centerY = Context->Yard->Y + yardType->Height / 2;
    }

    unit = App.GameState->TeamData[Context->Team].Units;
    while (unit != NULL && moved < AI_BASE_SHUFFLE_COUNT) {
        const UNIT_TYPE* unitType = GetUnitTypeById(unit->TypeId);
        if (unitType != NULL &&
            unitType->Damage > 0 &&
            unitType->Id != UNIT_TYPE_SCOUT &&
            unitType->Id != UNIT_TYPE_DRILLER &&
            unit->State == UNIT_STATE_IDLE &&
            !unit->IsMoving) {
            I32 dist = ChebyshevDistance(centerX, centerY, unit->X, unit->Y, mapW, mapH);
            if (dist <= AI_BASE_SHUFFLE_RADIUS) {
                I32 targetX;
                I32 targetY;
                if (FindFreeSpotNear(centerX, centerY, unitType->Width, unitType->Height,
                                     mapW, mapH, AI_BASE_SHUFFLE_RADIUS, &targetX, &targetY)) {
                    SetUnitMoveTarget(unit, targetX, targetY);
                    moved++;
                }
            }
        }
        unit = unit->Next;
    }

    if (moved > 0) {
        App.GameState->TeamData[Context->Team].AiLastShuffleTime = App.GameState->GameTime;
    }
    return moved > 0;
}

/************************************************************************/
