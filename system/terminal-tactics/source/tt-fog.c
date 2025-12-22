
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

#include "tt-fog.h"
#include "tt-map.h"

/************************************************************************/

void FreeTeamMemoryBuffers(void) {
    if (App.GameState == NULL) return;
    for (I32 team = 0; team < MAX_TEAMS; team++) {
        if (App.GameState->TeamData[team].MemoryMap != NULL) {
            free(App.GameState->TeamData[team].MemoryMap);
            App.GameState->TeamData[team].MemoryMap = NULL;
        }
        if (App.GameState->TeamData[team].VisibleNow != NULL) {
            free(App.GameState->TeamData[team].VisibleNow);
            App.GameState->TeamData[team].VisibleNow = NULL;
        }
    }
    App.GameState->TeamMemoryBytes = 0;
}

/************************************************************************/

BOOL EnsureTeamMemoryBuffers(I32 mapW, I32 mapH, I32 teamCount) {
    size_t cellCount;
    size_t memoryBytes;
    size_t visibleBytes;

    if (App.GameState == NULL) return FALSE;
    if (mapW <= 0 || mapH <= 0 || teamCount <= 0 || teamCount > MAX_TEAMS) return FALSE;

    cellCount = (size_t)mapW * (size_t)mapH;
    memoryBytes = cellCount * sizeof(MEMORY_CELL);
    visibleBytes = cellCount * sizeof(U8);

    if (App.GameState->TeamMemoryBytes != memoryBytes) {
        FreeTeamMemoryBuffers();
        App.GameState->TeamMemoryBytes = memoryBytes;
    }

    for (I32 team = 0; team < teamCount; team++) {
        BOOL allocatedMemory = FALSE;
        if (App.GameState->TeamData[team].MemoryMap == NULL) {
            App.GameState->TeamData[team].MemoryMap = (MEMORY_CELL*)malloc(memoryBytes);
            if (App.GameState->TeamData[team].MemoryMap == NULL) return FALSE;
            allocatedMemory = TRUE;
        }
        if (App.GameState->TeamData[team].VisibleNow == NULL) {
            App.GameState->TeamData[team].VisibleNow = (U8*)malloc(visibleBytes);
            if (App.GameState->TeamData[team].VisibleNow == NULL) return FALSE;
        }

        if (allocatedMemory) {
            memset(App.GameState->TeamData[team].MemoryMap, 0, memoryBytes);
        }
        memset(App.GameState->TeamData[team].VisibleNow, 0, visibleBytes);
    }

    return TRUE;
}

/************************************************************************/

static BOOL EnsureScratchOccupancy(I32 mapW, I32 mapH) {
    size_t bytes;

    if (App.GameState == NULL) return FALSE;
    if (mapW <= 0 || mapH <= 0) return FALSE;
    bytes = (size_t)mapW * (size_t)mapH * sizeof(MEMORY_CELL);

    if (App.GameState->ScratchOccupancyBytes < bytes) {
        if (App.GameState->ScratchOccupancy != NULL) {
            free(App.GameState->ScratchOccupancy);
        }
        App.GameState->ScratchOccupancy = (MEMORY_CELL*)malloc(bytes);
        if (App.GameState->ScratchOccupancy == NULL) {
            App.GameState->ScratchOccupancyBytes = 0;
            return FALSE;
        }
        App.GameState->ScratchOccupancyBytes = bytes;
    }

    if (App.GameState->ScratchOccupancy != NULL) {
        memset(App.GameState->ScratchOccupancy, 0, bytes);
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

BOOL IsCellVisibleToTeam(I32 x, I32 y, I32 team) {
    U8* visible;
    I32 mapW;
    I32 mapH;

    if (!IsValidTeam(team) || App.GameState == NULL) return FALSE;
    visible = App.GameState->TeamData[team].VisibleNow;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (visible == NULL || mapW <= 0 || mapH <= 0) return FALSE;

    x = WrapCoord(x, 0, mapW);
    y = WrapCoord(y, 0, mapH);
    return visible[(size_t)y * (size_t)mapW + (size_t)x] != 0;
}

/************************************************************************/

BOOL IsAreaVisibleToTeam(I32 x, I32 y, I32 width, I32 height, I32 team) {
    for (I32 dy = 0; dy < height; dy++) {
        for (I32 dx = 0; dx < width; dx++) {
            if (IsCellVisibleToTeam(x + dx, y + dy, team)) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/************************************************************************/

BOOL IsAreaExploredToTeam(I32 x, I32 y, I32 width, I32 height, I32 team) {
    I32 mapW;
    I32 mapH;
    MEMORY_CELL* memory;

    if (App.GameState == NULL) return FALSE;
    if (!IsValidTeam(team)) return FALSE;
    if (App.GameState->SeeEverything) return TRUE;

    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    memory = App.GameState->TeamData[team].MemoryMap;
    if (memory == NULL) return FALSE;

    for (I32 dy = 0; dy < height; dy++) {
        for (I32 dx = 0; dx < width; dx++) {
            I32 px = WrapCoord(x, dx, mapW);
            I32 py = WrapCoord(y, dy, mapH);
            size_t idx = (size_t)py * (size_t)mapW + (size_t)px;
            if (memory[idx].TerrainKnown == 0) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

/************************************************************************/

static BOOL IsCellExploredToTeam(I32 x, I32 y, I32 team) {
    I32 mapW;
    I32 mapH;
    MEMORY_CELL* memory;

    if (App.GameState == NULL) return FALSE;
    if (!IsValidTeam(team)) return FALSE;
    if (App.GameState->SeeEverything) return TRUE;

    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    memory = App.GameState->TeamData[team].MemoryMap;
    if (memory == NULL) return FALSE;

    x = WrapCoord(x, 0, mapW);
    y = WrapCoord(y, 0, mapH);
    return memory[(size_t)y * (size_t)mapW + (size_t)x].TerrainKnown != 0;
}

/************************************************************************/

BOOL IsAreaExploredToTeamWithMargin(I32 x, I32 y, I32 width, I32 height, I32 team, I32 margin) {
    if (margin <= 0) {
        return IsAreaExploredToTeam(x, y, width, height, team);
    }
    if (App.GameState == NULL) return FALSE;

    for (I32 dy = 0; dy < height; dy++) {
        for (I32 dx = 0; dx < width; dx++) {
            I32 px = x + dx;
            I32 py = y + dy;
            if (IsCellExploredToTeam(px, py, team)) continue;

            BOOL found = FALSE;
            for (I32 oy = -margin; oy <= margin && !found; oy++) {
                for (I32 ox = -margin; ox <= margin; ox++) {
                    if (IsCellExploredToTeam(px + ox, py + oy, team)) {
                        found = TRUE;
                        break;
                    }
                }
            }
            if (!found) return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

void UpdateFogOfWar(U32 currentTime) {
    I32 mapW;
    I32 mapH;
    size_t cellCount;

    if (App.GameState == NULL) return;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return;

    if (!EnsureTeamMemoryBuffers(mapW, mapH, App.GameState->TeamCount)) return;
    if (!EnsureScratchOccupancy(mapW, mapH)) return;

    cellCount = (size_t)mapW * (size_t)mapH;

    for (I32 team = 0; team < App.GameState->TeamCount; team++) {
        BUILDING* building = App.GameState->TeamData[team].Buildings;
        while (building != NULL) {
            const BUILDING_TYPE* bt = GetBuildingTypeById(building->TypeId);
            if (bt != NULL) {
                for (I32 dy = 0; dy < bt->Height; dy++) {
                    for (I32 dx = 0; dx < bt->Width; dx++) {
                        I32 px = WrapCoord(building->X, dx, mapW);
                        I32 py = WrapCoord(building->Y, dy, mapH);
                        size_t idx = (size_t)py * (size_t)mapW + (size_t)px;
                        App.GameState->ScratchOccupancy[idx].Team = (U8)(team & 0x7);
                        App.GameState->ScratchOccupancy[idx].IsBuilding = 1;
                        App.GameState->ScratchOccupancy[idx].OccupiedType = (U8)bt->Id;
                    }
                }
            }
            building = building->Next;
        }

        UNIT* unit = App.GameState->TeamData[team].Units;
        while (unit != NULL) {
            const UNIT_TYPE* ut = GetUnitTypeById(unit->TypeId);
            if (ut != NULL) {
                for (I32 dy = 0; dy < ut->Height; dy++) {
                    for (I32 dx = 0; dx < ut->Width; dx++) {
                        I32 px = WrapCoord(unit->X, dx, mapW);
                        I32 py = WrapCoord(unit->Y, dy, mapH);
                        size_t idx = (size_t)py * (size_t)mapW + (size_t)px;
                        App.GameState->ScratchOccupancy[idx].Team = (U8)(team & 0x7);
                        App.GameState->ScratchOccupancy[idx].IsBuilding = 0;
                        App.GameState->ScratchOccupancy[idx].OccupiedType = (U8)ut->Id;
                    }
                }
            }
            unit = unit->Next;
        }
    }

    for (I32 team = 0; team < App.GameState->TeamCount; team++) {
        if (App.GameState->TeamData[team].VisibleNow != NULL) {
            memset(App.GameState->TeamData[team].VisibleNow, 0, cellCount);
        }
    }

    for (I32 team = 0; team < App.GameState->TeamCount; team++) {
        U8* visible = App.GameState->TeamData[team].VisibleNow;
        MEMORY_CELL* memory = App.GameState->TeamData[team].MemoryMap;
        if (visible == NULL || memory == NULL) continue;

        BUILDING* building = App.GameState->TeamData[team].Buildings;
        while (building != NULL) {
            const BUILDING_TYPE* bt = GetBuildingTypeById(building->TypeId);
            if (bt != NULL) {
                for (I32 dy = 0; dy < bt->Height; dy++) {
                    for (I32 dx = 0; dx < bt->Width; dx++) {
                        I32 originX = WrapCoord(building->X, dx, mapW);
                        I32 originY = WrapCoord(building->Y, dy, mapH);
                        for (I32 vy = -FOG_OF_WAR_SIGHT_RADIUS; vy <= FOG_OF_WAR_SIGHT_RADIUS; vy++) {
                            for (I32 vx = -FOG_OF_WAR_SIGHT_RADIUS; vx <= FOG_OF_WAR_SIGHT_RADIUS; vx++) {
                                if ((vx * vx + vy * vy) > (FOG_OF_WAR_SIGHT_RADIUS * FOG_OF_WAR_SIGHT_RADIUS)) continue;
                                I32 px = WrapCoord(originX, vx, mapW);
                                I32 py = WrapCoord(originY, vy, mapH);
                                size_t idx = (size_t)py * (size_t)mapW + (size_t)px;
                                visible[idx] = 1;
                                if (team == HUMAN_TEAM_INDEX) {
                                    TerrainSetVisible(&App.GameState->Terrain[py][px], TRUE);
                                }
                                {
                                    MEMORY_CELL occ = App.GameState->ScratchOccupancy[idx];
                                    if (occ.OccupiedType == 0) {
                                        memory[idx].Team = 0;
                                        memory[idx].IsBuilding = 0;
                                        memory[idx].OccupiedType = 0;
                                    } else {
                                        memory[idx].Team = occ.Team;
                                        memory[idx].IsBuilding = occ.IsBuilding;
                                        memory[idx].OccupiedType = occ.OccupiedType;
                                    }
                                    memory[idx].TerrainType = (U8)(TerrainGetType(&App.GameState->Terrain[py][px]) & 0x7);
                                    memory[idx].TerrainKnown = 1;
                                }
                            }
                        }
                    }
                }
            }
            building = building->Next;
        }

        UNIT* unit = App.GameState->TeamData[team].Units;
        while (unit != NULL) {
            const UNIT_TYPE* ut = GetUnitTypeById(unit->TypeId);
            if (ut != NULL) {
                for (I32 vy = -FOG_OF_WAR_SIGHT_RADIUS; vy <= FOG_OF_WAR_SIGHT_RADIUS; vy++) {
                    for (I32 vx = -FOG_OF_WAR_SIGHT_RADIUS; vx <= FOG_OF_WAR_SIGHT_RADIUS; vx++) {
                        if ((vx * vx + vy * vy) > (FOG_OF_WAR_SIGHT_RADIUS * FOG_OF_WAR_SIGHT_RADIUS)) continue;
                        I32 px = WrapCoord(unit->X, vx, mapW);
                        I32 py = WrapCoord(unit->Y, vy, mapH);
                        size_t idx = (size_t)py * (size_t)mapW + (size_t)px;
                        visible[idx] = 1;
                        if (team == HUMAN_TEAM_INDEX) {
                            TerrainSetVisible(&App.GameState->Terrain[py][px], TRUE);
                        }
                        {
                            MEMORY_CELL occ = App.GameState->ScratchOccupancy[idx];
                            if (occ.OccupiedType == 0) {
                                memory[idx].Team = 0;
                                memory[idx].IsBuilding = 0;
                                memory[idx].OccupiedType = 0;
                            } else {
                                memory[idx].Team = occ.Team;
                                memory[idx].IsBuilding = occ.IsBuilding;
                                memory[idx].OccupiedType = occ.OccupiedType;
                            }
                            memory[idx].TerrainType = (U8)(TerrainGetType(&App.GameState->Terrain[py][px]) & 0x7);
                            memory[idx].TerrainKnown = 1;
                        }
                    }
                }
            }
            unit = unit->Next;
        }
    }

    App.GameState->FogDirty = FALSE;
    App.GameState->LastFogUpdate = currentTime;
}
