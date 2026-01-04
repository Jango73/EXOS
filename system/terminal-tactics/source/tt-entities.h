
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

#ifndef TT_ENTITIES_H
#define TT_ENTITIES_H

#include "tt-types.h"
#include "tt-map.h"

BUILDING** GetTeamBuildingHead(I32 team);
UNIT** GetTeamUnitHead(I32 team);
TEAM_RESOURCES* GetTeamResources(I32 team);
BOOL TeamHasConstructionYard(I32 team);
BOOL IsTeamEliminated(I32 team);
void LogTeamAction(I32 team, const char* action, U32 id, U32 x, U32 y, const char* name, const char* extra);
void LogTeamActionCounts(I32 team, const char* action, U32 a, U32 b, U32 c, U32 d);
BUILDING* FindTeamBuilding(I32 team, I32 typeId);
UNIT* FindTeamUnit(I32 team, I32 typeId);
BUILDING* FindBuildingById(I32 team, I32 buildingId);
UNIT* FindUnitById(I32 team, I32 unitId);
UNIT* FindUnitAtCell(I32 x, I32 y, I32 teamFilter);
BUILDING* CreateBuilding(I32 typeId, I32 team, I32 x, I32 y);
UNIT* CreateUnit(I32 typeId, I32 team, I32 x, I32 y);
void SetUnitMoveTarget(UNIT* unit, I32 targetX, I32 targetY);
void SetUnitStateIdle(UNIT* unit);
void SetUnitStateEscort(UNIT* unit, I32 targetTeam, I32 targetUnitId);
void SetUnitStateExplore(UNIT* unit, I32 targetX, I32 targetY);
void SetBuildingOccupancy(const BUILDING* building, BOOL occupied);
void SetUnitOccupancy(const UNIT* unit, BOOL occupied);
void RemoveUnitFromTeamList(I32 team, UNIT* target);
void RemoveBuildingFromTeamList(I32 team, BUILDING* target);
void RemoveTeamEntities(I32 team);

#endif /* TT_ENTITIES_H */
