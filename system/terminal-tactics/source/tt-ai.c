
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

#include "tt-ai.h"
#include "tt-types.h"
#include "tt-map.h"
#include "tt-fog.h"
#include "tt-entities.h"
#include "tt-production.h"
#include "tt-game.h"

/************************************************************************/

#define AI_CLUSTER_RADIUS 4
#define FORTRESS_CLEARANCE 3
#define FORTRESS_TURRET_SPACING 5

/************************************************************************/

static BOOL ShouldProcessAITeam(I32 team, U32 currentTime) {
    if (team == HUMAN_TEAM_INDEX) return FALSE;
    if (!IsValidTeam(team)) return FALSE;
    if (IsTeamEliminated(team)) return FALSE;

    U32 last = App.GameState->TeamData[team].AiLastUpdate;
    U32 interval = 0;
    if (App.GameState != NULL) {
        switch (App.GameState->Difficulty) {
            case DIFFICULTY_EASY:
                interval = AI_UPDATE_INTERVAL_EASY_MS;
                break;
            case DIFFICULTY_NORMAL:
                interval = AI_UPDATE_INTERVAL_NORMAL_MS;
                break;
            case DIFFICULTY_HARD:
            default:
                interval = AI_UPDATE_INTERVAL_HARD_MS;
                break;
        }
    }
    if (interval > 0 && last != 0 && currentTime - last < interval) return FALSE;

    App.GameState->TeamData[team].AiLastUpdate = currentTime;
    return TRUE;
}

/************************************************************************/

static I32 CountUnitsInRadius(I32 team, I32 centerX, I32 centerY, I32 radius, BOOL countEnemiesOnly) {
    I32 count = 0;
    I32 mapW = App.GameState != NULL ? App.GameState->MapWidth : 0;
    I32 mapH = App.GameState != NULL ? App.GameState->MapHeight : 0;
    MEMORY_CELL* memory = (App.GameState != NULL && IsValidTeam(team)) ? App.GameState->TeamData[team].MemoryMap : NULL;
    if (App.GameState == NULL || mapW <= 0 || mapH <= 0 || memory == NULL) return 0;

    for (I32 dy = -radius; dy <= radius; dy++) {
        for (I32 dx = -radius; dx <= radius; dx++) {
            if (ChebyshevDistance(0, 0, dx, dy, mapW, mapH) > radius) continue;
            I32 px = WrapCoord(centerX, dx, mapW);
            I32 py = WrapCoord(centerY, dy, mapH);
            size_t idx = (size_t)py * (size_t)mapW + (size_t)px;
            if (memory[idx].OccupiedType == 0) continue;
            if (memory[idx].IsBuilding) continue;
            if (countEnemiesOnly) {
                if (memory[idx].Team != team) count++;
            } else {
                if (memory[idx].Team == team) count++;
            }
        }
    }
    return count;
}

/************************************************************************/

static I32 ComputeMitigatedDamage(I32 baseDamage, I32 targetArmor) {
    I32 reduction;
    I32 result;
    I32 factor;

    if (baseDamage <= 0) return 0;
    if (targetArmor < 0) targetArmor = 0;

    /* Random mitigation between 0.5x and 1.5x armor, clamped to avoid over-reduction */
    factor = AI_DAMAGE_REDUCTION_MIN + (I32)(SimpleRandom() % (AI_DAMAGE_REDUCTION_MAX - AI_DAMAGE_REDUCTION_MIN + 1));
    reduction = (targetArmor * factor) / AI_DAMAGE_REDUCTION_DIVISOR;
    if (reduction > baseDamage - 1) reduction = baseDamage - 1;

    result = baseDamage - reduction;
    if (result < 1) result = 1;
    if (result > baseDamage) result = baseDamage;
    return result;
}

/************************************************************************/

static I32 ComputeUnitScore(const UNIT_TYPE* unitType) {
    if (unitType == NULL) return 0;
    return unitType->Damage * AI_UNIT_SCORE_DAMAGE_WEIGHT + unitType->MaxHp;
}

/************************************************************************/

static I32 ComputeTurretScore(const BUILDING_TYPE* buildingType) {
    if (buildingType == NULL) return 0;
    return buildingType->MaxHp;
}

/************************************************************************/

static BOOL IsHostileMemoryCell(I32 team, const MEMORY_CELL* cell, BOOL* outIsBuilding, I32* outTypeId) {
    if (cell == NULL) return FALSE;
    if (cell->OccupiedType == 0) return FALSE;
    if (cell->Team == team) return FALSE;

    if (cell->IsBuilding) {
        if (cell->OccupiedType != BUILDING_TYPE_TURRET) return FALSE;
    }

    if (outIsBuilding != NULL) *outIsBuilding = cell->IsBuilding ? TRUE : FALSE;
    if (outTypeId != NULL) *outTypeId = cell->OccupiedType;
    return TRUE;
}

/************************************************************************/

static BOOL IsHostileAnchorCell(I32 team, I32 x, I32 y, BOOL isBuilding, I32 typeId, const MEMORY_CELL* memory, I32 mapW, I32 mapH) {
    if (memory == NULL || mapW <= 0 || mapH <= 0) return FALSE;

    I32 leftX = WrapCoord(x, -1, mapW);
    I32 upY = WrapCoord(y, -1, mapH);
    const MEMORY_CELL* leftCell = &memory[(size_t)y * (size_t)mapW + (size_t)leftX];
    const MEMORY_CELL* upCell = &memory[(size_t)upY * (size_t)mapW + (size_t)x];

    if (leftCell->OccupiedType != 0 && leftCell->Team != team &&
        leftCell->IsBuilding == isBuilding && leftCell->OccupiedType == typeId) {
        return FALSE;
    }
    if (upCell->OccupiedType != 0 && upCell->Team != team &&
        upCell->IsBuilding == isBuilding && upCell->OccupiedType == typeId) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

static BOOL FindAttackCluster(I32 team, I32 availableForce, I32* outTargetX, I32* outTargetY, I32* outTargetScore) {
    I32 mapW = App.GameState != NULL ? App.GameState->MapWidth : 0;
    I32 mapH = App.GameState != NULL ? App.GameState->MapHeight : 0;
    MEMORY_CELL* memory = (App.GameState != NULL && IsValidTeam(team)) ? App.GameState->TeamData[team].MemoryMap : NULL;

    if (App.GameState == NULL || mapW <= 0 || mapH <= 0 || memory == NULL) return FALSE;
    if (availableForce <= 0) return FALSE;

    size_t cellCount = (size_t)mapW * (size_t)mapH;
    U8* visited = (U8*)malloc(cellCount);
    if (visited == NULL) return FALSE;
    memset(visited, 0, cellCount);

    POINT_2D* queue = (POINT_2D*)malloc(sizeof(POINT_2D) * cellCount);
    if (queue == NULL) {
        free(visited);
        return FALSE;
    }

    BOOL found = FALSE;
    I32 bestScore = 0;
    I32 bestX = 0;
    I32 bestY = 0;

    for (I32 y = 0; y < mapH; y++) {
        for (I32 x = 0; x < mapW; x++) {
            size_t idx = (size_t)y * (size_t)mapW + (size_t)x;
            if (visited[idx] != 0) continue;

            BOOL isBuilding;
            I32 typeId;
            if (!IsHostileMemoryCell(team, &memory[idx], &isBuilding, &typeId)) continue;

            I32 head = 0;
            I32 tail = 0;
            I32 clusterScore = 0;
            I32 clusterX = x;
            I32 clusterY = y;

            visited[idx] = 1;
            queue[tail].X = x;
            queue[tail].Y = y;
            tail++;

            while (head < tail) {
                POINT_2D current = queue[head++];

                size_t curIdx = (size_t)current.Y * (size_t)mapW + (size_t)current.X;
                BOOL curIsBuilding;
                I32 curTypeId;
                if (IsHostileMemoryCell(team, &memory[curIdx], &curIsBuilding, &curTypeId)) {
                    if (IsHostileAnchorCell(team, current.X, current.Y, curIsBuilding, curTypeId, memory, mapW, mapH)) {
                        if (curIsBuilding) {
                            const BUILDING_TYPE* bt = GetBuildingTypeById(curTypeId);
                            clusterScore += ComputeTurretScore(bt);
                        } else {
                            const UNIT_TYPE* ut = GetUnitTypeById(curTypeId);
                            clusterScore += ComputeUnitScore(ut);
                        }
                    }
                }

                for (I32 dy = -AI_CLUSTER_RADIUS; dy <= AI_CLUSTER_RADIUS; dy++) {
                    for (I32 dx = -AI_CLUSTER_RADIUS; dx <= AI_CLUSTER_RADIUS; dx++) {
                        if (ChebyshevDistance(0, 0, dx, dy, mapW, mapH) > AI_CLUSTER_RADIUS) continue;
                        I32 nx = WrapCoord(current.X, dx, mapW);
                        I32 ny = WrapCoord(current.Y, dy, mapH);
                        size_t nidx = (size_t)ny * (size_t)mapW + (size_t)nx;
                        if (visited[nidx] != 0) continue;
                        if (!IsHostileMemoryCell(team, &memory[nidx], NULL, NULL)) continue;

                        visited[nidx] = 1;
                        queue[tail].X = nx;
                        queue[tail].Y = ny;
                        tail++;
                    }
                }
            }

            if (clusterScore > 0 && availableForce * 2 >= clusterScore * 3) {
                if (!found || clusterScore > bestScore) {
                    bestScore = clusterScore;
                    bestX = clusterX;
                    bestY = clusterY;
                    found = TRUE;
                }
            }
        }
    }

    free(queue);
    free(visited);

    if (!found) return FALSE;

    if (outTargetX != NULL) *outTargetX = bestX;
    if (outTargetY != NULL) *outTargetY = bestY;
    if (outTargetScore != NULL) *outTargetScore = bestScore;
    return TRUE;
}

/************************************************************************/

static BOOL GetAttackClusterTarget(I32 team, I32 availableForce, I32* outTargetX, I32* outTargetY, I32* outTargetScore) {
    if (!IsValidTeam(team) || App.GameState == NULL) return FALSE;

    U32 now = App.GameState->GameTime;
    U32 last = App.GameState->TeamData[team].AiLastClusterUpdate;
    if (last != 0 && now - last < AI_CLUSTER_UPDATE_INTERVAL_MS) return FALSE;

    App.GameState->TeamData[team].AiLastClusterUpdate = now;
    return FindAttackCluster(team, availableForce, outTargetX, outTargetY, outTargetScore);
}

/************************************************************************/

static void ApplyDamageToUnit(I32 targetTeam, UNIT* target, I32 damage, U32 now) {
    if (target == NULL) return;
    if (damage <= 0) return;
    target->LastDamageTime = now;
    target->Hp -= damage;
    if (target->Hp <= 0) {
        RemoveUnitFromTeamList(targetTeam, target);
    }
}

/************************************************************************/

static void ApplyDamageToBuilding(I32 targetTeam, BUILDING* target, I32 damage) {
    if (target == NULL) return;
    if (damage <= 0) return;
    target->LastDamageTime = GetSystemTime();
    target->Hp -= damage;
    if (target->Hp <= 0) {
        RemoveBuildingFromTeamList(targetTeam, target);
    }
}

/************************************************************************/

static BOOL IsTargetInRange(I32 ax, I32 ay, I32 range, I32 tx, I32 ty, I32 tw, I32 th, I32 mapW, I32 mapH) {
    if (range <= 0 || mapW <= 0 || mapH <= 0) return FALSE;
    for (I32 dy = 0; dy < th; dy++) {
        for (I32 dx = 0; dx < tw; dx++) {
            I32 px = WrapCoord(tx, dx, mapW);
            I32 py = WrapCoord(ty, dy, mapH);
            if (ChebyshevDistance(ax, ay, px, py, mapW, mapH) <= range) return TRUE;
        }
    }
    return FALSE;
}

/************************************************************************/

static BOOL TryAttackTargets(UNIT* attacker, const UNIT_TYPE* attackerType, U32 currentTime) {
    I32 teamCount;
    I32 attackerTeam;
    I32 mapW;
    I32 mapH;
    I32 attackRange;

    if (App.GameState == NULL || attacker == NULL || attackerType == NULL) return FALSE;
    if (attackerType->Damage <= 0) return FALSE;

    attackerTeam = attacker->Team;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    teamCount = GetTeamCountSafe();
    /* Use vision as effective attack envelope (range and sight are treated the same here) */
    attackRange = (attackerType->Sight > 0) ? attackerType->Sight : 1;

    for (I32 team = 0; team < teamCount; team++) {
        if (team == attackerTeam) continue;

        /* Prioritize units */
        UNIT* enemyUnit = App.GameState->TeamData[team].Units;
        while (enemyUnit != NULL) {
            const UNIT_TYPE* enemyType = GetUnitTypeById(enemyUnit->TypeId);
            if (enemyType != NULL &&
                IsAreaVisibleToTeam(enemyUnit->X, enemyUnit->Y, enemyType->Width, enemyType->Height, attackerTeam) &&
                IsTargetInRange(attacker->X, attacker->Y, attackRange, enemyUnit->X, enemyUnit->Y, enemyType->Width, enemyType->Height, mapW, mapH)) {
                I32 dmg = ComputeMitigatedDamage(attackerType->Damage, enemyType->Armor);
                ApplyDamageToUnit(team, enemyUnit, dmg, currentTime);
                attacker->LastAttackTime = currentTime;
                return TRUE;
            }
            enemyUnit = enemyUnit->Next;
        }

        /* Then buildings */
        BUILDING* enemyBuilding = App.GameState->TeamData[team].Buildings;
        while (enemyBuilding != NULL) {
            const BUILDING_TYPE* enemyType = GetBuildingTypeById(enemyBuilding->TypeId);
            I32 armor = (enemyType != NULL) ? enemyType->Armor : 0;
            I32 width = (enemyType != NULL) ? enemyType->Width : 1;
            I32 height = (enemyType != NULL) ? enemyType->Height : 1;
            if (enemyType != NULL &&
                IsAreaVisibleToTeam(enemyBuilding->X, enemyBuilding->Y, width, height, attackerTeam) &&
                IsTargetInRange(attacker->X, attacker->Y, attackRange, enemyBuilding->X, enemyBuilding->Y, width, height, mapW, mapH)) {
                I32 dmg = ComputeMitigatedDamage(attackerType->Damage, armor);
                ApplyDamageToBuilding(team, enemyBuilding, dmg);
                attacker->LastAttackTime = currentTime;
                return TRUE;
            }
            enemyBuilding = enemyBuilding->Next;
        }
    }

    return FALSE;
}

/************************************************************************/

void ProcessUnitAttacks(U32 currentTime) {
    I32 teamCount = GetTeamCountSafe();
    if (App.GameState == NULL || teamCount <= 0) return;

    for (I32 team = 0; team < teamCount; team++) {
        UNIT* unit = App.GameState->TeamData[team].Units;
        while (unit != NULL) {
            const UNIT_TYPE* unitType = GetUnitTypeById(unit->TypeId);
            if (unitType != NULL) {
                I32 attackInterval = (unitType->AttackSpeed > 0) ? unitType->AttackSpeed : UNIT_ATTACK_INTERVAL_MS;
                if (unit->LastAttackTime == 0 || currentTime - unit->LastAttackTime >= (U32)attackInterval) {
                    TryAttackTargets(unit, unitType, currentTime);
                }
            }
            unit = unit->Next;
        }
    }
}

/************************************************************************/

static void EvaluateThreatNearTeamBuildings(I32 team, I32 radius, I32* outEnemyCount, I32* outFriendlyCount) {
    I32 maxEnemy = 0;
    I32 maxFriendly = 0;

    if (!IsValidTeam(team) || App.GameState == NULL) {
        if (outEnemyCount != NULL) *outEnemyCount = 0;
        if (outFriendlyCount != NULL) *outFriendlyCount = 0;
        return;
    }

    BUILDING* building = App.GameState->TeamData[team].Buildings;
    while (building != NULL) {
        const BUILDING_TYPE* bt = GetBuildingTypeById(building->TypeId);
        if (bt != NULL) {
            I32 cx = building->X + bt->Width / 2;
            I32 cy = building->Y + bt->Height / 2;
            I32 enemy = CountUnitsInRadius(team, cx, cy, radius, TRUE);
            I32 friendly = CountUnitsInRadius(team, cx, cy, radius, FALSE);
            if (enemy > maxEnemy) maxEnemy = enemy;
            if (friendly > maxFriendly) maxFriendly = friendly;
        }
        building = building->Next;
    }

    if (outEnemyCount != NULL) *outEnemyCount = maxEnemy;
    if (outFriendlyCount != NULL) *outFriendlyCount = maxFriendly;
}

/************************************************************************/

static BOOL CanAffordCheapestMobileUnit(I32 team) {
    TEAM_RESOURCES* res = GetTeamResources(team);
    if (res == NULL) return FALSE;

    I32 minPlasma = -1;

    for (I32 i = 0; i < UNIT_TYPE_COUNT; i++) {
        const UNIT_TYPE* ut = &UnitTypes[i];
        if (minPlasma < 0 || ut->CostPlasma < minPlasma) {
            minPlasma = ut->CostPlasma;
        }
    }

    if (minPlasma < 0) minPlasma = 0;

    if (res->Plasma < minPlasma) return FALSE;
    return TRUE;
}

/************************************************************************/

static BOOL AiQueueBuildingForTeam(I32 team, I32 typeId) {
    TEAM_RESOURCES* res = GetTeamResources(team);
    const BUILDING_TYPE* type = GetBuildingTypeById(typeId);
    BUILDING* yard;
    I32 queueCount;

    if (App.GameState == NULL || res == NULL || type == NULL) return FALSE;
    if (!HasTechLevel(type->TechLevel, team)) return FALSE;
    if (res->Plasma < type->CostPlasma) return FALSE;

    yard = FindTeamBuilding(team, BUILDING_TYPE_CONSTRUCTION_YARD);
    if (yard == NULL) return FALSE;
    queueCount = yard->BuildQueueCount;
    if (queueCount >= MAX_PLACEMENT_QUEUE) return FALSE;

    res->Plasma -= type->CostPlasma;
    yard->BuildQueue[queueCount].TypeId = type->Id;
    yard->BuildQueue[queueCount].TimeRemaining = (U32)type->BuildTime;
    yard->BuildQueueCount++;
    return TRUE;
}

/************************************************************************/

static BOOL AiProduceUnit(I32 team, I32 unitTypeId, BUILDING* producer) {
    return EnqueueUnitProduction(producer, unitTypeId, team, NULL);
}

/************************************************************************/

static I32 CountMobileUnits(I32 team) {
    UNIT* unit = IsValidTeam(team) ? App.GameState->TeamData[team].Units : NULL;
    I32 count = 0;
    while (unit != NULL) {
        const UNIT_TYPE* ut = GetUnitTypeById(unit->TypeId);
        if (ut != NULL && ut->Damage > 0) {
            count++;
        }
        unit = unit->Next;
    }
    return count;
}

/************************************************************************/

static I32 CountQueuedUnitType(const BUILDING* producer, I32 unitTypeId) {
    I32 count = 0;
    if (producer == NULL) return 0;
    for (I32 i = 0; i < producer->UnitQueueCount; i++) {
        if (producer->UnitQueue[i].TypeId == unitTypeId) {
            count++;
        }
    }
    return count;
}

/************************************************************************/

static I32 CountUnitsOfType(I32 team, I32 typeId) {
    UNIT* unit = IsValidTeam(team) ? App.GameState->TeamData[team].Units : NULL;
    I32 count = 0;
    while (unit != NULL) {
        if (unit->TypeId == typeId) count++;
        unit = unit->Next;
    }
    return count;
}

/************************************************************************/

typedef struct FORTRESS_PLAN {
    I32 AnchorX;
    I32 AnchorY;
    I32 MapW;
    I32 MapH;
    I32 InnerMinX;
    I32 InnerMinY;
    I32 InnerMaxX;
    I32 InnerMaxY;
    I32 WallMinX;
    I32 WallMinY;
    I32 WallMaxX;
    I32 WallMaxY;
    I32 GateWidth;
    I32 GateMinX;
    I32 GateMaxX;
    I32 GateMinY;
    I32 GateMaxY;
} FORTRESS_PLAN;

/************************************************************************/

/// @brief Zero-initialize a unit count array.
static void ClearUnitCounts(I32* counts) {
    if (counts == NULL) return;
    for (I32 i = 0; i < UNIT_TYPE_COUNT; i++) {
        counts[i] = 0;
    }
}

/************************************************************************/

/// @brief Count current units for a team by category.
static void GetUnitCounts(I32 team, I32* counts) {
    if (counts == NULL) return;
    ClearUnitCounts(counts);
    if (!IsValidTeam(team) || App.GameState == NULL) return;

    UNIT* unit = App.GameState->TeamData[team].Units;
    while (unit != NULL) {
        if (unit->TypeId >= 0 && unit->TypeId < UNIT_TYPE_COUNT) {
            counts[unit->TypeId]++;
        }
        unit = unit->Next;
    }
}

/************************************************************************/

/// @brief Add queued unit counts from a production building.
static void AddQueuedUnitCounts(const BUILDING* producer, I32* counts) {
    if (producer == NULL || counts == NULL) return;

    for (I32 i = 0; i < producer->UnitQueueCount; i++) {
        I32 typeId = producer->UnitQueue[i].TypeId;
        if (typeId >= 0 && typeId < UNIT_TYPE_COUNT) {
            counts[typeId]++;
        }
    }
}

/************************************************************************/

static I32 CountInfantryUnits(const I32* counts) {
    I32 total = 0;
    if (counts == NULL) return 0;
    total += counts[UNIT_TYPE_TROOPER];
    total += counts[UNIT_TYPE_SOLDIER];
    total += counts[UNIT_TYPE_ENGINEER];
    total += counts[UNIT_TYPE_SCOUT];
    return total;
}

/************************************************************************/

static I32 CountVehicleUnits(const I32* counts) {
    I32 total = 0;
    if (counts == NULL) return 0;
    total += counts[UNIT_TYPE_MOBILE_ARTILLERY];
    total += counts[UNIT_TYPE_TANK];
    total += counts[UNIT_TYPE_TRANSPORT];
    return total;
}

/************************************************************************/

/// @brief Pick an infantry unit type to enqueue for barracks.
static I32 SelectBarracksUnitType(I32 team, I32 mindset, I32 infantryTarget, const BUILDING* barracks) {
    I32 counts[UNIT_TYPE_COUNT];
    GetUnitCounts(team, counts);
    AddQueuedUnitCounts(barracks, counts);

    if (infantryTarget <= 0) return -1;

    if (counts[UNIT_TYPE_SOLDIER] == 0) return UNIT_TYPE_SOLDIER;
    if (counts[UNIT_TYPE_ENGINEER] == 0) return UNIT_TYPE_ENGINEER;

    I32 trooperTarget;
    I32 soldierTarget;
    I32 engineerTarget;

    switch (mindset) {
        case AI_MINDSET_PANIC:
            trooperTarget = (infantryTarget * 6) / 10;
            soldierTarget = (infantryTarget * 3) / 10;
            engineerTarget = infantryTarget / 10;
            break;
        case AI_MINDSET_URGENCY:
            trooperTarget = infantryTarget / 2;
            soldierTarget = infantryTarget / 3;
            engineerTarget = infantryTarget / 6;
            break;
        case AI_MINDSET_IDLE:
        default:
            trooperTarget = infantryTarget / 3;
            soldierTarget = infantryTarget / 2;
            engineerTarget = infantryTarget / 6;
            break;
    }

    if (trooperTarget < 1) trooperTarget = 1;
    if (soldierTarget < 1) soldierTarget = 1;
    if (engineerTarget < 1) engineerTarget = 1;

    I32 bestType = UNIT_TYPE_TROOPER;
    I32 bestScore = 0x7FFFFFFF;
    I32 bestTarget = 0;

    I32 trooperScore = (counts[UNIT_TYPE_TROOPER] * 100) / trooperTarget;
    if (trooperScore < bestScore || (trooperScore == bestScore && trooperTarget > bestTarget)) {
        bestScore = trooperScore;
        bestTarget = trooperTarget;
        bestType = UNIT_TYPE_TROOPER;
    }

    I32 soldierScore = (counts[UNIT_TYPE_SOLDIER] * 100) / soldierTarget;
    if (soldierScore < bestScore || (soldierScore == bestScore && soldierTarget > bestTarget)) {
        bestScore = soldierScore;
        bestTarget = soldierTarget;
        bestType = UNIT_TYPE_SOLDIER;
    }

    I32 engineerScore = (counts[UNIT_TYPE_ENGINEER] * 100) / engineerTarget;
    if (engineerScore < bestScore || (engineerScore == bestScore && engineerTarget > bestTarget)) {
        bestScore = engineerScore;
        bestTarget = engineerTarget;
        bestType = UNIT_TYPE_ENGINEER;
    }

    return bestType;
}

/************************************************************************/

/// @brief Pick a vehicle unit type to enqueue for factory.
static I32 SelectFactoryUnitType(I32 team, I32 mindset, I32 vehicleTarget, const BUILDING* factory) {
    I32 counts[UNIT_TYPE_COUNT];
    GetUnitCounts(team, counts);
    AddQueuedUnitCounts(factory, counts);

    if (vehicleTarget <= 0) return -1;

    if (!HasTechLevel(2, team)) {
        return UNIT_TYPE_TRANSPORT;
    }

    if (counts[UNIT_TYPE_MOBILE_ARTILLERY] == 0) return UNIT_TYPE_MOBILE_ARTILLERY;
    if (counts[UNIT_TYPE_TANK] == 0) return UNIT_TYPE_TANK;
    if (counts[UNIT_TYPE_TRANSPORT] == 0) return UNIT_TYPE_TRANSPORT;

    I32 tankTarget;
    I32 artilleryTarget;
    I32 transportTarget;

    switch (mindset) {
        case AI_MINDSET_PANIC:
            tankTarget = (vehicleTarget * 6) / 10;
            artilleryTarget = vehicleTarget / 3;
            transportTarget = vehicleTarget / 10;
            break;
        case AI_MINDSET_URGENCY:
            tankTarget = vehicleTarget / 2;
            artilleryTarget = vehicleTarget / 3;
            transportTarget = vehicleTarget / 6;
            break;
        case AI_MINDSET_IDLE:
        default:
            tankTarget = vehicleTarget / 2;
            artilleryTarget = vehicleTarget / 3;
            transportTarget = vehicleTarget / 6;
            break;
    }

    if (tankTarget < 1) tankTarget = 1;
    if (artilleryTarget < 1) artilleryTarget = 1;
    if (transportTarget < 1) transportTarget = 1;

    I32 bestType = UNIT_TYPE_TANK;
    I32 bestScore = 0x7FFFFFFF;
    I32 bestTarget = 0;

    I32 tankScore = (counts[UNIT_TYPE_TANK] * 100) / tankTarget;
    if (tankScore < bestScore || (tankScore == bestScore && tankTarget > bestTarget)) {
        bestScore = tankScore;
        bestTarget = tankTarget;
        bestType = UNIT_TYPE_TANK;
    }

    I32 artilleryScore = (counts[UNIT_TYPE_MOBILE_ARTILLERY] * 100) / artilleryTarget;
    if (artilleryScore < bestScore || (artilleryScore == bestScore && artilleryTarget > bestTarget)) {
        bestScore = artilleryScore;
        bestTarget = artilleryTarget;
        bestType = UNIT_TYPE_MOBILE_ARTILLERY;
    }

    I32 transportScore = (counts[UNIT_TYPE_TRANSPORT] * 100) / transportTarget;
    if (transportScore < bestScore || (transportScore == bestScore && transportTarget > bestTarget)) {
        bestScore = transportScore;
        bestTarget = transportTarget;
        bestType = UNIT_TYPE_TRANSPORT;
    }

    return bestType;
}

/************************************************************************/

static I32 RequiredUnitCount(I32 team, I32 unitTypeId) {
    if (!IsValidTeam(team)) return 0;
    if (App.GameState == NULL) return 0;

    switch (unitTypeId) {
        case UNIT_TYPE_DRILLER: {
            I32 drillerCount = CountUnitsOfType(team, UNIT_TYPE_DRILLER);
            I32 totalUnits = (I32)CountUnitsForTeam(team);
            I32 nonDrillerUnits = totalUnits - drillerCount;
            if (nonDrillerUnits < 0) nonDrillerUnits = 0;

            I32 target = AI_DRILLER_TARGET_COUNT;
            if (nonDrillerUnits > 0) {
                I32 ratioTarget = (nonDrillerUnits + AI_DRILLER_PER_NON_DRILLER - 1) / AI_DRILLER_PER_NON_DRILLER;
                if (ratioTarget > target) target = ratioTarget;
            }
            return target;
        }
        case UNIT_TYPE_SCOUT:
            return AI_SCOUT_TARGET_COUNT;
        case UNIT_TYPE_TROOPER: {
            I32 attitude = App.GameState->TeamData[team].AiAttitude;
            I32 mindset = App.GameState->TeamData[team].AiMindset;
            I32 target = (mindset == AI_MINDSET_PANIC) ? AI_MOBILE_TARGET_PANIC :
                (mindset == AI_MINDSET_URGENCY ? AI_MOBILE_TARGET_URGENCY : AI_MOBILE_TARGET_IDLE);
            if (attitude == AI_ATTITUDE_AGGRESSIVE) {
                I32 maxUnits = GetMaxUnitsForMap(App.GameState->MapWidth, App.GameState->MapHeight);
                I32 aggressiveMin = (maxUnits + 1) / 2;
                if (aggressiveMin < 1) aggressiveMin = 1;
                if (target < aggressiveMin) target = aggressiveMin;
            }
            return target;
        }
        default:
            return 0;
    }
}

/************************************************************************/

/// @brief Returns the shortest wrapped delta from origin to target.
static I32 WrapDelta(I32 origin, I32 target, I32 mapSize) {
    I32 delta = target - origin;
    if (mapSize <= 0) return delta;
    if (delta > mapSize / 2) delta -= mapSize;
    if (delta < -mapSize / 2) delta += mapSize;
    return delta;
}

/************************************************************************/

/// @brief Returns the max footprint extent among all mobile units.
static I32 GetMaxUnitExtent(void) {
    I32 maxExtent = 1;
    for (I32 i = 0; i < UNIT_TYPE_COUNT; i++) {
        const UNIT_TYPE* ut = &UnitTypes[i];
        if (ut->Width > maxExtent) maxExtent = ut->Width;
        if (ut->Height > maxExtent) maxExtent = ut->Height;
    }
    return maxExtent;
}

/************************************************************************/

/// @brief Builds the current fortress plan bounds for a team.
static BOOL BuildFortressPlan(I32 team, FORTRESS_PLAN* plan) {
    I32 mapW;
    I32 mapH;
    BUILDING* building;
    BUILDING* yard;
    BOOL first;
    I32 gateWidth;

    if (!IsValidTeam(team) || App.GameState == NULL || plan == NULL) return FALSE;

    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    yard = FindTeamBuilding(team, BUILDING_TYPE_CONSTRUCTION_YARD);
    building = App.GameState->TeamData[team].Buildings;
    if (building == NULL) return FALSE;

    if (yard != NULL) {
        plan->AnchorX = yard->X;
        plan->AnchorY = yard->Y;
    } else {
        plan->AnchorX = building->X;
        plan->AnchorY = building->Y;
    }
    plan->MapW = mapW;
    plan->MapH = mapH;

    first = TRUE;
    while (building != NULL) {
        if (building->TypeId == BUILDING_TYPE_WALL || building->TypeId == BUILDING_TYPE_TURRET) {
            building = building->Next;
            continue;
        }
        const BUILDING_TYPE* bt = GetBuildingTypeById(building->TypeId);
        I32 bw = (bt != NULL) ? bt->Width : 1;
        I32 bh = (bt != NULL) ? bt->Height : 1;

        I32 relLeft = WrapDelta(plan->AnchorX, building->X, mapW);
        I32 relTop = WrapDelta(plan->AnchorY, building->Y, mapH);
        I32 relRight = WrapDelta(plan->AnchorX, building->X + bw - 1, mapW);
        I32 relBottom = WrapDelta(plan->AnchorY, building->Y + bh - 1, mapH);

        I32 minX = (relLeft < relRight) ? relLeft : relRight;
        I32 maxX = (relLeft > relRight) ? relLeft : relRight;
        I32 minY = (relTop < relBottom) ? relTop : relBottom;
        I32 maxY = (relTop > relBottom) ? relTop : relBottom;

        if (first) {
            plan->InnerMinX = minX;
            plan->InnerMaxX = maxX;
            plan->InnerMinY = minY;
            plan->InnerMaxY = maxY;
            first = FALSE;
        } else {
            if (minX < plan->InnerMinX) plan->InnerMinX = minX;
            if (maxX > plan->InnerMaxX) plan->InnerMaxX = maxX;
            if (minY < plan->InnerMinY) plan->InnerMinY = minY;
            if (maxY > plan->InnerMaxY) plan->InnerMaxY = maxY;
        }

        building = building->Next;
    }

    if (first) return FALSE;

    plan->WallMinX = plan->InnerMinX - (FORTRESS_CLEARANCE + 1);
    plan->WallMaxX = plan->InnerMaxX + (FORTRESS_CLEARANCE + 1);
    plan->WallMinY = plan->InnerMinY - (FORTRESS_CLEARANCE + 1);
    plan->WallMaxY = plan->InnerMaxY + (FORTRESS_CLEARANCE + 1);

    gateWidth = GetMaxUnitExtent();
    if (gateWidth < 1) gateWidth = 1;
    I32 wallWidth = plan->WallMaxX - plan->WallMinX + 1;
    I32 wallHeight = plan->WallMaxY - plan->WallMinY + 1;
    I32 maxGate = wallWidth - 2;
    if (wallHeight - 2 < maxGate) maxGate = wallHeight - 2;
    if (maxGate < 1) maxGate = 1;
    if (gateWidth > maxGate) gateWidth = maxGate;
    plan->GateWidth = gateWidth;

    I32 midX = (plan->InnerMinX + plan->InnerMaxX) / 2;
    I32 midY = (plan->InnerMinY + plan->InnerMaxY) / 2;
    I32 half = gateWidth / 2;
    plan->GateMinX = midX - half;
    plan->GateMaxX = plan->GateMinX + gateWidth - 1;
    plan->GateMinY = midY - half;
    plan->GateMaxY = plan->GateMinY + gateWidth - 1;

    return TRUE;
}

/************************************************************************/

/// @brief Tests whether a wall cell is part of a gate opening.
static BOOL IsFortressGateCell(const FORTRESS_PLAN* plan, I32 relX, I32 relY) {
    if (plan == NULL) return FALSE;
    if ((relY == plan->WallMinY || relY == plan->WallMaxY) &&
        relX >= plan->GateMinX && relX <= plan->GateMaxX) {
        return TRUE;
    }
    if ((relX == plan->WallMinX || relX == plan->WallMaxX) &&
        relY >= plan->GateMinY && relY <= plan->GateMaxY) {
        return TRUE;
    }
    return FALSE;
}

/************************************************************************/

/// @brief Finds the next missing wall tile for the fortress perimeter.
static BOOL FindNextFortressWall(I32 team, const FORTRESS_PLAN* plan, I32* outX, I32* outY) {
    if (plan == NULL || App.GameState == NULL) return FALSE;

    for (I32 x = plan->WallMinX; x <= plan->WallMaxX; x++) {
        I32 relX = x;
        I32 relY = plan->WallMinY;
        if (!IsFortressGateCell(plan, relX, relY)) {
            I32 px = WrapCoord(plan->AnchorX, relX, plan->MapW);
            I32 py = WrapCoord(plan->AnchorY, relY, plan->MapH);
            if (!IsAreaBlocked(px, py, 1, 1, NULL, NULL) &&
                IsAreaExploredToTeamWithMargin(px, py, 1, 1, team, 2)) {
                if (outX != NULL) *outX = px;
                if (outY != NULL) *outY = py;
                return TRUE;
            }
        }
        relY = plan->WallMaxY;
        if (!IsFortressGateCell(plan, relX, relY)) {
            I32 px = WrapCoord(plan->AnchorX, relX, plan->MapW);
            I32 py = WrapCoord(plan->AnchorY, relY, plan->MapH);
            if (!IsAreaBlocked(px, py, 1, 1, NULL, NULL) &&
                IsAreaExploredToTeamWithMargin(px, py, 1, 1, team, 2)) {
                if (outX != NULL) *outX = px;
                if (outY != NULL) *outY = py;
                return TRUE;
            }
        }
    }

    for (I32 y = plan->WallMinY + 1; y <= plan->WallMaxY - 1; y++) {
        I32 relY = y;
        I32 relX = plan->WallMinX;
        if (!IsFortressGateCell(plan, relX, relY)) {
            I32 px = WrapCoord(plan->AnchorX, relX, plan->MapW);
            I32 py = WrapCoord(plan->AnchorY, relY, plan->MapH);
            if (!IsAreaBlocked(px, py, 1, 1, NULL, NULL) &&
                IsAreaExploredToTeamWithMargin(px, py, 1, 1, team, 2)) {
                if (outX != NULL) *outX = px;
                if (outY != NULL) *outY = py;
                return TRUE;
            }
        }
        relX = plan->WallMaxX;
        if (!IsFortressGateCell(plan, relX, relY)) {
            I32 px = WrapCoord(plan->AnchorX, relX, plan->MapW);
            I32 py = WrapCoord(plan->AnchorY, relY, plan->MapH);
            if (!IsAreaBlocked(px, py, 1, 1, NULL, NULL) &&
                IsAreaExploredToTeamWithMargin(px, py, 1, 1, team, 2)) {
                if (outX != NULL) *outX = px;
                if (outY != NULL) *outY = py;
                return TRUE;
            }
        }
    }

    return FALSE;
}

/************************************************************************/

/// @brief Validates turret placement for a team.
static BOOL TurretFitsAt(I32 team, I32 x, I32 y, const BUILDING_TYPE* turretType) {
    if (turretType == NULL) return FALSE;
    if (IsAreaBlocked(x, y, turretType->Width, turretType->Height, NULL, NULL)) return FALSE;
    if (!IsAreaExploredToTeamWithMargin(x, y, turretType->Width, turretType->Height, team, 2)) return FALSE;
    return TRUE;
}

/************************************************************************/

/// @brief Finds the next fortress turret position along the wall.
static BOOL FindNextFortressTurret(I32 team, const FORTRESS_PLAN* plan, I32* outX, I32* outY) {
    const BUILDING_TYPE* turretType = GetBuildingTypeById(BUILDING_TYPE_TURRET);
    if (plan == NULL || turretType == NULL) return FALSE;

    I32 w = turretType->Width;
    I32 h = turretType->Height;
    I32 spacing = (w + 2 > FORTRESS_TURRET_SPACING) ? w + 2 : FORTRESS_TURRET_SPACING;

    I32 topY = plan->WallMinY + 1;
    I32 bottomY = plan->WallMaxY - h;
    I32 leftX = plan->WallMinX + 1;
    I32 rightX = plan->WallMaxX - w;
    if (bottomY < topY || rightX < leftX) return FALSE;

    for (I32 x = plan->WallMinX + 1; x <= plan->WallMaxX - w; x += spacing) {
        if (x + w - 1 >= plan->GateMinX && x <= plan->GateMaxX) {
            continue;
        }
        I32 px = WrapCoord(plan->AnchorX, x, plan->MapW);
        I32 py = WrapCoord(plan->AnchorY, topY, plan->MapH);
        if (TurretFitsAt(team, px, py, turretType)) {
            if (outX != NULL) *outX = px;
            if (outY != NULL) *outY = py;
            return TRUE;
        }

        px = WrapCoord(plan->AnchorX, x, plan->MapW);
        py = WrapCoord(plan->AnchorY, bottomY, plan->MapH);
        if (TurretFitsAt(team, px, py, turretType)) {
            if (outX != NULL) *outX = px;
            if (outY != NULL) *outY = py;
            return TRUE;
        }
    }

    for (I32 y = plan->WallMinY + 1; y <= plan->WallMaxY - h; y += spacing) {
        if (y + h - 1 >= plan->GateMinY && y <= plan->GateMaxY) {
            continue;
        }
        I32 px = WrapCoord(plan->AnchorX, leftX, plan->MapW);
        I32 py = WrapCoord(plan->AnchorY, y, plan->MapH);
        if (TurretFitsAt(team, px, py, turretType)) {
            if (outX != NULL) *outX = px;
            if (outY != NULL) *outY = py;
            return TRUE;
        }

        px = WrapCoord(plan->AnchorX, rightX, plan->MapW);
        py = WrapCoord(plan->AnchorY, y, plan->MapH);
        if (TurretFitsAt(team, px, py, turretType)) {
            if (outX != NULL) *outX = px;
            if (outY != NULL) *outY = py;
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/// @brief Returns the next fortress placement spot for a wall or turret.
BOOL FindFortressPlacement(I32 team, I32 typeId, I32* outX, I32* outY) {
    FORTRESS_PLAN plan;

    if (typeId != BUILDING_TYPE_WALL && typeId != BUILDING_TYPE_TURRET) return FALSE;
    if (!BuildFortressPlan(team, &plan)) return FALSE;

    if (typeId == BUILDING_TYPE_WALL) {
        return FindNextFortressWall(team, &plan, outX, outY);
    }
    return FindNextFortressTurret(team, &plan, outX, outY);
}

/************************************************************************/

/// @brief Determines if the AI should invest in fortress construction this tick.
static BOOL ShouldInvestInFortress(I32 attitude, I32 plasma, I32 cost) {
    if (cost <= 0) return FALSE;
    if (attitude == AI_ATTITUDE_DEFENSIVE) {
        return plasma >= cost;
    }
    if (attitude == AI_ATTITUDE_AGGRESSIVE) {
        if (plasma < cost * 2) return FALSE;
        return (SimpleRandom() % AI_PERCENT_BASE) < AI_FORTRESS_AGGRESSIVE_CHANCE_PERCENT;
    }
    return FALSE;
}

/************************************************************************/

/// @brief Attempts to queue a wall or turret based on the fortress plan.
static void TryQueueFortressBuilding(I32 team, I32 attitude) {
    TEAM_RESOURCES* res;
    BUILDING* yard;
    const BUILDING_TYPE* type;
    I32 placeX;
    I32 placeY;

    if (!IsValidTeam(team) || App.GameState == NULL) return;
    if (!HasTechLevel(2, team)) return;

    yard = FindTeamBuilding(team, BUILDING_TYPE_CONSTRUCTION_YARD);
    if (yard == NULL) return;
    if (yard->BuildQueueCount >= MAX_PLACEMENT_QUEUE) return;

    if (FindFortressPlacement(team, BUILDING_TYPE_WALL, &placeX, &placeY)) {
        type = GetBuildingTypeById(BUILDING_TYPE_WALL);
    } else if (FindFortressPlacement(team, BUILDING_TYPE_TURRET, &placeX, &placeY)) {
        type = GetBuildingTypeById(BUILDING_TYPE_TURRET);
    } else {
        return;
    }

    if (type == NULL) return;
    res = GetTeamResources(team);
    if (res == NULL) return;
    if (!ShouldInvestInFortress(attitude, res->Plasma, type->CostPlasma)) return;
    AiQueueBuildingForTeam(team, type->Id);
}

/************************************************************************/

static void UpdateAIForTeam(I32 team, I32 mindset) {
    TEAM_RESOURCES* res = GetTeamResources(team);
    BUILDING* yard = FindTeamBuilding(team, BUILDING_TYPE_CONSTRUCTION_YARD);
    BOOL hasBarracks = FindTeamBuilding(team, BUILDING_TYPE_BARRACKS) != NULL;
    BOOL hasFactory = FindTeamBuilding(team, BUILDING_TYPE_FACTORY) != NULL;
    BOOL hasTechCenter = FindTeamBuilding(team, BUILDING_TYPE_TECH_CENTER) != NULL;
    const BUILDING_TYPE* techType = GetBuildingTypeById(BUILDING_TYPE_TECH_CENTER);
    BOOL energyLow = FALSE;
    BOOL queuedBuilding = FALSE;
    I32 drillerCount;
    I32 queuedDrillers;
    I32 drillerTarget;
    I32 mobileTarget;
    I32 mobileCount;
    I32 infantryTarget;
    I32 vehicleTarget;
    I32 counts[UNIT_TYPE_COUNT];
    BOOL allowUnitProduction = TRUE;

    if (res == NULL || yard == NULL) return;

    energyLow = (res->Energy <= 0 && res->MaxEnergy <= AI_ENERGY_LOW_MAX);
    drillerCount = CountUnitsOfType(team, UNIT_TYPE_DRILLER);
    queuedDrillers = CountQueuedUnitType(FindTeamBuilding(team, BUILDING_TYPE_FACTORY), UNIT_TYPE_DRILLER);
    drillerTarget = RequiredUnitCount(team, UNIT_TYPE_DRILLER);

    /* Build priority */
    if (drillerCount + queuedDrillers < drillerTarget && !hasFactory) {
        queuedBuilding = AiQueueBuildingForTeam(team, BUILDING_TYPE_FACTORY);
    }
    if (!queuedBuilding && !hasBarracks) {
        queuedBuilding = AiQueueBuildingForTeam(team, BUILDING_TYPE_BARRACKS);
    }
    if (!queuedBuilding && energyLow) {
        queuedBuilding = AiQueueBuildingForTeam(team, BUILDING_TYPE_POWER_PLANT);
    }
    if (!queuedBuilding && !hasTechCenter && techType != NULL && res->Plasma >= techType->CostPlasma) {
        queuedBuilding = AiQueueBuildingForTeam(team, BUILDING_TYPE_TECH_CENTER);
    }
    if (!queuedBuilding && !hasFactory) {
        AiQueueBuildingForTeam(team, BUILDING_TYPE_FACTORY);
    }

    if (!hasTechCenter && techType != NULL && res->Plasma < techType->CostPlasma &&
        mindset == AI_MINDSET_IDLE && (drillerCount + queuedDrillers) >= drillerTarget) {
        I32 mobileCount = CountMobileUnits(team);
        if (mobileCount >= AI_IDLE_MIN_DEFENSE) {
            allowUnitProduction = FALSE;
            if (App.GameState->TeamData[team].AiAttitude == AI_ATTITUDE_AGGRESSIVE) {
                I32 aggressiveTarget = RequiredUnitCount(team, UNIT_TYPE_TROOPER);
                if (mobileCount < aggressiveTarget) {
                    allowUnitProduction = TRUE;
                }
            }
        }
    }

    TryQueueFortressBuilding(team, App.GameState->TeamData[team].AiAttitude);

    /* Unit production */
    if (!allowUnitProduction) return;
    mobileTarget = RequiredUnitCount(team, UNIT_TYPE_TROOPER);
    mobileCount = CountMobileUnits(team);
    GetUnitCounts(team, counts);

    if (hasBarracks) {
        BUILDING* barracks = FindTeamBuilding(team, BUILDING_TYPE_BARRACKS);
        I32 infantryCounts[UNIT_TYPE_COUNT];
        for (I32 i = 0; i < UNIT_TYPE_COUNT; i++) {
            infantryCounts[i] = counts[i];
        }
        AddQueuedUnitCounts(barracks, infantryCounts);
        if (hasFactory) {
            vehicleTarget = mobileTarget / 3;
            infantryTarget = mobileTarget - vehicleTarget;
        } else {
            infantryTarget = mobileTarget;
        }
        if (infantryTarget < 0) infantryTarget = 0;

        if (mobileCount < mobileTarget && CountInfantryUnits(infantryCounts) < infantryTarget) {
            I32 unitTypeId = SelectBarracksUnitType(team, mindset, infantryTarget, barracks);
            if (unitTypeId >= 0) {
                AiProduceUnit(team, unitTypeId, barracks);
            }
        }
    }
    if (hasFactory) {
        BUILDING* factory = FindTeamBuilding(team, BUILDING_TYPE_FACTORY);
        if (factory != NULL) {
            I32 vehicleCounts[UNIT_TYPE_COUNT];
            for (I32 i = 0; i < UNIT_TYPE_COUNT; i++) {
                vehicleCounts[i] = counts[i];
            }
            AddQueuedUnitCounts(factory, vehicleCounts);
            if (drillerCount + queuedDrillers < drillerTarget) {
                AiProduceUnit(team, UNIT_TYPE_DRILLER, factory);
            } else if (mobileTarget > 0) {
                if (hasBarracks) {
                    vehicleTarget = mobileTarget / 3;
                } else {
                    vehicleTarget = mobileTarget;
                }
                if (vehicleTarget < 0) vehicleTarget = 0;

                if (mobileCount < mobileTarget && CountVehicleUnits(vehicleCounts) < vehicleTarget) {
                    I32 unitTypeId = SelectFactoryUnitType(team, mindset, vehicleTarget, factory);
                    if (unitTypeId >= 0) {
                        AiProduceUnit(team, unitTypeId, factory);
                    }
                }
            }
        }
    }
}

/************************************************************************/



static void AssignScoutOrders(I32 team) {
    UNIT* scouts[2];
    I32 scoutCount = 0;

    if (!IsValidTeam(team) || App.GameState == NULL) return;

    UNIT* unit = App.GameState->TeamData[team].Units;
    while (unit != NULL && scoutCount < 2) {
        const UNIT_TYPE* ut = GetUnitTypeById(unit->TypeId);
        if (ut != NULL && ut->Id == UNIT_TYPE_SCOUT) {
            scouts[scoutCount++] = unit;
        }
        unit = unit->Next;
    }

    BUILDING* barracks = FindTeamBuilding(team, BUILDING_TYPE_BARRACKS);
    I32 queuedScouts = CountQueuedUnitType(barracks, UNIT_TYPE_SCOUT);
    I32 targetScouts = RequiredUnitCount(team, UNIT_TYPE_SCOUT);
    while (scoutCount + queuedScouts < targetScouts) {
        if (barracks == NULL) {
            break;
        }
        if (!AiProduceUnit(team, UNIT_TYPE_SCOUT, barracks)) {
            break;
        }
        queuedScouts++;
    }

    for (I32 i = 0; i < scoutCount; i++) {
        UNIT* scout = scouts[i];
        if (scout == NULL) continue;
        if (scout->State != UNIT_STATE_EXPLORE) {
            I32 tx;
            I32 ty;
            if (PickExplorationTarget(team, &tx, &ty)) {
                SetUnitStateExplore(scout, tx, ty);
            }
        }
    }
}

/************************************************************************/

/// @brief Find the strongest combat unit in a team.
static UNIT* FindStrongestCombatUnit(I32 team, const UNIT* exclude) {
    UNIT* best = NULL;
    I32 bestScore = -1;

    if (!IsValidTeam(team) || App.GameState == NULL) return NULL;

    UNIT* unit = App.GameState->TeamData[team].Units;
    while (unit != NULL) {
        if (unit == exclude) {
            unit = unit->Next;
            continue;
        }

        const UNIT_TYPE* ut = GetUnitTypeById(unit->TypeId);
        if (ut != NULL && ut->Damage > 0) {
            I32 score = ComputeUnitScore(ut);
            if (best == NULL || score > bestScore) {
                best = unit;
                bestScore = score;
            }
        }
        unit = unit->Next;
    }

    return best;
}

/************************************************************************/

/// @brief Find the first driller unit for a team.
static UNIT* FindFirstDriller(I32 team) {
    UNIT* unit = IsValidTeam(team) ? App.GameState->TeamData[team].Units : NULL;
    while (unit != NULL) {
        if (unit->TypeId == UNIT_TYPE_DRILLER) return unit;
        unit = unit->Next;
    }
    return NULL;
}

/************************************************************************/

/// @brief Assign drillers to their nearest plasma fields.
static void AssignDrillerOrders(I32 team) {
    if (!IsValidTeam(team) || App.GameState == NULL) return;

    I32 avoidRadius = GetMaxUnitSight();

    UNIT* unit = App.GameState->TeamData[team].Units;
    while (unit != NULL) {
        if (unit->TypeId == UNIT_TYPE_DRILLER) {
            I32 tx;
            I32 ty;
            if (FindNearestSafePlasmaCell(team, unit->X, unit->Y, avoidRadius, &tx, &ty)) {
                if (unit->State != UNIT_STATE_EXPLORE || unit->StateTargetX != tx || unit->StateTargetY != ty) {
                    SetUnitStateExplore(unit, tx, ty);
                }
            }
        }
        unit = unit->Next;
    }
}

/************************************************************************/

/// @brief Clear escort state for units currently escorting a driller.
static void ClearDrillerEscorts(I32 team, I32 drillerId) {
    UNIT* unit = IsValidTeam(team) ? App.GameState->TeamData[team].Units : NULL;
    while (unit != NULL) {
        if (unit->State == UNIT_STATE_ESCORT && unit->EscortUnitId == drillerId) {
            SetUnitStateIdle(unit);
        }
        unit = unit->Next;
    }
}

/************************************************************************/

/// @brief Keep a driller protected depending on AI attitude.
static void UpdateDrillerEscort(I32 team, I32 attitude) {
    UNIT* driller = FindFirstDriller(team);
    if (driller == NULL) return;

    UNIT* escort = FindStrongestCombatUnit(team, driller);
    if (escort == NULL) return;

    if (attitude == AI_ATTITUDE_DEFENSIVE) {
        ClearDrillerEscorts(team, driller->Id);
        SetUnitStateEscort(escort, driller->Team, driller->Id);
        return;
    }

    U32 now = GetSystemTime();
    BOOL underAttack = (driller->LastDamageTime != 0 && now - driller->LastDamageTime <= AI_DRILLER_ALERT_MS);
    if (underAttack) {
        SetUnitStateEscort(escort, driller->Team, driller->Id);
    } else {
        ClearDrillerEscorts(team, driller->Id);
    }
}

/************************************************************************/

static void UpdateAggressiveOrders(I32 team) {
    if (!IsValidTeam(team) || App.GameState == NULL) return;

    I32 availableForce = 0;
    UNIT* unit = App.GameState->TeamData[team].Units;
    while (unit != NULL) {
        const UNIT_TYPE* ut = GetUnitTypeById(unit->TypeId);
        if (ut != NULL && ut->Damage > 0 &&
            ut->Id != UNIT_TYPE_SCOUT && ut->Id != UNIT_TYPE_DRILLER &&
            unit->State == UNIT_STATE_IDLE) {
            availableForce += ComputeUnitScore(ut);
        }
        unit = unit->Next;
    }

    I32 targetX;
    I32 targetY;
    I32 targetScore;
    if (!GetAttackClusterTarget(team, availableForce, &targetX, &targetY, &targetScore)) {
        return;
    }

    unit = App.GameState->TeamData[team].Units;
    while (unit != NULL) {
        const UNIT_TYPE* ut = GetUnitTypeById(unit->TypeId);
        if (ut != NULL && ut->Damage > 0 &&
            ut->Id != UNIT_TYPE_SCOUT && ut->Id != UNIT_TYPE_DRILLER &&
            unit->State == UNIT_STATE_IDLE &&
            !unit->IsMoving) {
            SetUnitMoveTarget(unit, targetX, targetY);
        }
        unit = unit->Next;
    }
}

/************************************************************************/

void ProcessAITeams(void) {
    if (App.GameState == NULL) return;

    U32 now = App.GameState->GameTime;
    I32 teamCount = GetTeamCountSafe();
    for (I32 team = 1; team < teamCount; team++) {
        if (IsTeamEliminated(team)) {
            RemoveTeamEntities(team);
            continue;
        }
        if (!ShouldProcessAITeam(team, now)) continue;

        I32 enemyNearby = 0;
        I32 friendlyNearby = 0;
        BOOL canAfford = CanAffordCheapestMobileUnit(team);

        EvaluateThreatNearTeamBuildings(team, AI_THREAT_RADIUS_DEFAULT, &enemyNearby, &friendlyNearby);
        BOOL threatActive = (enemyNearby > friendlyNearby);

        I32 currentMindset = App.GameState->TeamData[team].AiMindset;
        I32 nextMindset = currentMindset;

        switch (currentMindset) {
            case AI_MINDSET_IDLE:
                if (threatActive) {
                    nextMindset = canAfford ? AI_MINDSET_URGENCY : AI_MINDSET_PANIC;
                }
                break;

            case AI_MINDSET_URGENCY:
                if (threatActive && !canAfford) {
                    nextMindset = AI_MINDSET_PANIC;
                } else if (!threatActive) {
                    nextMindset = AI_MINDSET_IDLE;
                }
                break;

            case AI_MINDSET_PANIC:
                if (threatActive && canAfford) {
                    nextMindset = AI_MINDSET_URGENCY;
                } else if (!threatActive) {
                    nextMindset = AI_MINDSET_IDLE;
                }
                break;

            default:
                nextMindset = AI_MINDSET_IDLE;
                break;
        }

        App.GameState->TeamData[team].AiMindset = nextMindset;

        UpdateAIForTeam(team, nextMindset);
        AssignScoutOrders(team);
        AssignDrillerOrders(team);
        UpdateDrillerEscort(team, App.GameState->TeamData[team].AiAttitude);
        if (App.GameState->TeamData[team].AiAttitude == AI_ATTITUDE_AGGRESSIVE) {
            UpdateAggressiveOrders(team);
        }
    }
}
