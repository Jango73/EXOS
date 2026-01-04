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

#ifndef TT_LOG_H
#define TT_LOG_H

#include "tt-types.h"

void GameLogInit(void);
void GameLogShutdown(void);
void GameLogWrite(const char* origin, I32 team, const char* message);
const char* GetGameLogFileName(void);

#define GAME_LOG(team, message) GameLogWrite(__func__, (team), (message))
#define GAME_LOGF(team, format, ...)                          \
    do {                                                      \
        char _msg[128];                                       \
        snprintf(_msg, sizeof(_msg), (format), ##__VA_ARGS__);\
        GameLogWrite(__func__, (team), _msg);                 \
    } while (0)

#endif /* TT_LOG_H */
