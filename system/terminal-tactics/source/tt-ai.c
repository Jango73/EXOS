
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
#include "tt-ai-internal.h"
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

static void SetAiLastDecision(I32 team, const char* decision) {
    if (App.GameState == NULL) return;
    if (!IsValidTeam(team)) return;
    if (decision == NULL) decision = "";
    snprintf(App.GameState->TeamData[team].AiLastDecision,
             sizeof(App.GameState->TeamData[team].AiLastDecision),
             "%s",
             decision);
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
                if (App.GameState->GhostMode && memory[idx].Team == HUMAN_TEAM_INDEX) continue;
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

static I32 AiDrillerEscortMinForce = 0;

/************************************************************************/

/// @brief Compute the AI strength score for a unit type.
I32 AiComputeUnitScore(const UNIT_TYPE* unitType) {
    if (unitType == NULL) return 0;
    return unitType->Damage * AI_UNIT_SCORE_DAMAGE_WEIGHT + unitType->MaxHp;
}

/************************************************************************/

/// @brief Initialize AI constants computed from unit stats.
void InitializeAiConstants(void) {
    I32 best = 0;

    for (I32 i = 1; i <= UNIT_TYPE_COUNT; i++) {
        const UNIT_TYPE* unitType = GetUnitTypeById(i);
        if (unitType == NULL) continue;
        if (unitType->Damage <= 0) continue;
        if (unitType->Id == UNIT_TYPE_SCOUT || unitType->Id == UNIT_TYPE_DRILLER) continue;

        I32 score = AiComputeUnitScore(unitType);
        if (score > best) best = score;
    }

    AiDrillerEscortMinForce = best;
}

/************************************************************************/

static I32 ComputeTurretScore(const BUILDING_TYPE* buildingType) {
    if (buildingType == NULL) return 0;
    return buildingType->MaxHp;
}

/************************************************************************/

static I32 CountQueuedBuildingType(const BUILDING* yard, I32 typeId) {
    if (yard == NULL || typeId <= 0) return 0;
    I32 count = 0;
    for (I32 i = 0; i < yard->BuildQueueCount; i++) {
        if (yard->BuildQueue[i].TypeId == typeId) {
            count++;
        }
    }
    return count;
}

/************************************************************************/

static I32 ComputeAvailableEscortForce(I32 Team) {
    I32 force = 0;
    UNIT* Unit = IsValidTeam(Team) ? App.GameState->TeamData[Team].Units : NULL;
    while (Unit != NULL) {
        const UNIT_TYPE* ut = GetUnitTypeById(Unit->TypeId);
        if (ut != NULL &&
            ut->Damage > 0 &&
            ut->Id != UNIT_TYPE_SCOUT &&
            ut->Id != UNIT_TYPE_DRILLER) {
            force += AiComputeUnitScore(ut);
        }
        Unit = Unit->Next;
    }
    return force;
}

/************************************************************************/

static void UpdatePlannedBuilding(AI_CONTEXT* Context) {
    I32 plannedTypeId;
    const BUILDING_TYPE* plannedType;
    BOOL hasBarracks;
    BOOL hasFactory;
    BOOL hasTech;

    if (Context == NULL) return;

    Context->PlannedBuildingTypeId = -1;
    Context->PlannedBuildingCost = 0;
    if (!Context->YardHasSpace) return;

    hasBarracks = Context->HasBarracks || Context->QueuedBarracks > 0;
    hasFactory = Context->HasFactory || Context->QueuedFactory > 0;
    hasTech = Context->HasTechCenter || Context->QueuedTechCenter > 0;

    plannedTypeId = -1;
    if (Context->PowerPlantType != NULL && Context->EnergyLow) {
        plannedTypeId = BUILDING_TYPE_POWER_PLANT;
    } else if (Context->BarracksType != NULL && !hasBarracks) {
        plannedTypeId = BUILDING_TYPE_BARRACKS;
    } else if (Context->FactoryType != NULL &&
               !hasFactory &&
               (Context->DrillerCount + Context->QueuedDrillers) < Context->DrillerTarget) {
        plannedTypeId = BUILDING_TYPE_FACTORY;
    } else if (Context->TechCenterType != NULL && !hasTech) {
        plannedTypeId = BUILDING_TYPE_TECH_CENTER;
    } else if (Context->FactoryType != NULL && !hasFactory) {
        plannedTypeId = BUILDING_TYPE_FACTORY;
    } else if (Context->FortressTypeId >= 0) {
        plannedTypeId = Context->FortressTypeId;
    }

    if (plannedTypeId < 0) return;

    plannedType = GetBuildingTypeById(plannedTypeId);
    if (plannedType == NULL) return;

    Context->PlannedBuildingTypeId = plannedTypeId;
    Context->PlannedBuildingCost = plannedType->CostPlasma;
}

/************************************************************************/

static BOOL IsHostileMemoryCell(I32 team, const MEMORY_CELL* cell, BOOL* outIsBuilding, I32* outTypeId) {
    if (cell == NULL) return FALSE;
    if (cell->OccupiedType == 0) return FALSE;
    if (cell->Team == team) return FALSE;
    if (App.GameState != NULL && App.GameState->GhostMode && cell->Team == HUMAN_TEAM_INDEX) return FALSE;

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

static I32 ComputeKnownEnemyForce(I32 Team) {
    I32 mapW = App.GameState != NULL ? App.GameState->MapWidth : 0;
    I32 mapH = App.GameState != NULL ? App.GameState->MapHeight : 0;
    MEMORY_CELL* memory = (App.GameState != NULL && IsValidTeam(Team)) ? App.GameState->TeamData[Team].MemoryMap : NULL;
    U8* visible = (App.GameState != NULL && IsValidTeam(Team)) ? App.GameState->TeamData[Team].VisibleNow : NULL;
    if (App.GameState == NULL || mapW <= 0 || mapH <= 0 || memory == NULL || visible == NULL) return 0;

    I32 force = 0;
    for (I32 y = 0; y < mapH; y++) {
        for (I32 x = 0; x < mapW; x++) {
            size_t idx = (size_t)y * (size_t)mapW + (size_t)x;
            if (visible[idx] == 0) continue;
            BOOL isBuilding = FALSE;
            I32 typeId = 0;
            if (!IsHostileMemoryCell(Team, &memory[idx], &isBuilding, &typeId)) continue;
            if (!IsHostileAnchorCell(Team, x, y, isBuilding, typeId, memory, mapW, mapH)) continue;

            if (isBuilding) {
                const BUILDING_TYPE* bt = GetBuildingTypeById(typeId);
                if (bt != NULL) {
                    force += ComputeTurretScore(bt);
                }
            } else {
                const UNIT_TYPE* ut = GetUnitTypeById(typeId);
                if (ut != NULL) {
                    force += AiComputeUnitScore(ut);
                }
            }
        }
    }

    return force;
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
                            clusterScore += AiComputeUnitScore(ut);
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

/// @brief Get the best attack cluster target for the team.
BOOL GetAttackClusterTarget(I32 Team, I32 AvailableForce, I32* OutTargetX, I32* OutTargetY, I32* OutTargetScore) {
    if (!IsValidTeam(Team) || App.GameState == NULL) return FALSE;

    U32 Now = App.GameState->GameTime;
    U32 Last = App.GameState->TeamData[Team].AiLastClusterUpdate;
    if (Last != 0 && Now - Last < AI_CLUSTER_UPDATE_INTERVAL_MS) return FALSE;

    App.GameState->TeamData[Team].AiLastClusterUpdate = Now;
    return FindAttackCluster(Team, AvailableForce, OutTargetX, OutTargetY, OutTargetScore);
}

/************************************************************************/

static void ApplyDamageToUnit(I32 targetTeam, UNIT* target, I32 damage, U32 now) {
    if (target == NULL) return;
    if (damage <= 0) return;
    LogTeamAction(targetTeam, "UnitDamaged", (U32)target->Id, (U32)target->X, (U32)target->Y,
                  "", "");
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
    LogTeamAction(targetTeam, "BuildingDamaged", (U32)target->Id, (U32)target->X, (U32)target->Y,
                  "", "");
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

/// @brief Return a priority value for building attack targeting (lower is higher priority).
static I32 GetBuildingAttackPriority(I32 typeId) {
    switch (typeId) {
        case BUILDING_TYPE_TURRET:
            return 0;
        case BUILDING_TYPE_CONSTRUCTION_YARD:
            return 1;
        case BUILDING_TYPE_FACTORY:
            return 2;
        case BUILDING_TYPE_BARRACKS:
            return 3;
        case BUILDING_TYPE_POWER_PLANT:
            return 4;
        case BUILDING_TYPE_WALL:
            return 5;
        default:
            return 6;
    }
}

/************************************************************************/

static BOOL TryAttackTargetsFromList(I32 attackerTeam, I32 originX, I32 originY, I32 attackRange,
                                     I32 baseDamage, const char* attackerName, U32 attackerId, U32 currentTime,
                                     U32* inOutLastAttackTime) {
    if (App.GameState == NULL) return FALSE;
    if (!IsValidTeam(attackerTeam)) return FALSE;
    if (baseDamage <= 0) return FALSE;

    TEAM_DATA* teamData = &App.GameState->TeamData[attackerTeam];
    if (teamData == NULL) return FALSE;

    I32 mapW = App.GameState->MapWidth;
    I32 mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    BOOL ghostBlock = (App.GameState->GhostMode && attackerTeam != HUMAN_TEAM_INDEX);
    BUILDING* bestBuilding = NULL;
    const BUILDING_TYPE* bestBuildingType = NULL;
    I32 bestBuildingTeam = -1;
    I32 bestPriority = 0x7FFFFFFF;
    I32 bestBuildingId = 0x7FFFFFFF;

    for (I32 i = 0; i < teamData->VisibleEnemyUnitCount; i++) {
        VISIBLE_ENTITY entry = teamData->VisibleEnemyUnits[i];
        UNIT* enemyUnit = FindUnitById(entry.Team, entry.Id);
        if (enemyUnit == NULL) continue;
        if (ghostBlock && enemyUnit->Team == HUMAN_TEAM_INDEX) continue;
        const UNIT_TYPE* enemyType = GetUnitTypeById(enemyUnit->TypeId);
        if (enemyType != NULL &&
            IsTargetInRange(originX, originY, attackRange, enemyUnit->X, enemyUnit->Y, enemyType->Width, enemyType->Height, mapW, mapH)) {
            I32 dmg = ComputeMitigatedDamage(baseDamage, enemyType->Armor);
            LogTeamAction(attackerTeam, "AttackUnit", attackerId, (U32)enemyUnit->Id, (U32)enemyUnit->Team,
                          attackerName, enemyType->Name);
            ApplyDamageToUnit(entry.Team, enemyUnit, dmg, currentTime);
            if (inOutLastAttackTime != NULL) {
                *inOutLastAttackTime = currentTime;
            }
            return TRUE;
        }
    }

    for (I32 i = 0; i < teamData->VisibleEnemyBuildingCount; i++) {
        VISIBLE_ENTITY entry = teamData->VisibleEnemyBuildings[i];
        BUILDING* enemyBuilding = FindBuildingById(entry.Team, entry.Id);
        if (enemyBuilding == NULL) continue;
        if (ghostBlock && enemyBuilding->Team == HUMAN_TEAM_INDEX) continue;
        const BUILDING_TYPE* enemyType = GetBuildingTypeById(enemyBuilding->TypeId);
        if (enemyType == NULL) continue;

        I32 width = enemyType->Width;
        I32 height = enemyType->Height;
        if (!IsTargetInRange(originX, originY, attackRange, enemyBuilding->X, enemyBuilding->Y, width, height, mapW, mapH)) {
            continue;
        }

        I32 priority = GetBuildingAttackPriority(enemyType->Id);
        if (priority < bestPriority ||
            (priority == bestPriority && enemyBuilding->Id < bestBuildingId)) {
            bestPriority = priority;
            bestBuilding = enemyBuilding;
            bestBuildingType = enemyType;
            bestBuildingTeam = entry.Team;
            bestBuildingId = enemyBuilding->Id;
        }
    }

    if (bestBuilding != NULL && bestBuildingType != NULL) {
        I32 dmg = ComputeMitigatedDamage(baseDamage, bestBuildingType->Armor);
        LogTeamAction(attackerTeam, "AttackBuilding", attackerId, (U32)bestBuilding->Id, (U32)bestBuildingTeam,
                      attackerName, bestBuildingType->Name);
        ApplyDamageToBuilding(bestBuildingTeam, bestBuilding, dmg);
        if (inOutLastAttackTime != NULL) {
            *inOutLastAttackTime = currentTime;
        }
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

static BOOL TryAttackTargets(UNIT* attacker, const UNIT_TYPE* attackerType, U32 currentTime) {
    if (App.GameState == NULL || attacker == NULL || attackerType == NULL) return FALSE;
    if (attackerType->Damage <= 0) return FALSE;
    /* Use vision as effective attack envelope (range and sight are treated the same here) */
    I32 attackRange = (attackerType->Sight > 0) ? attackerType->Sight : 1;
    return TryAttackTargetsFromList(attacker->Team, attacker->X, attacker->Y, attackRange,
                                    attackerType->Damage, attackerType->Name, (U32)attacker->Id,
                                    currentTime, &attacker->LastAttackTime);
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

void ProcessTurretAttacks(U32 currentTime) {
    if (App.GameState == NULL) return;
    I32 teamCount = GetTeamCountSafe();
    if (teamCount <= 0) return;

    for (I32 team = 0; team < teamCount; team++) {
        BUILDING* building = App.GameState->TeamData[team].Buildings;
        while (building != NULL) {
            if (building->TypeId == BUILDING_TYPE_TURRET && IsBuildingPowered(building)) {
                const BUILDING_TYPE* bt = GetBuildingTypeById(building->TypeId);
                if (bt != NULL) {
                    I32 attackInterval = (bt->AttackSpeed > 0) ? bt->AttackSpeed : UNIT_ATTACK_INTERVAL_MS;
                    if (building->LastAttackTime == 0 || currentTime - building->LastAttackTime >= (U32)attackInterval) {
                        I32 centerX = building->X + bt->Width / 2;
                        I32 centerY = building->Y + bt->Height / 2;
                        I32 range = (bt->Range > 0) ? bt->Range : 1;
                        I32 damage = bt->Damage;
                        TryAttackTargetsFromList(team, centerX, centerY, range,
                                                damage, bt->Name, (U32)building->Id,
                                                currentTime, &building->LastAttackTime);
                    }
                }
            }
            building = building->Next;
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

BOOL AiQueueBuildingForTeam(I32 team, I32 typeId) {
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
    LogTeamAction(team, "QueueBuilding", (U32)type->Id, (U32)type->CostPlasma,
                  (U32)yard->BuildQueueCount, type->Name, "");
    return TRUE;
}

/************************************************************************/

BOOL AiProduceUnit(I32 team, I32 unitTypeId, BUILDING* producer) {
    BOOL result = EnqueueUnitProduction(producer, unitTypeId, team, NULL);
    if (result) {
        const UNIT_TYPE* ut = GetUnitTypeById(unitTypeId);
        const BUILDING_TYPE* bt = (producer != NULL) ? GetBuildingTypeById(producer->TypeId) : NULL;
        LogTeamAction(team, "QueueUnit", (U32)unitTypeId, (U32)(producer != NULL ? producer->Id : 0),
                      0, ut != NULL ? ut->Name : "Unknown", bt != NULL ? bt->Name : "Unknown");
    }
    return result;
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
I32 SelectBarracksUnitType(I32 team, I32 mindset, I32 infantryTarget, const BUILDING* barracks) {
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
I32 SelectFactoryUnitType(I32 team, I32 mindset, I32 vehicleTarget, const BUILDING* factory) {
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
            } else if (attitude == AI_ATTITUDE_DEFENSIVE) {
                I32 maxUnits = GetMaxUnitsForMap(App.GameState->MapWidth, App.GameState->MapHeight);
                I32 defensiveMin = (maxUnits + 2) / 3;
                if (defensiveMin < 1) defensiveMin = 1;
                if (target < defensiveMin) target = defensiveMin;
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

/// @brief Select a fortress building type to queue when a placement exists.
static I32 SelectFortressQueueType(I32 Team, I32 Attitude, I32 Plasma) {
    const BUILDING_TYPE* Type;
    I32 PlaceX;
    I32 PlaceY;

    if (!IsValidTeam(Team) || App.GameState == NULL) return -1;
    if (!HasTechLevel(2, Team)) return -1;

    if (FindFortressPlacement(Team, BUILDING_TYPE_WALL, &PlaceX, &PlaceY)) {
        Type = GetBuildingTypeById(BUILDING_TYPE_WALL);
    } else if (FindFortressPlacement(Team, BUILDING_TYPE_TURRET, &PlaceX, &PlaceY)) {
        Type = GetBuildingTypeById(BUILDING_TYPE_TURRET);
    } else {
        return -1;
    }

    if (Type == NULL) return -1;
    if (!ShouldInvestInFortress(Attitude, Plasma, Type->CostPlasma)) return -1;
    return Type->Id;
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
            I32 score = AiComputeUnitScore(ut);
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

/// @brief Return TRUE if at least one combat unit can be assigned as an escort.
static BOOL HasAvailableEscortCandidate(I32 team) {
    if (!IsValidTeam(team) || App.GameState == NULL) return FALSE;

    UNIT* unit = App.GameState->TeamData[team].Units;
    while (unit != NULL) {
        const UNIT_TYPE* ut = GetUnitTypeById(unit->TypeId);
        if (ut != NULL &&
            ut->Damage > 0 &&
            ut->Id != UNIT_TYPE_SCOUT &&
            ut->Id != UNIT_TYPE_DRILLER &&
            unit->State != UNIT_STATE_ESCORT) {
            return TRUE;
        }
        unit = unit->Next;
    }
    return FALSE;
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

/// @brief Clear escort state for units currently escorting a driller.
void ClearDrillerEscorts(I32 Team, I32 DrillerId) {
    UNIT* Unit = IsValidTeam(Team) ? App.GameState->TeamData[Team].Units : NULL;
    while (Unit != NULL) {
        if (Unit->State == UNIT_STATE_ESCORT && Unit->EscortUnitId == DrillerId) {
            SetUnitStateIdle(Unit);
            LogTeamAction(Team, "ClearEscort", (U32)Unit->Id, (U32)DrillerId, 0, "", "");
        }
        Unit = Unit->Next;
    }
}

/************************************************************************/

/// @brief Get escort status for a driller within the team.
static void GetDrillerEscortStatus(I32 Team, I32 DrillerId, const UNIT* PreferredEscort,
                                   I32* OutEscortForce, I32* OutEscortCount,
                                   BOOL* OutPreferredAssigned, BOOL* OutHasExtra) {
    BOOL PreferredAssigned = FALSE;
    BOOL HasExtra = FALSE;
    I32 EscortForce = 0;
    I32 EscortCount = 0;

    UNIT* Unit = IsValidTeam(Team) ? App.GameState->TeamData[Team].Units : NULL;
    while (Unit != NULL) {
        if (Unit->State == UNIT_STATE_ESCORT &&
            Unit->EscortUnitId == DrillerId &&
            Unit->EscortUnitTeam == Team) {
            EscortCount++;
            if (PreferredEscort != NULL && Unit == PreferredEscort) {
                PreferredAssigned = TRUE;
            } else {
                HasExtra = TRUE;
            }
            {
                const UNIT_TYPE* ut = GetUnitTypeById(Unit->TypeId);
                if (ut != NULL) {
                    EscortForce += AiComputeUnitScore(ut);
                }
            }
        }
        Unit = Unit->Next;
    }

    if (OutEscortForce != NULL) *OutEscortForce = EscortForce;
    if (OutEscortCount != NULL) *OutEscortCount = EscortCount;
    if (OutPreferredAssigned != NULL) *OutPreferredAssigned = PreferredAssigned;
    if (OutHasExtra != NULL) *OutHasExtra = HasExtra;
}

/************************************************************************/

BOOL AssignDrillerEscorts(I32 Team, UNIT* Driller, I32 DesiredForce) {
    if (Driller == NULL) return FALSE;
    if (!IsValidTeam(Team)) return FALSE;
    if (!HasAvailableEscortCandidate(Team)) return FALSE;

    ClearDrillerEscorts(Team, Driller->Id);

    I32 assignedForce = 0;
    BOOL assigned = FALSE;
    while (assignedForce < DesiredForce) {
        UNIT* best = NULL;
        I32 bestScore = 0;
        UNIT* Unit = App.GameState->TeamData[Team].Units;
        while (Unit != NULL) {
            const UNIT_TYPE* ut = GetUnitTypeById(Unit->TypeId);
            if (ut != NULL &&
                ut->Damage > 0 &&
                ut->Id != UNIT_TYPE_SCOUT &&
                ut->Id != UNIT_TYPE_DRILLER &&
                Unit->State != UNIT_STATE_ESCORT) {
                I32 score = AiComputeUnitScore(ut);
                if (best == NULL || score > bestScore) {
                    best = Unit;
                    bestScore = score;
                }
            }
            Unit = Unit->Next;
        }

        if (best == NULL) break;

        SetUnitStateEscort(best, Driller->Team, Driller->Id);
        LogTeamAction(Team, "SetEscort", (U32)best->Id, (U32)Driller->Id, 0, "", "Force");
        assignedForce += bestScore;
        assigned = TRUE;
    }
    return assigned;
}

/************************************************************************/

/// @brief Update the team mindset and cache values into the context.
static void UpdateMindsetForTeam(I32 Team, AI_CONTEXT* Context) {
    I32 EnemyNearby = 0;
    I32 FriendlyNearby = 0;

    BOOL CanAfford = CanAffordCheapestMobileUnit(Team);
    EvaluateThreatNearTeamBuildings(Team, AI_THREAT_RADIUS_DEFAULT, &EnemyNearby, &FriendlyNearby);
    BOOL ThreatActive = (EnemyNearby > FriendlyNearby);

    I32 CurrentMindset = App.GameState->TeamData[Team].AiMindset;
    I32 NextMindset = CurrentMindset;

    switch (CurrentMindset) {
        case AI_MINDSET_IDLE:
            if (ThreatActive) {
                NextMindset = CanAfford ? AI_MINDSET_URGENCY : AI_MINDSET_PANIC;
            }
            break;

        case AI_MINDSET_URGENCY:
            if (ThreatActive && !CanAfford) {
                NextMindset = AI_MINDSET_PANIC;
            } else if (!ThreatActive) {
                NextMindset = AI_MINDSET_IDLE;
            }
            break;

        case AI_MINDSET_PANIC:
            if (ThreatActive && CanAfford) {
                NextMindset = AI_MINDSET_URGENCY;
            } else if (!ThreatActive) {
                NextMindset = AI_MINDSET_IDLE;
            }
            break;

        default:
            NextMindset = AI_MINDSET_IDLE;
            break;
    }

    if (NextMindset != CurrentMindset) {
        LogTeamActionCounts(Team, "MindsetChange", (U32)CurrentMindset, (U32)NextMindset,
                            (U32)(ThreatActive ? 1 : 0), (U32)(CanAfford ? 1 : 0));
        LogTeamActionCounts(Team, "ThreatCounts", (U32)EnemyNearby, (U32)FriendlyNearby, 0, 0);
    }

    App.GameState->TeamData[Team].AiMindset = NextMindset;

    if (Context != NULL) {
        Context->Mindset = NextMindset;
        Context->EnemyNearby = EnemyNearby;
        Context->FriendlyNearby = FriendlyNearby;
        Context->ThreatActive = ThreatActive;
        Context->CanAffordCheapestMobile = CanAfford;
    }
}

/************************************************************************/

/// @brief Build the AI context used by condition checks.
static void BuildAIContext(I32 Team, AI_CONTEXT* Context) {
    if (Context == NULL || App.GameState == NULL) return;
    memset(Context, 0, sizeof(*Context));

    Context->Team = Team;
    Context->Now = App.GameState->GameTime;
    Context->NowSystem = GetSystemTime();
    Context->Attitude = App.GameState->TeamData[Team].AiAttitude;

    UpdateMindsetForTeam(Team, Context);

    Context->Resources = GetTeamResources(Team);
    Context->Plasma = (Context->Resources != NULL) ? Context->Resources->Plasma : 0;
    Context->Yard = FindTeamBuilding(Team, BUILDING_TYPE_CONSTRUCTION_YARD);
    Context->Barracks = FindTeamBuilding(Team, BUILDING_TYPE_BARRACKS);
    Context->Factory = FindTeamBuilding(Team, BUILDING_TYPE_FACTORY);
    Context->TechCenter = FindTeamBuilding(Team, BUILDING_TYPE_TECH_CENTER);
    Context->HasBarracks = Context->Barracks != NULL;
    Context->HasFactory = Context->Factory != NULL;
    Context->HasTechCenter = Context->TechCenter != NULL;
    Context->QueuedBarracks = CountQueuedBuildingType(Context->Yard, BUILDING_TYPE_BARRACKS);
    Context->QueuedFactory = CountQueuedBuildingType(Context->Yard, BUILDING_TYPE_FACTORY);
    Context->QueuedTechCenter = CountQueuedBuildingType(Context->Yard, BUILDING_TYPE_TECH_CENTER);
    Context->YardQueueCount = (Context->Yard != NULL) ? Context->Yard->BuildQueueCount : 0;
    Context->YardHasSpace = (Context->Yard != NULL && Context->Yard->BuildQueueCount < MAX_PLACEMENT_QUEUE);

    GetEnergyTotals(Team, &Context->EnergyProduction, &Context->EnergyConsumption);
    Context->EnergyLow = (Context->EnergyConsumption >= Context->EnergyProduction);

    Context->EnemyKnownForce = ComputeKnownEnemyForce(Team);

    Context->PowerPlantType = GetBuildingTypeById(BUILDING_TYPE_POWER_PLANT);
    Context->BarracksType = GetBuildingTypeById(BUILDING_TYPE_BARRACKS);
    Context->FactoryType = GetBuildingTypeById(BUILDING_TYPE_FACTORY);
    Context->TechCenterType = GetBuildingTypeById(BUILDING_TYPE_TECH_CENTER);

    Context->DrillerCount = CountUnitsOfType(Team, UNIT_TYPE_DRILLER);
    Context->QueuedDrillers = CountQueuedUnitType(Context->Factory, UNIT_TYPE_DRILLER);
    Context->DrillerTarget = RequiredUnitCount(Team, UNIT_TYPE_DRILLER);
    Context->MobileTarget = RequiredUnitCount(Team, UNIT_TYPE_TROOPER);
    Context->MobileCount = CountMobileUnits(Team);
    GetUnitCounts(Team, Context->UnitCounts);

    Context->ScoutCount = Context->UnitCounts[UNIT_TYPE_SCOUT];
    Context->QueuedScouts = CountQueuedUnitType(Context->Barracks, UNIT_TYPE_SCOUT);
    Context->TargetScouts = imax(1, App.GameState->MapMaxDim / 50);

    Context->AllowUnitProduction = TRUE;
    if (!Context->HasTechCenter && Context->TechCenterType != NULL &&
        Context->Plasma < Context->TechCenterType->CostPlasma &&
        Context->Mindset == AI_MINDSET_IDLE &&
        (Context->DrillerCount + Context->QueuedDrillers) >= Context->DrillerTarget) {
        if (Context->MobileCount >= AI_IDLE_MIN_DEFENSE) {
            Context->AllowUnitProduction = FALSE;
            if (Context->Attitude == AI_ATTITUDE_AGGRESSIVE) {
                I32 AggressiveTarget = RequiredUnitCount(Team, UNIT_TYPE_TROOPER);
                if (Context->MobileCount < AggressiveTarget) {
                    Context->AllowUnitProduction = TRUE;
                }
            }
        }
    }

    if (Context->HasBarracks) {
        if (Context->HasFactory) {
            Context->VehicleTarget = Context->MobileTarget / 3;
            Context->InfantryTarget = Context->MobileTarget - Context->VehicleTarget;
        } else {
            Context->InfantryTarget = Context->MobileTarget;
        }
    } else if (Context->HasFactory) {
        Context->VehicleTarget = Context->MobileTarget;
    }
    if (Context->InfantryTarget < 0) Context->InfantryTarget = 0;
    if (Context->VehicleTarget < 0) Context->VehicleTarget = 0;

    if (Context->Barracks != NULL) {
        I32 InfantryCounts[UNIT_TYPE_COUNT];
        for (I32 Index = 0; Index < UNIT_TYPE_COUNT; Index++) {
            InfantryCounts[Index] = Context->UnitCounts[Index];
        }
        AddQueuedUnitCounts(Context->Barracks, InfantryCounts);
        Context->InfantryCountWithQueue = CountInfantryUnits(InfantryCounts);
    }

    if (Context->Factory != NULL) {
        I32 VehicleCounts[UNIT_TYPE_COUNT];
        for (I32 Index = 0; Index < UNIT_TYPE_COUNT; Index++) {
            VehicleCounts[Index] = Context->UnitCounts[Index];
        }
        AddQueuedUnitCounts(Context->Factory, VehicleCounts);
        Context->VehicleCountWithQueue = CountVehicleUnits(VehicleCounts);
    }

    Context->FortressTypeId = SelectFortressQueueType(Team, Context->Attitude, Context->Plasma);
    UpdatePlannedBuilding(Context);

    UNIT* Unit = App.GameState->TeamData[Team].Units;
    while (Unit != NULL) {
        const UNIT_TYPE* UnitType = GetUnitTypeById(Unit->TypeId);
        if (Context->ScoutToOrder == NULL && UnitType != NULL &&
            UnitType->Id == UNIT_TYPE_SCOUT && Unit->State != UNIT_STATE_EXPLORE) {
            Context->ScoutToOrder = Unit;
        }

        if (UnitType != NULL && UnitType->Damage > 0 &&
            UnitType->Id != UNIT_TYPE_SCOUT && UnitType->Id != UNIT_TYPE_DRILLER &&
            Unit->State == UNIT_STATE_IDLE) {
            Context->AvailableForce += AiComputeUnitScore(UnitType);
        }
        Unit = Unit->Next;
    }

    Context->Driller = FindFirstDriller(Team);
    Context->Escort = FindStrongestCombatUnit(Team, Context->Driller);
    if (Context->Driller != NULL) {
        BOOL PreferredAssigned = FALSE;
        BOOL HasExtra = FALSE;
        I32 EscortForce = 0;
        I32 EscortCount = 0;
        BOOL HasAvailableEscort;
        BOOL PreferredAvailable;
        BOOL NeedsEscortUpdate;
        GetDrillerEscortStatus(Team, Context->Driller->Id, Context->Escort,
                               &EscortForce, &EscortCount, &PreferredAssigned, &HasExtra);

        Context->HasDrillerEscort = (EscortCount > 0);
        Context->CurrentEscortForce = EscortForce;
        {
            I32 desired = AiDrillerEscortMinForce +
                          (Context->EnemyKnownForce / AI_DRILLER_ESCORT_FORCE_DIVISOR);
            I32 available = ComputeAvailableEscortForce(Team);
            if (desired > available) desired = available;
            if (desired < 0) desired = 0;
            Context->DesiredEscortForce = desired;
        }

        HasAvailableEscort = HasAvailableEscortCandidate(Team);
        PreferredAvailable = (Context->Escort != NULL &&
                              (Context->Escort->State != UNIT_STATE_ESCORT ||
                               (Context->Escort->EscortUnitTeam == Team &&
                                Context->Escort->EscortUnitId == Context->Driller->Id)));
        NeedsEscortUpdate = (EscortCount == 0 || HasExtra);
        if (!NeedsEscortUpdate && !PreferredAssigned && PreferredAvailable) {
            NeedsEscortUpdate = TRUE;
        }

        if (Context->DesiredEscortForce > 0 &&
            EscortForce < Context->DesiredEscortForce &&
            HasAvailableEscort &&
            NeedsEscortUpdate) {
            Context->EscortNeedsUpdate = TRUE;
        }
    }
}

/************************************************************************/

/// @brief Execute exactly one AI action based on priority.
static void ThinkAITeam(AI_CONTEXT* Context) {
    if (Context == NULL) return;

    static const AI_DECISION Decisions[] = {
        {ConditionForQueuePowerPlant, ActionQueuePowerPlant, "QueuePowerPlant"},
        {ConditionForUpdateDrillerEscort, ActionUpdateDrillerEscort, "UpdateDrillerEscort"},
        {ConditionForQueueBarracks, ActionQueueBarracks, "QueueBarracks"},
        {ConditionForQueueFactoryForDrillers, ActionQueueFactoryForDrillers, "QueueFactoryForDrillers"},
        {ConditionForQueueTechCenter, ActionQueueTechCenter, "QueueTechCenter"},
        {ConditionForQueueFactory, ActionQueueFactory, "QueueFactory"},
        {ConditionForProduceDriller, ActionProduceDriller, "ProduceDriller"},
        {ConditionForProduceScout, ActionProduceScout, "ProduceScout"},
        {ConditionForOrderScoutExplore, ActionOrderScoutExplore, "OrderScoutExplore"},
        {ConditionForProduceBarracksUnit, ActionProduceBarracksUnit, "ProduceBarracksUnit"},
        {ConditionForProduceFactoryUnit, ActionProduceFactoryUnit, "ProduceFactoryUnit"},
        {ConditionForAggressiveOrders, ActionAggressiveOrders, "AggressiveOrders"},
        {ConditionForShuffleBaseUnits, ActionShuffleBaseUnits, "ShuffleBaseUnits"},
        {ConditionForQueueFortress, ActionQueueFortress, "QueueFortress"}
    };

    for (I32 Index = 0; Index < (I32)(sizeof(Decisions) / sizeof(Decisions[0])); Index++) {
        if (Decisions[Index].Condition(Context)) {
            if (Decisions[Index].Action(Context)) {
                SetAiLastDecision(Context->Team, Decisions[Index].Name);
                return;
            }
        }
    }
}

/************************************************************************/

void ProcessAITeams(void) {
    if (App.GameState == NULL) return;

    U32 Now = App.GameState->GameTime;
    I32 TeamCount = GetTeamCountSafe();
    for (I32 Team = 1; Team < TeamCount; Team++) {
        if (IsTeamEliminated(Team)) {
            RemoveTeamEntities(Team);
            continue;
        }
        if (!ShouldProcessAITeam(Team, Now)) continue;

        AI_CONTEXT Context;
        BuildAIContext(Team, &Context);
        ThinkAITeam(&Context);
    }
}
