
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

#ifndef TT_MAP_H
#define TT_MAP_H

#include "tt-types.h"

I32 WrapCoord(I32 value, I32 delta, I32 size);
I32 WrapDistance(I32 a, I32 b, I32 size);
I32 ChebyshevDistance(I32 ax, I32 ay, I32 bx, I32 by, I32 mapW, I32 mapH);

void TerrainSetOccupied(TERRAIN* cell, BOOL occupied);
BOOL TerrainIsOccupied(const TERRAIN* cell);
void TerrainSetVisible(TERRAIN* cell, BOOL visible);
BOOL TerrainIsVisible(const TERRAIN* cell);
U8 TerrainGetType(const TERRAIN* cell);
void TerrainInitCell(TERRAIN* cell, U8 type);
char TerrainTypeToChar(U8 type);
U8 TerrainCharToType(char tile);
BOOL IsUnitTypeMountainCapable(I32 unitTypeId);
BOOL IsTerrainWalkableForUnitType(I32 x, I32 y, I32 width, I32 height, I32 unitTypeId);

BOOL AllocateMap(I32 width, I32 height);
void FreeMap(void);
void RebuildOccupancy(void);
void SetUnitOccupancy(const UNIT* unit, BOOL occupied);
void SetBuildingOccupancy(const BUILDING* building, BOOL occupied);
BOOL IsAreaBlocked(I32 x, I32 y, I32 width, I32 height, const BUILDING* ignoreBuilding, const UNIT* ignoreUnit);
BOOL IsAreaBlockedForUnitType(I32 x, I32 y, I32 width, I32 height, I32 unitTypeId);

#endif /* TT_MAP_H */
