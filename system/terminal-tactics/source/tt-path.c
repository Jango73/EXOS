
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

#include "tt-path.h"
#include "tt-map.h"

/************************************************************************/

static I32* PathQueue = NULL;
static I32* PathCameFrom = NULL;
static size_t PathCells = 0;

/************************************************************************/

/**
 * @brief Ensure BFS buffers are large enough for the current map size.
 */
static BOOL EnsurePathBuffers(I32 mapW, I32 mapH) {
    size_t cells;
    size_t bytes;

    if (mapW <= 0 || mapH <= 0) return FALSE;
    cells = (size_t)mapW * (size_t)mapH;
    if (cells == 0) return FALSE;

    if (PathCells < cells) {
        if (PathQueue != NULL) {
            free(PathQueue);
            PathQueue = NULL;
        }
        if (PathCameFrom != NULL) {
            free(PathCameFrom);
            PathCameFrom = NULL;
        }

        bytes = cells * sizeof(I32);
        PathQueue = (I32*)malloc(bytes);
        PathCameFrom = (I32*)malloc(bytes);
        if (PathQueue == NULL || PathCameFrom == NULL) {
            if (PathQueue != NULL) free(PathQueue);
            if (PathCameFrom != NULL) free(PathCameFrom);
            PathQueue = NULL;
            PathCameFrom = NULL;
            PathCells = 0;
            return FALSE;
        }
        PathCells = cells;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Check if a unit footprint is walkable at a given position.
 */
static BOOL IsWalkableAt(I32 x, I32 y, const UNIT* unit, const UNIT_TYPE* unitType, I32 mapW, I32 mapH) {
    UNUSED(mapW);
    UNUSED(mapH);
    if (unitType == NULL) return FALSE;
    if (!IsTerrainWalkableForUnitType(x, y, unitType->Width, unitType->Height, unitType->Id)) return FALSE;
    if (IsAreaBlocked(x, y, unitType->Width, unitType->Height, NULL, unit)) return FALSE;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Validate diagonal movement without corner cutting.
 */
static BOOL IsDiagonalAllowed(I32 fromX, I32 fromY, I32 stepX, I32 stepY,
                              const UNIT* unit, const UNIT_TYPE* unitType,
                              I32 mapW, I32 mapH) {
    I32 orthoX;
    I32 orthoY;

    if (stepX == 0 || stepY == 0) return TRUE;

    orthoX = WrapCoord(fromX, stepX, mapW);
    orthoY = fromY;
    if (!IsWalkableAt(orthoX, orthoY, unit, unitType, mapW, mapH)) return FALSE;

    orthoX = fromX;
    orthoY = WrapCoord(fromY, stepY, mapH);
    if (!IsWalkableAt(orthoX, orthoY, unit, unitType, mapW, mapH)) return FALSE;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Clear cached path nodes for a unit.
 */
void ClearUnitPath(UNIT* unit) {
    if (unit == NULL) return;

    PATH_NODE* current = unit->PathHead;
    while (current != NULL) {
        PATH_NODE* next = current->Next;
        free(current);
        current = next;
    }
    unit->PathHead = NULL;
    unit->PathTail = NULL;
}

/************************************************************************/

/**
 * @brief Build and cache a BFS path for a unit.
 */
BOOL BuildUnitPathBFS(UNIT* unit, I32 targetX, I32 targetY) {
    static const I32 offsets[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };
    const UNIT_TYPE* unitType;
    I32 mapW;
    I32 mapH;
    I32 startX;
    I32 startY;
    I32 startIndex;
    I32 targetIndex;
    I32 head = 0;
    I32 tail = 0;

    if (App.GameState == NULL || unit == NULL) return FALSE;
    mapW = App.GameState->MapWidth;
    mapH = App.GameState->MapHeight;
    if (mapW <= 0 || mapH <= 0) return FALSE;

    unitType = GetUnitTypeById(unit->TypeId);
    if (unitType == NULL) return FALSE;

    startX = unit->X;
    startY = unit->Y;
    if (startX == targetX && startY == targetY) return FALSE;

    if (!EnsurePathBuffers(mapW, mapH)) return FALSE;

    ClearUnitPath(unit);

    for (size_t i = 0; i < PathCells; i++) {
        PathCameFrom[i] = -1;
    }

    startIndex = startY * mapW + startX;
    targetIndex = targetY * mapW + targetX;
    PathCameFrom[startIndex] = startIndex;
    PathQueue[tail++] = startIndex;

    while (head < tail) {
        I32 currentIndex = PathQueue[head++];
        I32 cx = currentIndex % mapW;
        I32 cy = currentIndex / mapW;

        if (currentIndex == targetIndex) {
            break;
        }

        for (I32 i = 0; i < 8; i++) {
            I32 stepX = offsets[i][0];
            I32 stepY = offsets[i][1];
            I32 nx = WrapCoord(cx, stepX, mapW);
            I32 ny = WrapCoord(cy, stepY, mapH);
            I32 nIndex = ny * mapW + nx;

            if (PathCameFrom[nIndex] >= 0) continue;
            if (!IsWalkableAt(nx, ny, unit, unitType, mapW, mapH)) continue;
            if (!IsDiagonalAllowed(cx, cy, stepX, stepY, unit, unitType, mapW, mapH)) continue;

            PathCameFrom[nIndex] = currentIndex;
            PathQueue[tail++] = nIndex;
        }
    }

    if (PathCameFrom[targetIndex] < 0) return FALSE;

    {
        I32 current = targetIndex;
        I32 pathLength = 0;

        while (current >= 0 && current != startIndex) {
            PathQueue[pathLength++] = current;
            current = PathCameFrom[current];
        }
        if (current != startIndex || pathLength <= 0) return FALSE;

        for (I32 i = pathLength - 1; i >= 0; i--) {
            I32 index = PathQueue[i];
            PATH_NODE* node = (PATH_NODE*)malloc(sizeof(PATH_NODE));
            if (node == NULL) {
                ClearUnitPath(unit);
                return FALSE;
            }
            node->Position.X = index % mapW;
            node->Position.Y = index / mapW;
            node->Next = NULL;
            if (unit->PathTail != NULL) {
                unit->PathTail->Next = node;
            } else {
                unit->PathHead = node;
            }
            unit->PathTail = node;
        }
    }

    unit->PathTargetX = targetX;
    unit->PathTargetY = targetY;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Pop the next step from a cached path.
 */
BOOL PopUnitPathNext(UNIT* unit, I32* outX, I32* outY) {
    PATH_NODE* node;

    if (unit == NULL || outX == NULL || outY == NULL) return FALSE;
    if (unit->PathHead == NULL) return FALSE;

    node = unit->PathHead;
    *outX = node->Position.X;
    *outY = node->Position.Y;
    unit->PathHead = node->Next;
    if (unit->PathHead == NULL) {
        unit->PathTail = NULL;
    }
    free(node);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Release pathfinding buffers.
 */
void FreePathfindingBuffers(void) {
    if (PathQueue != NULL) {
        free(PathQueue);
        PathQueue = NULL;
    }
    if (PathCameFrom != NULL) {
        free(PathCameFrom);
        PathCameFrom = NULL;
    }
    PathCells = 0;
}
