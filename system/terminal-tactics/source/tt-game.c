
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

#include "tt-game.h"
#include "tt-map.h"
#include "tt-entities.h"
#include "tt-path.h"
#include "tt-fog.h"
#include "tt-render.h"
#include "tt-ai.h"
#include "tt-commands.h"
#include "tt-log.h"

/************************************************************************/

static I32 TeamStartPositions[MAX_TEAMS][2];
static BOOL TeamStartPositionsReady = FALSE;
static U32 LastDeployWarningTime = 0;

/************************************************************************/

static I32 GetMaxUnitFootprint(I32* outWidth, I32* outHeight) {
    static I32 CachedDim = 0;
    static I32 CachedWidth = 0;
    static I32 CachedHeight = 0;

    if (CachedDim > 0) {
        if (outWidth != NULL) *outWidth = CachedWidth;
        if (outHeight != NULL) *outHeight = CachedHeight;
        return CachedDim;
    }

    for (I32 i = 0; i < UNIT_TYPE_COUNT; i++) {
        const UNIT_TYPE* ut = GetUnitTypeById(i + 1);
        if (ut == NULL) continue;
        if (ut->Width > CachedWidth) CachedWidth = ut->Width;
        if (ut->Height > CachedHeight) CachedHeight = ut->Height;
        I32 dim = (ut->Width > ut->Height) ? ut->Width : ut->Height;
        if (dim > CachedDim) CachedDim = dim;
    }

    if (CachedWidth < 1) CachedWidth = 1;
    if (CachedHeight < 1) CachedHeight = 1;
    if (CachedDim < 1) CachedDim = 1;

    if (outWidth != NULL) *outWidth = CachedWidth;
    if (outHeight != NULL) *outHeight = CachedHeight;
    return CachedDim;
}

/************************************************************************/

/// @brief Check if a unit footprint can reach the edge of a radius.
static BOOL HasUnitEscapeRoute(I32 startX, I32 startY, I32 mapW, I32 mapH, I32 radius,
                               I32 unitTypeId, I32 unitWidth, I32 unitHeight) {
    const I32 steps[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    I32 diameter;
    I32 maxCells;
    U8* visited;
    I32* queueX;
    I32* queueY;
    I32 head = 0;
    I32 tail = 0;
    BOOL result = FALSE;

    if (mapW <= 0 || mapH <= 0) return FALSE;
    if (radius < 0) return FALSE;
    if (unitWidth <= 0 || unitHeight <= 0) return FALSE;
    if (IsAreaBlockedForUnitType(startX, startY, unitWidth, unitHeight, unitTypeId)) return FALSE;

    diameter = radius * 2 + 1;
    if (diameter <= 0) return FALSE;
    maxCells = diameter * diameter;
    if (maxCells <= 0) return FALSE;

    visited = (U8*)malloc((size_t)maxCells);
    queueX = (I32*)malloc(sizeof(I32) * (size_t)maxCells);
    queueY = (I32*)malloc(sizeof(I32) * (size_t)maxCells);
    if (visited == NULL || queueX == NULL || queueY == NULL) {
        if (visited != NULL) free(visited);
        if (queueX != NULL) free(queueX);
        if (queueY != NULL) free(queueY);
        return FALSE;
    }
    memset(visited, 0, (size_t)maxCells);

    visited[radius * diameter + radius] = 1;
    queueX[tail] = 0;
    queueY[tail] = 0;
    tail++;

    while (head < tail) {
        I32 dx = queueX[head];
        I32 dy = queueY[head];
        head++;

        if (dx == -radius || dx == radius || dy == -radius || dy == radius) {
            result = TRUE;
            break;
        }

        for (I32 i = 0; i < (I32)(sizeof(steps) / sizeof(steps[0])); i++) {
            I32 ndx = dx + steps[i][0];
            I32 ndy = dy + steps[i][1];
            if (ndx < -radius || ndx > radius || ndy < -radius || ndy > radius) continue;

            I32 vx = ndx + radius;
            I32 vy = ndy + radius;
            I32 vIndex = vy * diameter + vx;
            if (visited[vIndex]) continue;

            I32 mapX = WrapCoord(startX, ndx, mapW);
            I32 mapY = WrapCoord(startY, ndy, mapH);
            if (IsAreaBlockedForUnitType(mapX, mapY, unitWidth, unitHeight, unitTypeId)) continue;

            visited[vIndex] = 1;
            queueX[tail] = ndx;
            queueY[tail] = ndy;
            tail++;
        }
    }

    free(visited);
    free(queueX);
    free(queueY);
    return result;
}

/************************************************************************/

/// @brief Check if a map cell is inside a footprint anchored at a position.
static BOOL IsPointInFootprint(I32 px, I32 py, I32 anchorX, I32 anchorY, I32 width, I32 height, I32 mapW, I32 mapH) {
    for (I32 dy = 0; dy < height; dy++) {
        for (I32 dx = 0; dx < width; dx++) {
            I32 tx = WrapCoord(anchorX, dx, mapW);
            I32 ty = WrapCoord(anchorY, dy, mapH);
            if (tx == px && ty == py) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/************************************************************************/

/// @brief Check if a map cell is blocked by existing buildings or a pending one.
static BOOL IsCellBlockedByBuildings(I32 px, I32 py, I32 mapW, I32 mapH, I32 pendingX, I32 pendingY, const BUILDING_TYPE* pendingType) {
    I32 teamCount = GetTeamCountSafe();

    if (pendingType != NULL &&
        IsPointInFootprint(px, py, pendingX, pendingY, pendingType->Width, pendingType->Height, mapW, mapH)) {
        return TRUE;
    }

    for (I32 team = 0; team < teamCount; team++) {
        BUILDING* building = App.GameState->TeamData[team].Buildings;
        while (building != NULL) {
            const BUILDING_TYPE* bt = GetBuildingTypeById(building->TypeId);
            if (bt != NULL &&
                IsPointInFootprint(px, py, building->X, building->Y, bt->Width, bt->Height, mapW, mapH)) {
                return TRUE;
            }
            building = building->Next;
        }
    }

    return FALSE;
}

/************************************************************************/

/// @brief Ensure at least 75% of cells in radius around yard are not water/mountain.
static BOOL IsYardSurroundingOpen(I32 yardX, I32 yardY, const BUILDING_TYPE* yardType, I32 mapW, I32 mapH) {
    if (yardType == NULL) return FALSE;
    if (App.GameState == NULL || App.GameState->Terrain == NULL) return FALSE;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    I32 centerX = WrapCoord(yardX, yardType->Width / 2, mapW);
    I32 centerY = WrapCoord(yardY, yardType->Height / 2, mapH);
    I32 radius = TEAM_START_SEARCH_RADIUS;
    I32 total = 0;
    I32 open = 0;

    for (I32 dy = -radius; dy <= radius; dy++) {
        for (I32 dx = -radius; dx <= radius; dx++) {
            if (ChebyshevDistance(0, 0, dx, dy, mapW, mapH) > radius) continue;
            I32 px = WrapCoord(centerX, dx, mapW);
            I32 py = WrapCoord(centerY, dy, mapH);
            U8 type = TerrainGetType(&App.GameState->Terrain[py][px]);
            total++;
            if (type != TERRAIN_TYPE_WATER && type != TERRAIN_TYPE_MOUNTAIN) {
                open++;
            }
        }
    }

    if (total <= 0) return FALSE;
    return (open * 4) >= (total * 3);
}

/************************************************************************/

/// @brief Check if a position has an escape route within a radius, honoring buildings.
static BOOL HasUnitEscapeRouteWithBuildings(I32 startX, I32 startY, I32 mapW, I32 mapH, I32 radius,
                                            I32 unitTypeId, I32 unitWidth, I32 unitHeight,
                                            I32 pendingX, I32 pendingY, const BUILDING_TYPE* pendingType) {
    const I32 steps[][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    I32 diameter;
    I32 maxCells;
    U8* visited;
    I32* queueX;
    I32* queueY;
    I32 head = 0;
    I32 tail = 0;
    BOOL result = FALSE;

    if (mapW <= 0 || mapH <= 0) return FALSE;
    if (radius < 0) return FALSE;
    if (unitWidth <= 0 || unitHeight <= 0) return FALSE;
    if (IsAreaBlockedForUnitType(startX, startY, unitWidth, unitHeight, unitTypeId)) return FALSE;
    if (IsCellBlockedByBuildings(startX, startY, mapW, mapH, pendingX, pendingY, pendingType)) return FALSE;

    diameter = radius * 2 + 1;
    if (diameter <= 0) return FALSE;
    maxCells = diameter * diameter;
    if (maxCells <= 0) return FALSE;

    visited = (U8*)malloc((size_t)maxCells);
    queueX = (I32*)malloc(sizeof(I32) * (size_t)maxCells);
    queueY = (I32*)malloc(sizeof(I32) * (size_t)maxCells);
    if (visited == NULL || queueX == NULL || queueY == NULL) {
        if (visited != NULL) free(visited);
        if (queueX != NULL) free(queueX);
        if (queueY != NULL) free(queueY);
        return FALSE;
    }
    memset(visited, 0, (size_t)maxCells);

    visited[radius * diameter + radius] = 1;
    queueX[tail] = 0;
    queueY[tail] = 0;
    tail++;

    while (head < tail) {
        I32 dx = queueX[head];
        I32 dy = queueY[head];
        head++;

        if (dx == -radius || dx == radius || dy == -radius || dy == radius) {
            result = TRUE;
            break;
        }

        for (I32 i = 0; i < (I32)(sizeof(steps) / sizeof(steps[0])); i++) {
            I32 ndx = dx + steps[i][0];
            I32 ndy = dy + steps[i][1];
            if (ndx < -radius || ndx > radius || ndy < -radius || ndy > radius) continue;

            I32 vx = ndx + radius;
            I32 vy = ndy + radius;
            I32 vIndex = vy * diameter + vx;
            if (visited[vIndex]) continue;

            I32 mapX = WrapCoord(startX, ndx, mapW);
            I32 mapY = WrapCoord(startY, ndy, mapH);
            if (IsAreaBlockedForUnitType(mapX, mapY, unitWidth, unitHeight, unitTypeId)) continue;
            if (IsCellBlockedByBuildings(mapX, mapY, mapW, mapH, pendingX, pendingY, pendingType)) continue;

            visited[vIndex] = 1;
            queueX[tail] = ndx;
            queueY[tail] = ndy;
            tail++;
        }
    }

    free(visited);
    free(queueX);
    free(queueY);
    return result;
}

/************************************************************************/

/// @brief Check if a pending building would newly enclose a team unit within the escape radius.
static BOOL WouldEncloseTeamUnit(I32 team, I32 placeX, I32 placeY, const BUILDING_TYPE* type) {
    if (!IsValidTeam(team) || type == NULL || App.GameState == NULL) return FALSE;

    I32 mapW = App.GameState->MapWidth;
    I32 mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    I32 maxDim = (type->Width > type->Height) ? type->Width : type->Height;
    I32 escapeRadius = GetMaxUnitFootprint(NULL, NULL);
    UNIT* unit = App.GameState->TeamData[team].Units;
    while (unit != NULL) {
        I32 dist = ChebyshevDistance(unit->X, unit->Y, placeX, placeY, mapW, mapH);
        if (dist <= escapeRadius + maxDim) {
            const UNIT_TYPE* unitType = GetUnitTypeById(unit->TypeId);
            if (unitType == NULL) {
                unit = unit->Next;
                continue;
            }
            BOOL hadEscape = HasUnitEscapeRouteWithBuildings(unit->X, unit->Y, mapW, mapH, escapeRadius,
                                                             unit->TypeId, unitType->Width, unitType->Height,
                                                             0, 0, NULL);
            if (hadEscape &&
                !HasUnitEscapeRouteWithBuildings(unit->X, unit->Y, mapW, mapH, escapeRadius,
                                                 unit->TypeId, unitType->Width, unitType->Height,
                                                 placeX, placeY, type)) {
                return TRUE;
            }
        }
        unit = unit->Next;
    }

    return FALSE;
}

/************************************************************************/

static void InitTeamStartPositions(void) {
    I32 zones[MAX_TEAMS][2];
    I32 zoneIndices[MAX_TEAMS];
    I32 zoneCount;
    I32 mapW;
    I32 mapH;

    if (App.GameState == NULL) return;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return;

    zoneCount = MAX_TEAMS;
    zones[0][0] = mapW / TEAM_START_ZONE_HALF_DIVISOR;
    zones[0][1] = mapH / TEAM_START_ZONE_HALF_DIVISOR;
    zones[1][0] = (mapW * TEAM_START_ZONE_THREE_QUARTERS_NUM) / TEAM_START_ZONE_DIVISOR;
    zones[1][1] = mapH / TEAM_START_ZONE_DIVISOR;
    zones[2][0] = mapW / TEAM_START_ZONE_DIVISOR;
    zones[2][1] = (mapH * TEAM_START_ZONE_THREE_QUARTERS_NUM) / TEAM_START_ZONE_DIVISOR;
    zones[3][0] = (mapW * TEAM_START_ZONE_THREE_QUARTERS_NUM) / TEAM_START_ZONE_DIVISOR;
    zones[3][1] = (mapH * TEAM_START_ZONE_THREE_QUARTERS_NUM) / TEAM_START_ZONE_DIVISOR;
    zones[4][0] = mapW / TEAM_START_ZONE_DIVISOR;
    zones[4][1] = mapH / TEAM_START_ZONE_DIVISOR;

    for (I32 i = 0; i < zoneCount; i++) {
        zoneIndices[i] = i;
    }
    for (I32 i = zoneCount - 1; i > 0; i--) {
        I32 swapIndex = (I32)(SimpleRandom() % (U32)(i + 1));
        I32 temp = zoneIndices[i];
        zoneIndices[i] = zoneIndices[swapIndex];
        zoneIndices[swapIndex] = temp;
    }

    for (I32 team = 0; team < App.GameState->TeamCount; team++) {
        I32 zone = zoneIndices[team % zoneCount];
        I32 baseX = zones[zone][0];
        I32 baseY = zones[zone][1];

        I32 maxWidth;
        I32 maxHeight;
        I32 escapeRadius = GetMaxUnitFootprint(&maxWidth, &maxHeight);
        if (!HasUnitEscapeRoute(baseX, baseY, mapW, mapH, escapeRadius,
                                UNIT_TYPE_TANK, maxWidth, maxHeight)) {
            BOOL found = FALSE;
            I32 maxRadius = App.GameState->MapMaxDim;
            for (I32 radius = 1; radius <= maxRadius && !found; radius++) {
                for (I32 dy = -radius; dy <= radius && !found; dy++) {
                    for (I32 dx = -radius; dx <= radius && !found; dx++) {
                        I32 candidateX = WrapCoord(baseX, dx, mapW);
                        I32 candidateY = WrapCoord(baseY, dy, mapH);
                        if (HasUnitEscapeRoute(candidateX, candidateY, mapW, mapH, escapeRadius,
                                               UNIT_TYPE_TANK, maxWidth, maxHeight)) {
                            baseX = candidateX;
                            baseY = candidateY;
                            found = TRUE;
                        }
                    }
                }
            }
        }

        TeamStartPositions[team][0] = baseX;
        TeamStartPositions[team][1] = baseY;
    }

    TeamStartPositionsReady = TRUE;
}

/************************************************************************/
BOOL FindFreeSpotNear(I32 centerX, I32 centerY, I32 width, I32 height, I32 mapW, I32 mapH, I32 searchRadius,
                      I32* outX, I32* outY) {
    if (mapW <= 0 || mapH <= 0) return FALSE;

    const BUILDING_TYPE* yardType = GetBuildingTypeById(BUILDING_TYPE_CONSTRUCTION_YARD);
    BOOL enforceYardOpen = (yardType != NULL && width == yardType->Width && height == yardType->Height);

    for (I32 radius = 0; radius <= searchRadius; radius++) {
        for (I32 dy = -radius; dy <= radius; dy++) {
            for (I32 dx = -radius; dx <= radius; dx++) {
                I32 x = WrapCoord(centerX, dx, mapW);
                I32 y = WrapCoord(centerY, dy, mapH);
                if (!IsAreaBlocked(x, y, width, height, NULL, NULL)) {
                    if (enforceYardOpen && !IsYardSurroundingOpen(x, y, yardType, mapW, mapH)) {
                        continue;
                    }
                    if (outX != NULL) *outX = x;
                    if (outY != NULL) *outY = y;
                    return TRUE;
                }
            }
        }
    }
    return FALSE;
}

/************************************************************************/

/// @brief Find a free spot near a center with margin and escape room.
static BOOL FindFreeSpotNearWithMarginForTeam(I32 Team, I32 CenterX, I32 CenterY, I32 Width, I32 Height, I32 MapW, I32 MapH,
                                              I32 SearchRadius, I32 Margin, const BUILDING_TYPE* PendingType, I32* OutX, I32* OutY) {
    I32 EffectiveMargin;

    if (MapW <= 0 || MapH <= 0) return FALSE;
    if (PendingType == NULL) return FALSE;
    if (!IsValidTeam(Team)) return FALSE;

    EffectiveMargin = (Margin > 0) ? Margin : 0;

    for (I32 Radius = 0; Radius <= SearchRadius; Radius++) {
        for (I32 Dy = -Radius; Dy <= Radius; Dy++) {
            for (I32 Dx = -Radius; Dx <= Radius; Dx++) {
                I32 X = WrapCoord(CenterX, Dx, MapW);
                I32 Y = WrapCoord(CenterY, Dy, MapH);
                if (EffectiveMargin > 0) {
                    I32 CheckX = X - EffectiveMargin;
                    I32 CheckY = Y - EffectiveMargin;
                    I32 CheckW = Width + EffectiveMargin * 2;
                    I32 CheckH = Height + EffectiveMargin * 2;
                    if (IsAreaBlocked(CheckX, CheckY, CheckW, CheckH, NULL, NULL)) continue;
                } else {
                    if (IsAreaBlocked(X, Y, Width, Height, NULL, NULL)) continue;
                }
                if (WouldEncloseTeamUnit(Team, X, Y, PendingType)) continue;
                if (OutX != NULL) *OutX = X;
                if (OutY != NULL) *OutY = Y;
                return TRUE;
            }
        }
    }

    return FALSE;
}

/************************************************************************/

/// @brief Spawn a debug base for each team (F5).
void SpawnDebugBaseForAllTeams(void) {
    const I32 DebugMargin = 1;
    const I32 DebugRadius = BUILDING_AUTOPLACE_RADIUS + 6;
    const I32 DebugTypes[] = {
        BUILDING_TYPE_POWER_PLANT,
        BUILDING_TYPE_POWER_PLANT,
        BUILDING_TYPE_POWER_PLANT,
        BUILDING_TYPE_POWER_PLANT,
        BUILDING_TYPE_BARRACKS,
        BUILDING_TYPE_FACTORY,
        BUILDING_TYPE_TECH_CENTER
    };
    const I32 DebugTypeCount = (I32)(sizeof(DebugTypes) / sizeof(DebugTypes[0]));

    if (App.GameState == NULL) return;

    for (I32 Team = 0; Team < App.GameState->TeamCount; Team++) {
        BUILDING* Yard = FindTeamBuilding(Team, BUILDING_TYPE_CONSTRUCTION_YARD);
        const BUILDING_TYPE* YardType = (Yard != NULL) ? GetBuildingTypeById(Yard->TypeId) : NULL;
        I32 MapW = App.GameState->MapWidth;
        I32 MapH = App.GameState->MapHeight;
        I32 CenterX;
        I32 CenterY;

        if (Yard == NULL || YardType == NULL) continue;
        CenterX = WrapCoord(Yard->X, YardType->Width / 2, MapW);
        CenterY = WrapCoord(Yard->Y, YardType->Height / 2, MapH);

        for (I32 Index = 0; Index < DebugTypeCount; Index++) {
            const BUILDING_TYPE* Type = GetBuildingTypeById(DebugTypes[Index]);
            I32 PlaceX = 0;
            I32 PlaceY = 0;
            BUILDING* Building;
            BUILDING** Head;

            if (Type == NULL) continue;
            if (!FindFreeSpotNearWithMarginForTeam(Team, CenterX, CenterY, Type->Width, Type->Height, MapW, MapH,
                                                   DebugRadius, DebugMargin, Type, &PlaceX, &PlaceY)) {
                continue;
            }

            Building = CreateBuilding(Type->Id, Team, PlaceX, PlaceY);
            if (Building == NULL) continue;
            Head = GetTeamBuildingHead(Team);
            if (Head != NULL) {
                Building->Next = *Head;
                *Head = Building;
            }
        }
    }

    RecalculateEnergy();
    SetStatus("Debug base spawned");
}

/************************************************************************/

static BOOL SpawnStartingYards(void) {
    const BUILDING_TYPE* yardType = GetBuildingTypeById(BUILDING_TYPE_CONSTRUCTION_YARD);
    I32 mapW;
    I32 mapH;

    if (App.GameState == NULL || yardType == NULL) return FALSE;

    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;

    if (!TeamStartPositionsReady) {
        InitTeamStartPositions();
    }

    for (I32 team = 0; team < App.GameState->TeamCount; team++) {
        const I32 halfW = yardType->Width / 2;
        const I32 halfH = yardType->Height / 2;
        I32 baseX;
        I32 baseY;
        BOOL placed = FALSE;

        baseX = TeamStartPositions[team][0];
        baseY = TeamStartPositions[team][1];

        for (I32 radius = 0; radius <= TEAM_START_SEARCH_RADIUS && !placed; radius++) {
            for (I32 dy = -radius; dy <= radius && !placed; dy++) {
                for (I32 dx = -radius; dx <= radius && !placed; dx++) {
                    I32 centerX = WrapCoord(baseX, dx, mapW);
                    I32 centerY = WrapCoord(baseY, dy, mapH);
                    I32 x = centerX - halfW;
                    I32 y = centerY - halfH;

                    if (IsAreaBlocked(x, y, yardType->Width, yardType->Height, NULL, NULL)) continue;
                    if (!IsYardSurroundingOpen(x, y, yardType, mapW, mapH)) continue;

                    BUILDING* yard = CreateBuilding(BUILDING_TYPE_CONSTRUCTION_YARD, team, x, y);
                    if (yard != NULL) {
                        BUILDING** head = GetTeamBuildingHead(team);
                        if (head != NULL) {
                            yard->Next = *head;
                            *head = yard;
                        }
                        placed = TRUE;
                    }
                }
            }
        }

        if (!placed) {
            I32 x = 0;
            I32 y = 0;
            if (FindFreeSpotNear(baseX, baseY, yardType->Width, yardType->Height, mapW, mapH,
                                 App.GameState->MapMaxDim, &x, &y)) {
                BUILDING* yard = CreateBuilding(BUILDING_TYPE_CONSTRUCTION_YARD, team, x, y);
                if (yard != NULL) {
                    BUILDING** head = GetTeamBuildingHead(team);
                    if (head != NULL) {
                        yard->Next = *head;
                        *head = yard;
                    }
                }
            }
        }
        if (!placed) {
            return FALSE;
        }
    }
    return TRUE;
}

/************************************************************************/

static BOOL FindFreeSpotNearExplored(I32 team, I32 centerX, I32 centerY, I32 width, I32 height, I32 mapW, I32 mapH, I32 searchRadius, I32 margin, I32* outX, I32* outY) {
    if (mapW <= 0 || mapH <= 0) return FALSE;
    if (!IsValidTeam(team)) return FALSE;

    for (I32 radius = 0; radius <= searchRadius; radius++) {
        for (I32 dy = -radius; dy <= radius; dy++) {
            for (I32 dx = -radius; dx <= radius; dx++) {
                I32 x = WrapCoord(centerX, dx, mapW);
                I32 y = WrapCoord(centerY, dy, mapH);
                if (IsAreaBlocked(x, y, width, height, NULL, NULL)) continue;
                if (!IsAreaExploredToTeamWithMargin(x, y, width, height, team, margin)) continue;
                if (outX != NULL) *outX = x;
                if (outY != NULL) *outY = y;
                return TRUE;
            }
        }
    }
    return FALSE;
}

/************************************************************************/

static I32* PlasmaSearchQueueX = NULL;
static I32* PlasmaSearchQueueY = NULL;
static U8* PlasmaSearchVisited = NULL;
static size_t PlasmaSearchCells = 0;

/************************************************************************/

static BOOL EnsurePlasmaSearchBuffers(I32 mapW, I32 mapH) {
    size_t cells;
    size_t queueBytes;
    size_t visitedBytes;

    if (mapW <= 0 || mapH <= 0) return FALSE;
    cells = (size_t)mapW * (size_t)mapH;
    if (cells == 0) return FALSE;

    if (PlasmaSearchCells < cells) {
        if (PlasmaSearchQueueX != NULL) {
            free(PlasmaSearchQueueX);
            PlasmaSearchQueueX = NULL;
        }
        if (PlasmaSearchQueueY != NULL) {
            free(PlasmaSearchQueueY);
            PlasmaSearchQueueY = NULL;
        }
        if (PlasmaSearchVisited != NULL) {
            free(PlasmaSearchVisited);
            PlasmaSearchVisited = NULL;
        }

        queueBytes = cells * sizeof(I32);
        visitedBytes = cells * sizeof(U8);
        PlasmaSearchQueueX = (I32*)malloc(queueBytes);
        PlasmaSearchQueueY = (I32*)malloc(queueBytes);
        PlasmaSearchVisited = (U8*)malloc(visitedBytes);
        if (PlasmaSearchQueueX == NULL || PlasmaSearchQueueY == NULL || PlasmaSearchVisited == NULL) {
            if (PlasmaSearchQueueX != NULL) free(PlasmaSearchQueueX);
            if (PlasmaSearchQueueY != NULL) free(PlasmaSearchQueueY);
            if (PlasmaSearchVisited != NULL) free(PlasmaSearchVisited);
            PlasmaSearchQueueX = NULL;
            PlasmaSearchQueueY = NULL;
            PlasmaSearchVisited = NULL;
            PlasmaSearchCells = 0;
            return FALSE;
        }
        PlasmaSearchCells = cells;
    }

    return TRUE;
}

/************************************************************************/

/// @brief Check if plasma exists within a unit footprint.
static BOOL HasPlasmaInFootprint(I32 x, I32 y, I32 width, I32 height) {
    if (App.GameState == NULL || App.GameState->PlasmaDensity == NULL) return FALSE;
    I32 mapW = App.GameState->MapWidth;
    I32 mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;
    if (width <= 0 || height <= 0) return FALSE;

    for (I32 dy = 0; dy < height; dy++) {
        for (I32 dx = 0; dx < width; dx++) {
            I32 px = WrapCoord(x, dx, mapW);
            I32 py = WrapCoord(y, dy, mapH);
            if (App.GameState->PlasmaDensity[py][px] > 0) {
                return TRUE;
            }
        }
    }

    return FALSE;
}

/************************************************************************/

/// @brief Find a reachable plasma cell target for a unit footprint.
static BOOL FindNearestReachablePlasmaTargetInternal(const UNIT* unit, const UNIT_TYPE* unitType,
                                                     BOOL skipTarget, I32 skipX, I32 skipY,
                                                     I32* outX, I32* outY) {
    static const I32 steps[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };
    I32 mapW;
    I32 mapH;
    I32 head = 0;
    I32 tail = 0;

    if (App.GameState == NULL || unit == NULL || unitType == NULL) return FALSE;
    if (App.GameState->PlasmaDensity == NULL) return FALSE;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    if (!EnsurePlasmaSearchBuffers(mapW, mapH)) return FALSE;
    memset(PlasmaSearchVisited, 0, PlasmaSearchCells * sizeof(U8));

    {
        I32 sx = unit->X;
        I32 sy = unit->Y;
        I32 sIndex = sy * mapW + sx;
        PlasmaSearchVisited[sIndex] = 1;
        PlasmaSearchQueueX[tail] = sx;
        PlasmaSearchQueueY[tail] = sy;
        tail++;
    }

    while (head < tail) {
        I32 cx = PlasmaSearchQueueX[head];
        I32 cy = PlasmaSearchQueueY[head];
        head++;

        for (I32 dy = 0; dy < unitType->Height; dy++) {
            for (I32 dx = 0; dx < unitType->Width; dx++) {
                I32 px = WrapCoord(cx, dx, mapW);
                I32 py = WrapCoord(cy, dy, mapH);
                if (App.GameState->PlasmaDensity[py][px] > 0) {
                    if (skipTarget && cx == skipX && cy == skipY) {
                        continue;
                    }
                    if (outX != NULL) *outX = cx;
                    if (outY != NULL) *outY = cy;
                    return TRUE;
                }
            }
        }

        for (I32 i = 0; i < (I32)(sizeof(steps) / sizeof(steps[0])); i++) {
            I32 stepX = steps[i][0];
            I32 stepY = steps[i][1];
            I32 nx = WrapCoord(cx, stepX, mapW);
            I32 ny = WrapCoord(cy, stepY, mapH);
            I32 nIndex = ny * mapW + nx;

            if (PlasmaSearchVisited[nIndex]) continue;

            if (stepX != 0 && stepY != 0) {
                I32 ox = WrapCoord(cx, stepX, mapW);
                I32 oy = cy;
                if (IsAreaBlocked(ox, oy, unitType->Width, unitType->Height, NULL, unit)) continue;
                ox = cx;
                oy = WrapCoord(cy, stepY, mapH);
                if (IsAreaBlocked(ox, oy, unitType->Width, unitType->Height, NULL, unit)) continue;
            }

            if (IsAreaBlocked(nx, ny, unitType->Width, unitType->Height, NULL, unit)) continue;

            PlasmaSearchVisited[nIndex] = 1;
            PlasmaSearchQueueX[tail] = nx;
            PlasmaSearchQueueY[tail] = ny;
            tail++;
        }
    }

    return FALSE;
}

/************************************************************************/

static BOOL FindNearestReachablePlasmaTarget(const UNIT* unit, const UNIT_TYPE* unitType, I32* outX, I32* outY) {
    return FindNearestReachablePlasmaTargetInternal(unit, unitType, FALSE, 0, 0, outX, outY);
}

/************************************************************************/

BOOL FindUnitSpawnNear(const BUILDING* producer, const UNIT_TYPE* unitType, I32* outX, I32* outY) {
    if (producer == NULL || unitType == NULL || App.GameState == NULL) return FALSE;
    const BUILDING_TYPE* bt = GetBuildingTypeById(producer->TypeId);
    I32 centerX = producer->X;
    I32 centerY = producer->Y;
    if (bt != NULL) {
        centerX = producer->X + bt->Width / 2;
        centerY = producer->Y + bt->Height / 2;
    }
    return FindFreeSpotNear(centerX, centerY, unitType->Width, unitType->Height, App.GameState->MapWidth, App.GameState->MapHeight, UNIT_DEPLOY_RADIUS, outX, outY);
}

/************************************************************************/

/// @brief Choose a fog-of-war exploration target for a team.
BOOL PickExplorationTarget(I32 team, I32* outX, I32* outY) {
    I32 mapW = App.GameState != NULL ? App.GameState->MapWidth : 0;
    I32 mapH = App.GameState != NULL ? App.GameState->MapHeight : 0;
    U8* visible = (App.GameState != NULL && IsValidTeam(team)) ? App.GameState->TeamData[team].VisibleNow : NULL;

    if (App.GameState == NULL || mapW <= 0 || mapH <= 0 || visible == NULL) return FALSE;
    if (App.GameState->Terrain == NULL) return FALSE;

    for (I32 attempt = 0; attempt < EXPLORE_FIND_ATTEMPTS; attempt++) {
        I32 rx = (I32)(SimpleRandom() % (U32)mapW);
        I32 ry = (I32)(SimpleRandom() % (U32)mapH);
        size_t idx = (size_t)ry * (size_t)mapW + (size_t)rx;
        if (TerrainGetType(&App.GameState->Terrain[ry][rx]) == TERRAIN_TYPE_WATER) continue;
        if (visible[idx] == 0) {
            if (outX != NULL) *outX = rx;
            if (outY != NULL) *outY = ry;
            return TRUE;
        }
    }

    for (I32 attempt = 0; attempt < mapW * mapH; attempt++) {
        I32 rx = (I32)(SimpleRandom() % (U32)mapW);
        I32 ry = (I32)(SimpleRandom() % (U32)mapH);
        if (TerrainGetType(&App.GameState->Terrain[ry][rx]) == TERRAIN_TYPE_WATER) continue;
        if (outX != NULL) *outX = rx;
        if (outY != NULL) *outY = ry;
        return TRUE;
    }
    return TRUE;
}

/************************************************************************/

/// @brief Find the closest plasma cell to a start position.
BOOL FindNearestPlasmaCell(I32 startX, I32 startY, I32* outX, I32* outY) {
    if (App.GameState == NULL || App.GameState->PlasmaDensity == NULL) return FALSE;

    I32 mapW = App.GameState->MapWidth;
    I32 mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    I32 maxRadius = App.GameState->MapMaxDim;
    for (I32 radius = 0; radius <= maxRadius; radius++) {
        for (I32 dy = -radius; dy <= radius; dy++) {
            for (I32 dx = -radius; dx <= radius; dx++) {
                if (dx != -radius && dx != radius && dy != -radius && dy != radius) continue;
                I32 px = WrapCoord(startX, dx, mapW);
                I32 py = WrapCoord(startY, dy, mapH);
                if (App.GameState->PlasmaDensity[py][px] > 0) {
                    if (outX != NULL) *outX = px;
                    if (outY != NULL) *outY = py;
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

/************************************************************************/

BOOL FindNearestSafePlasmaCell(I32 team, I32 startX, I32 startY, I32 minEnemyDistance, I32* outX, I32* outY) {
    if (App.GameState == NULL || App.GameState->PlasmaDensity == NULL) return FALSE;
    if (!IsValidTeam(team)) return FALSE;

    I32 mapW = App.GameState->MapWidth;
    I32 mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    MEMORY_CELL* memory = App.GameState->TeamData[team].MemoryMap;
    if (memory == NULL) return FALSE;

    I32 teamCount = GetTeamCountSafe();
    I32 maxRadius = App.GameState->MapMaxDim;
    for (I32 radius = 0; radius <= maxRadius; radius++) {
        for (I32 dy = -radius; dy <= radius; dy++) {
            for (I32 dx = -radius; dx <= radius; dx++) {
                if (dx != -radius && dx != radius && dy != -radius && dy != radius) continue;
                I32 px = WrapCoord(startX, dx, mapW);
                I32 py = WrapCoord(startY, dy, mapH);
                size_t idx = (size_t)py * (size_t)mapW + (size_t)px;
                if (memory[idx].TerrainKnown == 0) continue;
                if (memory[idx].TerrainType != TERRAIN_TYPE_PLASMA) continue;
                if (App.GameState->PlasmaDensity[py][px] <= 0) continue;

                BOOL tooClose = FALSE;
                for (I32 enemyTeam = 0; enemyTeam < teamCount && !tooClose; enemyTeam++) {
                    if (enemyTeam == team) continue;
                    BUILDING* building = App.GameState->TeamData[enemyTeam].Buildings;
                    while (building != NULL) {
                        if (building->TypeId == BUILDING_TYPE_CONSTRUCTION_YARD) {
                            I32 dist = ChebyshevDistance(px, py, building->X, building->Y, mapW, mapH);
                            if (dist <= minEnemyDistance) {
                                tooClose = TRUE;
                                break;
                            }
                        }
                        building = building->Next;
                    }
                }

                if (!tooClose) {
                    if (outX != NULL) *outX = px;
                    if (outY != NULL) *outY = py;
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

/************************************************************************/

static void SpawnStartingTroopers(I32 difficulty) {
    const UNIT_TYPE* trooper = GetUnitTypeById(UNIT_TYPE_TROOPER);
    I32 spawnCount;
    I32 maxRadius;

    if (App.GameState == NULL || trooper == NULL) return;

    switch (difficulty) {
        case DIFFICULTY_EASY:   spawnCount = 2; break;
        case DIFFICULTY_NORMAL: spawnCount = 1; break;
        default:                spawnCount = 0; break;
    }
    if (spawnCount <= 0) return;

    if (!TeamStartPositionsReady) {
        InitTeamStartPositions();
    }

    maxRadius = App.GameState->MapMaxDim;

    for (I32 team = 0; team < App.GameState->TeamCount; team++) {
        UNIT** head = GetTeamUnitHead(team);
        if (head == NULL) continue;

        I32 placed = 0;
        for (I32 spawnIndex = 0; spawnIndex < spawnCount; spawnIndex++) {
            BOOL placedThis = FALSE;
            for (I32 radius = 0; radius <= maxRadius && !placedThis; radius++) {
                I32 spawnX;
                I32 spawnY;
                if (!FindFreeSpotNear(TeamStartPositions[team][0], TeamStartPositions[team][1],
                                      trooper->Width, trooper->Height,
                                      App.GameState->MapWidth, App.GameState->MapHeight,
                                      radius, &spawnX, &spawnY)) {
                    continue;
                }
                {
                    I32 escapeRadius = GetMaxUnitFootprint(NULL, NULL);
                    if (!HasUnitEscapeRoute(spawnX, spawnY, App.GameState->MapWidth, App.GameState->MapHeight,
                                            escapeRadius, trooper->Id, trooper->Width, trooper->Height)) {
                        continue;
                    }
                }

                UNIT* unit = CreateUnit(UNIT_TYPE_TROOPER, team, spawnX, spawnY);
                if (unit != NULL) {
                    unit->MoveProgress = 0;
                    unit->Next = *head;
                    *head = unit;
                    placed++;
                    placedThis = TRUE;
                }
            }
        }
    }
}

/************************************************************************/

static void SpawnStartingDrillers(void) {
    const UNIT_TYPE* driller = GetUnitTypeById(UNIT_TYPE_DRILLER);
    if (App.GameState == NULL || driller == NULL) return;

    for (I32 team = 0; team < App.GameState->TeamCount; team++) {
        BUILDING* yard = FindTeamBuilding(team, BUILDING_TYPE_CONSTRUCTION_YARD);
        UNIT** head = GetTeamUnitHead(team);
        if (yard == NULL || head == NULL) continue;

        I32 centerX = yard->X + 1;
        I32 centerY = yard->Y + 1;
        I32 spawnX;
        I32 spawnY;
        if (!FindFreeSpotNear(centerX, centerY, driller->Width, driller->Height,
                              App.GameState->MapWidth, App.GameState->MapHeight, START_DRILLER_SPAWN_RADIUS, &spawnX, &spawnY)) {
            continue;
        }
        {
            I32 escapeRadius = GetMaxUnitFootprint(NULL, NULL);
            if (!HasUnitEscapeRoute(spawnX, spawnY, App.GameState->MapWidth, App.GameState->MapHeight,
                                    escapeRadius, driller->Id, driller->Width, driller->Height)) {
                continue;
            }
        }

        UNIT* unit = CreateUnit(UNIT_TYPE_DRILLER, team, spawnX, spawnY);
        if (unit != NULL) {
            unit->MoveProgress = 0;
            unit->Next = *head;
            *head = unit;
            if (team != HUMAN_TEAM_INDEX) {
                I32 TargetX = unit->X;
                I32 TargetY = unit->Y;
                if (!FindNearestReachablePlasmaTarget(unit, driller, &TargetX, &TargetY)) {
                    FindNearestPlasmaCell(unit->X, unit->Y, &TargetX, &TargetY);
                }
                SetUnitStateExplore(unit, TargetX, TargetY);
                LogTeamAction(team, "SetExplore", (U32)unit->Id, (U32)TargetX, (U32)TargetY, "Driller", "Spawn");
            }
        }
    }
}

/************************************************************************/

float Interpolate(float a, float b, float t) {
    /* Smooth interpolation */
    float ft = t * 3.1415927f;
    float f = (1.0f - cos(ft)) * 0.5f;
    return a * (1.0f - f) + b * f;
}

/************************************************************************/

float Noise2D(I32 x, I32 y) {
    U32 seed = (App.GameState != NULL) ? App.GameState->NoiseSeed : 0;
    U32 n = (U32)(x * 374761393U + y * 668265263U + seed * 374761393U);
    n = (n ^ (n >> 13)) * 1274124967U;
    return (float)(n & 0x7FFFFFFF) / 2147483647.0f;
}

/************************************************************************/

float SmoothNoise(I32 x, I32 y) {
    float corners = (Noise2D(x-1, y-1) + Noise2D(x+1, y-1) +
                    Noise2D(x-1, y+1) + Noise2D(x+1, y+1)) / 16.0f;
    float sides = (Noise2D(x-1, y) + Noise2D(x+1, y) +
                  Noise2D(x, y-1) + Noise2D(x, y+1)) / 8.0f;
    float center = Noise2D(x, y) / 4.0f;
    return corners + sides + center;
}

/************************************************************************/

float PerlinNoise(float x, float y, float persistence, I32 octaves) {
    float total = 0;
    float frequency = 1.0f;
    float amplitude = 1.0f;
    float maxValue = 0;

    for (I32 i = 0; i < octaves; i++) {
        total += SmoothNoise(x * frequency, y * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= persistence;
        frequency *= 2.0f;
    }

    return total / maxValue;
}

/************************************************************************/

/// @brief Set plasma on a cell unless it is water or mountain.
static void SetPlasmaCellIfAllowed(I32 x, I32 y, I32 minDensity, I32 rangeDensity) {
    if (App.GameState == NULL) return;
    U8 type = TerrainGetType(&App.GameState->Terrain[y][x]);
    if (type == TERRAIN_TYPE_WATER || type == TERRAIN_TYPE_MOUNTAIN) return;
    TerrainInitCell(&App.GameState->Terrain[y][x], TERRAIN_TYPE_PLASMA);
    App.GameState->PlasmaDensity[y][x] = minDensity + (I32)(RandomFloat() * (F32)rangeDensity);
}

/************************************************************************/

void GenerateMap(void) {
    I32 i, j;
    float waterLevel;
    float forestLevel;
    float mountainLevel;
    I32 clusterCount;

    /* Set parameters based on difficulty */
    waterLevel = 0.42f;
    forestLevel = 0.6f;
    mountainLevel = 0.66f;

    /* Generate terrain using Perlin noise */
    for (i = 0; i < App.GameState->MapHeight; i++) {
        for (j = 0; j < App.GameState->MapWidth; j++) {
            float nx = (float)j / App.GameState->MapWidth * MAP_NOISE_SCALE;
            float ny = (float)i / App.GameState->MapHeight * MAP_NOISE_SCALE;

            float noise = PerlinNoise(nx, ny, 0.5f, 4);
            float mountainNoise = PerlinNoise(nx * MAP_NOISE_SCALE, ny * MAP_NOISE_SCALE, 0.5f, 2);
            U8 tileType;

            /* Determine terrain type */
            if (noise < waterLevel) {
                tileType = TERRAIN_TYPE_WATER;
                App.GameState->PlasmaDensity[i][j] = 0;
            } else if (noise < 0.5f) {
                tileType = TERRAIN_TYPE_PLAINS;
                /* Add some plasma deposits */
                float plasmaNoise = PerlinNoise(nx * 8.0f, ny * 8.0f, 0.5f, 2);
                if (plasmaNoise > 0.6f) {
                    tileType = TERRAIN_TYPE_PLASMA;
                    App.GameState->PlasmaDensity[i][j] = 75 + (I32)(plasmaNoise * 175.0f);
                } else {
                    App.GameState->PlasmaDensity[i][j] = 0;
                }
            } else if (noise < forestLevel) {
                tileType = TERRAIN_TYPE_FOREST;
                App.GameState->PlasmaDensity[i][j] = 0;
            } else if (noise < mountainLevel || mountainNoise > 0.8f) {
                tileType = TERRAIN_TYPE_MOUNTAIN;
                App.GameState->PlasmaDensity[i][j] = 0;
            } else {
                tileType = TERRAIN_TYPE_FOREST;
                App.GameState->PlasmaDensity[i][j] = 0;
            }
            TerrainInitCell(&App.GameState->Terrain[i][j], tileType);
        }
    }

    clusterCount = (App.GameState->MapWidth * App.GameState->MapHeight) / 800;
    if (clusterCount < 6) clusterCount = 6;
    for (I32 c = 0; c < clusterCount; c++) {
        I32 centerX = (I32)(SimpleRandom() % (U32)App.GameState->MapWidth);
        I32 centerY = (I32)(SimpleRandom() % (U32)App.GameState->MapHeight);
        I32 radius = 2 + (I32)(SimpleRandom() % 3);
        for (I32 dy = -radius; dy <= radius; dy++) {
            for (I32 dx = -radius; dx <= radius; dx++) {
                if (ChebyshevDistance(0, 0, dx, dy, App.GameState->MapWidth, App.GameState->MapHeight) > radius) continue;
                I32 px = WrapCoord(centerX, dx, App.GameState->MapWidth);
                I32 py = WrapCoord(centerY, dy, App.GameState->MapHeight);
                SetPlasmaCellIfAllowed(px, py, 120, 120);
            }
        }
    }

    /* Ensure starting area is clear */
    I32 startX = App.GameState->MapWidth / 2;
    I32 startY = App.GameState->MapHeight / 2;

    for (i = startY - 10; i < startY + 10; i++) {
        for (j = startX - 10; j < startX + 10; j++) {
            if (i >= 0 && i < App.GameState->MapHeight &&
                j >= 0 && j < App.GameState->MapWidth) {
                /* Make area mostly plains */
                U8 type = TerrainGetType(&App.GameState->Terrain[i][j]);
                if (type == TERRAIN_TYPE_WATER ||
                    type == TERRAIN_TYPE_MOUNTAIN) {
                    TerrainInitCell(&App.GameState->Terrain[i][j], TERRAIN_TYPE_PLAINS);
                }
            }
        }
    }
}

/************************************************************************/

static void EnsureStartingPlasmaReachable(void) {
    const UNIT_TYPE* drillerType;
    const BUILDING_TYPE* yardType;
    I32 mapW;
    I32 mapH;

    if (App.GameState == NULL) return;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return;

    drillerType = GetUnitTypeById(UNIT_TYPE_DRILLER);
    yardType = GetBuildingTypeById(BUILDING_TYPE_CONSTRUCTION_YARD);
    if (drillerType == NULL || yardType == NULL) return;

    for (I32 team = 0; team < App.GameState->TeamCount; team++) {
        BUILDING* yard = FindTeamBuilding(team, BUILDING_TYPE_CONSTRUCTION_YARD);
        UNIT* driller = App.GameState->TeamData[team].Units;
        UNIT tempUnit;
        const UNIT* origin;
        I32 targetX = 0;
        I32 targetY = 0;

        if (yard == NULL) continue;

        while (driller != NULL && driller->TypeId != UNIT_TYPE_DRILLER) {
            driller = driller->Next;
        }

        if (driller != NULL) {
            origin = driller;
        } else {
            memset(&tempUnit, 0, sizeof(tempUnit));
            tempUnit.X = yard->X + yardType->Width / 2;
            tempUnit.Y = yard->Y + yardType->Height / 2;
            origin = &tempUnit;
        }

        if (FindNearestReachablePlasmaTarget(origin, drillerType, &targetX, &targetY)) {
            continue;
        }

        I32 centerX = yard->X + yardType->Width / 2;
        I32 centerY = yard->Y + yardType->Height / 2;
        for (I32 dy = -3; dy <= 3; dy++) {
            for (I32 dx = -3; dx <= 3; dx++) {
                if (ChebyshevDistance(0, 0, dx, dy, mapW, mapH) > 3) continue;
                I32 px = WrapCoord(centerX, dx, mapW);
                I32 py = WrapCoord(centerY, dy, mapH);
                SetPlasmaCellIfAllowed(px, py, 120, 120);
            }
        }
    }
}

/************************************************************************/

BOOL InitializeGame(I32 mapWidth, I32 mapHeight, I32 difficulty, I32 teamCount) {
    /* Allocate game state */
    App.GameState = (GAME_STATE*)malloc(sizeof(GAME_STATE));
    if (App.GameState == NULL) return FALSE;

    /* Initialize game state */
    memset(App.GameState, 0, sizeof(GAME_STATE));
    App.GameState->NoiseSeed = GetSystemTime();
    GameLogInit();

    /* Allocate and generate map */
    if (!AllocateMap(mapWidth, mapHeight)) {
        free(App.GameState);
        App.GameState = NULL;
        return FALSE;
    }

    App.GameState->Difficulty = difficulty;
    if (teamCount < 1) teamCount = 1;
    if (teamCount > MAX_TEAMS) teamCount = MAX_TEAMS;
    App.GameState->TeamCount = teamCount;
    App.GameState->NextUnitId = 1;
    App.GameState->NextBuildingId = 1;
    if (!EnsureTeamMemoryBuffers(App.GameState->MapWidth, App.GameState->MapHeight, App.GameState->TeamCount)) {
        FreeMap();
        free(App.GameState);
        App.GameState = NULL;
        return FALSE;
    }
    /* Initialize viewport position */
    App.GameState->ViewportPos.X = App.GameState->MapWidth / 2 - VIEWPORT_WIDTH / 2;
    App.GameState->ViewportPos.Y = App.GameState->MapHeight / 2 - VIEWPORT_HEIGHT / 2;

    I32 aiTeams = (App.GameState->TeamCount > 0) ? (App.GameState->TeamCount - 1) : 0;
    I32 aggressiveTarget = aiTeams / 2;
    if ((aiTeams % 2) != 0 && RandomFloat() > AI_ATTITUDE_RANDOM_THRESHOLD) {
        aggressiveTarget++;
    }
    I32 remainingAggressive = aggressiveTarget;
    I32 remainingDefensive = aiTeams - aggressiveTarget;
    I32 remainingAi = aiTeams;

    /* Initialize resources based on difficulty */
    for (I32 team = 0; team < App.GameState->TeamCount; team++) {
        TEAM_RESOURCES* res = &App.GameState->TeamData[team].Resources;
        switch (difficulty) {
            case DIFFICULTY_EASY:
                res->Plasma = START_PLASMA_EASY;
                res->Energy = START_ENERGY_EASY;
                res->MaxEnergy = START_MAX_ENERGY_EASY;
                break;

            case DIFFICULTY_NORMAL:
                res->Plasma = START_PLASMA_NORMAL;
                res->Energy = START_ENERGY_NORMAL;
                res->MaxEnergy = START_MAX_ENERGY_NORMAL;
                break;

            case DIFFICULTY_HARD:
                res->Plasma = START_PLASMA_HARD;
                res->Energy = START_ENERGY_HARD;
                res->MaxEnergy = START_MAX_ENERGY_HARD;
                break;
        }
        if (team == HUMAN_TEAM_INDEX) {
            App.GameState->TeamData[team].AiAttitude = 0;
        } else {
            I32 attitude;
            if (remainingAggressive == 0) {
                attitude = AI_ATTITUDE_DEFENSIVE;
                remainingDefensive--;
            } else if (remainingDefensive == 0) {
                attitude = AI_ATTITUDE_AGGRESSIVE;
                remainingAggressive--;
            } else if (remainingAi == remainingAggressive) {
                attitude = AI_ATTITUDE_AGGRESSIVE;
                remainingAggressive--;
            } else if (remainingAi == remainingDefensive) {
                attitude = AI_ATTITUDE_DEFENSIVE;
                remainingDefensive--;
            } else if (RandomFloat() > AI_ATTITUDE_RANDOM_THRESHOLD) {
                attitude = AI_ATTITUDE_AGGRESSIVE;
                remainingAggressive--;
            } else {
                attitude = AI_ATTITUDE_DEFENSIVE;
                remainingDefensive--;
            }
            remainingAi--;
            App.GameState->TeamData[team].AiAttitude = attitude;
        }
        App.GameState->TeamData[team].AiMindset = AI_MINDSET_IDLE;
        App.GameState->TeamData[team].AiLastClusterUpdate = 0;
        App.GameState->TeamData[team].AiLastShuffleTime = 0;
    }

    /* Initialize game settings */
    App.GameState->GameSpeed = 1;
    App.GameState->IsPaused = FALSE;
    App.GameState->IsPlacingBuilding = FALSE;
    App.GameState->PendingBuildingTypeId = 0;
    App.GameState->PlacementX = 0;
    App.GameState->PlacementY = 0;
    App.GameState->PlacingFromQueue = FALSE;
    App.GameState->PendingQueueIndex = -1;
    App.GameState->IsRunning = TRUE;
    App.GameState->SelectedUnit = NULL;
    App.GameState->SelectedBuilding = NULL;
    App.GameState->ProductionMenuActive = FALSE;
    App.GameState->MenuPage = 0;
    App.GameState->ShowGrid = TRUE;
    App.GameState->ShowCoordinates = FALSE;
    App.GameState->SeeEverything = FALSE;
    App.GameState->GhostMode = FALSE;
    App.GameState->IsCommandMode = FALSE;
    App.GameState->CommandType = COMMAND_NONE;
    App.GameState->CommandX = 0;
    App.GameState->CommandY = 0;
    App.Render.BorderDrawn = FALSE;
    App.GameState->GameTime = 0;
    App.GameState->LastUpdate = GetSystemTime();
    App.GameState->LastFogUpdate = 0;
    App.GameState->FogDirty = TRUE;
    InitializeAiConstants();

    BOOL placed = FALSE;
    U32 attempt = 0;
    while (!placed) {
        for (I32 team = 0; team < App.GameState->TeamCount; team++) {
            RemoveTeamEntities(team);
        }

        App.GameState->NoiseSeed = GetSystemTime() + attempt;
        GenerateMap();

        App.GameState->NextUnitId = 1;
        App.GameState->NextBuildingId = 1;

        TeamStartPositionsReady = FALSE;
        InitTeamStartPositions();

        placed = SpawnStartingYards();
        attempt++;
    }
    SpawnStartingTroopers(difficulty);
    SpawnStartingDrillers();
    EnsureStartingPlasmaReachable();
    RebuildOccupancy();
    RecalculateEnergy();

    {
        BUILDING* yard = FindTeamBuilding(HUMAN_TEAM_INDEX, BUILDING_TYPE_CONSTRUCTION_YARD);
        const BUILDING_TYPE* yardType = GetBuildingTypeById(BUILDING_TYPE_CONSTRUCTION_YARD);
        if (yard != NULL && yardType != NULL) {
            CenterViewportOn(yard->X + yardType->Width / 2, yard->Y + yardType->Height / 2);
        }
    }

    return TRUE;
}

/************************************************************************/

void CleanupGame(void) {
    if (App.GameState == NULL) return;

    GameLogShutdown();

    I32 teamCount = GetTeamCountSafe();
    if (teamCount <= 0) teamCount = MAX_TEAMS;

    for (I32 team = 0; team < teamCount; team++) {
        UNIT* currentUnit = App.GameState->TeamData[team].Units;
        while (currentUnit != NULL) {
            UNIT* next = currentUnit->Next;
            free(currentUnit);
            currentUnit = next;
        }

        BUILDING* currentBuilding = App.GameState->TeamData[team].Buildings;
        while (currentBuilding != NULL) {
            BUILDING* next = currentBuilding->Next;
            free(currentBuilding);
            currentBuilding = next;
        }
    }

    FreeMap();
    FreePathfindingBuffers();
    if (App.GameState->ScratchOccupancy != NULL) {
        free(App.GameState->ScratchOccupancy);
        App.GameState->ScratchOccupancy = NULL;
        App.GameState->ScratchOccupancyBytes = 0;
    }
    free(App.GameState);
    App.GameState = NULL;
}

/************************************************************************/

I32 GetMaxUnitsForMap(I32 mapW, I32 mapH) {
    I32 maxUnits = (mapW + mapH) / 2;
    if (maxUnits < 1) maxUnits = 1;
    return maxUnits;
}

/************************************************************************/

U32 CountUnitsAllTeams(void) {
    U32 count = 0;
    I32 teamCount;

    if (App.GameState == NULL) return 0;
    teamCount = GetTeamCountSafe();
    if (teamCount <= 0) return 0;

    for (I32 team = 0; team < teamCount; team++) {
        UNIT* unit = App.GameState->TeamData[team].Units;
        while (unit != NULL) {
            count++;
            unit = unit->Next;
        }
    }

    return count;
}

/************************************************************************/

U32 CountUnitsForTeam(I32 team) {
    U32 count = 0;

    if (App.GameState == NULL) return 0;
    if (!IsValidTeam(team)) return 0;

    UNIT* unit = App.GameState->TeamData[team].Units;
    while (unit != NULL) {
        count++;
        unit = unit->Next;
    }

    return count;
}

/************************************************************************/

U32 CountBuildingsForTeam(I32 team) {
    U32 count = 0;

    if (App.GameState == NULL) return 0;
    if (!IsValidTeam(team)) return 0;

    BUILDING* building = App.GameState->TeamData[team].Buildings;
    while (building != NULL) {
        count++;
        building = building->Next;
    }

    return count;
}

/************************************************************************/

I32 CalculateTeamScore(I32 team) {
    I32 score = 0;

    if (App.GameState == NULL) return 0;
    if (!IsValidTeam(team)) return 0;

    BUILDING* building = App.GameState->TeamData[team].Buildings;
    while (building != NULL) {
        const BUILDING_TYPE* bt = GetBuildingTypeById(building->TypeId);
        if (bt != NULL) {
            score += bt->MaxHp * SCORE_BUILDING_HP_WEIGHT;
            score += bt->CostPlasma * SCORE_BUILDING_COST_WEIGHT;
        }
        building = building->Next;
    }

    UNIT* unit = App.GameState->TeamData[team].Units;
    while (unit != NULL) {
        const UNIT_TYPE* ut = GetUnitTypeById(unit->TypeId);
        if (ut != NULL) {
            score += ut->MaxHp * SCORE_UNIT_HP_WEIGHT;
            score += ut->Damage * SCORE_UNIT_DAMAGE_WEIGHT;
        }
        unit = unit->Next;
    }

    return score;
}

/************************************************************************/

static I32 GetBuildingPowerPriority(I32 typeId) {
    switch (typeId) {
        case BUILDING_TYPE_BARRACKS: return 0;
        case BUILDING_TYPE_FACTORY: return 1;
        case BUILDING_TYPE_TECH_CENTER: return 2;
        case BUILDING_TYPE_TURRET: return 3;
        case BUILDING_TYPE_CONSTRUCTION_YARD: return 4;
        case BUILDING_TYPE_POWER_PLANT: return 5;
        case BUILDING_TYPE_WALL: return 6;
        default: return 7;
    }
}

/************************************************************************/

static I32 BuildPowerPriorityList(I32 team, BUILDING** outList, I32 maxCount) {
    I32 count = 0;

    if (outList == NULL || maxCount <= 0) return 0;
    if (App.GameState == NULL) return 0;
    if (!IsValidTeam(team)) return 0;

    BUILDING* building = App.GameState->TeamData[team].Buildings;
    while (building != NULL && count < maxCount) {
        const BUILDING_TYPE* type = GetBuildingTypeById(building->TypeId);
        if (type != NULL &&
            !building->UnderConstruction &&
            type->EnergyConsumption > 0) {
            outList[count++] = building;
        }
        building = building->Next;
    }

    for (I32 i = 1; i < count; i++) {
        BUILDING* key = outList[i];
        I32 keyPriority = GetBuildingPowerPriority(key->TypeId);
        I32 keyId = key->Id;
        I32 j = i - 1;

        while (j >= 0) {
            BUILDING* current = outList[j];
            I32 currentPriority = GetBuildingPowerPriority(current->TypeId);
            I32 currentId = current->Id;
            if (currentPriority < keyPriority) break;
            if (currentPriority == keyPriority && currentId <= keyId) break;
            outList[j + 1] = outList[j];
            j--;
        }
        outList[j + 1] = key;
    }

    return count;
}

/************************************************************************/

/**
 * @brief Compute total energy production and consumption for a team.
 */
void GetEnergyTotals(I32 team, I32* outProduction, I32* outConsumption) {
    I32 Production = 0;
    I32 Consumption = 0;

    if (outProduction != NULL) *outProduction = 0;
    if (outConsumption != NULL) *outConsumption = 0;
    if (App.GameState == NULL) return;
    if (!IsValidTeam(team)) return;

    BUILDING* Building = App.GameState->TeamData[team].Buildings;
    while (Building != NULL) {
        const BUILDING_TYPE* Type = GetBuildingTypeById(Building->TypeId);
        if (Type != NULL && !Building->UnderConstruction) {
            Production += Type->EnergyProduction;
            Consumption += Type->EnergyConsumption;
        }
        Building = Building->Next;
    }

    if (Production < 0) Production = 0;
    if (Consumption < 0) Consumption = 0;

    if (outProduction != NULL) *outProduction = Production;
    if (outConsumption != NULL) *outConsumption = Consumption;
}

/************************************************************************/

/**
 * @brief Return TRUE if a building is powered based on team energy.
 */
BOOL IsBuildingPowered(const BUILDING* building) {
    const BUILDING_TYPE* type;
    I32 production = 0;
    I32 consumption = 0;
    I32 available;

    if (App.GameState == NULL || building == NULL) return FALSE;
    if (!IsValidTeam(building->Team)) return FALSE;
    if (building->UnderConstruction) return FALSE;

    type = GetBuildingTypeById(building->TypeId);
    if (type == NULL) return FALSE;
    if (type->EnergyConsumption <= 0) return TRUE;
    if (building->TypeId == BUILDING_TYPE_CONSTRUCTION_YARD) return TRUE;

    GetEnergyTotals(building->Team, &production, &consumption);
    if (production >= consumption) return TRUE;

    available = production;
    BUILDING* list[MAX_BUILDINGS];
    I32 count = BuildPowerPriorityList(building->Team, list, MAX_BUILDINGS);
    for (I32 i = 0; i < count; i++) {
        const BUILDING_TYPE* listType = GetBuildingTypeById(list[i]->TypeId);
        I32 needed = (listType != NULL) ? listType->EnergyConsumption : 0;
        if (needed <= 0) continue;
        if (needed <= available) {
            if (list[i] == building) return TRUE;
            available -= needed;
        } else {
            if (list[i] == building) return FALSE;
        }
    }

    return FALSE;
}

/************************************************************************/

void RecalculateEnergy(void) {
    if (App.GameState == NULL) return;

    I32 teamCount = GetTeamCountSafe();
    for (I32 team = 0; team < teamCount; team++) {
        I32 Production = 0;
        I32 Consumption = 0;
        TEAM_RESOURCES* res = GetTeamResources(team);
        if (res != NULL) {
            GetEnergyTotals(team, &Production, &Consumption);
            res->MaxEnergy = Production;
            if (res->MaxEnergy < 0) res->MaxEnergy = 0;

            if (Production >= Consumption) {
                res->Energy = Production - Consumption;
            } else {
                res->Energy = 0;
            }
        }
    }
}

/************************************************************************/

static BUILDING* GetHumanConstructionYard(void) {
    if (!IsValidTeam(HUMAN_TEAM_INDEX)) return NULL;
    BUILDING* selected = App.GameState != NULL ? App.GameState->SelectedBuilding : NULL;
    if (selected != NULL && selected->TypeId == BUILDING_TYPE_CONSTRUCTION_YARD) {
        return selected;
    }
    return FindTeamBuilding(HUMAN_TEAM_INDEX, BUILDING_TYPE_CONSTRUCTION_YARD);
}

/************************************************************************/

static void RemovePlacementAt(BUILDING* producer, I32 index) {
    if (producer == NULL) return;
    if (index < 0 || index >= producer->BuildQueueCount) return;
    {
        const BUILDING_TYPE* type = GetBuildingTypeById(producer->BuildQueue[index].TypeId);
        GAME_LOGF(producer->Team, "BuildQueueRemove Producer=%x Type=%s Count=%u",
                  (U32)producer->Id, type != NULL ? type->Name : "Unknown",
                  (U32)(producer->BuildQueueCount - 1));
    }
    for (I32 i = index + 1; i < producer->BuildQueueCount; i++) {
        producer->BuildQueue[i - 1] = producer->BuildQueue[i];
    }
    producer->BuildQueueCount--;
}

/************************************************************************/

BOOL CancelSelectedBuildingProduction(void) {
    BUILDING* building;
    TEAM_RESOURCES* res;
    char msg[MAX_SCREEN_WIDTH + 1];

    if (App.GameState == NULL) return FALSE;
    building = App.GameState->SelectedBuilding;
    if (building == NULL) {
        SetStatus("No building selected");
        return FALSE;
    }

    res = GetTeamResources(building->Team);

    if (building->TypeId == BUILDING_TYPE_CONSTRUCTION_YARD) {
        if (building->BuildQueueCount <= 0) {
            SetStatus("No build to cancel");
            return FALSE;
        }

        I32 index = 0;
        I32 typeId = building->BuildQueue[index].TypeId;
        const BUILDING_TYPE* type = GetBuildingTypeById(typeId);
        if (res != NULL && type != NULL && type->CostPlasma > 0) {
            res->Plasma += type->CostPlasma;
        }
        RemovePlacementAt(building, index);

        if (type != NULL) {
            snprintf(msg, sizeof(msg), "Cancelled %s", type->Name);
            SetStatus(msg);
        } else {
            SetStatus("Cancelled build");
        }
        return TRUE;
    }

    if (building->TypeId == BUILDING_TYPE_BARRACKS || building->TypeId == BUILDING_TYPE_FACTORY) {
        if (building->UnitQueueCount <= 0) {
            SetStatus("No unit to cancel");
            return FALSE;
        }

        UNIT_JOB* job = &building->UnitQueue[0];
        const UNIT_TYPE* type = GetUnitTypeById(job->TypeId);
        if (res != NULL && type != NULL && type->CostPlasma > 0) {
            res->Plasma += type->CostPlasma;
        }

        building->UnitQueueCount--;
        for (I32 i = 1; i <= building->UnitQueueCount; i++) {
            building->UnitQueue[i - 1] = building->UnitQueue[i];
        }
        GAME_LOGF(building->Team, "UnitQueueRemove Producer=%x Type=%s Count=%u",
                  (U32)building->Id, type != NULL ? type->Name : "Unknown",
                  (U32)building->UnitQueueCount);

        if (type != NULL) {
            snprintf(msg, sizeof(msg), "Cancelled %s", type->Name);
            SetStatus(msg);
        } else {
            SetStatus("Cancelled unit");
        }
        return TRUE;
    }

    SetStatus("No production to cancel");
    return FALSE;
}

/************************************************************************/

BOOL EnqueuePlacement(I32 typeId) {
    const BUILDING_TYPE* type = GetBuildingTypeById(typeId);
    TEAM_RESOURCES* res = GetTeamResources(HUMAN_TEAM_INDEX);
    BUILDING* yard = GetHumanConstructionYard();
    I32 count = (yard != NULL) ? yard->BuildQueueCount : 0;

    if (App.GameState == NULL || type == NULL || res == NULL || yard == NULL) return FALSE;
    if (count >= MAX_PLACEMENT_QUEUE) {
        SetStatus("Placement queue full (max 3)");
        return FALSE;
    }
    if (!HasTechLevel(type->TechLevel, HUMAN_TEAM_INDEX)) {
        SetStatus("Requires Tech Level 2 (build a Tech Center)");
        return FALSE;
    }
    if (res->Plasma < type->CostPlasma) {
        char msg[MAX_SCREEN_WIDTH + 1];
        snprintf(msg, sizeof(msg), "Not enough plasma for %s (need %d)", type->Name, type->CostPlasma);
        SetStatus(msg);
        return FALSE;
    }

    res->Plasma -= type->CostPlasma;
    yard->BuildQueue[count].TypeId = typeId;
    yard->BuildQueue[count].TimeRemaining = (U32)type->BuildTime;
    yard->BuildQueueCount++;
    GAME_LOGF(HUMAN_TEAM_INDEX, "BuildQueueAdd Producer=%x Type=%s Count=%u",
              (U32)yard->Id, type->Name, (U32)yard->BuildQueueCount);

    return TRUE;
}

/************************************************************************/

static void AdjustViewportForPlacement(const BUILDING_TYPE* type) {
    I32 mapW;
    I32 mapH;
    I32 sx;
    I32 sy;
    I32 viewW;
    I32 viewH;
    I32 typeW;
    I32 typeH;
    if (App.GameState == NULL || type == NULL) return;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return;
    viewW = (I32)VIEWPORT_WIDTH;
    viewH = (I32)VIEWPORT_HEIGHT;
    typeW = (I32)type->Width;
    typeH = (I32)type->Height;

    sx = App.GameState->PlacementX - App.GameState->ViewportPos.X;
    if (sx < 0) sx += mapW;
    else if (sx >= mapW) sx -= mapW;

    if (sx + typeW > viewW) {
        I32 delta = sx + typeW - viewW;
        App.GameState->ViewportPos.X = WrapCoord(App.GameState->ViewportPos.X, delta, mapW);
    }

    sy = App.GameState->PlacementY - App.GameState->ViewportPos.Y;
    if (sy < 0) sy += mapH;
    else if (sy >= mapH) sy -= mapH;

    if (sy + typeH > viewH) {
        I32 delta = sy + typeH - viewH;
        App.GameState->ViewportPos.Y = WrapCoord(App.GameState->ViewportPos.Y, delta, mapH);
    }
}

/************************************************************************/

BOOL StartPlacementFromQueue(void) {
    BUILDING* yard = GetHumanConstructionYard();
    I32 count = (yard != NULL) ? yard->BuildQueueCount : 0;

    if (App.GameState == NULL || yard == NULL) return FALSE;
    if (count <= 0) {
        SetStatus("Placement queue empty");
        return FALSE;
    }

    I32 readyIndex = -1;
    for (I32 i = 0; i < count; i++) {
        if (yard->BuildQueue[i].TimeRemaining == 0) {
            readyIndex = i;
            break;
        }
    }

    if (readyIndex < 0) {
        SetStatus("No finished building to place");
        return FALSE;
    }

    I32 typeId = yard->BuildQueue[readyIndex].TypeId;
    const BUILDING_TYPE* type = GetBuildingTypeById(typeId);
    if (type == NULL) {
        RemovePlacementAt(yard, readyIndex);
        SetStatus("Invalid queued building removed");
        return FALSE;
    }

    App.GameState->PendingBuildingTypeId = typeId;
    App.GameState->IsPlacingBuilding = TRUE;
    App.GameState->PlacingFromQueue = TRUE;
    App.GameState->PendingQueueIndex = readyIndex;
    App.GameState->PlacementX = WrapCoord(0, App.GameState->ViewportPos.X + VIEWPORT_WIDTH / 2 - type->Width / 2, App.GameState->MapWidth);
    App.GameState->PlacementY = WrapCoord(0, App.GameState->ViewportPos.Y + VIEWPORT_HEIGHT / 2 - type->Height / 2, App.GameState->MapHeight);
    AdjustViewportForPlacement(type);

    {
        char msg[MAX_SCREEN_WIDTH + 1];
        snprintf(msg, sizeof(msg), "Placing %s from queue", type->Name);
        SetStatus(msg);
    }
    return TRUE;
}

/************************************************************************/

void CancelBuildingPlacement(void) {
    if (App.GameState == NULL) return;
    App.GameState->IsPlacingBuilding = FALSE;
    App.GameState->PendingBuildingTypeId = 0;
    App.GameState->PlacingFromQueue = FALSE;
    App.GameState->PendingQueueIndex = -1;
    SetStatus(" ");
}

/************************************************************************/

BOOL ConfirmBuildingPlacement(void) {
    const BUILDING_TYPE* type;
    BUILDING* building;
    BUILDING* yard;
    TEAM_RESOURCES* res;

    if (App.GameState == NULL) {
        SetStatus("Placement failed");
        return FALSE;
    }

    type = GetBuildingTypeById(App.GameState->PendingBuildingTypeId);
    yard = GetHumanConstructionYard();
    res = GetTeamResources(HUMAN_TEAM_INDEX);
    if (type == NULL || res == NULL) {
        SetStatus("Placement failed");
        return FALSE;
    }

    if (IsAreaBlocked(App.GameState->PlacementX, App.GameState->PlacementY, type->Width, type->Height, NULL, NULL)) {
        SetStatus("Cannot place building here");
        return FALSE;
    }
    if (!IsAreaExploredToTeamWithMargin(App.GameState->PlacementX, App.GameState->PlacementY, type->Width, type->Height, HUMAN_TEAM_INDEX, 2)) {
        SetStatus("Cannot place buildings in unexplored area");
        return FALSE;
    }

    if (!App.GameState->PlacingFromQueue) {
        if (res->Plasma < type->CostPlasma) {
            char msg[MAX_SCREEN_WIDTH + 1];
            snprintf(msg, sizeof(msg), "Not enough plasma for %s (need %d)", type->Name, type->CostPlasma);
            SetStatus(msg);
            return FALSE;
        }
        if (!HasTechLevel(type->TechLevel, HUMAN_TEAM_INDEX)) {
            SetStatus("Requires Tech Level 2 (build a Tech Center)");
            return FALSE;
        }
    }

    building = CreateBuilding(type->Id, HUMAN_TEAM_INDEX, App.GameState->PlacementX, App.GameState->PlacementY);
    if (building == NULL) {
        SetStatus("Placement failed");
        return FALSE;
    }
    building->UnderConstruction = FALSE;
    building->BuildTimeRemaining = 0;
    if (!App.GameState->PlacingFromQueue) {
        res->Plasma -= type->CostPlasma;
    }

    BUILDING** head = GetTeamBuildingHead(HUMAN_TEAM_INDEX);
    if (head != NULL) {
        building->Next = *head;
        *head = building;
    }

    if (App.GameState->PlacingFromQueue && yard != NULL &&
        App.GameState->PendingQueueIndex >= 0 &&
        App.GameState->PendingQueueIndex < yard->BuildQueueCount) {
        RemovePlacementAt(yard, App.GameState->PendingQueueIndex);
    }

    CancelBuildingPlacement();
    {
        char msg[MAX_SCREEN_WIDTH + 1];
        snprintf(msg, sizeof(msg), "%s placed", type->Name);
        SetStatus(msg);
    }
    RecalculateEnergy();
    return TRUE;
}

/************************************************************************/

void MovePlacement(I32 dx, I32 dy) {
    const BUILDING_TYPE* type = GetBuildingTypeById(App.GameState->PendingBuildingTypeId);
    if (App.GameState == NULL || type == NULL) return;

    App.GameState->PlacementX = WrapCoord(App.GameState->PlacementX, dx, App.GameState->MapWidth);
    App.GameState->PlacementY = WrapCoord(App.GameState->PlacementY, dy, App.GameState->MapHeight);
    AdjustViewportForPlacement(type);
}

/************************************************************************/

void ProcessUnitQueueForProducer(BUILDING* producer, U32 timeStep, BOOL notify) {
    if (producer == NULL) return;
    if (!IsBuildingPowered(producer)) return;
    if (producer->UnitQueueCount <= 0) return;

    UNIT_JOB* job = &producer->UnitQueue[0];
    if (job->TimeRemaining > 0) {
        if (job->TimeRemaining > timeStep) {
            job->TimeRemaining -= timeStep;
        } else {
            job->TimeRemaining = 0;
        }
    }

    if (job->TimeRemaining == 0) {
        const UNIT_TYPE* ut = GetUnitTypeById(job->TypeId);
          if (ut != NULL) {
              I32 spawnX;
              I32 spawnY;
              if (FindUnitSpawnNear(producer, ut, &spawnX, &spawnY)) {
                UNIT* unit = CreateUnit(ut->Id, producer->Team, spawnX, spawnY);
                if (unit != NULL) {
                    UNIT** head = GetTeamUnitHead(producer->Team);
                    if (head != NULL) {
                        unit->Next = *head;
                        *head = unit;
                    }
                    if (unit->TypeId == UNIT_TYPE_DRILLER && producer->Team != HUMAN_TEAM_INDEX) {
                        I32 TargetX = unit->X;
                        I32 TargetY = unit->Y;
                        const UNIT_TYPE* DrillerType = GetUnitTypeById(UNIT_TYPE_DRILLER);
                        if (DrillerType != NULL &&
                            FindNearestReachablePlasmaTarget(unit, DrillerType, &TargetX, &TargetY)) {
                        } else {
                            FindNearestPlasmaCell(unit->X, unit->Y, &TargetX, &TargetY);
                        }
                        SetUnitStateExplore(unit, TargetX, TargetY);
                        LogTeamAction(producer->Team, "SetExplore", (U32)unit->Id, (U32)TargetX, (U32)TargetY,
                                      "Driller", "Spawn");
                    }
                    App.GameState->FogDirty = TRUE;
                    /* Shift unit queue */
                    producer->UnitQueueCount--;
                      for (I32 i = 1; i <= producer->UnitQueueCount; i++) {
                          producer->UnitQueue[i - 1] = producer->UnitQueue[i];
                      }
                      GAME_LOGF(producer->Team, "UnitQueueRemove Producer=%x Type=%s Count=%u",
                                (U32)producer->Id, ut->Name, (U32)producer->UnitQueueCount);
                      if (notify && producer->Team == HUMAN_TEAM_INDEX) {
                          char msg[MAX_SCREEN_WIDTH + 1];
                          snprintf(msg, sizeof(msg), "%s deployed", ut->Name);
                          SetStatus(msg);
                      }
                  }
              } else if (notify && producer->Team == HUMAN_TEAM_INDEX && App.GameState != NULL) {
                  U32 now = App.GameState->GameTime;
                  if (LastDeployWarningTime == 0 || now - LastDeployWarningTime >= UNIT_DEPLOY_WARN_INTERVAL_MS) {
                      const BUILDING_TYPE* bt = GetBuildingTypeById(producer->TypeId);
                      char msg[MAX_SCREEN_WIDTH + 1];
                      const char* name = (bt != NULL) ? bt->Name : "Building";
                      snprintf(msg, sizeof(msg), "No space to deploy unit from %s", name);
                      SetStatus(msg);
                      LastDeployWarningTime = now;
                  }
              }
          }
      }
}

/************************************************************************/

static BOOL TryAutoPlaceForProducer(BUILDING* producer, I32* queueIndex) {
    const BUILDING_TYPE* type;
    I32 mapW;
    I32 mapH;
    I32 placeX;
    I32 placeY;

    if (producer == NULL || queueIndex == NULL) return FALSE;
    if (*queueIndex < 0 || *queueIndex >= producer->BuildQueueCount) return FALSE;

    type = GetBuildingTypeById(producer->BuildQueue[*queueIndex].TypeId);
    if (type == NULL) {
        RemovePlacementAt(producer, *queueIndex);
        return FALSE;
    }

    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;

    if (producer->Team != HUMAN_TEAM_INDEX &&
        (type->Id == BUILDING_TYPE_WALL || type->Id == BUILDING_TYPE_TURRET)) {
        if (!FindFortressPlacement(producer->Team, type->Id, &placeX, &placeY)) {
            return FALSE;
        }
    } else {
        if (!FindFreeSpotNearExplored(producer->Team, producer->X, producer->Y, type->Width, type->Height, mapW, mapH,
                                      BUILDING_AUTOPLACE_RADIUS, BUILDING_AUTOPLACE_MARGIN, &placeX, &placeY)) {
            return FALSE;
        }
    }

    if (IsAreaBlocked(placeX, placeY, type->Width, type->Height, NULL, NULL)) {
        return FALSE;
    }

    if (producer->Team != HUMAN_TEAM_INDEX &&
        WouldEncloseTeamUnit(producer->Team, placeX, placeY, type)) {
        return FALSE;
    }

    BUILDING* building = CreateBuilding(type->Id, producer->Team, placeX, placeY);
    if (building == NULL) return FALSE;

    BUILDING** head = GetTeamBuildingHead(producer->Team);
    if (head != NULL) {
        building->Next = *head;
        *head = building;
    }
    RecalculateEnergy();
    RemovePlacementAt(producer, *queueIndex);
    return TRUE;
}

/************************************************************************/

static void UpdateBuildQueueForProducer(BUILDING* producer, U32 timeStep, BOOL notify) {
    if (producer == NULL) return;
    if (producer->BuildQueueCount <= 0) return;

    for (I32 i = 0; i < producer->BuildQueueCount; i++) {
        BUILD_JOB* job = &producer->BuildQueue[i];
        U32 previous = job->TimeRemaining;
        if (job->TimeRemaining > 0) {
            if (job->TimeRemaining > timeStep) {
                job->TimeRemaining -= timeStep;
            } else {
                job->TimeRemaining = 0;
                const BUILDING_TYPE* type = GetBuildingTypeById(job->TypeId);
                if (type != NULL && previous > 0) {
                    GAME_LOGF(producer->Team, "BuildReady Producer=%x Type=%s QueueIndex=%x",
                              (U32)producer->Id, type->Name, (U32)i);
                }
                if (type != NULL && notify && producer->Team == HUMAN_TEAM_INDEX) {
                    char msg[MAX_SCREEN_WIDTH + 1];
                    snprintf(msg, sizeof(msg), "%s ready to place", type->Name);
                    SetStatus(msg);
                }
            }
        } else {
            if (producer->Team != HUMAN_TEAM_INDEX) {
                I32 readyIndex = i;
                TryAutoPlaceForProducer(producer, &readyIndex);
            }
        }
        break; /* Only one build progresses at a time per producer */
    }
}

/************************************************************************/

static void UpdateBuildQueueForTeam(I32 team, U32 timeStep, BOOL notify) {
    BUILDING* building = IsValidTeam(team) ? App.GameState->TeamData[team].Buildings : NULL;
    while (building != NULL) {
        if (building->TypeId == BUILDING_TYPE_CONSTRUCTION_YARD) {
            UpdateBuildQueueForProducer(building, timeStep, notify);
        }
        if (building->TypeId == BUILDING_TYPE_BARRACKS || building->TypeId == BUILDING_TYPE_FACTORY) {
            ProcessUnitQueueForProducer(building, timeStep, notify);
        }
        building = building->Next;
    }
}

/************************************************************************/

static I32 SignedWrapDelta(I32 origin, I32 target, I32 size) {
    I32 delta = target - origin;
    if (size > 0) {
        if (delta > size / 2) delta -= size;
        else if (delta < -size / 2) delta += size;
    }
    return delta;
}

/************************************************************************/

/// @brief Compute the gap between two wrapped intervals.
static I32 RectWrapGap(I32 aPos, I32 aSize, I32 bPos, I32 bSize, I32 mapSize) {
    I32 delta = SignedWrapDelta(aPos, bPos, mapSize);

    if (delta >= 0) {
        if (delta < aSize) return 0;
        return delta - aSize;
    }

    delta = -delta;
    if (delta < bSize) return 0;
    return delta - bSize;
}

/************************************************************************/

/// @brief Check if two units are adjacent or overlapping in grid space.
static BOOL AreUnitsAdjacent(const UNIT* a, const UNIT_TYPE* aType,
                             const UNIT* b, const UNIT_TYPE* bType,
                             I32 mapW, I32 mapH) {
    if (a == NULL || b == NULL || aType == NULL || bType == NULL) return FALSE;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    I32 gapX = RectWrapGap(a->X, aType->Width, b->X, bType->Width, mapW);
    I32 gapY = RectWrapGap(a->Y, aType->Height, b->Y, bType->Height, mapH);
    return (gapX <= 1 && gapY <= 1);
}

/************************************************************************/

/// @brief Find any immediate step a unit can move to.
static BOOL FindAnyFreeStepForUnit(const UNIT* unit, const UNIT_TYPE* unitType, I32* outX, I32* outY) {
    const I32 steps[][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };
    I32 mapW;
    I32 mapH;

    if (App.GameState == NULL || unit == NULL || unitType == NULL) return FALSE;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    for (I32 i = 0; i < (I32)(sizeof(steps) / sizeof(steps[0])); i++) {
        I32 candX = WrapCoord(unit->X, steps[i][0], mapW);
        I32 candY = WrapCoord(unit->Y, steps[i][1], mapH);
        if (!IsAreaBlocked(candX, candY, unitType->Width, unitType->Height, NULL, unit)) {
            if (outX != NULL) *outX = candX;
            if (outY != NULL) *outY = candY;
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/// @brief Check if a unit is blocked in every direction.
static BOOL IsUnitTotallyBlocked(const UNIT* unit, const UNIT_TYPE* unitType) {
    return !FindAnyFreeStepForUnit(unit, unitType, NULL, NULL);
}

/************************************************************************/

/// @brief Apply a short detour move to help resolve local gridlock.
static BOOL StartGridlockDetour(UNIT* unit, const UNIT_TYPE* unitType) {
    I32 detourX;
    I32 detourY;
    I32 originalTargetX;
    I32 originalTargetY;

    if (unit == NULL || unitType == NULL) return FALSE;
    if (unit->IsMoving) return FALSE;
    if (!FindAnyFreeStepForUnit(unit, unitType, &detourX, &detourY)) return FALSE;

    originalTargetX = unit->TargetX;
    originalTargetY = unit->TargetY;

    SetUnitMoveTarget(unit, detourX, detourY);
    unit->StuckDetourActive = TRUE;
    unit->StuckDetourCount = 0;
    unit->StuckOriginalTargetX = originalTargetX;
    unit->StuckOriginalTargetY = originalTargetY;
    unit->StuckDetourTargetX = unit->TargetX;
    unit->StuckDetourTargetY = unit->TargetY;
    unit->IsGridlocked = TRUE;
    return TRUE;
}

/************************************************************************/

/// @brief Resolve a local gridlock starting from a blocked unit.
static void ResolveUnitGridlock(UNIT* root, const UNIT_TYPE* rootType, U32 currentTime) {
    I32 mapW;
    I32 mapH;
    I32 team;
    I32 unitCount = 0;
    UNIT* iter;
    UNIT** units = NULL;
    const UNIT_TYPE** unitTypes = NULL;
    U8* blocked = NULL;
    U8* inGroup = NULL;
    U8* reliever = NULL;
    I32* queue = NULL;
    I32 head = 0;
    I32 tail = 0;
    I32 rootIndex = -1;
    I32 moveCount = 0;

    if (App.GameState == NULL || root == NULL || rootType == NULL) return;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return;
    if (!IsValidTeam(root->Team)) return;

    if (!IsUnitTotallyBlocked(root, rootType)) {
        if (root->IsGridlocked && !root->StuckDetourActive) {
            root->IsGridlocked = FALSE;
        }
        return;
    }

    team = root->Team;
    iter = App.GameState->TeamData[team].Units;
    while (iter != NULL) {
        unitCount++;
        iter = iter->Next;
    }
    if (unitCount <= 0) return;

    units = (UNIT**)malloc(sizeof(UNIT*) * (size_t)unitCount);
    unitTypes = (const UNIT_TYPE**)malloc(sizeof(UNIT_TYPE*) * (size_t)unitCount);
    blocked = (U8*)malloc((size_t)unitCount);
    inGroup = (U8*)malloc((size_t)unitCount);
    reliever = (U8*)malloc((size_t)unitCount);
    queue = (I32*)malloc(sizeof(I32) * (size_t)unitCount);
    if (units == NULL || unitTypes == NULL || blocked == NULL || inGroup == NULL || reliever == NULL || queue == NULL) {
        if (units != NULL) free(units);
        if (unitTypes != NULL) free((void*)unitTypes);
        if (blocked != NULL) free(blocked);
        if (inGroup != NULL) free(inGroup);
        if (reliever != NULL) free(reliever);
        if (queue != NULL) free(queue);
        return;
    }

    iter = App.GameState->TeamData[team].Units;
    for (I32 i = 0; i < unitCount; i++) {
        units[i] = iter;
        unitTypes[i] = (iter != NULL) ? GetUnitTypeById(iter->TypeId) : NULL;
        blocked[i] = (iter != NULL && unitTypes[i] != NULL && IsUnitTotallyBlocked(iter, unitTypes[i])) ? 1 : 0;
        inGroup[i] = 0;
        reliever[i] = 0;
        if (iter == root) {
            rootIndex = i;
        }
        if (iter != NULL) {
            iter = iter->Next;
        }
    }

    if (rootIndex < 0 || !blocked[rootIndex]) {
        free(units);
        free((void*)unitTypes);
        free(blocked);
        free(inGroup);
        free(reliever);
        free(queue);
        return;
    }

    queue[tail++] = rootIndex;
    inGroup[rootIndex] = 1;

    while (head < tail) {
        I32 idx = queue[head++];
        const UNIT* a = units[idx];
        const UNIT_TYPE* aType = unitTypes[idx];
        if (a == NULL || aType == NULL) continue;

        for (I32 i = 0; i < unitCount; i++) {
            if (i == idx || units[i] == NULL || unitTypes[i] == NULL) continue;
            if (inGroup[i]) continue;
            if (!AreUnitsAdjacent(a, aType, units[i], unitTypes[i], mapW, mapH)) continue;

            if (blocked[i]) {
                inGroup[i] = 1;
                queue[tail++] = i;
            } else {
                reliever[i] = 1;
            }
        }
    }

    for (I32 i = 0; i < unitCount; i++) {
        if (inGroup[i] && units[i] != NULL) {
            units[i]->IsGridlocked = TRUE;
            units[i]->GridlockLastUpdateTime = currentTime;
        }
    }

    for (I32 i = 0; i < unitCount && moveCount < UNIT_GRIDLOCK_MOVE_LIMIT; i++) {
        if (!reliever[i] || units[i] == NULL || unitTypes[i] == NULL) continue;
        if (units[i]->GridlockLastUpdateTime == currentTime) continue;
        if (StartGridlockDetour(units[i], unitTypes[i])) {
            units[i]->GridlockLastUpdateTime = currentTime;
            moveCount++;
        }
    }

    free(units);
    free((void*)unitTypes);
    free(blocked);
    free(inGroup);
    free(reliever);
    free(queue);
}

/************************************************************************/

/// @brief Update gridlock detection for a unit.
static void UpdateUnitGridlock(UNIT* unit, const UNIT_TYPE* unitType, U32 currentTime) {
    if (unit == NULL || unitType == NULL) return;
    if (unit->GridlockLastUpdateTime == currentTime) return;
    if (unit->IsMoving) return;

    if (!IsUnitTotallyBlocked(unit, unitType)) {
        if (unit->IsGridlocked && !unit->StuckDetourActive) {
            unit->IsGridlocked = FALSE;
        }
        return;
    }

    ResolveUnitGridlock(unit, unitType, currentTime);
}

/************************************************************************/

/// @brief Apply a backoff move when a unit is stuck away from its target.
static void UpdateUnitStuckBehavior(UNIT* unit, const UNIT_TYPE* unitType, U32 currentTime) {
    I32 mapW;
    I32 mapH;
    I32 targetX;
    I32 targetY;
    I32 originalTargetX;
    I32 originalTargetY;
    I32 deltaX;
    I32 deltaY;
    I32 stepX;
    I32 stepY;
    I32 detourX;
    I32 detourY;
    U32 moveTime;
    U32 timeoutMs;

    if (App.GameState == NULL || unit == NULL || unitType == NULL) return;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return;

    if (unit->IsGridlocked) {
        if (unit->StuckDetourActive &&
            unit->X == unit->TargetX &&
            unit->Y == unit->TargetY) {
            unit->StuckDetourActive = FALSE;
            unit->IsGridlocked = FALSE;
            SetUnitMoveTarget(unit, unit->StuckOriginalTargetX, unit->StuckOriginalTargetY);
        }
        return;
    }

    if (unit->TargetX == unit->X && unit->TargetY == unit->Y) {
        unit->StuckDetourActive = FALSE;
        return;
    }

    if (unit->StuckDetourActive &&
        unit->X == unit->TargetX &&
        unit->Y == unit->TargetY) {
        unit->StuckDetourActive = FALSE;
        SetUnitMoveTarget(unit, unit->StuckOriginalTargetX, unit->StuckOriginalTargetY);
        return;
    }

    if (unit->LastMoveTime == 0) {
        unit->LastMoveTime = currentTime;
        unit->LastMoveX = unit->X;
        unit->LastMoveY = unit->Y;
        return;
    }

    if (unit->X != unit->LastMoveX || unit->Y != unit->LastMoveY) {
        unit->LastMoveX = unit->X;
        unit->LastMoveY = unit->Y;
        unit->LastMoveTime = currentTime;
        return;
    }

    moveTime = (unitType->MoveTimeMs > 0) ? (U32)unitType->MoveTimeMs : (U32)UNIT_MOVE_TIME_MS;
    timeoutMs = moveTime * (U32)UNIT_STUCK_TIMEOUT_MULTIPLIER;
    if (timeoutMs == 0) timeoutMs = (U32)UNIT_MOVE_TIME_MS;

    if (currentTime - unit->LastMoveTime < timeoutMs) return;

    if (unit->State == UNIT_STATE_EXPLORE &&
        unit->StuckDetourCount > 0) {
        I32 newTargetX = UNIT_STATE_TARGET_NONE;
        I32 newTargetY = UNIT_STATE_TARGET_NONE;
        BOOL found = FALSE;

        if (unitType->Id == UNIT_TYPE_DRILLER) {
            BOOL skipTarget = (unit->StateTargetX != UNIT_STATE_TARGET_NONE &&
                               unit->StateTargetY != UNIT_STATE_TARGET_NONE);
            found = FindNearestReachablePlasmaTargetInternal(unit, unitType, skipTarget,
                                                             unit->StateTargetX, unit->StateTargetY,
                                                             &newTargetX, &newTargetY);
        } else {
            found = PickExplorationTarget(unit->Team, &newTargetX, &newTargetY);
        }

        if (found) {
            SetUnitStateExplore(unit, newTargetX, newTargetY);
        } else {
            unit->StateTargetX = UNIT_STATE_TARGET_NONE;
            unit->StateTargetY = UNIT_STATE_TARGET_NONE;
            unit->IsMoving = FALSE;
            unit->MoveProgress = 0;
            ClearUnitPath(unit);
        }
        unit->StuckDetourActive = FALSE;
        unit->StuckDetourCount = 0;
        unit->LastMoveTime = currentTime;
        return;
    }

    if (unit->StuckDetourActive) {
        targetX = unit->StuckOriginalTargetX;
        targetY = unit->StuckOriginalTargetY;
    } else {
        targetX = unit->TargetX;
        targetY = unit->TargetY;
    }

    deltaX = SignedWrapDelta(unit->X, targetX, mapW);
    deltaY = SignedWrapDelta(unit->Y, targetY, mapH);
    stepX = (deltaX > 0) ? 1 : (deltaX < 0 ? -1 : 0);
    stepY = (deltaY > 0) ? 1 : (deltaY < 0 ? -1 : 0);
    if (stepX == 0 && stepY == 0) return;

    detourX = WrapCoord(unit->X, -stepX * UNIT_STUCK_BACKOFF_TILES, mapW);
    detourY = WrapCoord(unit->Y, -stepY * UNIT_STUCK_BACKOFF_TILES, mapH);

    if (!unit->StuckDetourActive) {
        originalTargetX = unit->TargetX;
        originalTargetY = unit->TargetY;
    } else {
        originalTargetX = unit->StuckOriginalTargetX;
        originalTargetY = unit->StuckOriginalTargetY;
    }

    SetUnitMoveTarget(unit, detourX, detourY);
    unit->StuckDetourActive = TRUE;
    unit->StuckDetourCount++;
    unit->StuckOriginalTargetX = originalTargetX;
    unit->StuckOriginalTargetY = originalTargetY;
    unit->StuckDetourTargetX = detourX;
    unit->StuckDetourTargetY = detourY;
    unit->LastMoveTime = currentTime;
}

/************************************************************************/

static BOOL SelectDirectMoveStep(const UNIT* unit, const UNIT_TYPE* unitType, I32* outX, I32* outY) {
    I32 mapW;
    I32 mapH;
    I32 deltaX;
    I32 deltaY;
    I32 stepX;
    I32 stepY;
    I32 candX;
    I32 candY;

    if (App.GameState == NULL || unit == NULL || unitType == NULL || outX == NULL || outY == NULL) return FALSE;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    deltaX = SignedWrapDelta(unit->X, unit->TargetX, mapW);
    deltaY = SignedWrapDelta(unit->Y, unit->TargetY, mapH);
    stepX = (deltaX > 0) ? 1 : (deltaX < 0 ? -1 : 0);
    stepY = (deltaY > 0) ? 1 : (deltaY < 0 ? -1 : 0);

    if (stepX == 0 && stepY == 0) return FALSE;

    if (stepX != 0 && stepY != 0) {
        candX = WrapCoord(unit->X, stepX, mapW);
        candY = WrapCoord(unit->Y, stepY, mapH);
        if (!IsAreaBlocked(candX, candY, unitType->Width, unitType->Height, NULL, unit)) {
            *outX = candX;
            *outY = candY;
            return TRUE;
        }
    }

    if (abs(deltaX) >= abs(deltaY)) {
        if (stepX != 0) {
            candX = WrapCoord(unit->X, stepX, mapW);
            candY = unit->Y;
            if (!IsAreaBlocked(candX, candY, unitType->Width, unitType->Height, NULL, unit)) {
                *outX = candX;
                *outY = candY;
                return TRUE;
            }
        }
        if (stepY != 0) {
            candX = unit->X;
            candY = WrapCoord(unit->Y, stepY, mapH);
            if (!IsAreaBlocked(candX, candY, unitType->Width, unitType->Height, NULL, unit)) {
                *outX = candX;
                *outY = candY;
                return TRUE;
            }
        }
    } else {
        if (stepY != 0) {
            candX = unit->X;
            candY = WrapCoord(unit->Y, stepY, mapH);
            if (!IsAreaBlocked(candX, candY, unitType->Width, unitType->Height, NULL, unit)) {
                *outX = candX;
                *outY = candY;
                return TRUE;
            }
        }
        if (stepX != 0) {
            candX = WrapCoord(unit->X, stepX, mapW);
            candY = unit->Y;
            if (!IsAreaBlocked(candX, candY, unitType->Width, unitType->Height, NULL, unit)) {
                *outX = candX;
                *outY = candY;
                return TRUE;
            }
        }
    }

    return FALSE;
}

/************************************************************************/

static BOOL FindPlasmaInUnitSight(const UNIT* unit, const UNIT_TYPE* unitType, I32* outX, I32* outY) {
    I32 mapW;
    I32 mapH;
    I32 radius;
    I32 centerX;
    I32 centerY;
    BOOL found = FALSE;
    I32 bestDistance = 0;

    if (App.GameState == NULL || unit == NULL || unitType == NULL) return FALSE;
    if (App.GameState->PlasmaDensity == NULL) return FALSE;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    radius = (unitType->Sight > 0) ? unitType->Sight : 1;
    centerX = WrapCoord(unit->X, unitType->Width / 2, mapW);
    centerY = WrapCoord(unit->Y, unitType->Height / 2, mapH);

    for (I32 dy = -radius; dy <= radius; dy++) {
        for (I32 dx = -radius; dx <= radius; dx++) {
            if (ChebyshevDistance(0, 0, dx, dy, mapW, mapH) > radius) continue;
            I32 px = WrapCoord(centerX, dx, mapW);
            I32 py = WrapCoord(centerY, dy, mapH);
            if (App.GameState->PlasmaDensity[py][px] > 0) {
                I32 dist = ChebyshevDistance(centerX, centerY, px, py, mapW, mapH);
                if (!found || dist < bestDistance) {
                    bestDistance = dist;
                    if (outX != NULL) *outX = px;
                    if (outY != NULL) *outY = py;
                    found = TRUE;
                }
            }
        }
    }

    return found;
}

/************************************************************************/

/// @brief Update autonomous unit behavior for non-idle states.
static void UpdateUnitStateBehavior(UNIT* unit, const UNIT_TYPE* unitType, U32 currentTime) {
    if (unit == NULL || unitType == NULL || App.GameState == NULL) return;
    if (unit->State == UNIT_STATE_IDLE) return;
    if (unit->IsGridlocked) return;
    if (unit->LastStateUpdateTime != 0 &&
        currentTime - unit->LastStateUpdateTime < UNIT_STATE_UPDATE_INTERVAL_MS) return;

    unit->LastStateUpdateTime = currentTime;

    if (unit->State == UNIT_STATE_ESCORT) {
        UNIT* target = FindUnitById(unit->EscortUnitTeam, unit->EscortUnitId);
        if (target == NULL || target == unit) {
            SetUnitStateIdle(unit);
            return;
        }

        I32 mapW = App.GameState->MapWidth;
        I32 mapH = App.GameState->MapHeight;
        if (mapW <= 0 || mapH <= 0) return;

        if (ChebyshevDistance(unit->X, unit->Y, target->X, target->Y, mapW, mapH) <= 1) {
            return;
        }

        I32 escortX;
        I32 escortY;
        if (FindFreeSpotNear(target->X, target->Y, unitType->Width, unitType->Height, mapW, mapH, ESCORT_SPAWN_RADIUS, &escortX, &escortY)) {
            if (unit->TargetX != escortX || unit->TargetY != escortY) {
                SetUnitMoveTarget(unit, escortX, escortY);
            }
        }
        return;
    }

    if (unit->State == UNIT_STATE_EXPLORE) {
        if (unit->StuckDetourActive) {
            return;
        }
        BOOL needsTarget = (unit->StateTargetX == UNIT_STATE_TARGET_NONE || unit->StateTargetY == UNIT_STATE_TARGET_NONE);

        if (!needsTarget && unit->X == unit->StateTargetX && unit->Y == unit->StateTargetY) {
            needsTarget = TRUE;
        }
        if (unitType->Id == UNIT_TYPE_DRILLER && !needsTarget) {
            if (!HasPlasmaInFootprint(unit->StateTargetX, unit->StateTargetY, unitType->Width, unitType->Height)) {
                needsTarget = TRUE;
            }
        }

        if (needsTarget) {
            I32 targetX = UNIT_STATE_TARGET_NONE;
            I32 targetY = UNIT_STATE_TARGET_NONE;
            BOOL found = FALSE;

            if (unitType->Id == UNIT_TYPE_DRILLER) {
                found = FindNearestReachablePlasmaTarget(unit, unitType, &targetX, &targetY);
            } else {
                found = PickExplorationTarget(unit->Team, &targetX, &targetY);
            }

            if (!found) {
                unit->StateTargetX = UNIT_STATE_TARGET_NONE;
                unit->StateTargetY = UNIT_STATE_TARGET_NONE;
                unit->IsMoving = FALSE;
                unit->MoveProgress = 0;
                ClearUnitPath(unit);
                return;
            }

            unit->StateTargetX = targetX;
            unit->StateTargetY = targetY;
        }

        if (unit->StateTargetX != UNIT_STATE_TARGET_NONE && unit->StateTargetY != UNIT_STATE_TARGET_NONE) {
            if (unit->TargetX != unit->StateTargetX || unit->TargetY != unit->StateTargetY) {
                SetUnitMoveTarget(unit, unit->StateTargetX, unit->StateTargetY);
            }
        }
    }
}

/************************************************************************/

void UpdateGame(void) {
    if (App.GameState == NULL || App.GameState->IsPaused) return;

    U32 currentTime = GetSystemTime();
    U32 deltaTime = currentTime - App.GameState->LastUpdate;
    U32 timeStep = deltaTime * (U32)App.GameState->GameSpeed;
    I32 teamCount = GetTeamCountSafe();

    /* Update game time */
    App.GameState->GameTime += deltaTime * App.GameState->GameSpeed;

    if (App.GameState->IsGameOver) return;
    if (IsTeamEliminated(HUMAN_TEAM_INDEX)) {
        App.GameState->IsGameOver = TRUE;
        App.GameState->IsPaused = TRUE;
        App.Menu.CurrentMenu = MENU_GAME_OVER;
        App.Menu.PrevMenu = -1;
        CancelBuildingPlacement();
        App.GameState->IsCommandMode = FALSE;
        return;
    }

    /* Update units */
    for (I32 team = 0; team < teamCount; team++) {
        UNIT* unit = App.GameState->TeamData[team].Units;
        while (unit != NULL) {
            const UNIT_TYPE* unitType = GetUnitTypeById(unit->TypeId);
            if (unitType == NULL) {
                unit = unit->Next;
                continue;
            }

            UpdateUnitStateBehavior(unit, unitType, App.GameState->GameTime);

            /* Movement */
            if (unit->IsMoving) {
                I32 moveTime = unitType->MoveTimeMs > 0 ? unitType->MoveTimeMs : UNIT_MOVE_TIME_MS;
                unit->MoveProgress += timeStep;
                while (unit->IsMoving && unit->MoveProgress >= (U32)moveTime) {
                    unit->MoveProgress -= (U32)moveTime;
                    I32 nextX;
                    I32 nextY;

                    if (unit->X == unit->TargetX && unit->Y == unit->TargetY) {
                        unit->IsMoving = FALSE;
                        break;
                    }

                    if (ENABLE_PATHFINDING) {
                        if (unit->PathTargetX != unit->TargetX || unit->PathTargetY != unit->TargetY) {
                            ClearUnitPath(unit);
                        }

                        if (unit->PathHead == NULL) {
                            if (!BuildUnitPathBFS(unit, unit->TargetX, unit->TargetY)) {
                                unit->IsMoving = FALSE;
                                break;
                            }
                        }

                        if (!PopUnitPathNext(unit, &nextX, &nextY)) {
                            unit->IsMoving = FALSE;
                            break;
                        }
                    } else {
                        if (!SelectDirectMoveStep(unit, unitType, &nextX, &nextY)) {
                            unit->IsMoving = FALSE;
                            break;
                        }
                    }

                    if (ENABLE_PATHFINDING &&
                        IsAreaBlocked(nextX, nextY, unitType->Width, unitType->Height, NULL, unit)) {
                        ClearUnitPath(unit);
                        if (!BuildUnitPathBFS(unit, unit->TargetX, unit->TargetY) ||
                            !PopUnitPathNext(unit, &nextX, &nextY) ||
                            IsAreaBlocked(nextX, nextY, unitType->Width, unitType->Height, NULL, unit)) {
                            unit->IsMoving = FALSE;
                            break;
                        }
                    }

                    if (!IsAreaBlocked(nextX, nextY, unitType->Width, unitType->Height, NULL, unit)) {
                        SetUnitOccupancy(unit, FALSE);
                        unit->X = nextX;
                        unit->Y = nextY;
                        SetUnitOccupancy(unit, TRUE);
                        App.GameState->FogDirty = TRUE;
                        unit->LastMoveX = unit->X;
                        unit->LastMoveY = unit->Y;
                        unit->LastMoveTime = App.GameState->GameTime;
                    }

                    /* Check if reached destination */
                    if (unit->X == unit->TargetX && unit->Y == unit->TargetY) {
                        unit->IsMoving = FALSE;
                    }
                }
            }

            UpdateUnitGridlock(unit, unitType, App.GameState->GameTime);
            UpdateUnitStuckBehavior(unit, unitType, App.GameState->GameTime);

            if (unitType->Id == UNIT_TYPE_DRILLER) {
                if (App.GameState->GameTime - unit->LastHarvestTime >= DRILLER_HARVEST_INTERVAL_MS &&
                    App.GameState->PlasmaDensity != NULL) {
                    I32 plasmaX;
                    I32 plasmaY;
                    if (!FindPlasmaInUnitSight(unit, unitType, &plasmaX, &plasmaY)) {
                        unit = unit->Next;
                        continue;
                    }
                    TEAM_RESOURCES* res = GetTeamResources(unit->Team);
                    if (res != NULL) {
                        I32 available = App.GameState->PlasmaDensity[plasmaY][plasmaX];
                        I32 harvested = (available >= DRILLER_HARVEST_AMOUNT) ? DRILLER_HARVEST_AMOUNT : available;
                        if (harvested > 0) {
                            res->Plasma += harvested;
                            App.GameState->PlasmaDensity[plasmaY][plasmaX] = available - harvested;
                            if (App.GameState->PlasmaDensity[plasmaY][plasmaX] <= 0) {
                                TerrainInitCell(&App.GameState->Terrain[plasmaY][plasmaX], TERRAIN_TYPE_PLAINS);
                            }
                        }
                    }
                    unit->LastHarvestTime = App.GameState->GameTime;
                }
            }
            unit = unit->Next;
        }
    }

    ProcessUnitAttacks(currentTime);
    ProcessTurretAttacks(currentTime);

    /* Update buildings under construction */
    for (I32 team = 0; team < teamCount; team++) {
        BUILDING* building = App.GameState->TeamData[team].Buildings;
        while (building != NULL) {
            if (building->UnderConstruction) {
                if (building->BuildTimeRemaining > timeStep) {
                    building->BuildTimeRemaining -= timeStep;
                } else {
                    building->BuildTimeRemaining = 0;
                    building->UnderConstruction = FALSE;
                    building->Hp = GetBuildingTypeById(building->TypeId) != NULL ? GetBuildingTypeById(building->TypeId)->MaxHp : building->Hp;
                    RecalculateEnergy();
                }
            }
            building = building->Next;
        }
    }

    for (I32 team = 0; team < teamCount; team++) {
        UpdateBuildQueueForTeam(team, timeStep, TRUE);
    }

    if (App.GameState->FogDirty) {
        U32 lastFog = App.GameState->LastFogUpdate;
        if (lastFog == 0 || currentTime - lastFog >= FOG_OF_WAR_UPDATE_INTERVAL_MS) {
            UpdateFogOfWar(currentTime);
        }
    }

    ProcessAITeams();

    App.GameState->LastUpdate = currentTime;
}
