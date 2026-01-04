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

#ifndef TT_AI_INTERNAL_H
#define TT_AI_INTERNAL_H

#include "tt-types.h"

/************************************************************************/

typedef struct tag_AI_CONTEXT {
    I32 Team;
    U32 Now;
    U32 NowSystem;
    I32 Mindset;
    I32 Attitude;
    BOOL CanAffordCheapestMobile;
    I32 EnemyNearby;
    I32 FriendlyNearby;
    BOOL ThreatActive;
    I32 EnemyKnownForce;
    TEAM_RESOURCES* Resources;
    I32 Plasma;
    BUILDING* Yard;
    BUILDING* Barracks;
    BUILDING* Factory;
    BUILDING* TechCenter;
    BOOL HasBarracks;
    BOOL HasFactory;
    BOOL HasTechCenter;
    I32 QueuedBarracks;
    I32 QueuedFactory;
    I32 QueuedTechCenter;
    BOOL YardHasSpace;
    I32 YardQueueCount;
    I32 EnergyProduction;
    I32 EnergyConsumption;
    BOOL EnergyLow;
    const BUILDING_TYPE* PowerPlantType;
    const BUILDING_TYPE* BarracksType;
    const BUILDING_TYPE* FactoryType;
    const BUILDING_TYPE* TechCenterType;
    I32 DrillerCount;
    I32 QueuedDrillers;
    I32 DrillerTarget;
    I32 MobileTarget;
    I32 MobileCount;
    I32 InfantryTarget;
    I32 VehicleTarget;
    BOOL AllowUnitProduction;
    I32 UnitCounts[UNIT_TYPE_COUNT];
    I32 InfantryCountWithQueue;
    I32 VehicleCountWithQueue;
    I32 ScoutCount;
    I32 QueuedScouts;
    I32 TargetScouts;
    UNIT* ScoutToOrder;
    UNIT* Driller;
    UNIT* Escort;
    BOOL DrillerUnderAttack;
    BOOL EscortNeedsUpdate;
    BOOL HasDrillerEscort;
    I32 DesiredEscortForce;
    I32 CurrentEscortForce;
    I32 FortressTypeId;
    I32 PlannedBuildingTypeId;
    I32 PlannedBuildingCost;
    I32 AvailableForce;
    BOOL HasAttackTarget;
    I32 AttackTargetX;
    I32 AttackTargetY;
    I32 AttackTargetScore;
} AI_CONTEXT;

/************************************************************************/

typedef BOOL (*AI_CONDITION_FUNC)(AI_CONTEXT* Context);
typedef BOOL (*AI_ACTION_FUNC)(AI_CONTEXT* Context);

typedef struct tag_AI_DECISION {
    AI_CONDITION_FUNC Condition;
    AI_ACTION_FUNC Action;
    const char* Name;
} AI_DECISION;

/************************************************************************/

BOOL AiQueueBuildingForTeam(I32 Team, I32 TypeId);
BOOL AiProduceUnit(I32 Team, I32 UnitTypeId, BUILDING* Producer);
I32 SelectBarracksUnitType(I32 Team, I32 Mindset, I32 InfantryTarget, const BUILDING* Barracks);
I32 SelectFactoryUnitType(I32 Team, I32 Mindset, I32 VehicleTarget, const BUILDING* Factory);
BOOL GetAttackClusterTarget(I32 Team, I32 AvailableForce, I32* OutTargetX, I32* OutTargetY, I32* OutTargetScore);
void ClearDrillerEscorts(I32 Team, I32 DrillerId);
BOOL AssignDrillerEscorts(I32 Team, UNIT* Driller, I32 DesiredForce);

/************************************************************************/

BOOL ConditionForUpdateDrillerEscort(AI_CONTEXT* Context);
BOOL ConditionForQueueFactoryForDrillers(AI_CONTEXT* Context);
BOOL ConditionForQueueBarracks(AI_CONTEXT* Context);
BOOL ConditionForQueuePowerPlant(AI_CONTEXT* Context);
BOOL ConditionForQueueTechCenter(AI_CONTEXT* Context);
BOOL ConditionForQueueFactory(AI_CONTEXT* Context);
BOOL ConditionForQueueFortress(AI_CONTEXT* Context);
BOOL ConditionForProduceDriller(AI_CONTEXT* Context);
BOOL ConditionForProduceScout(AI_CONTEXT* Context);
BOOL ConditionForOrderScoutExplore(AI_CONTEXT* Context);
BOOL ConditionForProduceBarracksUnit(AI_CONTEXT* Context);
BOOL ConditionForProduceFactoryUnit(AI_CONTEXT* Context);
BOOL ConditionForAggressiveOrders(AI_CONTEXT* Context);
BOOL ConditionForShuffleBaseUnits(AI_CONTEXT* Context);

/************************************************************************/

BOOL ActionUpdateDrillerEscort(AI_CONTEXT* Context);
BOOL ActionQueueFactoryForDrillers(AI_CONTEXT* Context);
BOOL ActionQueueBarracks(AI_CONTEXT* Context);
BOOL ActionQueuePowerPlant(AI_CONTEXT* Context);
BOOL ActionQueueTechCenter(AI_CONTEXT* Context);
BOOL ActionQueueFactory(AI_CONTEXT* Context);
BOOL ActionQueueFortress(AI_CONTEXT* Context);
BOOL ActionProduceDriller(AI_CONTEXT* Context);
BOOL ActionProduceScout(AI_CONTEXT* Context);
BOOL ActionOrderScoutExplore(AI_CONTEXT* Context);
BOOL ActionProduceBarracksUnit(AI_CONTEXT* Context);
BOOL ActionProduceFactoryUnit(AI_CONTEXT* Context);
BOOL ActionAggressiveOrders(AI_CONTEXT* Context);
BOOL ActionShuffleBaseUnits(AI_CONTEXT* Context);

/************************************************************************/

#endif /* TT_AI_INTERNAL_H */
