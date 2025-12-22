
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

#ifndef TT_PRODUCTION_H
#define TT_PRODUCTION_H

#include "tt-types.h"

/************************************************************************/

#define PRODUCTION_RESULT_OK            0
#define PRODUCTION_RESULT_INVALID       1
#define PRODUCTION_RESULT_QUEUE_FULL    2
#define PRODUCTION_RESULT_TECH_LEVEL    3
#define PRODUCTION_RESULT_RESOURCES     4

/************************************************************************/

typedef struct {
    I32 TypeId;
    I32 KeyVK;
    BOOL IsBuilding;
} PRODUCTION_OPTION;

/************************************************************************/

/**
 * @brief Check if a building type can produce something.
 */
BOOL IsProductionBuildingType(I32 typeId);

/************************************************************************/

/**
 * @brief Check if a building type can produce units.
 */
BOOL IsUnitProductionBuildingType(I32 typeId);

/************************************************************************/

/**
 * @brief Return the production options for a building type.
 */
const PRODUCTION_OPTION* GetProductionOptions(I32 buildingTypeId, I32* outCount);

/************************************************************************/

/**
 * @brief Enqueue a unit production job.
 */
BOOL EnqueueUnitProduction(BUILDING* producer, I32 unitTypeId, I32 team, I32* outResult);

/************************************************************************/

#endif /* TT_PRODUCTION_H */
