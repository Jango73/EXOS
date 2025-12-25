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

#include "tt-log.h"
#include "tt-save.h"

/************************************************************************/

static FILE* GameLogFile = NULL;

/************************************************************************/

const char* GetGameLogFileName(void) {
    return "terminal-tactics.log";
}

/************************************************************************/

void GameLogInit(void) {
    if (GameLogFile != NULL) {
        fclose(GameLogFile);
        GameLogFile = NULL;
    }

    char path[MAX_PATH_NAME];
    if (ResolveAppFilePath(GetGameLogFileName(), path, sizeof(path))) {
        GameLogFile = fopen(path, "a");
    }
    if (App.GameState != NULL) {
        for (I32 i = 0; i < MAX_TEAMS; i++) {
            App.GameState->TeamDefeatedLogged[i] = FALSE;
        }
    }
}

/************************************************************************/

void GameLogShutdown(void) {
    if (GameLogFile != NULL) {
        fclose(GameLogFile);
        GameLogFile = NULL;
    }
}

/************************************************************************/

void GameLogWrite(const char* origin, I32 team, const char* message) {
    const char* name = (origin != NULL && origin[0] != '\0') ? origin : "Unknown";
    const char* text = (message != NULL) ? message : "";
    U32 timeMs = (App.GameState != NULL) ? App.GameState->GameTime : 0;

    if (GameLogFile == NULL) return;
    fprintf(GameLogFile, "[%s] Time=%u Team=%x %s\n", name, timeMs, (U32)team, text);
    fflush(GameLogFile);
}
