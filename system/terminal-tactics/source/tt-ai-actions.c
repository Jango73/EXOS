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

/************************************************************************/

/// @brief Apply the driller escort behavior when needed.
void ActionUpdateDrillerEscort(AI_CONTEXT* Context) {
    if (Context == NULL) return;
    if (Context->Driller == NULL || Context->Escort == NULL) return;

    AssignDrillerEscorts(Context->Team, Context->Driller, Context->DesiredEscortForce);
}

/************************************************************************/

/// @brief Queue a factory so the AI can reach the driller target.
void ActionQueueFactoryForDrillers(AI_CONTEXT* Context) {
    if (Context == NULL) return;
    AiQueueBuildingForTeam(Context->Team, BUILDING_TYPE_FACTORY);
}

/************************************************************************/

/// @brief Queue a barracks.
void ActionQueueBarracks(AI_CONTEXT* Context) {
    if (Context == NULL) return;
    AiQueueBuildingForTeam(Context->Team, BUILDING_TYPE_BARRACKS);
}

/************************************************************************/

/// @brief Queue a power plant.
void ActionQueuePowerPlant(AI_CONTEXT* Context) {
    if (Context == NULL) return;
    AiQueueBuildingForTeam(Context->Team, BUILDING_TYPE_POWER_PLANT);
}

/************************************************************************/

/// @brief Queue a tech center.
void ActionQueueTechCenter(AI_CONTEXT* Context) {
    if (Context == NULL) return;
    AiQueueBuildingForTeam(Context->Team, BUILDING_TYPE_TECH_CENTER);
}

/************************************************************************/

/// @brief Queue a factory as a fallback.
void ActionQueueFactory(AI_CONTEXT* Context) {
    if (Context == NULL) return;
    AiQueueBuildingForTeam(Context->Team, BUILDING_TYPE_FACTORY);
}

/************************************************************************/

/// @brief Queue a fortress wall or turret when a placement exists.
void ActionQueueFortress(AI_CONTEXT* Context) {
    if (Context == NULL) return;
    if (Context->FortressTypeId < 0) return;
    AiQueueBuildingForTeam(Context->Team, Context->FortressTypeId);
}

/************************************************************************/

/// @brief Produce a driller at the factory.
void ActionProduceDriller(AI_CONTEXT* Context) {
    if (Context == NULL) return;
    if (Context->Factory == NULL) return;
    AiProduceUnit(Context->Team, UNIT_TYPE_DRILLER, Context->Factory);
}

/************************************************************************/

/// @brief Produce a scout at the barracks.
void ActionProduceScout(AI_CONTEXT* Context) {
    if (Context == NULL) return;
    if (Context->Barracks == NULL) return;
    AiProduceUnit(Context->Team, UNIT_TYPE_SCOUT, Context->Barracks);
}

/************************************************************************/

/// @brief Assign a scout exploration order.
void ActionOrderScoutExplore(AI_CONTEXT* Context) {
    if (Context == NULL) return;
    if (Context->ScoutToOrder == NULL) return;

    I32 TargetX;
    I32 TargetY;
    if (!PickExplorationTarget(Context->Team, &TargetX, &TargetY)) return;

    SetUnitStateExplore(Context->ScoutToOrder, TargetX, TargetY);
    LogTeamAction(Context->Team, "SetExplore", (U32)Context->ScoutToOrder->Id,
                  (U32)TargetX, (U32)TargetY, "Scout", "");
}

/************************************************************************/

/// @brief Produce the next infantry unit from barracks.
void ActionProduceBarracksUnit(AI_CONTEXT* Context) {
    if (Context == NULL) return;
    if (Context->Barracks == NULL) return;

    I32 UnitTypeId = SelectBarracksUnitType(Context->Team, Context->Mindset,
                                            Context->InfantryTarget, Context->Barracks);
    if (UnitTypeId >= 0) {
        AiProduceUnit(Context->Team, UnitTypeId, Context->Barracks);
    }
}

/************************************************************************/

/// @brief Produce the next vehicle unit from factory.
void ActionProduceFactoryUnit(AI_CONTEXT* Context) {
    if (Context == NULL) return;
    if (Context->Factory == NULL) return;

    I32 UnitTypeId = SelectFactoryUnitType(Context->Team, Context->Mindset,
                                           Context->VehicleTarget, Context->Factory);
    if (UnitTypeId >= 0) {
        AiProduceUnit(Context->Team, UnitTypeId, Context->Factory);
    }
}

/************************************************************************/

/// @brief Issue aggressive move orders toward the selected cluster.
void ActionAggressiveOrders(AI_CONTEXT* Context) {
    if (Context == NULL) return;
    if (!Context->HasAttackTarget) return;

    UNIT* Unit = App.GameState->TeamData[Context->Team].Units;
    while (Unit != NULL) {
        const UNIT_TYPE* UnitType = GetUnitTypeById(Unit->TypeId);
        if (UnitType != NULL && UnitType->Damage > 0 &&
            UnitType->Id != UNIT_TYPE_SCOUT && UnitType->Id != UNIT_TYPE_DRILLER &&
            Unit->State == UNIT_STATE_IDLE && !Unit->IsMoving) {
            SetUnitMoveTarget(Unit, Context->AttackTargetX, Context->AttackTargetY);
            LogTeamAction(Context->Team, "SetMoveTarget", (U32)Unit->Id,
                          (U32)Context->AttackTargetX, (U32)Context->AttackTargetY, "", "");
        }
        Unit = Unit->Next;
    }
}

/************************************************************************/
