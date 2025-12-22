
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

#ifndef TT_PATH_H
#define TT_PATH_H

#include "tt-types.h"

/************************************************************************/

/**
 * @brief Build and cache a BFS path for a unit.
 */
BOOL BuildUnitPathBFS(UNIT* unit, I32 targetX, I32 targetY);

/************************************************************************/

/**
 * @brief Pop the next step from a cached path.
 */
BOOL PopUnitPathNext(UNIT* unit, I32* outX, I32* outY);

/************************************************************************/

/**
 * @brief Clear cached path nodes for a unit.
 */
void ClearUnitPath(UNIT* unit);

/************************************************************************/

/**
 * @brief Release pathfinding buffers.
 */
void FreePathfindingBuffers(void);

/************************************************************************/

#endif /* TT_PATH_H */
