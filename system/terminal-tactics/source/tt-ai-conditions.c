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
#include "tt-map.h"

/************************************************************************/

static I32 GetMinCombatUnitCost(void) {
    I32 minCost = -1;

    for (I32 i = 0; i < UNIT_TYPE_COUNT; i++) {
        const UNIT_TYPE* ut = &UnitTypes[i];
        if (ut->Damage <= 0) continue;
        if (minCost < 0 || ut->CostPlasma < minCost) {
            minCost = ut->CostPlasma;
        }
    }

    if (minCost < 0) minCost = 0;
    return minCost;
}

/************************************************************************/

static I32 GetUnitReserveFactorForBuilding(I32 attitude) {
    if (attitude == AI_ATTITUDE_DEFENSIVE) return 3;
    if (attitude == AI_ATTITUDE_AGGRESSIVE) return 2;
    return 2;
}

/************************************************************************/

static I32 GetUnitReserveFactorForFortress(I32 attitude) {
    if (attitude == AI_ATTITUDE_DEFENSIVE) return 4;
    if (attitude == AI_ATTITUDE_AGGRESSIVE) return 3;
    return 3;
}

/************************************************************************/

static I32 GetPlannedReserveFactorForUnit(I32 attitude) {
    if (attitude == AI_ATTITUDE_DEFENSIVE) return 3;
    if (attitude == AI_ATTITUDE_AGGRESSIVE) return 2;
    return 2;
}

/************************************************************************/

static BOOL CanSpendPlasmaBalanced(const AI_CONTEXT* Context, I32 cost, I32 unitReserveFactor, I32 plannedReserveFactor) {
    I32 reserve = 0;
    I32 drillerCount;

    if (Context == NULL) return FALSE;
    if (cost <= 0) return TRUE;

    if (unitReserveFactor > 0) {
        reserve += GetMinCombatUnitCost() * unitReserveFactor;
    }

    if (plannedReserveFactor > 0 &&
        Context->PlannedBuildingTypeId >= 0 &&
        Context->PlannedBuildingCost > 0) {
        drillerCount = Context->DrillerCount;
        if (drillerCount < 0) drillerCount = 0;
        reserve += (Context->PlannedBuildingCost * plannedReserveFactor) / (drillerCount + 4);
    }

    if (reserve < 0) reserve = 0;
    return (Context->Plasma - reserve) >= cost;
}

/************************************************************************/

static BOOL CanQueueBuilding(const AI_CONTEXT* Context, I32 typeId, I32 unitReserveFactor) {
    const BUILDING_TYPE* type;
    I32 cost;

    if (Context == NULL) return FALSE;
    type = GetBuildingTypeById(typeId);
    if (type == NULL) return FALSE;
    cost = type->CostPlasma;
    if (Context->PlannedBuildingTypeId >= 0 &&
        Context->PlannedBuildingTypeId != typeId &&
        Context->Plasma < Context->PlannedBuildingCost) {
        return FALSE;
    }
    return CanSpendPlasmaBalanced(Context, cost, unitReserveFactor, 0);
}

/************************************************************************/

static BOOL CanSpendOnUnit(const AI_CONTEXT* Context, I32 unitCost) {
    I32 factor;

    if (Context == NULL) return FALSE;
    factor = GetPlannedReserveFactorForUnit(Context->Attitude);
    return CanSpendPlasmaBalanced(Context, unitCost, 0, factor);
}

/************************************************************************/

/// @brief Decide if the driller escort state needs a refresh.
BOOL ConditionForUpdateDrillerEscort(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    return Context->EscortNeedsUpdate;
}

/************************************************************************/

/// @brief Determine if the AI should queue a factory to reach the driller target.
BOOL ConditionForQueueFactoryForDrillers(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (!Context->YardHasSpace) return FALSE;
    if (Context->FactoryType == NULL) return FALSE;
    if (!CanQueueBuilding(Context, BUILDING_TYPE_FACTORY, GetUnitReserveFactorForBuilding(Context->Attitude))) return FALSE;
    if (Context->Plasma < Context->FactoryType->CostPlasma) return FALSE;
    if (Context->HasFactory || Context->QueuedFactory > 0) return FALSE;
    return (Context->DrillerCount + Context->QueuedDrillers) < Context->DrillerTarget;
}

/************************************************************************/

/// @brief Determine if the AI should queue a barracks.
BOOL ConditionForQueueBarracks(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (!Context->YardHasSpace) return FALSE;
    if (Context->BarracksType == NULL) return FALSE;
    if (!CanQueueBuilding(Context, BUILDING_TYPE_BARRACKS, GetUnitReserveFactorForBuilding(Context->Attitude))) return FALSE;
    if (Context->Plasma < Context->BarracksType->CostPlasma) return FALSE;
    if (Context->HasBarracks || Context->QueuedBarracks > 0) return FALSE;
    return TRUE;
}

/************************************************************************/

/// @brief Determine if the AI should queue a power plant.
BOOL ConditionForQueuePowerPlant(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (!Context->YardHasSpace) return FALSE;
    if (Context->PowerPlantType == NULL) return FALSE;
    if (!CanQueueBuilding(Context, BUILDING_TYPE_POWER_PLANT, 0)) return FALSE;
    if (Context->Plasma < Context->PowerPlantType->CostPlasma) return FALSE;
    return Context->EnergyLow;
}

/************************************************************************/

/// @brief Determine if the AI should queue a tech center.
BOOL ConditionForQueueTechCenter(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (!Context->YardHasSpace) return FALSE;
    if (Context->TechCenterType == NULL) return FALSE;
    if (!CanQueueBuilding(Context, BUILDING_TYPE_TECH_CENTER, GetUnitReserveFactorForBuilding(Context->Attitude))) return FALSE;
    if (Context->Plasma < Context->TechCenterType->CostPlasma) return FALSE;
    if (Context->HasTechCenter || Context->QueuedTechCenter > 0) return FALSE;
    return TRUE;
}

/************************************************************************/

/// @brief Determine if the AI should queue a factory as a fallback.
BOOL ConditionForQueueFactory(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (!Context->YardHasSpace) return FALSE;
    if (Context->FactoryType == NULL) return FALSE;
    if (!CanQueueBuilding(Context, BUILDING_TYPE_FACTORY, GetUnitReserveFactorForBuilding(Context->Attitude))) return FALSE;
    if (Context->Plasma < Context->FactoryType->CostPlasma) return FALSE;
    if (Context->HasFactory || Context->QueuedFactory > 0) return FALSE;
    return TRUE;
}

/************************************************************************/

/// @brief Determine if the AI should queue a fortress building.
BOOL ConditionForQueueFortress(AI_CONTEXT* Context) {
    I32 reserveFactor;
    if (Context == NULL) return FALSE;
    if (!Context->YardHasSpace) return FALSE;
    if (Context->FortressTypeId < 0) return FALSE;
    reserveFactor = GetUnitReserveFactorForFortress(Context->Attitude);
    if (!CanQueueBuilding(Context, Context->FortressTypeId, reserveFactor)) return FALSE;
    return Context->FortressTypeId >= 0;
}

/************************************************************************/

/// @brief Determine if the AI should produce a driller.
BOOL ConditionForProduceDriller(AI_CONTEXT* Context) {
    const UNIT_TYPE* unitType;

    if (Context == NULL) return FALSE;
    if (!Context->AllowUnitProduction) return FALSE;
    if (Context->Factory == NULL) return FALSE;
    unitType = GetUnitTypeById(UNIT_TYPE_DRILLER);
    if (unitType == NULL) return FALSE;
    if (!CanSpendOnUnit(Context, unitType->CostPlasma)) return FALSE;
    return (Context->DrillerCount + Context->QueuedDrillers) < Context->DrillerTarget;
}

/************************************************************************/

/// @brief Determine if the AI should produce a scout.
BOOL ConditionForProduceScout(AI_CONTEXT* Context) {
    const UNIT_TYPE* unitType;

    if (Context == NULL) return FALSE;
    if (Context->Barracks == NULL) return FALSE;
    unitType = GetUnitTypeById(UNIT_TYPE_SCOUT);
    if (unitType == NULL) return FALSE;
    if (!CanSpendOnUnit(Context, unitType->CostPlasma)) return FALSE;
    return (Context->ScoutCount + Context->QueuedScouts) < Context->TargetScouts;
}

/************************************************************************/

/// @brief Determine if the AI should issue a scout exploration order.
BOOL ConditionForOrderScoutExplore(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    return Context->ScoutToOrder != NULL;
}

/************************************************************************/

/// @brief Determine if the AI should produce an infantry unit.
BOOL ConditionForProduceBarracksUnit(AI_CONTEXT* Context) {
    I32 unitTypeId;
    const UNIT_TYPE* unitType;

    if (Context == NULL) return FALSE;
    if (!Context->AllowUnitProduction) return FALSE;
    if (Context->Barracks == NULL) return FALSE;
    if (Context->MobileTarget <= 0) return FALSE;
    if (Context->InfantryTarget <= 0) return FALSE;
    if (Context->MobileCount >= Context->MobileTarget) return FALSE;
    if (Context->InfantryCountWithQueue >= Context->InfantryTarget) return FALSE;

    unitTypeId = SelectBarracksUnitType(Context->Team, Context->Mindset,
                                        Context->InfantryTarget, Context->Barracks);
    if (unitTypeId < 0) return FALSE;
    unitType = GetUnitTypeById(unitTypeId);
    if (unitType == NULL) return FALSE;
    return CanSpendOnUnit(Context, unitType->CostPlasma);
}

/************************************************************************/

/// @brief Determine if the AI should produce a vehicle unit.
BOOL ConditionForProduceFactoryUnit(AI_CONTEXT* Context) {
    I32 unitTypeId;
    const UNIT_TYPE* unitType;

    if (Context == NULL) return FALSE;
    if (!Context->AllowUnitProduction) return FALSE;
    if (Context->Factory == NULL) return FALSE;
    if (Context->MobileTarget <= 0) return FALSE;
    if (Context->VehicleTarget <= 0) return FALSE;
    if (Context->MobileCount >= Context->MobileTarget) return FALSE;
    if ((Context->DrillerCount + Context->QueuedDrillers) < Context->DrillerTarget) return FALSE;
    if (Context->VehicleCountWithQueue >= Context->VehicleTarget) return FALSE;

    unitTypeId = SelectFactoryUnitType(Context->Team, Context->Mindset,
                                       Context->VehicleTarget, Context->Factory);
    if (unitTypeId < 0) return FALSE;
    unitType = GetUnitTypeById(unitTypeId);
    if (unitType == NULL) return FALSE;
    return CanSpendOnUnit(Context, unitType->CostPlasma);
}

/************************************************************************/

/// @brief Determine if the AI should launch an aggressive order.
BOOL ConditionForAggressiveOrders(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (Context->Attitude != AI_ATTITUDE_AGGRESSIVE) return FALSE;
    if (Context->AvailableForce <= 0) return FALSE;
    if (!GetAttackClusterTarget(Context->Team, Context->AvailableForce,
                                &Context->AttackTargetX, &Context->AttackTargetY,
                                &Context->AttackTargetScore)) {
        return FALSE;
    }
    Context->HasAttackTarget = TRUE;
    return TRUE;
}

/************************************************************************/

static I32 CountIdleCombatUnitsNearBase(const AI_CONTEXT* Context, I32 radius) {
    I32 mapW;
    I32 mapH;
    I32 centerX;
    I32 centerY;
    I32 count = 0;
    const BUILDING_TYPE* yardType;
    UNIT* unit;

    if (Context == NULL || App.GameState == NULL) return 0;
    if (Context->Yard == NULL) return 0;
    if (radius <= 0) return 0;

    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return 0;

    yardType = GetBuildingTypeById(Context->Yard->TypeId);
    centerX = Context->Yard->X;
    centerY = Context->Yard->Y;
    if (yardType != NULL) {
        centerX = Context->Yard->X + yardType->Width / 2;
        centerY = Context->Yard->Y + yardType->Height / 2;
    }

    unit = App.GameState->TeamData[Context->Team].Units;
    while (unit != NULL) {
        const UNIT_TYPE* unitType = GetUnitTypeById(unit->TypeId);
        if (unitType != NULL &&
            unitType->Damage > 0 &&
            unitType->Id != UNIT_TYPE_SCOUT &&
            unitType->Id != UNIT_TYPE_DRILLER &&
            unit->State == UNIT_STATE_IDLE &&
            !unit->IsMoving) {
            I32 dist = ChebyshevDistance(centerX, centerY, unit->X, unit->Y, mapW, mapH);
            if (dist <= radius) {
                count++;
            }
        }
        unit = unit->Next;
    }

    return count;
}

/************************************************************************/

/// @brief Determine if the AI should shuffle idle units around the base.
BOOL ConditionForShuffleBaseUnits(AI_CONTEXT* Context) {
    U32 now;
    U32 last;

    if (Context == NULL || App.GameState == NULL) return FALSE;
    if (Context->Yard == NULL) return FALSE;

    now = App.GameState->GameTime;
    last = App.GameState->TeamData[Context->Team].AiLastShuffleTime;
    if (now - last < AI_BASE_SHUFFLE_COOLDOWN_MS) return FALSE;

    return CountIdleCombatUnitsNearBase(Context, AI_BASE_SHUFFLE_RADIUS) > 0;
}

/************************************************************************/
