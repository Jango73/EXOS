
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

#include "tt-production.h"
#include "tt-entities.h"
#include "tt-log.h"

/************************************************************************/

static const PRODUCTION_OPTION YardOptions[] = {
    {BUILDING_TYPE_BARRACKS, VK_1, TRUE},
    {BUILDING_TYPE_FACTORY, VK_2, TRUE},
    {BUILDING_TYPE_POWER_PLANT, VK_3, TRUE},
    {BUILDING_TYPE_TECH_CENTER, VK_4, TRUE},
    {BUILDING_TYPE_TURRET, VK_5, TRUE},
    {BUILDING_TYPE_WALL, VK_6, TRUE}
};

/************************************************************************/

static const PRODUCTION_OPTION BarracksOptions[] = {
    {UNIT_TYPE_TROOPER, VK_1, FALSE},
    {UNIT_TYPE_SOLDIER, VK_2, FALSE},
    {UNIT_TYPE_ENGINEER, VK_3, FALSE},
    {UNIT_TYPE_SCOUT, VK_4, FALSE}
};

/************************************************************************/

static const PRODUCTION_OPTION FactoryOptions[] = {
    {UNIT_TYPE_MOBILE_ARTILLERY, VK_1, FALSE},
    {UNIT_TYPE_TANK, VK_2, FALSE},
    {UNIT_TYPE_TRANSPORT, VK_3, FALSE},
    {UNIT_TYPE_DRILLER, VK_4, FALSE}
};

/************************************************************************/

/**
 * @brief Check if a building type can produce something.
 */
BOOL IsProductionBuildingType(I32 typeId) {
    return typeId == BUILDING_TYPE_CONSTRUCTION_YARD ||
           typeId == BUILDING_TYPE_BARRACKS ||
           typeId == BUILDING_TYPE_FACTORY;
}

/************************************************************************/

/**
 * @brief Check if a building type can produce units.
 */
BOOL IsUnitProductionBuildingType(I32 typeId) {
    return typeId == BUILDING_TYPE_BARRACKS ||
           typeId == BUILDING_TYPE_FACTORY;
}

/************************************************************************/

/**
 * @brief Return the production options for a building type.
 */
const PRODUCTION_OPTION* GetProductionOptions(I32 buildingTypeId, I32* outCount) {
    if (outCount != NULL) *outCount = 0;

    switch (buildingTypeId) {
        case BUILDING_TYPE_CONSTRUCTION_YARD:
            if (outCount != NULL) *outCount = (I32)(sizeof(YardOptions) / sizeof(YardOptions[0]));
            return YardOptions;
        case BUILDING_TYPE_BARRACKS:
            if (outCount != NULL) *outCount = (I32)(sizeof(BarracksOptions) / sizeof(BarracksOptions[0]));
            return BarracksOptions;
        case BUILDING_TYPE_FACTORY:
            if (outCount != NULL) *outCount = (I32)(sizeof(FactoryOptions) / sizeof(FactoryOptions[0]));
            return FactoryOptions;
        default:
            break;
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Enqueue a unit production job.
 */
BOOL EnqueueUnitProduction(BUILDING* producer, I32 unitTypeId, I32 team, I32* outResult) {
    const UNIT_TYPE* ut;
    TEAM_RESOURCES* res;
    I32 idx;

    if (outResult != NULL) *outResult = PRODUCTION_RESULT_OK;
    if (producer == NULL || !IsValidTeam(team)) {
        if (outResult != NULL) *outResult = PRODUCTION_RESULT_INVALID;
        return FALSE;
    }
    if (!IsUnitProductionBuildingType(producer->TypeId)) {
        if (outResult != NULL) *outResult = PRODUCTION_RESULT_INVALID;
        return FALSE;
    }
    if (producer->Team != team) {
        if (outResult != NULL) *outResult = PRODUCTION_RESULT_INVALID;
        return FALSE;
    }

    ut = GetUnitTypeById(unitTypeId);
    if (ut == NULL) {
        if (outResult != NULL) *outResult = PRODUCTION_RESULT_INVALID;
        return FALSE;
    }

    if (producer->UnitQueueCount >= MAX_UNIT_QUEUE) {
        if (outResult != NULL) *outResult = PRODUCTION_RESULT_QUEUE_FULL;
        return FALSE;
    }
    if (!HasTechLevel(ut->TechLevel, team)) {
        if (outResult != NULL) *outResult = PRODUCTION_RESULT_TECH_LEVEL;
        return FALSE;
    }

    res = GetTeamResources(team);
    if (res == NULL) {
        if (outResult != NULL) *outResult = PRODUCTION_RESULT_INVALID;
        return FALSE;
    }
    if (res->Plasma < ut->CostPlasma) {
        if (outResult != NULL) *outResult = PRODUCTION_RESULT_RESOURCES;
        return FALSE;
    }

    idx = producer->UnitQueueCount;
    res->Plasma -= ut->CostPlasma;
    producer->UnitQueue[idx].TypeId = unitTypeId;
    producer->UnitQueue[idx].TimeRemaining = (U32)ut->BuildTime;
    producer->UnitQueueCount++;
    GAME_LOGF(team, "UnitQueueAdd Producer=%x Type=%s Count=%u",
              (U32)producer->Id, ut->Name, (U32)producer->UnitQueueCount);
    return TRUE;
}
