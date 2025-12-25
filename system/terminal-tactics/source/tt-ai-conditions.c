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

/************************************************************************/

static BOOL CanQueueBuilding(const AI_CONTEXT* Context, I32 typeId) {
    if (Context == NULL) return FALSE;
    if (Context->PlannedBuildingTypeId < 0) return TRUE;
    if (Context->PlannedBuildingTypeId == typeId) return TRUE;
    if (Context->Plasma < Context->PlannedBuildingCost) return FALSE;
    return TRUE;
}

/************************************************************************/

static BOOL CanSpendOnUnit(const AI_CONTEXT* Context, I32 unitCost) {
    I32 drillerCount;
    I32 reserve;

    if (Context == NULL) return FALSE;
    if (unitCost <= 0) return TRUE;
    if (Context->PlannedBuildingTypeId < 0 || Context->PlannedBuildingCost <= 0) {
        return Context->Plasma >= unitCost;
    }

    drillerCount = Context->DrillerCount;
    if (drillerCount < 0) drillerCount = 0;
    reserve = (Context->PlannedBuildingCost * 3) / (drillerCount + 4);
    if (reserve < 0) reserve = 0;
    return (Context->Plasma - reserve) >= unitCost;
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
    if (!CanQueueBuilding(Context, BUILDING_TYPE_FACTORY)) return FALSE;
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
    if (!CanQueueBuilding(Context, BUILDING_TYPE_BARRACKS)) return FALSE;
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
    if (!CanQueueBuilding(Context, BUILDING_TYPE_POWER_PLANT)) return FALSE;
    if (Context->Plasma < Context->PowerPlantType->CostPlasma) return FALSE;
    return Context->EnergyLow;
}

/************************************************************************/

/// @brief Determine if the AI should queue a tech center.
BOOL ConditionForQueueTechCenter(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (!Context->YardHasSpace) return FALSE;
    if (Context->TechCenterType == NULL) return FALSE;
    if (!CanQueueBuilding(Context, BUILDING_TYPE_TECH_CENTER)) return FALSE;
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
    if (!CanQueueBuilding(Context, BUILDING_TYPE_FACTORY)) return FALSE;
    if (Context->Plasma < Context->FactoryType->CostPlasma) return FALSE;
    if (Context->HasFactory || Context->QueuedFactory > 0) return FALSE;
    return TRUE;
}

/************************************************************************/

/// @brief Determine if the AI should queue a fortress building.
BOOL ConditionForQueueFortress(AI_CONTEXT* Context) {
    if (Context == NULL) return FALSE;
    if (!Context->YardHasSpace) return FALSE;
    if (!CanQueueBuilding(Context, Context->FortressTypeId)) return FALSE;
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
