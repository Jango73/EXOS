
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

#include "tt-map.h"
#include "tt-fog.h"

/************************************************************************/

static BOOL PointInEntityArea(I32 px, I32 py, I32 areaX, I32 areaY, I32 width, I32 height, I32 mapW, I32 mapH) {
    for (I32 dy = 0; dy < height; dy++) {
        for (I32 dx = 0; dx < width; dx++) {
            I32 tx = WrapCoord(areaX, dx, mapW);
            I32 ty = WrapCoord(areaY, dy, mapH);
            if (tx == px && ty == py) {
                return TRUE;
            }
        }
    }
    return FALSE;
}

/************************************************************************/

I32 WrapCoord(I32 value, I32 delta, I32 size) {
    if (size <= 0) return value + delta;
    value += delta;
    if (value >= size) value -= size;
    else if (value < 0) value += size;
    return value;
}

/************************************************************************/

I32 WrapDistance(I32 a, I32 b, I32 size) {
    I32 delta = abs(a - b);
    if (size > 0 && delta > size / 2) {
        delta = size - delta;
    }
    return delta;
}

/************************************************************************/

I32 ChebyshevDistance(I32 ax, I32 ay, I32 bx, I32 by, I32 mapW, I32 mapH) {
    I32 dx = WrapDistance(ax, bx, mapW);
    I32 dy = WrapDistance(ay, by, mapH);
    return (dx > dy) ? dx : dy;
}

/************************************************************************/

void TerrainSetOccupied(TERRAIN* cell, BOOL occupied) {
    if (cell == NULL) return;
    if (occupied) {
        cell->Bits = (U8)(cell->Bits | TERRAIN_FLAG_OCCUPIED);
    } else {
        cell->Bits = (U8)(cell->Bits & (U8)(~TERRAIN_FLAG_OCCUPIED));
    }
}

/************************************************************************/

BOOL TerrainIsOccupied(const TERRAIN* cell) {
    if (cell == NULL) return FALSE;
    return (cell->Bits & TERRAIN_FLAG_OCCUPIED) != 0;
}

/************************************************************************/

void TerrainSetVisible(TERRAIN* cell, BOOL visible) {
    if (cell == NULL) return;
    if (visible) {
        cell->Bits = (U8)(cell->Bits | TERRAIN_FLAG_VISIBLE);
    } else {
        cell->Bits = (U8)(cell->Bits & (U8)(~TERRAIN_FLAG_VISIBLE));
    }
}

/************************************************************************/

BOOL TerrainIsVisible(const TERRAIN* cell) {
    if (cell == NULL) return FALSE;
    return (cell->Bits & TERRAIN_FLAG_VISIBLE) != 0;
}

/************************************************************************/

U8 TerrainGetType(const TERRAIN* cell) {
    if (cell == NULL) return TERRAIN_TYPE_PLAINS;
    return (U8)(cell->Bits & TERRAIN_TYPE_MASK);
}

/************************************************************************/

void TerrainInitCell(TERRAIN* cell, U8 type) {
    if (cell == NULL) return;
    cell->Bits = (U8)(type & TERRAIN_TYPE_MASK);
    TerrainSetOccupied(cell, FALSE);
    TerrainSetVisible(cell, FALSE);
}

/************************************************************************/

char TerrainTypeToChar(U8 type) {
    switch (type & TERRAIN_TYPE_MASK) {
        case TERRAIN_TYPE_FOREST:
            return TERRAIN_CHAR_FOREST;
        case TERRAIN_TYPE_PLASMA:
            return TERRAIN_CHAR_PLASMA;
        case TERRAIN_TYPE_MOUNTAIN:
            return TERRAIN_CHAR_MOUNTAIN;
        case TERRAIN_TYPE_WATER:
            return TERRAIN_CHAR_WATER;
        case TERRAIN_TYPE_PLAINS:
        default:
            return TERRAIN_CHAR_PLAINS;
    }
}

/************************************************************************/

U8 TerrainCharToType(char tile) {
    switch (tile) {
        case TERRAIN_CHAR_FOREST:
            return TERRAIN_TYPE_FOREST;
        case TERRAIN_CHAR_PLASMA:
            return TERRAIN_TYPE_PLASMA;
        case TERRAIN_CHAR_MOUNTAIN:
            return TERRAIN_TYPE_MOUNTAIN;
        case TERRAIN_CHAR_WATER:
            return TERRAIN_TYPE_WATER;
        default:
            return TERRAIN_TYPE_PLAINS;
    }
}

/************************************************************************/

BOOL IsUnitTypeMountainCapable(I32 unitTypeId) {
    return (unitTypeId == UNIT_TYPE_TROOPER ||
            unitTypeId == UNIT_TYPE_SOLDIER ||
            unitTypeId == UNIT_TYPE_SCOUT);
}

/************************************************************************/

BOOL IsTerrainWalkableForUnitType(I32 x, I32 y, I32 width, I32 height, I32 unitTypeId) {
    I32 mapW;
    I32 mapH;
    BOOL allowMountain;

    if (App.GameState == NULL || App.GameState->Terrain == NULL) return FALSE;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    allowMountain = IsUnitTypeMountainCapable(unitTypeId);

    for (I32 dy = 0; dy < height; dy++) {
        for (I32 dx = 0; dx < width; dx++) {
            I32 px = WrapCoord(x, dx, mapW);
            I32 py = WrapCoord(y, dy, mapH);
            U8 type = TerrainGetType(&App.GameState->Terrain[py][px]);
            if (type == TERRAIN_TYPE_WATER) {
                return FALSE;
            }
            if (type == TERRAIN_TYPE_MOUNTAIN && !allowMountain) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

/************************************************************************/

BOOL AllocateMap(I32 width, I32 height) {
    I32 i;

    if (width < MIN_MAP_SIZE || width > MAX_MAP_SIZE ||
        height < MIN_MAP_SIZE || height > MAX_MAP_SIZE) {
        return FALSE;
    }

    App.GameState->Terrain = (TERRAIN**)malloc(height * sizeof(TERRAIN*));
    if (App.GameState->Terrain == NULL) return FALSE;

    for (i = 0; i < height; i++) {
        App.GameState->Terrain[i] = (TERRAIN*)malloc(width * sizeof(TERRAIN));
        if (App.GameState->Terrain[i] == NULL) {
            while (--i >= 0) free(App.GameState->Terrain[i]);
            free(App.GameState->Terrain);
            return FALSE;
        }
    }

    App.GameState->PlasmaDensity = (I32**)malloc(height * sizeof(I32*));
    if (App.GameState->PlasmaDensity == NULL) {
        for (i = 0; i < height; i++) free(App.GameState->Terrain[i]);
        free(App.GameState->Terrain);
        return FALSE;
    }

    for (i = 0; i < height; i++) {
        App.GameState->PlasmaDensity[i] = (I32*)malloc(width * sizeof(I32));
        if (App.GameState->PlasmaDensity[i] == NULL) {
            while (--i >= 0) free(App.GameState->PlasmaDensity[i]);
            free(App.GameState->PlasmaDensity);
            for (i = 0; i < height; i++) free(App.GameState->Terrain[i]);
            free(App.GameState->Terrain);
            return FALSE;
        }
    }

    App.GameState->MapWidth = width;
    App.GameState->MapHeight = height;
    return TRUE;
}

/************************************************************************/

void FreeMap(void) {
    I32 i;

    if (App.GameState->Terrain != NULL) {
        for (i = 0; i < App.GameState->MapHeight; i++) {
            if (App.GameState->Terrain[i] != NULL) {
                free(App.GameState->Terrain[i]);
            }
        }
        free(App.GameState->Terrain);
        App.GameState->Terrain = NULL;
    }

    if (App.GameState->PlasmaDensity != NULL) {
        for (i = 0; i < App.GameState->MapHeight; i++) {
            if (App.GameState->PlasmaDensity[i] != NULL) {
                free(App.GameState->PlasmaDensity[i]);
            }
        }
        free(App.GameState->PlasmaDensity);
        App.GameState->PlasmaDensity = NULL;
    }

    FreeTeamMemoryBuffers();
    App.GameState->MapWidth = 0;
    App.GameState->MapHeight = 0;
}

/************************************************************************/

static void SetAreaOccupied(I32 x, I32 y, I32 width, I32 height, BOOL occupied) {
    if (App.GameState == NULL || App.GameState->Terrain == NULL) return;
    I32 mapW = App.GameState->MapWidth;
    I32 mapH = App.GameState->MapHeight;
    for (I32 dy = 0; dy < height; dy++) {
        for (I32 dx = 0; dx < width; dx++) {
            I32 px = WrapCoord(x, dx, mapW);
            I32 py = WrapCoord(y, dy, mapH);
            TerrainSetOccupied(&App.GameState->Terrain[py][px], occupied);
        }
    }
}

/************************************************************************/

void SetBuildingOccupancy(const BUILDING* building, BOOL occupied) {
    if (App.GameState == NULL || building == NULL) return;
    const BUILDING_TYPE* bt = GetBuildingTypeById(building->TypeId);
    if (bt == NULL) return;
    SetAreaOccupied(building->X, building->Y, bt->Width, bt->Height, occupied);
}

/************************************************************************/

void SetUnitOccupancy(const UNIT* unit, BOOL occupied) {
    if (App.GameState == NULL || unit == NULL) return;
    const UNIT_TYPE* ut = GetUnitTypeById(unit->TypeId);
    if (ut == NULL) return;
    SetAreaOccupied(unit->X, unit->Y, ut->Width, ut->Height, occupied);
}

/************************************************************************/

void RebuildOccupancy(void) {
    if (App.GameState == NULL || App.GameState->Terrain == NULL) return;
    I32 mapW = App.GameState->MapWidth;
    I32 mapH = App.GameState->MapHeight;

    for (I32 y = 0; y < mapH; y++) {
        for (I32 x = 0; x < mapW; x++) {
            TerrainSetOccupied(&App.GameState->Terrain[y][x], FALSE);
        }
    }

    for (I32 team = 0; team < App.GameState->TeamCount; team++) {
        BUILDING* building = App.GameState->TeamData[team].Buildings;
        while (building != NULL) {
            SetBuildingOccupancy(building, TRUE);
            building = building->Next;
        }

        UNIT* unit = App.GameState->TeamData[team].Units;
        while (unit != NULL) {
            SetUnitOccupancy(unit, TRUE);
            unit = unit->Next;
        }
    }
}

/************************************************************************/

BOOL IsAreaBlocked(I32 x, I32 y, I32 width, I32 height, const BUILDING* ignoreBuilding, const UNIT* ignoreUnit) {
    I32 mapW;
    I32 mapH;
    I32 unitTypeId;

    if (App.GameState == NULL || App.GameState->Terrain == NULL) return TRUE;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    unitTypeId = (ignoreUnit != NULL) ? ignoreUnit->TypeId : -1;

    if (!IsTerrainWalkableForUnitType(x, y, width, height, unitTypeId)) {
        return TRUE;
    }

    for (I32 dy = 0; dy < height; dy++) {
        for (I32 dx = 0; dx < width; dx++) {
            I32 px = WrapCoord(x, dx, mapW);
            I32 py = WrapCoord(y, dy, mapH);
            if (TerrainIsOccupied(&App.GameState->Terrain[py][px])) {
                BOOL ignore = FALSE;
                if (!ignore && ignoreBuilding != NULL) {
                    const BUILDING_TYPE* bt = GetBuildingTypeById(ignoreBuilding->TypeId);
                    if (bt != NULL && PointInEntityArea(px, py, ignoreBuilding->X, ignoreBuilding->Y, bt->Width, bt->Height, mapW, mapH)) {
                        ignore = TRUE;
                    }
                }
                if (!ignore && ignoreUnit != NULL) {
                    const UNIT_TYPE* ut = GetUnitTypeById(ignoreUnit->TypeId);
                    if (ut != NULL && PointInEntityArea(px, py, ignoreUnit->X, ignoreUnit->Y, ut->Width, ut->Height, mapW, mapH)) {
                        ignore = TRUE;
                    }
                }
                if (!ignore) {
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}
