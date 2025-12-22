
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

#ifndef TT_FOG_H
#define TT_FOG_H

#include "tt-types.h"

BOOL EnsureTeamMemoryBuffers(I32 mapW, I32 mapH, I32 teamCount);
void FreeTeamMemoryBuffers(void);
void UpdateFogOfWar(U32 currentTime);
BOOL IsCellVisibleToTeam(I32 x, I32 y, I32 team);
BOOL IsAreaVisibleToTeam(I32 x, I32 y, I32 width, I32 height, I32 team);
BOOL IsAreaExploredToTeam(I32 x, I32 y, I32 width, I32 height, I32 team);
BOOL IsAreaExploredToTeamWithMargin(I32 x, I32 y, I32 width, I32 height, I32 team, I32 margin);

#endif /* TT_FOG_H */
