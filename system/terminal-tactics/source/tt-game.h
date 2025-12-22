
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

#ifndef TT_GAME_H
#define TT_GAME_H

#include "tt-types.h"

BOOL InitializeGame(I32 mapWidth, I32 mapHeight, I32 difficulty, I32 teamCount);
void CleanupGame(void);
void UpdateGame(void);
void RunGameLoop(void);
I32 GetMaxUnitsForMap(I32 mapW, I32 mapH);
U32 CountUnitsAllTeams(void);
U32 CountUnitsForTeam(I32 team);
U32 CountBuildingsForTeam(I32 team);
I32 CalculateTeamScore(I32 team);
void GetEnergyTotals(I32 team, I32* outProduction, I32* outConsumption);
BOOL IsBuildingPowered(const BUILDING* building);
void RecalculateEnergy(void);
void CancelBuildingPlacement(void);
BOOL ConfirmBuildingPlacement(void);
BOOL EnqueuePlacement(I32 typeId);
BOOL StartPlacementFromQueue(void);
BOOL CancelSelectedBuildingProduction(void);
void MovePlacement(I32 dx, I32 dy);
BOOL PickExplorationTarget(I32 team, I32* outX, I32* outY);
BOOL FindNearestPlasmaCell(I32 startX, I32 startY, I32* outX, I32* outY);
BOOL FindNearestSafePlasmaCell(I32 team, I32 startX, I32 startY, I32 minEnemyDistance, I32* outX, I32* outY);

#endif /* TT_GAME_H */
