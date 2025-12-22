
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

#ifndef TT_COMMANDS_H
#define TT_COMMANDS_H

#include "tt-types.h"

void CancelUnitCommand(void);
void MoveCommandCursor(I32 dx, I32 dy);
void ConfirmUnitCommand(void);
void StartUnitCommand(I32 commandType);
void MoveViewport(I32 deltaX, I32 deltaY);
void CenterViewportOn(I32 x, I32 y);

#endif /* TT_COMMANDS_H */
