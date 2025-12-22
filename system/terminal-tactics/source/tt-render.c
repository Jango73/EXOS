
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

#include "tt-render.h"
#include "tt-map.h"
#include "tt-entities.h"
#include "tt-fog.h"
#include "tt-commands.h"
#include "tt-ai.h"
#include "tt-save.h"
#include "tt-manual.h"
#include "tt-production.h"
#include "tt-game.h"

/************************************************************************/

static const char* GetMindsetName(I32 mindset) {
    switch (mindset) {
        case AI_MINDSET_IDLE:     return "Idle";
        case AI_MINDSET_URGENCY:  return "Urgency";
        case AI_MINDSET_PANIC:    return "Panic";
        default:                  return "Unknown";
    }
}

/************************************************************************/

static const char* GetAttitudeName(I32 attitude) {
    switch (attitude) {
        case AI_ATTITUDE_AGGRESSIVE: return "Aggressive";
        case AI_ATTITUDE_DEFENSIVE:  return "Defensive";
        default:                     return "Unknown";
    }
}

/************************************************************************/

#define MENU_TOKEN_MAX 64

/************************************************************************/

static void AddMenuToken(const char* token, const char** tokens, I32* tokenCount, I32 maxTokens) {
    if (tokens == NULL || tokenCount == NULL) return;
    if (*tokenCount >= maxTokens) return;
    if (token == NULL || token[0] == '\0') return;
    tokens[*tokenCount] = token;
    (*tokenCount)++;
}

/************************************************************************/

static void AddMenuTokenBuffer(char tokenStorage[][UI_TOKEN_SIZE], I32* storageIndex, I32 storageMax,
                               const char** tokens, I32* tokenCount, I32 maxTokens, const char* value) {
    if (tokenStorage == NULL || storageIndex == NULL) return;
    if (*storageIndex >= storageMax) return;
    if (tokens == NULL || tokenCount == NULL) return;
    if (*tokenCount >= maxTokens) return;
    if (value == NULL || value[0] == '\0') return;

    snprintf(tokenStorage[*storageIndex], UI_TOKEN_SIZE, "%s", value);
    tokens[*tokenCount] = tokenStorage[*storageIndex];
    (*tokenCount)++;
    (*storageIndex)++;
}

/************************************************************************/

static void BuildBottomMenuLinesFromTokens(const char* const* tokens, I32 tokenCount,
                                           char* line0, size_t size0,
                                           char* line1, size_t size1,
                                           char* line2, size_t size2) {
    char* lines[3] = {line0, line1, line2};
    size_t sizes[3] = {size0, size1, size2};
    size_t remaining[3];
    I32 line = 0;

    for (I32 i = 0; i < 3; i++) {
        if (lines[i] != NULL && sizes[i] > 0) {
            lines[i][0] = '\0';
            remaining[i] = sizes[i] - 1;
        } else {
            remaining[i] = 0;
        }
    }

    if (tokens == NULL || tokenCount <= 0) return;

    for (I32 i = 0; i < tokenCount && line < 3; i++) {
        const char* token = tokens[i];
        if (token == NULL || token[0] == '\0') continue;
        const char* cursor = token;
        while (*cursor != '\0' && line < 3) {
            size_t sepLen = (lines[line] != NULL && lines[line][0] != '\0') ? 2 : 0;
            if (remaining[line] <= sepLen) {
                line++;
                continue;
            }

            size_t tokenLen = strlen(cursor);
            size_t space = remaining[line] - sepLen;
            if (tokenLen > space) {
                if (sepLen > 0) {
                    line++;
                    continue;
                }
                tokenLen = space;
            }

            if (lines[line] != NULL && sizes[line] > 0) {
                size_t offset = strlen(lines[line]);
                if (sepLen > 0) {
                    memcpy(&lines[line][offset], "  ", sepLen);
                    offset += sepLen;
                }
                memcpy(&lines[line][offset], cursor, tokenLen);
                offset += tokenLen;
                lines[line][offset] = '\0';
                remaining[line] = sizes[line] - 1 - offset;
            }

            cursor += tokenLen;
            if (*cursor != '\0') {
                line++;
            }
        }
    }
}

/************************************************************************/
static U8 GetUnitHighlightAttr(const UNIT* unit, U32 now) {
    U8 fore;
    U8 back = CONSOLE_BLACK;

    if (unit == NULL || App.GameState == NULL) return MakeAttr(CONSOLE_WHITE, CONSOLE_BLACK);

    fore = TeamColors[(U32)unit->Team % MAX_TEAMS];

    if (unit->LastDamageTime != 0 && now >= unit->LastDamageTime && now - unit->LastDamageTime < UI_COMBAT_FLASH_MS) {
        back = CONSOLE_RED;
    } else if (unit->LastAttackTime != 0 && now >= unit->LastAttackTime && now - unit->LastAttackTime < UI_COMBAT_FLASH_MS) {
        back = CONSOLE_BROWN;
    }

    if ((back == CONSOLE_RED || back == CONSOLE_BROWN) && fore == back) {
        fore = CONSOLE_WHITE;
    }

    return MakeAttr(fore, back);
}

/************************************************************************/

void ResetRenderCache(void) {
    memset(App.Render.PrevViewBuffer, 0, sizeof(App.Render.PrevViewBuffer));
    memset(App.Render.ViewColors, 0, sizeof(App.Render.ViewColors));
    memset(App.Render.PrevViewColors, 0, sizeof(App.Render.PrevViewColors));
    memset(App.Render.PrevTopLine0, 0, sizeof(App.Render.PrevTopLine0));
    memset(App.Render.PrevTopLine1, 0, sizeof(App.Render.PrevTopLine1));
    memset(App.Render.PrevBottom, 0, sizeof(App.Render.PrevBottom));
    memset(App.Render.StatusLine, 0, sizeof(App.Render.StatusLine));
    memset(App.Render.PrevStatusLine, 0, sizeof(App.Render.PrevStatusLine));
    App.Render.BorderDrawn = FALSE;
    App.Render.MainMenuDrawn = FALSE;
    App.Render.CachedNGWidth = -1;
    App.Render.CachedNGHeight = -1;
    App.Render.CachedNGDifficulty = -1;
    App.Render.CachedNGTeams = -1;
    App.Render.CachedNGSelection = -1;
    App.Render.CachedLoadSelected = -1;
    App.Render.CachedLoadCount = -1;
    App.Render.CachedSaveName[0] = '\0';
    App.Render.DebugDrawn = FALSE;
    memset(App.Render.ScreenBuffer, 0, sizeof(App.Render.ScreenBuffer));
    memset(App.Render.PrevScreenBuffer, 0, sizeof(App.Render.PrevScreenBuffer));
    memset(App.Render.ScreenAttr, 0, sizeof(App.Render.ScreenAttr));
}

/************************************************************************/

void GotoCursor(I32 x, I32 y) {
    POINT pos;
    pos.X = x;
    pos.Y = y;
    ConsoleGotoXY(&pos);
}

/************************************************************************/

void DrawBox(I32 x, I32 y, I32 width, I32 height) {
    I32 i, j;

    GotoCursor(x, y);
    printf("+");
    GotoCursor(x + width - 1, y);
    printf("+");
    GotoCursor(x, y + height - 1);
    printf("+");
    GotoCursor(x + width - 1, y + height - 1);
    printf("+");

    for (i = 1; i < width - 1; i++) {
        GotoCursor(x + i, y);
        printf("-");
        GotoCursor(x + i, y + height - 1);
        printf("-");
    }

    for (j = 1; j < height - 1; j++) {
        GotoCursor(x, y + j);
        printf("|");
        GotoCursor(x + width - 1, y + j);
        printf("|");
    }
}

/************************************************************************/

void PrintCentered(I32 y, const char* text) {
    I32 x = (SCREEN_WIDTH - (I32)strlen(text)) / 2;
    GotoCursor(x < 0 ? 0 : x, y);
    printf("%s", text);
}

/************************************************************************/

void SetStatus(const char* status) {
    if (status == NULL || (status[0] == ' ' && status[1] == '\0') || status[0] == '\0') {
        App.Render.StatusLine[0] = '\0';
        App.Render.StatusStartTime = 0;
        return;
    }
    snprintf(App.Render.StatusLine, sizeof(App.Render.StatusLine), "%-*s", SCREEN_WIDTH, status);
    App.Render.StatusLine[SCREEN_WIDTH] = '\0';
    App.Render.StatusStartTime = (App.GameState != NULL) ? App.GameState->GameTime : 0;
}

/************************************************************************/

static void HighlightArea(I32 screenX, I32 screenY, I32 width, I32 height, U8 attr) {
    for (I32 dy = 0; dy < height; dy++) {
        for (I32 dx = 0; dx < width; dx++) {
            I32 sx = screenX + dx;
            I32 sy = screenY + dy;
            if (sx < 0 || sx >= VIEWPORT_WIDTH || sy < 0 || sy >= VIEWPORT_HEIGHT) continue;
            App.Render.ViewColors[sy][sx] = attr;
        }
    }
}

/************************************************************************/

static U8 GetTerrainColor(U8 terrainType) {
    switch (terrainType & TERRAIN_TYPE_MASK) {
        case TERRAIN_TYPE_FOREST:
            return MakeAttr(CONSOLE_GREEN, CONSOLE_BLACK);
        case TERRAIN_TYPE_PLASMA:
            return MakeAttr(CONSOLE_SALMON, CONSOLE_BLACK);
        case TERRAIN_TYPE_MOUNTAIN:
            return MakeAttr(CONSOLE_BROWN, CONSOLE_BLACK);
        case TERRAIN_TYPE_WATER:
            return MakeAttr(CONSOLE_BLUE, CONSOLE_BLACK);
        case TERRAIN_TYPE_PLAINS:
            return MakeAttr(CONSOLE_BLACK, CONSOLE_BLACK);
        default:
            return MakeAttr(CONSOLE_GRAY, CONSOLE_BLACK);
    }
}

/************************************************************************/

static char GetIconChar(const char* icon, I32 row, I32 col) {
    const char* p = icon;
    I32 currentRow = 0;
    I32 currentCol = 0;

    if (p == NULL) return ' ';

    while (*p != '\0') {
        if (*p == '\n') {
            currentRow++;
            currentCol = 0;
            p++;
            if (currentRow > row) break;
            continue;
        }

        if (currentRow == row && currentCol == col) {
            return *p;
        }

        currentCol++;
        p++;
    }

    return ' ';
}

/************************************************************************/

static void RenderIconToBuffer(I32 screenX, I32 screenY, const char* icon, I32 width, I32 height, U8 attr) {
    for (I32 dy = 0; dy < height; dy++) {
        for (I32 dx = 0; dx < width; dx++) {
            I32 drawX = screenX + dx;
            I32 drawY = screenY + dy;
            if (drawX < 0 || drawX >= VIEWPORT_WIDTH || drawY < 0 || drawY >= VIEWPORT_HEIGHT) continue;
            App.Render.ViewBuffer[drawY][drawX] = GetIconChar(icon, dy, dx);
            App.Render.ViewColors[drawY][drawX] = attr;
        }
    }
}

/************************************************************************/

static void RenderBuildingSprite(const BUILDING* building, const BUILDING_TYPE* buildingType, I32 screenX, I32 screenY) {
    U8 back = CONSOLE_BLACK;
    if (building->LastDamageTime != 0 && App.GameState != NULL) {
        U32 now = GetSystemTime();
        if (now >= building->LastDamageTime && now - building->LastDamageTime < UI_COMBAT_FLASH_MS) {
            back = CONSOLE_RED;
        }
    }
    U8 fore = TeamColors[(U32)building->Team % MAX_TEAMS];
    if ((back == CONSOLE_RED || back == CONSOLE_BROWN) && fore == back) {
        fore = CONSOLE_WHITE;
    }
    U8 attr = MakeAttr(fore, back);

    RenderIconToBuffer(screenX, screenY, buildingType->Icon, buildingType->Width, buildingType->Height, attr);
    if (App.GameState != NULL && App.GameState->SelectedBuilding == building) {
        HighlightArea(screenX, screenY, buildingType->Width, buildingType->Height, MakeAttr(CONSOLE_WHITE, back));
    }

    if (building->Hp < buildingType->MaxHp) {
        I32 overlayX = screenX;
        I32 overlayY = screenY;
        if (overlayX >= 0 && overlayX < VIEWPORT_WIDTH && overlayY >= 0 && overlayY < VIEWPORT_HEIGHT) {
            I32 hp = building->Hp;
            if (hp > UI_HP_MAX_DISPLAY) hp = UI_HP_MAX_DISPLAY;
            char buffer[UI_HP_BUFFER_SIZE];
            I32 length;
            if (hp >= UI_HP_3_DIGITS_MIN) {
                buffer[0] = (char)('0' + (hp / UI_DECIMAL_BASE_SQUARED) % UI_DECIMAL_BASE);
                buffer[1] = (char)('0' + (hp / UI_DECIMAL_BASE) % UI_DECIMAL_BASE);
                buffer[2] = (char)('0' + (hp % UI_DECIMAL_BASE));
                length = 3;
            } else if (hp >= UI_HP_2_DIGITS_MIN) {
                buffer[0] = (char)('0' + (hp / UI_DECIMAL_BASE) % UI_DECIMAL_BASE);
                buffer[1] = (char)('0' + (hp % UI_DECIMAL_BASE));
                length = 2;
            } else {
                buffer[0] = (char)('0' + hp);
                length = 1;
            }
            for (I32 i = 0; i < length && overlayX + i < VIEWPORT_WIDTH; i++) {
                if (overlayX + i < screenX + buildingType->Width) {
                    App.Render.ViewBuffer[overlayY][overlayX + i] = buffer[i];
                    App.Render.ViewColors[overlayY][overlayX + i] = MakeAttr(CONSOLE_WHITE, CONSOLE_BLACK);
                }
            }
        }
    }

    if (building->TypeId == BUILDING_TYPE_CONSTRUCTION_YARD &&
        building->Team == HUMAN_TEAM_INDEX &&
        App.GameState != NULL && building->BuildQueueCount > 0) {
        I32 indicatorX = screenX + buildingType->Width - 1;
        I32 indicatorY = screenY + buildingType->Height - 1;
        I32 seconds = -1;
        for (I32 i = 0; i < building->BuildQueueCount; i++) {
            if (building->BuildQueue[i].TimeRemaining > 0) {
                seconds = (I32)((building->BuildQueue[i].TimeRemaining + UI_BUILD_TIME_ROUND_MS) / UI_MS_PER_SECOND);
                break;
            }
        }
        if (seconds >= 0 && indicatorX >= 0 && indicatorX < VIEWPORT_WIDTH && indicatorY >= 0 && indicatorY < VIEWPORT_HEIGHT) {
            if (seconds > UI_BUILD_TIME_MAX_SECONDS) seconds = UI_BUILD_TIME_MAX_SECONDS;
            if (seconds >= UI_TWO_DIGIT_MIN && indicatorX - 1 >= 0) {
                App.Render.ViewBuffer[indicatorY][indicatorX - 1] = (char)('0' + (seconds / UI_DECIMAL_BASE) % UI_DECIMAL_BASE);
                App.Render.ViewBuffer[indicatorY][indicatorX] = (char)('0' + (seconds % UI_DECIMAL_BASE));
                App.Render.ViewColors[indicatorY][indicatorX - 1] = MakeAttr(CONSOLE_YELLOW, CONSOLE_BLACK);
                App.Render.ViewColors[indicatorY][indicatorX] = MakeAttr(CONSOLE_YELLOW, CONSOLE_BLACK);
            } else {
                App.Render.ViewBuffer[indicatorY][indicatorX] = (char)('0' + seconds);
                App.Render.ViewColors[indicatorY][indicatorX] = MakeAttr(CONSOLE_YELLOW, CONSOLE_BLACK);
            }
        }
    } else if (building->UnderConstruction && building->BuildTimeRemaining > 0) {
        I32 indicatorX = screenX + buildingType->Width - 1;
        I32 indicatorY = screenY + buildingType->Height - 1;
        if (indicatorX >= 0 && indicatorX < VIEWPORT_WIDTH && indicatorY >= 0 && indicatorY < VIEWPORT_HEIGHT) {
            I32 seconds = (I32)(building->BuildTimeRemaining / UI_MS_PER_SECOND);
            if (seconds > UI_BUILD_TIME_MAX_SECONDS) seconds = UI_BUILD_TIME_MAX_SECONDS;
            if (seconds >= UI_TWO_DIGIT_MIN && indicatorX - 1 >= 0) {
                App.Render.ViewBuffer[indicatorY][indicatorX - 1] = (char)('0' + (seconds / UI_DECIMAL_BASE) % UI_DECIMAL_BASE);
                App.Render.ViewBuffer[indicatorY][indicatorX] = (char)('0' + (seconds % UI_DECIMAL_BASE));
                App.Render.ViewColors[indicatorY][indicatorX - 1] = MakeAttr(CONSOLE_YELLOW, CONSOLE_BLACK);
                App.Render.ViewColors[indicatorY][indicatorX] = MakeAttr(CONSOLE_YELLOW, CONSOLE_BLACK);
            } else {
                App.Render.ViewBuffer[indicatorY][indicatorX] = (char)('0' + seconds);
                App.Render.ViewColors[indicatorY][indicatorX] = MakeAttr(CONSOLE_YELLOW, CONSOLE_BLACK);
            }
        }
    }

    if (!building->UnderConstruction && !IsBuildingPowered(building)) {
        I32 indicatorX = screenX;
        I32 indicatorY = screenY + buildingType->Height - 1;
        if (indicatorX >= 0 && indicatorX < VIEWPORT_WIDTH && indicatorY >= 0 && indicatorY < VIEWPORT_HEIGHT) {
            App.Render.ViewBuffer[indicatorY][indicatorX] = '!';
            App.Render.ViewColors[indicatorY][indicatorX] = MakeAttr(CONSOLE_RED, CONSOLE_BLACK);
        }
    }
}

/************************************************************************/

static void RenderUnitSprite(const UNIT* unit, const UNIT_TYPE* unitType, I32 screenX, I32 screenY, U32 now) {
    U8 attr = GetUnitHighlightAttr(unit, now);
    U8 back = (U8)((attr >> 4) & 0x0F);

    RenderIconToBuffer(screenX, screenY, unitType->Icon, unitType->Width, unitType->Height, attr);
    if (unit->IsSelected) {
        HighlightArea(screenX, screenY, unitType->Width, unitType->Height, MakeAttr(CONSOLE_WHITE, back));
    }

    if (unit->Hp < unitType->MaxHp) {
        I32 overlayX = screenX;
        I32 overlayY = screenY;
        if (overlayX >= 0 && overlayX < VIEWPORT_WIDTH && overlayY >= 0 && overlayY < VIEWPORT_HEIGHT) {
            I32 hp = unit->Hp;
            if (hp > UI_HP_MAX_DISPLAY) hp = UI_HP_MAX_DISPLAY;
            char buffer[UI_HP_BUFFER_SIZE];
            I32 length;
            if (hp >= UI_HP_3_DIGITS_MIN) {
                buffer[0] = (char)('0' + (hp / UI_DECIMAL_BASE_SQUARED) % UI_DECIMAL_BASE);
                buffer[1] = (char)('0' + (hp / UI_DECIMAL_BASE) % UI_DECIMAL_BASE);
                buffer[2] = (char)('0' + (hp % UI_DECIMAL_BASE));
                length = 3;
            } else if (hp >= UI_HP_2_DIGITS_MIN) {
                buffer[0] = (char)('0' + (hp / UI_DECIMAL_BASE) % UI_DECIMAL_BASE);
                buffer[1] = (char)('0' + (hp % UI_DECIMAL_BASE));
                length = 2;
            } else {
                buffer[0] = (char)('0' + hp);
                length = 1;
            }
            for (I32 i = 0; i < length && overlayX + i < VIEWPORT_WIDTH; i++) {
                if (overlayX + i < screenX + unitType->Width) {
                    App.Render.ViewBuffer[overlayY][overlayX + i] = buffer[i];
                    App.Render.ViewColors[overlayY][overlayX + i] = MakeAttr(CONSOLE_WHITE, CONSOLE_BLACK);
                }
            }
        }
    }
}

/************************************************************************/

static void ClearFrameBuffers(void) {
    U8 defaultAttr = MakeAttr(CONSOLE_WHITE, CONSOLE_BLACK);

    for (I32 y = 0; y < SCREEN_HEIGHT; y++) {
        memset(App.Render.ScreenBuffer[y], ' ', SCREEN_WIDTH);
        App.Render.ScreenBuffer[y][SCREEN_WIDTH] = '\0';
        for (I32 x = 0; x < SCREEN_WIDTH; x++) {
            App.Render.ScreenAttr[y][x] = defaultAttr;
        }
    }
}

/************************************************************************/

static void WriteLineToFrame(I32 y, I32 xStart, I32 maxWidth, const char* text, U8 attr) {
    size_t len;

    if (text == NULL) return;
    if (y < 0 || y >= SCREEN_HEIGHT) return;
    if (xStart < 0 || xStart >= SCREEN_WIDTH) return;
    if (maxWidth <= 0) return;

    len = strlen(text);
    if (len > (size_t)maxWidth) len = (size_t)maxWidth;
    if ((I32)len > (SCREEN_WIDTH - xStart)) len = (size_t)(SCREEN_WIDTH - xStart);

    if (len > 0) {
        memcpy(&App.Render.ScreenBuffer[y][xStart], text, len);
        for (size_t i = 0; i < len; i++) {
            App.Render.ScreenAttr[y][xStart + (I32)i] = attr;
        }
    }
}

/************************************************************************/

static void WriteCenteredToFrame(I32 y, const char* text, U8 attr) {
    size_t len;
    I32 x;

    if (text == NULL) return;
    len = strlen(text);
    if (len > (size_t)SCREEN_WIDTH) len = (size_t)SCREEN_WIDTH;
    x = (SCREEN_WIDTH - (I32)len) / 2;
    if (x < 0) x = 0;
    WriteLineToFrame(y, x, (I32)len, text, attr);
}

/************************************************************************/

static void BlitFrameBuffer(void) {
    CONSOLEBLITBUFFER Frame;

    Frame.Text = (LPCSTR)App.Render.ScreenBuffer[0];
    Frame.Width = SCREEN_WIDTH;
    Frame.Height = SCREEN_HEIGHT;
    Frame.X = 0;
    Frame.Y = 0;
    Frame.Attr = (const U8*)App.Render.ScreenAttr[0];
    Frame.TextPitch = SCREEN_WIDTH + 1;
    Frame.AttrPitch = SCREEN_WIDTH;
    ConsoleBlitBuffer(&Frame);
}

/************************************************************************/

void RenderTopBar(void) {
    char line0[SCREEN_WIDTH + 1];
    if (App.GameState == NULL) return;

    TEAM_RESOURCES* res = GetTeamResources(HUMAN_TEAM_INDEX);
    if (res == NULL) return;

    snprintf(line0, sizeof(line0),
             "Plasma: %6d | Energy: %3d/%3d | Units: %3u/%3d | Day: %3d",
             res->Plasma,
             res->Energy,
             res->MaxEnergy,
             CountUnitsForTeam(HUMAN_TEAM_INDEX),
             GetMaxUnitsForMap(App.GameState->MapWidth, App.GameState->MapHeight),
             App.GameState->GameTime / GAME_TIME_MS_PER_DAY);

    if (App.GameState->ShowCoordinates) {
        char keyInfo[UI_KEYINFO_SIZE];
        snprintf(keyInfo, sizeof(keyInfo), " | VK:%02x AS:%02x", (U32)(App.Input.LastKeyVK & 0xFF), (U32)(App.Input.LastKeyASCII & 0xFF));
        strncat(line0, keyInfo, sizeof(line0) - strlen(line0) - 1);
    }

    char buf0[SCREEN_WIDTH + 1];
    size_t len0 = strlen(line0);
    if (len0 > (size_t)SCREEN_WIDTH) len0 = SCREEN_WIDTH;
    memset(buf0, ' ', SCREEN_WIDTH);
    memcpy(buf0, line0, len0);
    buf0[SCREEN_WIDTH] = '\0';

    WriteLineToFrame(0, 0, SCREEN_WIDTH, buf0, MakeAttr(CONSOLE_GRAY, CONSOLE_BLACK));
}

/************************************************************************/

void RenderMapArea(void) {
    I32 viewX, viewY;
    I32 mapX, mapY;

    if (App.GameState == NULL) return;
    I32 teamCount = GetTeamCountSafe();
    U32 currentTimeMs = GetSystemTime();

    for (viewY = 0; viewY < VIEWPORT_HEIGHT; viewY++) {
        for (viewX = 0; viewX < VIEWPORT_WIDTH; viewX++) {
            mapX = App.GameState->ViewportPos.X + viewX;
            mapY = App.GameState->ViewportPos.Y + viewY;

            if (mapX >= App.GameState->MapWidth) mapX -= App.GameState->MapWidth;
            else if (mapX < 0) mapX += App.GameState->MapWidth;
            if (mapY >= App.GameState->MapHeight) mapY -= App.GameState->MapHeight;
            else if (mapY < 0) mapY += App.GameState->MapHeight;

            U8 terrainType = TerrainGetType(&App.GameState->Terrain[mapY][mapX]);
            if (TerrainIsVisible(&App.GameState->Terrain[mapY][mapX])) {
                App.Render.ViewBuffer[viewY][viewX] = TerrainTypeToChar(terrainType);
            } else {
                App.Render.ViewBuffer[viewY][viewX] = ' ';
            }
            App.Render.ViewColors[viewY][viewX] = GetTerrainColor(terrainType);
        }
        App.Render.ViewBuffer[viewY][VIEWPORT_WIDTH] = '\0';
    }

    for (I32 team = 0; team < teamCount; team++) {
        BUILDING* building = App.GameState->TeamData[team].Buildings;
        while (building != NULL) {
            const BUILDING_TYPE* buildingType = GetBuildingTypeById(building->TypeId);
            if (buildingType == NULL) {
                building = building->Next;
                continue;
            }

            I32 screenX;
            I32 screenY;
            if (GetScreenPosition(building->X, building->Y, buildingType->Width, buildingType->Height, &screenX, &screenY)) {
                if (IsAreaVisible(building->X, building->Y, buildingType->Width, buildingType->Height)) {
                    RenderBuildingSprite(building, buildingType, screenX, screenY);
                } else {
                    for (I32 dy = 0; dy < buildingType->Height; dy++) {
                        for (I32 dx = 0; dx < buildingType->Width; dx++) {
                            I32 px = WrapCoord(building->X, dx, App.GameState->MapWidth);
                            I32 py = WrapCoord(building->Y, dy, App.GameState->MapHeight);
                            size_t idx = (size_t)py * (size_t)App.GameState->MapWidth + (size_t)px;
                            MEMORY_CELL cell = App.GameState->TeamData[HUMAN_TEAM_INDEX].MemoryMap[idx];
                            if (cell.IsBuilding && cell.OccupiedType == (U8)building->TypeId) {
                                App.Render.ViewBuffer[screenY + dy][screenX + dx] = buildingType->Symbol;
                                App.Render.ViewColors[screenY + dy][screenX + dx] = MakeAttr(CONSOLE_GRAY, CONSOLE_BLACK);
                            }
                        }
                    }
                }
            }
            building = building->Next;
        }
    }

    for (I32 team = 0; team < teamCount; team++) {
        UNIT* unit = App.GameState->TeamData[team].Units;
        while (unit != NULL) {
            const UNIT_TYPE* unitType = GetUnitTypeById(unit->TypeId);
            if (unitType == NULL) {
                unit = unit->Next;
                continue;
            }

            I32 screenX;
            I32 screenY;
            if (GetScreenPosition(unit->X, unit->Y, unitType->Width, unitType->Height, &screenX, &screenY)) {
                if (IsAreaVisible(unit->X, unit->Y, unitType->Width, unitType->Height)) {
                    RenderUnitSprite(unit, unitType, screenX, screenY, currentTimeMs);
                } else {
                    for (I32 dy = 0; dy < unitType->Height; dy++) {
                        for (I32 dx = 0; dx < unitType->Width; dx++) {
                            I32 px = WrapCoord(unit->X, dx, App.GameState->MapWidth);
                            I32 py = WrapCoord(unit->Y, dy, App.GameState->MapHeight);
                            size_t idx = (size_t)py * (size_t)App.GameState->MapWidth + (size_t)px;
                            MEMORY_CELL cell = App.GameState->TeamData[HUMAN_TEAM_INDEX].MemoryMap[idx];
                            if (!cell.IsBuilding && cell.OccupiedType == (U8)unit->TypeId) {
                                App.Render.ViewBuffer[screenY + dy][screenX + dx] = unitType->Symbol;
                                App.Render.ViewColors[screenY + dy][screenX + dx] = MakeAttr(CONSOLE_GRAY, CONSOLE_BLACK);
                            }
                        }
                    }
                }
            }
            unit = unit->Next;
        }
    }

    if (App.GameState->IsPlacingBuilding) {
        const BUILDING_TYPE* previewType = GetBuildingTypeById(App.GameState->PendingBuildingTypeId);
        if (previewType != NULL && App.GameState->Terrain != NULL) {
            BUILDING preview;
            memcpy(&preview, App.GameState->TeamData[HUMAN_TEAM_INDEX].Buildings, sizeof(BUILDING));
            preview.TypeId = previewType->Id;
            preview.Team = HUMAN_TEAM_INDEX;
            preview.X = App.GameState->PlacementX;
            preview.Y = App.GameState->PlacementY;
            preview.Hp = previewType->MaxHp;
            preview.LastDamageTime = 0;
            preview.BuildTimeRemaining = 0;
            preview.UnderConstruction = TRUE;
            I32 screenX;
            I32 screenY;
            if (GetScreenPosition(preview.X, preview.Y, previewType->Width, previewType->Height, &screenX, &screenY)) {
                RenderBuildingSprite(&preview, previewType, screenX, screenY);
            }
        }
    } else if (App.GameState->IsCommandMode) {
        I32 screenX = 0;
        I32 screenY = 0;
        if (GetScreenPosition(App.GameState->CommandX, App.GameState->CommandY, 1, 1, &screenX, &screenY)) {
            App.Render.ViewBuffer[screenY][screenX] = 'X';
            App.Render.ViewColors[screenY][screenX] = MakeAttr(CONSOLE_WHITE, CONSOLE_BLACK);
        }
    }
    App.Render.ViewBlitInfo.Text = (LPCSTR)App.Render.ViewBuffer[0];
    App.Render.ViewBlitInfo.Attr = (const U8*)App.Render.ViewColors[0];
}

/************************************************************************/

static void CopyMapToFrame(void) {
    for (I32 y = 0; y < VIEWPORT_HEIGHT; y++) {
        I32 destY = TOP_BAR_HEIGHT + y;
        if (destY >= SCREEN_HEIGHT) break;

        for (I32 x = 0; x < VIEWPORT_WIDTH; x++) {
            if (x >= SCREEN_WIDTH) break;
            App.Render.ScreenBuffer[destY][x] = App.Render.ViewBuffer[y][x];
            App.Render.ScreenAttr[destY][x] = App.Render.ViewColors[y][x];
        }
    }
}

/************************************************************************/

static void DrawBottomMenuFrame(void) {
    I32 top = TOP_BAR_HEIGHT + MAP_VIEW_HEIGHT;
    I32 bottom = SCREEN_HEIGHT - 1;
    U8 attr = MakeAttr(CONSOLE_WHITE, CONSOLE_BLACK);

    if (top >= bottom || bottom >= SCREEN_HEIGHT) return;

    App.Render.ScreenBuffer[top][0] = '+';
    App.Render.ScreenBuffer[top][SCREEN_WIDTH - 1] = '+';
    App.Render.ScreenAttr[top][0] = attr;
    App.Render.ScreenAttr[top][SCREEN_WIDTH - 1] = attr;
    for (I32 x = 1; x < SCREEN_WIDTH - 1; x++) {
        App.Render.ScreenBuffer[top][x] = '-';
        App.Render.ScreenAttr[top][x] = attr;
    }

    for (I32 y = top + 1; y <= bottom; y++) {
        App.Render.ScreenBuffer[y][0] = '|';
        App.Render.ScreenBuffer[y][SCREEN_WIDTH - 1] = '|';
        App.Render.ScreenAttr[y][0] = attr;
        App.Render.ScreenAttr[y][SCREEN_WIDTH - 1] = attr;
    }

    App.Render.ScreenBuffer[bottom][0] = '+';
    App.Render.ScreenBuffer[bottom][SCREEN_WIDTH - 1] = '+';
    App.Render.ScreenAttr[bottom][0] = attr;
    App.Render.ScreenAttr[bottom][SCREEN_WIDTH - 1] = attr;
}

/************************************************************************/

/**
 * @brief Format a production hotkey label.
 */
static void FormatProductionKeyLabel(I32 key, char* label, size_t size) {
    if (label == NULL || size == 0) return;

    switch (key) {
        case VK_1: snprintf(label, size, "[1]"); return;
        case VK_2: snprintf(label, size, "[2]"); return;
        case VK_3: snprintf(label, size, "[3]"); return;
        case VK_4: snprintf(label, size, "[4]"); return;
        case VK_5: snprintf(label, size, "[5]"); return;
        case VK_6: snprintf(label, size, "[6]"); return;
        case VK_L: snprintf(label, size, "[L]"); return;
        case VK_H: snprintf(label, size, "[H]"); return;
        case VK_E: snprintf(label, size, "[E]"); return;
        case VK_M: snprintf(label, size, "[M]"); return;
        case VK_T: snprintf(label, size, "[T]"); return;
        case VK_X: snprintf(label, size, "[X]"); return;
        case VK_D: snprintf(label, size, "[D]"); return;
        default: break;
    }

    snprintf(label, size, "[?]");
}

/************************************************************************/

/**
 * @brief Resolve production option metadata.
 */
static BOOL GetProductionOptionInfo(const PRODUCTION_OPTION* option, const char** outName, I32* outCost, I32* outTech) {
    if (option == NULL) return FALSE;
    if (outName != NULL) *outName = "Unknown";
    if (outCost != NULL) *outCost = 0;
    if (outTech != NULL) *outTech = 0;

    if (option->IsBuilding) {
        const BUILDING_TYPE* type = GetBuildingTypeById(option->TypeId);
        if (type == NULL) return FALSE;
        if (outName != NULL) *outName = type->Name;
        if (outCost != NULL) *outCost = type->CostPlasma;
        if (outTech != NULL) *outTech = type->TechLevel;
        return TRUE;
    }

    const UNIT_TYPE* type = GetUnitTypeById(option->TypeId);
    if (type == NULL) return FALSE;
    if (outName != NULL) *outName = type->Name;
    if (outCost != NULL) *outCost = type->CostPlasma;
    if (outTech != NULL) *outTech = type->TechLevel;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Build production menu tokens for the selected building.
 */
static I32 BuildProductionMenuTokens(const BUILDING* building, const char** tokens, I32 maxTokens,
                                     char tokenStorage[][UI_TOKEN_SIZE], I32 storageMax, I32* storageIndex) {
    I32 tokenCount = 0;
    I32 optionCount = 0;
    const PRODUCTION_OPTION* options = NULL;

    if (building == NULL || tokens == NULL || maxTokens <= 0) return 0;

    AddMenuToken((building->TypeId == BUILDING_TYPE_CONSTRUCTION_YARD) ? "Build:" : "Produce:", tokens, &tokenCount, maxTokens);

    options = GetProductionOptions(building->TypeId, &optionCount);
    for (I32 i = 0; i < optionCount; i++) {
        const char* name = NULL;
        I32 cost = 0;
        I32 tech = 0;
        BOOL locked = FALSE;
        char keyLabel[8];
        char token[UI_TOKEN_SIZE];

        if (!GetProductionOptionInfo(&options[i], &name, &cost, &tech)) continue;
        if (!HasTechLevel(tech, HUMAN_TEAM_INDEX)) locked = TRUE;

        FormatProductionKeyLabel(options[i].KeyVK, keyLabel, sizeof(keyLabel));
        if (locked) {
            snprintf(token, sizeof(token), "%s %s(%d)[LOCK]", keyLabel, name, cost);
            AddMenuTokenBuffer(tokenStorage, storageIndex, storageMax, tokens, &tokenCount, maxTokens, token);
        } else {
            snprintf(token, sizeof(token), "%s %s(%d)", keyLabel, name, cost);
            AddMenuTokenBuffer(tokenStorage, storageIndex, storageMax, tokens, &tokenCount, maxTokens, token);
        }
    }

    AddMenuToken("[ESC] Back", tokens, &tokenCount, maxTokens);
    return tokenCount;
}

/************************************************************************/

/**
 * @brief Build production queue summary tokens.
 */
static I32 BuildProductionQueueTokens(const BUILDING* building, const char** tokens, I32 maxTokens,
                                      char tokenStorage[][UI_TOKEN_SIZE], I32 storageMax, I32* storageIndex) {
    I32 tokenCount = 0;

    if (building == NULL || tokens == NULL || maxTokens <= 0) return 0;

    if (building->TypeId == BUILDING_TYPE_CONSTRUCTION_YARD) {
        AddMenuToken("Building:", tokens, &tokenCount, maxTokens);
        for (I32 i = 0; i < building->BuildQueueCount; i++) {
            const BUILD_JOB* job = &building->BuildQueue[i];
            const BUILDING_TYPE* qType = GetBuildingTypeById(job->TypeId);
            const char* name = qType != NULL ? qType->Name : "Unknown";
            if (job->TimeRemaining == 0) {
                char token[UI_TOKEN_SIZE];
                snprintf(token, sizeof(token), "%s(ready)", name);
                AddMenuTokenBuffer(tokenStorage, storageIndex, storageMax, tokens, &tokenCount, maxTokens, token);
            } else {
                I32 seconds = (I32)((job->TimeRemaining + UI_BUILD_TIME_ROUND_MS) / UI_MS_PER_SECOND);
                if (seconds > UI_BUILD_TIME_MAX_SECONDS) seconds = UI_BUILD_TIME_MAX_SECONDS;
                char token[UI_TOKEN_SIZE];
                snprintf(token, sizeof(token), "%s(%ds)", name, seconds);
                AddMenuTokenBuffer(tokenStorage, storageIndex, storageMax, tokens, &tokenCount, maxTokens, token);
            }
        }
    } else {
        AddMenuToken("Units:", tokens, &tokenCount, maxTokens);
        for (I32 i = 0; i < building->UnitQueueCount; i++) {
            const UNIT_JOB* job = &building->UnitQueue[i];
            const UNIT_TYPE* qType = GetUnitTypeById(job->TypeId);
            const char* name = qType != NULL ? qType->Name : "Unknown";
            if (job->TimeRemaining == 0) {
                char token[UI_TOKEN_SIZE];
                snprintf(token, sizeof(token), "%s(ready)", name);
                AddMenuTokenBuffer(tokenStorage, storageIndex, storageMax, tokens, &tokenCount, maxTokens, token);
            } else {
                I32 seconds = (I32)((job->TimeRemaining + UI_BUILD_TIME_ROUND_MS) / UI_MS_PER_SECOND);
                if (seconds > UI_BUILD_TIME_MAX_SECONDS) seconds = UI_BUILD_TIME_MAX_SECONDS;
                char token[UI_TOKEN_SIZE];
                snprintf(token, sizeof(token), "%s(%ds)", name, seconds);
                AddMenuTokenBuffer(tokenStorage, storageIndex, storageMax, tokens, &tokenCount, maxTokens, token);
            }
        }
    }

    return tokenCount;
}

/************************************************************************/

/**
 * @brief Build production status line.
 */
static I32 BuildProductionStatusTokens(const BUILDING* building, const char** tokens, I32 maxTokens,
                                       char tokenStorage[][UI_TOKEN_SIZE], I32 storageMax, I32* storageIndex) {
    BOOL hasReady = FALSE;
    I32 tokenCount = 0;

    if (building == NULL || tokens == NULL || maxTokens <= 0) return 0;

    if (building->TypeId == BUILDING_TYPE_CONSTRUCTION_YARD) {
        for (I32 i = 0; i < building->BuildQueueCount; i++) {
            if (building->BuildQueue[i].TimeRemaining == 0) {
                hasReady = TRUE;
                break;
            }
        }
        AddMenuToken("[B] Build", tokens, &tokenCount, maxTokens);
        if (hasReady) {
            AddMenuToken("[P] Place queued", tokens, &tokenCount, maxTokens);
        }
        {
            char token[UI_TOKEN_SIZE];
            snprintf(token, sizeof(token), "(Queue %d/%d)", building->BuildQueueCount, MAX_PLACEMENT_QUEUE);
            AddMenuTokenBuffer(tokenStorage, storageIndex, storageMax, tokens, &tokenCount, maxTokens, token);
        }
    } else {
        AddMenuToken("[B] Produce", tokens, &tokenCount, maxTokens);
        {
            char token[UI_TOKEN_SIZE];
            snprintf(token, sizeof(token), "(Queue %d/%d)", building->UnitQueueCount, MAX_UNIT_QUEUE);
            AddMenuTokenBuffer(tokenStorage, storageIndex, storageMax, tokens, &tokenCount, maxTokens, token);
        }
    }

    return tokenCount;
}

/************************************************************************/

void RenderBottomMenu(void) {
    I32 menuY = TOP_BAR_HEIGHT + MAP_VIEW_HEIGHT;
    char line0[SCREEN_WIDTH + 1];
    char line1[SCREEN_WIDTH + 1];
    char line2[SCREEN_WIDTH + 1];
    const char* tokens[MENU_TOKEN_MAX];
    char tokenStorage[MENU_TOKEN_MAX][UI_TOKEN_SIZE];
    I32 tokenCount = 0;
    I32 storageIndex = 0;
    static char BlankLine[SCREEN_WIDTH + 1] = {0};
    static BOOL BlankInit = FALSE;
    line0[0] = '\0';
    line1[0] = '\0';
    line2[0] = '\0';

    if (App.GameState == NULL) return;

    if (!BlankInit) {
        memset(BlankLine, ' ', SCREEN_WIDTH);
        BlankLine[SCREEN_WIDTH] = '\0';
        BlankInit = TRUE;
    }

    if (App.GameState->IsPlacingBuilding) {
        const BUILDING_TYPE* type = GetBuildingTypeById(App.GameState->PendingBuildingTypeId);
        {
            char token[UI_TOKEN_SIZE];
            snprintf(token, sizeof(token), "Placing: %s", type != NULL ? type->Name : "Unknown");
            AddMenuTokenBuffer(tokenStorage, &storageIndex, MENU_TOKEN_MAX, tokens, &tokenCount, MENU_TOKEN_MAX, token);
        }
        AddMenuToken("[P] Place", tokens, &tokenCount, MENU_TOKEN_MAX);
        AddMenuToken("[ESC] Cancel", tokens, &tokenCount, MENU_TOKEN_MAX);
        AddMenuToken("Arrows move placement, viewport follows", tokens, &tokenCount, MENU_TOKEN_MAX);
    } else if (App.GameState->IsCommandMode) {
        const char* action = "Move";
        if (App.GameState->CommandType == COMMAND_ATTACK) {
            action = "Attack";
        } else if (App.GameState->CommandType == COMMAND_ESCORT) {
            action = "Escort";
        }
        char confirmKey = (App.GameState->CommandType == COMMAND_ESCORT) ? 'E' : 'M';
        {
            char token[UI_TOKEN_SIZE];
            snprintf(token, sizeof(token), "%s target:", action);
            AddMenuTokenBuffer(tokenStorage, &storageIndex, MENU_TOKEN_MAX, tokens, &tokenCount, MENU_TOKEN_MAX, token);
        }
        {
            char token[UI_TOKEN_SIZE];
            snprintf(token, sizeof(token), "[%c] Confirm", confirmKey);
            AddMenuTokenBuffer(tokenStorage, &storageIndex, MENU_TOKEN_MAX, tokens, &tokenCount, MENU_TOKEN_MAX, token);
        }
        AddMenuToken("[ESC] Cancel", tokens, &tokenCount, MENU_TOKEN_MAX);
        AddMenuToken("Use arrows to adjust target", tokens, &tokenCount, MENU_TOKEN_MAX);
    } else {
        switch (App.Menu.CurrentMenu) {
            case MENU_MAIN:
                if (App.GameState != NULL && App.Menu.SavedGameCount > 0) {
                    AddMenuToken("[N] New Game", tokens, &tokenCount, MENU_TOKEN_MAX);
                    AddMenuToken("[L] Load Game", tokens, &tokenCount, MENU_TOKEN_MAX);
                    AddMenuToken("[S] Save Game", tokens, &tokenCount, MENU_TOKEN_MAX);
                    AddMenuToken("[Q] Quit", tokens, &tokenCount, MENU_TOKEN_MAX);
                } else if (App.GameState != NULL) {
                    AddMenuToken("[N] New Game", tokens, &tokenCount, MENU_TOKEN_MAX);
                    AddMenuToken("[S] Save Game", tokens, &tokenCount, MENU_TOKEN_MAX);
                    AddMenuToken("[Q] Quit", tokens, &tokenCount, MENU_TOKEN_MAX);
                } else if (App.Menu.SavedGameCount > 0) {
                    AddMenuToken("[N] New Game", tokens, &tokenCount, MENU_TOKEN_MAX);
                    AddMenuToken("[L] Load Game", tokens, &tokenCount, MENU_TOKEN_MAX);
                    AddMenuToken("[Q] Quit", tokens, &tokenCount, MENU_TOKEN_MAX);
                } else {
                    AddMenuToken("[N] New Game", tokens, &tokenCount, MENU_TOKEN_MAX);
                    AddMenuToken("[Q] Quit", tokens, &tokenCount, MENU_TOKEN_MAX);
                }
                break;

            case MENU_NEW_GAME:
                AddMenuToken("[LEFT/RIGHT] Width/Height/Teams", tokens, &tokenCount, MENU_TOKEN_MAX);
                AddMenuToken("[UP/DOWN] Change selection", tokens, &tokenCount, MENU_TOKEN_MAX);
                AddMenuToken("[ENTER] Start", tokens, &tokenCount, MENU_TOKEN_MAX);
                AddMenuToken("[ESC] Back", tokens, &tokenCount, MENU_TOKEN_MAX);
                break;

            case MENU_SAVE:
                AddMenuToken("Type filename", tokens, &tokenCount, MENU_TOKEN_MAX);
                AddMenuToken("ENTER to save", tokens, &tokenCount, MENU_TOKEN_MAX);
                AddMenuToken("ESC to cancel", tokens, &tokenCount, MENU_TOKEN_MAX);
                break;

            case MENU_LOAD:
                AddMenuToken("Select save with UP/DOWN", tokens, &tokenCount, MENU_TOKEN_MAX);
                AddMenuToken("ENTER to load", tokens, &tokenCount, MENU_TOKEN_MAX);
                AddMenuToken("ESC to cancel", tokens, &tokenCount, MENU_TOKEN_MAX);
                break;

            case MENU_IN_GAME:
                if (App.GameState->SelectedBuilding != NULL &&
                    IsProductionBuildingType(App.GameState->SelectedBuilding->TypeId)) {
                    BUILDING* producer = App.GameState->SelectedBuilding;
                    if (App.GameState->ProductionMenuActive) {
                        tokenCount += BuildProductionMenuTokens(producer, &tokens[tokenCount], MENU_TOKEN_MAX - tokenCount,
                                                                tokenStorage, MENU_TOKEN_MAX, &storageIndex);
                    } else {
                        const BUILDING_TYPE* bt = GetBuildingTypeById(producer->TypeId);
                        const char* name = (bt != NULL) ? bt->Name : "Unknown";
                        char token[UI_TOKEN_SIZE];
                        snprintf(token, sizeof(token), "%s", name);
                        AddMenuTokenBuffer(tokenStorage, &storageIndex, MENU_TOKEN_MAX, tokens, &tokenCount, MENU_TOKEN_MAX, token);
                        tokenCount += BuildProductionStatusTokens(producer, &tokens[tokenCount], MENU_TOKEN_MAX - tokenCount,
                                                                  tokenStorage, MENU_TOKEN_MAX, &storageIndex);
                        if (producer->BuildQueueCount > 0 || producer->UnitQueueCount > 0) {
                            I32 added = BuildProductionQueueTokens(producer, &tokens[tokenCount],
                                                                   MENU_TOKEN_MAX - tokenCount,
                                                                   tokenStorage, MENU_TOKEN_MAX, &storageIndex);
                            tokenCount += added;
                            AddMenuToken("[DEL] Cancel production", tokens, &tokenCount, MENU_TOKEN_MAX);
                        }
                    }
                } else {
                    if (App.GameState != NULL && App.GameState->SelectedUnit != NULL) {
                        const UNIT_TYPE* ut = GetUnitTypeById(App.GameState->SelectedUnit->TypeId);
                        const char* name = (ut != NULL) ? ut->Name : "Unknown";
                        {
                        char token[UI_TOKEN_SIZE];
                        snprintf(token, sizeof(token), "%s", name);
                        AddMenuTokenBuffer(tokenStorage, &storageIndex, MENU_TOKEN_MAX, tokens, &tokenCount, MENU_TOKEN_MAX, token);
                    }
                    AddMenuToken("[PgDn] Next", tokens, &tokenCount, MENU_TOKEN_MAX);
                        AddMenuToken("[PgUp] Prev", tokens, &tokenCount, MENU_TOKEN_MAX);
                        AddMenuToken("[C] Center", tokens, &tokenCount, MENU_TOKEN_MAX);
                        AddMenuToken("[M] Move", tokens, &tokenCount, MENU_TOKEN_MAX);
                        AddMenuToken("[A] Attack", tokens, &tokenCount, MENU_TOKEN_MAX);
                        AddMenuToken("[E] Escort", tokens, &tokenCount, MENU_TOKEN_MAX);
                        AddMenuToken("[X] Explore", tokens, &tokenCount, MENU_TOKEN_MAX);
                        AddMenuToken("[ESC] Cancel cmd", tokens, &tokenCount, MENU_TOKEN_MAX);
                    } else {
                        if (App.GameState != NULL && App.GameState->SelectedBuilding != NULL) {
                            const BUILDING_TYPE* bt = GetBuildingTypeById(App.GameState->SelectedBuilding->TypeId);
                            const char* name = (bt != NULL) ? bt->Name : "Unknown";
                            char token[UI_TOKEN_SIZE];
                            snprintf(token, sizeof(token), "%s", name);
                            AddMenuTokenBuffer(tokenStorage, &storageIndex, MENU_TOKEN_MAX, tokens, &tokenCount, MENU_TOKEN_MAX, token);
                        }
                        AddMenuToken("[PgDn] Next", tokens, &tokenCount, MENU_TOKEN_MAX);
                        AddMenuToken("[PgUp] Prev", tokens, &tokenCount, MENU_TOKEN_MAX);
                        AddMenuToken("[C] Center", tokens, &tokenCount, MENU_TOKEN_MAX);
                        AddMenuToken("[SPACE] Pause", tokens, &tokenCount, MENU_TOKEN_MAX);
                        AddMenuToken("[ESC] Menu", tokens, &tokenCount, MENU_TOKEN_MAX);
                    }
                }
                break;

            default:
                if (App.GameState != NULL && App.GameState->SelectedBuilding != NULL) {
                    const BUILDING_TYPE* bt = GetBuildingTypeById(App.GameState->SelectedBuilding->TypeId);
                    const char* name = (bt != NULL) ? bt->Name : "Unknown";
                    {
                        char token[UI_TOKEN_SIZE];
                        snprintf(token, sizeof(token), "%s", name);
                        AddMenuTokenBuffer(tokenStorage, &storageIndex, MENU_TOKEN_MAX, tokens, &tokenCount, MENU_TOKEN_MAX, token);
                    }
                }
                AddMenuToken("[PgDn] Next", tokens, &tokenCount, MENU_TOKEN_MAX);
                AddMenuToken("[PgUp] Prev", tokens, &tokenCount, MENU_TOKEN_MAX);
                AddMenuToken("[C] Center", tokens, &tokenCount, MENU_TOKEN_MAX);
                AddMenuToken("[SPACE] Pause", tokens, &tokenCount, MENU_TOKEN_MAX);
                AddMenuToken("[ESC] Menu", tokens, &tokenCount, MENU_TOKEN_MAX);
                break;
        }
    }

    BuildBottomMenuLinesFromTokens(tokens, tokenCount, line0, sizeof(line0), line1, sizeof(line1), line2, sizeof(line2));

    if (line0[0] == '\0') snprintf(line0, sizeof(line0), "%s", BlankLine);
    if (line1[0] == '\0') snprintf(line1, sizeof(line1), "%s", BlankLine);
    if (line2[0] == '\0') snprintf(line2, sizeof(line2), "%s", BlankLine);

    for (I32 i = 0; i < BOTTOM_BAR_HEIGHT; i++) {
        I32 y = menuY + i;
        char desired[SCREEN_WIDTH + 1];
        switch (i) {
            case 0: snprintf(desired, sizeof(desired), "%-*s", SCREEN_WIDTH - 2, line0); break;
            case 1: snprintf(desired, sizeof(desired), "%-*s", SCREEN_WIDTH - 2, line1); break;
            case 2: snprintf(desired, sizeof(desired), "%-*s", SCREEN_WIDTH - 2, line2); break;
            default: snprintf(desired, sizeof(desired), "%-*s", SCREEN_WIDTH - 2, " "); break;
        }
        WriteLineToFrame(y, 1, SCREEN_WIDTH - 2, desired, MakeAttr(CONSOLE_GRAY, CONSOLE_BLACK));
        strcpy(App.Render.PrevBottom[i], desired);
    }
}

/************************************************************************/

static void RenderStatusBar(void) {
    I32 statusY = SCREEN_HEIGHT - 1;
    char lineBuf[SCREEN_WIDTH + 1];
    char suffix[UI_SUFFIX_SIZE];
    size_t suffixLen = 0;
    size_t startPos = 0;

    if (App.GameState != NULL && App.Render.StatusStartTime != 0) {
        if (App.GameState->GameTime - App.Render.StatusStartTime >= STATUS_MESSAGE_DURATION_MS) {
            App.Render.StatusLine[0] = '\0';
            App.Render.StatusStartTime = 0;
        }
    }

    if (App.Render.StatusLine[0] == '\0') {
        snprintf(App.Render.StatusLine, sizeof(App.Render.StatusLine), "%-*s", SCREEN_WIDTH, " ");
        App.Render.StatusLine[SCREEN_WIDTH] = '\0';
    }

    memset(lineBuf, ' ', SCREEN_WIDTH);
    lineBuf[SCREEN_WIDTH] = '\0';

    size_t len = strlen(App.Render.StatusLine);
    if (len > (size_t)SCREEN_WIDTH) len = SCREEN_WIDTH;
    if (len > 0) memcpy(lineBuf, App.Render.StatusLine, len);

    snprintf(suffix, sizeof(suffix), " | Speed:%dx %s",
             App.GameState != NULL ? App.GameState->GameSpeed : 1,
             (App.GameState != NULL && App.GameState->IsPaused) ? "PAUSED" : "RUNNING");
    suffixLen = strlen(suffix);
    if (suffixLen > (size_t)SCREEN_WIDTH) suffixLen = SCREEN_WIDTH;
    if (suffixLen > 0) {
        startPos = (size_t)SCREEN_WIDTH - suffixLen;
        memcpy(&lineBuf[startPos], suffix, suffixLen);
    }

    WriteLineToFrame(statusY, 0, SCREEN_WIDTH, lineBuf, MakeAttr(CONSOLE_GRAY, CONSOLE_BLACK));
    strcpy(App.Render.PrevStatusLine, App.Render.StatusLine);
}

/************************************************************************/

void RenderInGameScreen(void) {
    CONSOLEBLITBUFFER Frame;

    ClearFrameBuffers();
    RenderTopBar();
    RenderMapArea();
    CopyMapToFrame();
    DrawBottomMenuFrame();
    RenderBottomMenu();
    RenderStatusBar();

    Frame.Text = (LPCSTR)App.Render.ScreenBuffer[0];
    Frame.Width = SCREEN_WIDTH;
    Frame.Height = SCREEN_HEIGHT;
    Frame.X = 0;
    Frame.Y = 0;
    Frame.Attr = (const U8*)App.Render.ScreenAttr[0];
    Frame.TextPitch = SCREEN_WIDTH + 1;
    Frame.AttrPitch = SCREEN_WIDTH;
    ConsoleBlitBuffer(&Frame);
}

/************************************************************************/

void RenderDebugScreen(void) {
    if (App.GameState == NULL) return;

    I32 teamCount = GetTeamCountSafe();
    if (teamCount <= 0) teamCount = 1;

    for (I32 y = 0; y < SCREEN_HEIGHT; y++) {
        memset(App.Render.ScreenBuffer[y], ' ', SCREEN_WIDTH);
        App.Render.ScreenBuffer[y][SCREEN_WIDTH] = '\0';
    }

    const char* title = "DEBUG - TEAM STATE (ESC to return)";
    I32 titleX = (SCREEN_WIDTH - (I32)strlen(title)) / 2;
    if (titleX < 0) titleX = 0;
    memcpy(&App.Render.ScreenBuffer[1][titleX], title, strlen(title));

    for (I32 team = 0; team < teamCount; team++) {
        TEAM_RESOURCES* res = &App.GameState->TeamData[team].Resources;
        I32 buildingCount = 0;
        I32 unitCount = 0;
        BUILDING* b = App.GameState->TeamData[team].Buildings;
        while (b != NULL) {
            buildingCount++;
            b = b->Next;
        }
        UNIT* u = App.GameState->TeamData[team].Units;
        while (u != NULL) {
            unitCount++;
            u = u->Next;
        }
        I32 attitude = App.GameState->TeamData[team].AiAttitude;
        I32 mindset = App.GameState->TeamData[team].AiMindset;

        char line0[SCREEN_WIDTH + 1];
        char line1[SCREEN_WIDTH + 1];
        snprintf(line0, sizeof(line0),
                 "Team %d | Plasma:%d Energy:%d/%d",
                 team,
                 res->Plasma, res->Energy, res->MaxEnergy);
        snprintf(line1, sizeof(line1),
                 "Buildings:%d Units:%d | Attitude:%s | Mindset:%s",
                 buildingCount, unitCount,
                 GetAttitudeName(attitude),
                 GetMindsetName(mindset));

        I32 y0 = 3 + team * 2;
        I32 y1 = y0 + 1;
        if (y1 >= SCREEN_HEIGHT) break;

        size_t len0 = strlen(line0);
        size_t len1 = strlen(line1);
        if (len0 > (size_t)SCREEN_WIDTH) len0 = SCREEN_WIDTH;
        if (len1 > (size_t)SCREEN_WIDTH) len1 = SCREEN_WIDTH;
        memcpy(&App.Render.ScreenBuffer[y0][0], line0, len0);
        memcpy(&App.Render.ScreenBuffer[y1][0], line1, len1);
    }

    for (I32 y = 0; y < SCREEN_HEIGHT; y++) {
        CONSOLEBLITBUFFER Line = {0, (U32)y, SCREEN_WIDTH, 1, (LPCSTR)App.Render.ScreenBuffer[y], CONSOLE_GRAY, CONSOLE_BLACK, SCREEN_WIDTH, NULL, 0};
        ConsoleBlitBuffer(&Line);
        strcpy(App.Render.PrevScreenBuffer[y], App.Render.ScreenBuffer[y]);
    }

    App.Render.DebugDrawn = TRUE;
}

/************************************************************************/

void RenderMainMenuScreen(void) {
    const char* title = "Terminal Tactics";
    const char* options[8];
    I32 optionCount = 0;
    BOOL hasGame = (App.GameState != NULL);
    BOOL hasSaves = (App.Menu.SavedGameCount > 0);
    U8 attr = MakeAttr(CONSOLE_GRAY, CONSOLE_BLACK);

    if (App.Render.MainMenuDrawn && App.Menu.PrevMenu == (I32)MENU_MAIN) return;

    options[optionCount++] = "[N] New Game";
    if (hasSaves) options[optionCount++] = "[L] Load Game";
    if (hasGame) options[optionCount++] = "[S] Save Game";
    options[optionCount++] = "[M] Manual";
    if (hasGame) options[optionCount++] = "[ESC] Return to game";
    options[optionCount++] = "[Q] Quit";

    ClearFrameBuffers();

    WriteCenteredToFrame(MAIN_MENU_TITLE_Y, title, attr);
    for (I32 i = 0; i < optionCount; i++) {
        WriteCenteredToFrame(MAIN_MENU_OPTION_START_Y + i * MAIN_MENU_OPTION_STEP_Y, options[i], attr);
    }

    BlitFrameBuffer();
    App.Render.MainMenuDrawn = TRUE;
}

/************************************************************************/

/// @brief Render the manual screen with scrollable text.
void RenderManualScreen(void) {
    const char* title = "Manual";
    const char* footer = "[UP/DOWN] Scroll  [PGUP/PGDN] Page  [HOME/END] Jump  [ESC] Back";
    U8 attr = MakeAttr(CONSOLE_GRAY, CONSOLE_BLACK);
    I32 visibleLines = MANUAL_CONTENT_BOTTOM - MANUAL_CONTENT_TOP + 1;
    I32 maxScroll = GetManualScrollMax(visibleLines);
    I32 startLine;

    ClearFrameBuffers();
    WriteCenteredToFrame(MANUAL_TITLE_Y, title, attr);

    if (App.Menu.MenuPage < 0) App.Menu.MenuPage = 0;
    if (App.Menu.MenuPage > maxScroll) App.Menu.MenuPage = maxScroll;
    startLine = App.Menu.MenuPage;

    for (I32 i = 0; i < visibleLines; i++) {
        I32 lineIndex = startLine + i;
        const char* start = NULL;
        I32 length = 0;
        char line[SCREEN_WIDTH + 1];

        line[0] = '\0';
        if (GetManualLineSpan(lineIndex, &start, &length)) {
            if (length < 0) length = 0;
            if (length > SCREEN_WIDTH) length = SCREEN_WIDTH;
            memcpy(line, start, (size_t)length);
            line[length] = '\0';
        }

        WriteLineToFrame(MANUAL_CONTENT_TOP + i, 0, SCREEN_WIDTH, line, attr);
    }

    WriteCenteredToFrame(MANUAL_FOOTER_Y, footer, attr);
    BlitFrameBuffer();
}

/************************************************************************/

void RenderNewGameScreen(void) {
    char buffer[SCREEN_WIDTH + 1];
    U8 attr = MakeAttr(CONSOLE_GRAY, CONSOLE_BLACK);
    U8 selectedAttr = MakeAttr(CONSOLE_RED, CONSOLE_BLACK);

    if (App.Render.CachedNGWidth == App.Menu.PendingMapWidth &&
        App.Render.CachedNGHeight == App.Menu.PendingMapHeight &&
        App.Render.CachedNGDifficulty == App.Menu.PendingDifficulty &&
        App.Render.CachedNGTeams == App.Menu.PendingTeamCount &&
        App.Render.CachedNGSelection == App.Menu.SelectedOption) {
        return;
    }

    ClearFrameBuffers();

    WriteCenteredToFrame(NEW_GAME_TITLE_Y, "New Game", attr);

    snprintf(buffer, sizeof(buffer), "Map Width:  %3d %s", App.Menu.PendingMapWidth, App.Menu.SelectedOption == 0 ? "<" : " ");
    WriteCenteredToFrame(NEW_GAME_WIDTH_Y, buffer, App.Menu.SelectedOption == 0 ? selectedAttr : attr);

    snprintf(buffer, sizeof(buffer), "Map Height: %3d %s", App.Menu.PendingMapHeight, App.Menu.SelectedOption == 1 ? "<" : " ");
    WriteCenteredToFrame(NEW_GAME_HEIGHT_Y, buffer, App.Menu.SelectedOption == 1 ? selectedAttr : attr);

    snprintf(buffer, sizeof(buffer), "Teams:      %3d %s", App.Menu.PendingTeamCount, App.Menu.SelectedOption == 2 ? "<" : " ");
    WriteCenteredToFrame(NEW_GAME_TEAMS_Y, buffer, App.Menu.SelectedOption == 2 ? selectedAttr : attr);

    snprintf(buffer, sizeof(buffer), "Difficulty: %s %s", App.Menu.PendingDifficulty == 0 ? "Easy" : (App.Menu.PendingDifficulty == 1 ? "Normal" : "Hard"), App.Menu.SelectedOption == 3 ? "<" : " ");
    WriteCenteredToFrame(NEW_GAME_DIFFICULTY_Y, buffer, App.Menu.SelectedOption == 3 ? selectedAttr : attr);

    WriteCenteredToFrame(NEW_GAME_FOOTER_Y, "[ENTER] Start   [ESC] Back", attr);

    BlitFrameBuffer();

    App.Render.CachedNGWidth = App.Menu.PendingMapWidth;
    App.Render.CachedNGHeight = App.Menu.PendingMapHeight;
    App.Render.CachedNGDifficulty = App.Menu.PendingDifficulty;
    App.Render.CachedNGTeams = App.Menu.PendingTeamCount;
    App.Render.CachedNGSelection = App.Menu.SelectedOption;
}

/************************************************************************/

void RenderLoadGameScreen(void) {
    U8 attr = MakeAttr(CONSOLE_GRAY, CONSOLE_BLACK);
    U8 selectedAttr = MakeAttr(CONSOLE_RED, CONSOLE_BLACK);

    if (App.Render.CachedLoadSelected == App.Menu.SelectedSaveIndex &&
        App.Render.CachedLoadCount == App.Menu.SavedGameCount) {
        return;
    }

    ClearFrameBuffers();

    WriteCenteredToFrame(LOAD_GAME_TITLE_Y, "Load Game", attr);

    I32 startY = LOAD_GAME_START_Y;
    if (App.Menu.SavedGameCount > 0) {
        for (I32 i = 0; i < App.Menu.SavedGameCount && i < LOAD_GAME_MAX_ITEMS; i++) {
            char line[SCREEN_WIDTH + 1];
            snprintf(line, sizeof(line), "%c %s", (i == App.Menu.SelectedSaveIndex) ? '>' : ' ', App.Menu.SavedGames[i]);
            WriteCenteredToFrame(startY + i, line, (i == App.Menu.SelectedSaveIndex) ? selectedAttr : attr);
        }
    } else {
        WriteCenteredToFrame(startY + LOAD_GAME_EMPTY_OFFSET, "No saves available", attr);
    }

    BlitFrameBuffer();
    App.Render.CachedLoadSelected = App.Menu.SelectedSaveIndex;
    App.Render.CachedLoadCount = App.Menu.SavedGameCount;
}

/************************************************************************/

void RenderSaveGameScreen(void) {
    const char* title = "Save Game";
    const char* prompt = "Type a filename, ENTER to save, ESC to cancel";
    char line[SCREEN_WIDTH + 1];
    U8 attr = MakeAttr(CONSOLE_GRAY, CONSOLE_BLACK);

    if (App.Menu.PrevMenu == (I32)MENU_SAVE && strcmp(App.Render.CachedSaveName, App.Menu.SaveFileName) == 0) {
        return;
    }

    ClearFrameBuffers();

    WriteCenteredToFrame((SCREEN_HEIGHT / 2) - 3, title, attr);
    WriteCenteredToFrame((SCREEN_HEIGHT / 2) - 1, prompt, attr);

    snprintf(line, sizeof(line), "Filename: %s", App.Menu.SaveFileName);
    WriteCenteredToFrame((SCREEN_HEIGHT / 2) + 1, line, attr);
    BlitFrameBuffer();
    CopyName(App.Render.CachedSaveName, App.Menu.SaveFileName);
}

/************************************************************************/

static void RenderGameOverScreen(void) {
    U8 attr = MakeAttr(CONSOLE_GRAY, CONSOLE_BLACK);
    const char* header = "Team | Plasma |  Energy | Buildings | Units | Score";
    I32 tableWidth = (I32)strlen(header);
    I32 tableX = (SCREEN_WIDTH - tableWidth) / 2;

    ClearFrameBuffers();

    WriteCenteredToFrame(GAME_OVER_TITLE_Y, "Game Over", attr);
    WriteCenteredToFrame(GAME_OVER_MESSAGE_Y, "Your team has been eliminated", attr);

    I32 startY = GAME_OVER_LIST_START_Y;
    if (tableX < 0) tableX = 0;
    WriteLineToFrame(startY, tableX, tableWidth, header, attr);
    startY += GAME_OVER_LIST_STEP_Y;

    if (App.GameState != NULL) {
        I32 teamCount = GetTeamCountSafe();
        for (I32 team = 0; team < teamCount; team++) {
            TEAM_RESOURCES* res = GetTeamResources(team);
            U32 buildingCount = CountBuildingsForTeam(team);
            U32 unitCount = CountUnitsForTeam(team);
            I32 score = CalculateTeamScore(team);
            I32 plasma = (res != NULL) ? res->Plasma : 0;
            I32 energy = (res != NULL) ? res->Energy : 0;
            I32 maxEnergy = (res != NULL) ? res->MaxEnergy : 0;

            char line[SCREEN_WIDTH + 1];
            snprintf(line, sizeof(line),
                     "T%d   | %6d |  %3d/%3d | %9u | %5u | %6d",
                     team, plasma, energy, maxEnergy, buildingCount, unitCount, score);
            WriteLineToFrame(startY, tableX, tableWidth, line, attr);
            startY += GAME_OVER_LIST_STEP_Y;
        }
    }

    WriteCenteredToFrame(GAME_OVER_FOOTER_Y, "[ESC] Back to main menu", attr);

    BlitFrameBuffer();
}

/************************************************************************/

void RenderScreen(void) {
    if ((I32)App.Menu.CurrentMenu != App.Menu.PrevMenu) {
        if (App.Menu.CurrentMenu == MENU_MAIN) {
            LoadSaveList();
        }
        ConsoleClear();
        ResetRenderCache();
        App.Render.DebugDrawn = FALSE;
        App.Menu.PrevMenu = (I32)App.Menu.CurrentMenu;
    }

    switch (App.Menu.CurrentMenu) {
        case MENU_MAIN:
            RenderMainMenuScreen();
            break;
        case MENU_NEW_GAME:
            RenderNewGameScreen();
            break;
        case MENU_MANUAL:
            RenderManualScreen();
            break;
        case MENU_SAVE:
            RenderSaveGameScreen();
            break;
        case MENU_LOAD:
            RenderLoadGameScreen();
            break;
        case MENU_IN_GAME:
            RenderInGameScreen();
            break;
        case MENU_DEBUG:
            RenderDebugScreen();
            break;
        case MENU_GAME_OVER:
            RenderGameOverScreen();
            break;
        default:
            RenderMainMenuScreen();
            break;
    }
}
