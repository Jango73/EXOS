
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

#include "tt-save.h"
#include "tt-map.h"
#include "tt-fog.h"
#include "tt-entities.h"
#include "tt-game.h"
#include "tt-render.h"
#include "tt-ai.h"
#include "tt-log.h"

/************************************************************************/

#define SAVE_VERSION 11

/************************************************************************/

BOOL IsValidFilenameChar(char c) {
    if (c >= 'a' && c <= 'z') return TRUE;
    if (c >= 'A' && c <= 'Z') return TRUE;
    if (c >= '0' && c <= '9') return TRUE;
    if (c == '.' || c == '-' || c == '_') return TRUE;
    return FALSE;
}

/************************************************************************/

static BOOL ResolveSaveDirectory(char* directory, U32 directorySize) {
    const char* exePath = (_argv != NULL) ? _argv[0] : NULL;
    const char* lastSlash;
    char cwd[MAX_PATH_NAME];

    if (directory == NULL || directorySize == 0) return FALSE;

    if (getcwd(cwd, sizeof(cwd)) == NULL || cwd[0] == '\0') {
        return FALSE;
    }

    snprintf(directory, directorySize, "%s", cwd);

    if (strcmp(directory, ROOT) == 0 && exePath != NULL) {
        lastSlash = strrchr(exePath, PATH_SEP);
        if (lastSlash != NULL && lastSlash != exePath) {
            U32 len = (U32)(lastSlash - exePath);
            if (len >= directorySize) {
                len = directorySize - 1;
            }
            memmove(directory, exePath, len);
            directory[len] = '\0';
        }
    }

    if (directory[0] == '\0') {
        directory[0] = PATH_SEP;
        directory[1] = '\0';
    }

    return TRUE;
}

/************************************************************************/

static U32 CountBuildings(void) {
    U32 count = 0;
    if (App.GameState == NULL) return 0;

    I32 teamCount = GetTeamCountSafe();
    if (teamCount <= 0) return 0;

    for (I32 team = 0; team < teamCount; team++) {
        BUILDING* current = App.GameState->TeamData[team].Buildings;
        while (current != NULL) {
            count++;
            current = current->Next;
        }
    }
    return count;
}

/************************************************************************/

static U32 CountUnits(void) {
    U32 count = 0;
    if (App.GameState == NULL) return 0;

    I32 teamCount = GetTeamCountSafe();
    if (teamCount <= 0) return 0;

    for (I32 team = 0; team < teamCount; team++) {
        UNIT* current = App.GameState->TeamData[team].Units;
        while (current != NULL) {
            count++;
            current = current->Next;
        }
    }
    return count;
}

/************************************************************************/

static BOOL WriteAll(FILE* file, const void* buffer, size_t size) {
    return fwrite(buffer, 1, size, file) == size;
}

/************************************************************************/

BOOL ResolveAppFilePath(const char* fileName, char* fullPath, U32 fullPathSize) {
    char directory[MAX_PATH_NAME];
    U32 dirLength;
    U32 nameLength;
    BOOL needsSeparator;

    if (fileName == NULL || fullPath == NULL || fullPathSize == 0) return FALSE;

    if (strchr(fileName, PATH_SEP) != NULL) {
        if (strlen(fileName) + 1 > fullPathSize) {
            return FALSE;
        }
        snprintf(fullPath, fullPathSize, "%s", fileName);
        return TRUE;
    }

    if (!ResolveSaveDirectory(directory, sizeof(directory))) {
        return FALSE;
    }

    dirLength = (U32)strlen(directory);
    nameLength = (U32)strlen(fileName);
    needsSeparator = (dirLength > 0 && directory[dirLength - 1] != PATH_SEP);

    if (dirLength + (needsSeparator ? 1 : 0) + nameLength + 1 > fullPathSize) {
        return FALSE;
    }

    snprintf(fullPath, fullPathSize, "%s%s%s", directory, needsSeparator ? "/" : "", fileName);
    return TRUE;
}

BOOL SaveGame(const char* path) {
    char fullPath[MAX_PATH_NAME];

    if (App.GameState == NULL) return FALSE;

    RebuildOccupancy();


    if (!ResolveAppFilePath(path, fullPath, sizeof(fullPath))) {
        return FALSE;
    }

    FILE* file = fopen(fullPath, "wb");
    if (file == NULL) {
        return FALSE;
    }

    /* Header */
    U32 magic = 0x54544143; /* 'TTAC' */
    U32 version = SAVE_VERSION;
    I32 mapWidth = (I32)App.GameState->MapWidth;
    I32 mapHeight = (I32)App.GameState->MapHeight;
    I32 difficulty = (I32)App.GameState->Difficulty;
    I32 viewportX = (I32)App.GameState->ViewportPos.X;
    I32 viewportY = (I32)App.GameState->ViewportPos.Y;
    I32 teamCount = App.GameState->TeamCount;
    if (teamCount < 1) teamCount = 1;
    if (teamCount > MAX_TEAMS) teamCount = MAX_TEAMS;
    TEAM_RESOURCES res[MAX_TEAMS];
    I32 aiAttitudes[MAX_TEAMS];
    I32 aiMindsets[MAX_TEAMS];
    for (I32 i = 0; i < MAX_TEAMS; i++) {
        res[i] = App.GameState->TeamData[i].Resources;
        aiAttitudes[i] = App.GameState->TeamData[i].AiAttitude;
        aiMindsets[i] = App.GameState->TeamData[i].AiMindset;
    }
    U32 gameTime = (U32)App.GameState->GameTime;
    U32 lastUpdate = (U32)App.GameState->LastUpdate;
    I32 gameSpeed = (I32)App.GameState->GameSpeed;
    I32 isPaused = App.GameState->IsPaused ? 1 : 0;
    I32 menuPage = (I32)App.GameState->MenuPage;
    I32 showGrid = App.GameState->ShowGrid ? 1 : 0;
    I32 showCoordinates = App.GameState->ShowCoordinates ? 1 : 0;
    I32 isPlacing = App.GameState->IsPlacingBuilding ? 1 : 0;
    I32 pendingType = App.GameState->PendingBuildingTypeId;
    I32 placementX = App.GameState->PlacementX;
    I32 placementY = App.GameState->PlacementY;

    if (!WriteAll(file, &magic, sizeof(magic)) ||
        !WriteAll(file, &version, sizeof(version)) ||
        !WriteAll(file, &mapWidth, sizeof(mapWidth)) ||
        !WriteAll(file, &mapHeight, sizeof(mapHeight)) ||
        !WriteAll(file, &difficulty, sizeof(difficulty)) ||
        !WriteAll(file, &viewportX, sizeof(viewportX)) ||
        !WriteAll(file, &viewportY, sizeof(viewportY)) ||
        !WriteAll(file, &teamCount, sizeof(teamCount)) ||
        !WriteAll(file, &res, sizeof(res)) ||
        !WriteAll(file, &aiAttitudes, sizeof(aiAttitudes)) ||
        !WriteAll(file, &aiMindsets, sizeof(aiMindsets)) ||
        !WriteAll(file, &gameTime, sizeof(gameTime)) ||
        !WriteAll(file, &lastUpdate, sizeof(lastUpdate)) ||
        !WriteAll(file, &gameSpeed, sizeof(gameSpeed)) ||
        !WriteAll(file, &isPaused, sizeof(isPaused)) ||
        !WriteAll(file, &menuPage, sizeof(menuPage)) ||
        !WriteAll(file, &showGrid, sizeof(showGrid)) ||
        !WriteAll(file, &showCoordinates, sizeof(showCoordinates)) ||
        !WriteAll(file, &isPlacing, sizeof(isPlacing)) ||
        !WriteAll(file, &pendingType, sizeof(pendingType)) ||
        !WriteAll(file, &placementX, sizeof(placementX)) ||
        !WriteAll(file, &placementY, sizeof(placementY))) {
        fclose(file);
        return FALSE;
    }

    /* Map data */
    for (I32 y = 0; y < App.GameState->MapHeight; y++) {
        if (!WriteAll(file, App.GameState->Terrain[y], sizeof(TERRAIN) * (size_t)App.GameState->MapWidth)) {
            fclose(file);
            return FALSE;
        }
    }

    /* Plasma density serialized as 32-bit values for cross-arch consistency */
    {
        int32_t plasmaRow[MAX_MAP_SIZE];
        for (I32 y = 0; y < App.GameState->MapHeight; y++) {
            for (I32 x = 0; x < App.GameState->MapWidth; x++) {
                plasmaRow[x] = (int32_t)App.GameState->PlasmaDensity[y][x];
            }
            if (!WriteAll(file, plasmaRow, sizeof(int32_t) * (size_t)App.GameState->MapWidth)) {
                fclose(file);
                return FALSE;
            }
        }
    }

    /* Buildings */
    U32 buildingCount = CountBuildings();
    if (!WriteAll(file, &buildingCount, sizeof(buildingCount))) {
        fclose(file);
        return FALSE;
    }

    for (I32 team = 0; team < teamCount; team++) {
        BUILDING* building = App.GameState->TeamData[team].Buildings;
        while (building != NULL) {
            BUILDING record = *building;
            record.Next = NULL;

            if (GetBuildingTypeById(record.TypeId) == NULL) {
                fclose(file);
                return FALSE;
            }

            if (!WriteAll(file, &record, sizeof(record))) {
                fclose(file);
                return FALSE;
            }

            building = building->Next;
        }
    }

    /* Units */
    U32 unitCount = CountUnits();
    if (!WriteAll(file, &unitCount, sizeof(unitCount))) {
        fclose(file);
        return FALSE;
    }

    for (I32 team = 0; team < teamCount; team++) {
        UNIT* unit = App.GameState->TeamData[team].Units;
        while (unit != NULL) {
            UNIT record = *unit;
            record.Next = NULL;
            record.PathHead = NULL;
            record.PathTail = NULL;
            record.PathTargetX = record.TargetX;
            record.PathTargetY = record.TargetY;

            if (GetUnitTypeById(record.TypeId) == NULL) {
                fclose(file);
                return FALSE;
            }

            if (!WriteAll(file, &record, sizeof(record))) {
                fclose(file);
                return FALSE;
            }

            unit = unit->Next;
        }
    }

    fclose(file);
    return TRUE;
}

/************************************************************************/

static long GetFileSize(FILE* file) {
    long current = ftell(file);
    long size;

    if (current < 0) return -1;
    if (fseek(file, 0, SEEK_END) != 0) return -1;

    size = ftell(file);
    if (size < 0) return -1;

    if (fseek(file, current, SEEK_SET) != 0) return -1;

    return size;
}

/************************************************************************/

static BOOL ReadBlock(FILE* file, void* buffer, size_t size, const char* label, long fileSize) {
    UNUSED(label);
    UNUSED(fileSize);
    size_t readCount = fread(buffer, 1, size, file);
    if (readCount != size) {
        return FALSE;
    }
    return TRUE;
}

/************************************************************************/

void LoadSaveList(void) {
    FILEFINDINFO FindInfo;
    char saveDirectory[MAX_PATH_NAME];

    if (!ResolveSaveDirectory(saveDirectory, sizeof(saveDirectory))) {
        saveDirectory[0] = '.';
        saveDirectory[1] = '\0';
    }

    App.Menu.SavedGameCount = 0;
    App.Menu.SelectedSaveIndex = 0;

    memset(&FindInfo, 0, sizeof(FindInfo));
    FindInfo.Header.Size = sizeof(FILEFINDINFO);
    FindInfo.Header.Version = EXOS_ABI_VERSION;
    FindInfo.Header.Flags = 0;
    FindInfo.Path = (LPCSTR)saveDirectory;
    FindInfo.Pattern = (LPCSTR)"*.sav";
    FindInfo.SearchHandle = 0;

    if (FindFirstFile(&FindInfo)) {
        do {
            if (App.Menu.SavedGameCount < MAX_SAVED_GAMES) {
                CopyName(App.Menu.SavedGames[App.Menu.SavedGameCount], (const char*)FindInfo.Name);
                App.Menu.SavedGameCount++;
            }
        } while (FindNextFile(&FindInfo));

        if (FindInfo.SearchHandle != 0) {
            DeleteObject(FindInfo.SearchHandle);
            FindInfo.SearchHandle = 0;
        }
    }
}

/************************************************************************/

BOOL LoadGame(const char* path) {
    char fullPath[MAX_PATH_NAME];
    FILE* file;
    long fileSize;


    if (!ResolveAppFilePath(path, fullPath, sizeof(fullPath))) {
        return FALSE;
    }

    file = fopen(fullPath, "rb");
    if (file == NULL) {
        return FALSE;
    }

    fileSize = GetFileSize(file);

    TEAM_RESOURCES res[MAX_TEAMS];
    I32 aiAttitudes[MAX_TEAMS];
    I32 aiMindsets[MAX_TEAMS];
    I32 teamCount = 1;

    U32 magic = 0;
    U32 version = 0;
    I32 mapWidth = 0;
    I32 mapHeight = 0;
    I32 difficulty = 0;
    I32 viewportX = 0;
    I32 viewportY = 0;
    U32 gameTime = 0;
    U32 lastUpdate = 0;
    I32 gameSpeed = 1;
    I32 isPaused = 0;
    I32 menuPage = 0;
    I32 showGrid = 0;
    I32 showCoordinates = 0;
    I32 isPlacing = 0;
    I32 pendingType = 0;
    I32 placementX = 0;
    I32 placementY = 0;

    memset(res, 0, sizeof(res));
    memset(aiAttitudes, 0, sizeof(aiAttitudes));
    memset(aiMindsets, 0, sizeof(aiMindsets));

    if (!ReadBlock(file, &magic, sizeof(magic), "magic", fileSize) ||
        !ReadBlock(file, &version, sizeof(version), "version", fileSize)) {
        fclose(file);
        return FALSE;
    }

    if (magic != 0x54544143 || version != SAVE_VERSION) {
        fclose(file);
        return FALSE;
    }

    if (!ReadBlock(file, &mapWidth, sizeof(mapWidth), "mapWidth", fileSize) ||
        !ReadBlock(file, &mapHeight, sizeof(mapHeight), "mapHeight", fileSize) ||
        !ReadBlock(file, &difficulty, sizeof(difficulty), "difficulty", fileSize) ||
        !ReadBlock(file, &viewportX, sizeof(viewportX), "viewportX", fileSize) ||
        !ReadBlock(file, &viewportY, sizeof(viewportY), "viewportY", fileSize)) {
        fclose(file);
        return FALSE;
    }

    if (!ReadBlock(file, &teamCount, sizeof(teamCount), "teamCount", fileSize) ||
        !ReadBlock(file, &res, sizeof(res), "resources[]", fileSize) ||
        !ReadBlock(file, &aiAttitudes, sizeof(aiAttitudes), "aiAttitudes[]", fileSize) ||
        !ReadBlock(file, &aiMindsets, sizeof(aiMindsets), "aiMindsets[]", fileSize)) {
        fclose(file);
        return FALSE;
    }

    if (!ReadBlock(file, &gameTime, sizeof(gameTime), "gameTime", fileSize) ||
        !ReadBlock(file, &lastUpdate, sizeof(lastUpdate), "lastUpdate", fileSize) ||
        !ReadBlock(file, &gameSpeed, sizeof(gameSpeed), "gameSpeed", fileSize) ||
        !ReadBlock(file, &isPaused, sizeof(isPaused), "isPaused", fileSize) ||
        !ReadBlock(file, &menuPage, sizeof(menuPage), "menuPage", fileSize) ||
        !ReadBlock(file, &showGrid, sizeof(showGrid), "showGrid", fileSize) ||
        !ReadBlock(file, &showCoordinates, sizeof(showCoordinates), "showCoordinates", fileSize) ||
        !ReadBlock(file, &isPlacing, sizeof(isPlacing), "isPlacing", fileSize) ||
        !ReadBlock(file, &pendingType, sizeof(pendingType), "pendingType", fileSize) ||
        !ReadBlock(file, &placementX, sizeof(placementX), "placementX", fileSize) ||
        !ReadBlock(file, &placementY, sizeof(placementY), "placementY", fileSize)) {
        fclose(file);
        return FALSE;
    }

    if (mapWidth < MIN_MAP_SIZE || mapWidth > MAX_MAP_SIZE ||
        mapHeight < MIN_MAP_SIZE || mapHeight > MAX_MAP_SIZE) {
        fclose(file);
        return FALSE;
    }

    if (teamCount < 1 || teamCount > MAX_TEAMS) {
        fclose(file);
        return FALSE;
    }

    GAME_STATE* newState = (GAME_STATE*)malloc(sizeof(GAME_STATE));
    if (newState == NULL) {
        fclose(file);
        return FALSE;
    }
    memset(newState, 0, sizeof(GAME_STATE));
    newState->NoiseSeed = GetSystemTime();

    GAME_STATE* oldState = App.GameState;
    App.GameState = newState;
    GameLogInit();

    if (!AllocateMap(mapWidth, mapHeight)) {
        App.GameState = newState;
        CleanupGame();
        App.GameState = oldState;
        fclose(file);
        return FALSE;
    }

    App.GameState->TeamCount = teamCount;

    for (I32 y = 0; y < mapHeight; y++) {
        char label[UI_SAVE_LABEL_SIZE];
        snprintf(label, sizeof(label), "terrain row %d/%d", y + 1, mapHeight);
        if (!ReadBlock(file, App.GameState->Terrain[y], sizeof(TERRAIN) * (size_t)mapWidth, label, fileSize)) {
            App.GameState = newState;
            CleanupGame();
            App.GameState = oldState;
            fclose(file);
            return FALSE;
        }
    }

    {
        int32_t plasmaRow[MAX_MAP_SIZE];
        for (I32 y = 0; y < mapHeight; y++) {
            char label[UI_SAVE_LABEL_SIZE];
            snprintf(label, sizeof(label), "plasma row %d/%d", y + 1, mapHeight);
            if (!ReadBlock(file, plasmaRow, sizeof(int32_t) * (size_t)mapWidth, label, fileSize)) {
                App.GameState = newState;
                CleanupGame();
                App.GameState = oldState;
                fclose(file);
                return FALSE;
            }
            for (I32 x = 0; x < mapWidth; x++) {
                App.GameState->PlasmaDensity[y][x] = (I32)plasmaRow[x];
            }
        }
    }

    U32 buildingCount = 0;
    if (!ReadBlock(file, &buildingCount, sizeof(buildingCount), "buildingCount", fileSize) || buildingCount > MAX_BUILDINGS) {
        App.GameState = newState;
        CleanupGame();
        App.GameState = oldState;
        fclose(file);
        return FALSE;
    }


    for (U32 i = 0; i < buildingCount; i++) {
        BUILDING temp;
        char label[UI_SAVE_LABEL_SIZE];
        snprintf(label, sizeof(label), "building %u/%u", i + 1, buildingCount);
        if (!ReadBlock(file, &temp, sizeof(BUILDING), label, fileSize)) {
            App.GameState = newState;
            CleanupGame();
            App.GameState = oldState;
            fclose(file);
            return FALSE;
        }
        if (GetBuildingTypeById(temp.TypeId) == NULL) {
            App.GameState = newState;
            CleanupGame();
            App.GameState = oldState;
            fclose(file);
            return FALSE;
        }
        BUILDING* node = (BUILDING*)malloc(sizeof(BUILDING));
        if (node == NULL) {
            App.GameState = newState;
            CleanupGame();
            App.GameState = oldState;
            fclose(file);
            return FALSE;
        }
        *node = temp;
        BUILDING** head = GetTeamBuildingHead(temp.Team);
        if (head == NULL) {
            App.GameState = newState;
            CleanupGame();
            App.GameState = oldState;
            free(node);
            fclose(file);
            return FALSE;
        }
        node->Next = *head;
        *head = node;
    }

    U32 unitCount = 0;
    I32 maxUnits = GetMaxUnitsForMap(mapWidth, mapHeight);
    if (!ReadBlock(file, &unitCount, sizeof(unitCount), "unitCount", fileSize) || unitCount > (U32)maxUnits) {
        App.GameState = newState;
        CleanupGame();
        App.GameState = oldState;
        fclose(file);
        return FALSE;
    }


    for (U32 i = 0; i < unitCount; i++) {
        UNIT temp;
        char label[UI_SAVE_LABEL_SIZE];
        snprintf(label, sizeof(label), "unit %u/%u", i + 1, unitCount);
        if (!ReadBlock(file, &temp, sizeof(UNIT), label, fileSize)) {
            App.GameState = newState;
            CleanupGame();
            App.GameState = oldState;
            fclose(file);
            return FALSE;
        }
        if (GetUnitTypeById(temp.TypeId) == NULL) {
            App.GameState = newState;
            CleanupGame();
            App.GameState = oldState;
            fclose(file);
            return FALSE;
        }
        UNIT* node = (UNIT*)malloc(sizeof(UNIT));
        if (node == NULL) {
            App.GameState = newState;
            CleanupGame();
            App.GameState = oldState;
            fclose(file);
            return FALSE;
        }
        *node = temp;
        node->MoveProgress = 0;
        node->PathHead = NULL;
        node->PathTail = NULL;
        node->PathTargetX = node->TargetX;
        node->PathTargetY = node->TargetY;
        UNIT** head = GetTeamUnitHead(temp.Team);
        if (head == NULL) {
            App.GameState = newState;
            CleanupGame();
            App.GameState = oldState;
            free(node);
            fclose(file);
            return FALSE;
        }
        node->Next = *head;
        *head = node;
    }

    fclose(file);

    App.GameState->MapWidth = mapWidth;
    App.GameState->MapHeight = mapHeight;
    App.GameState->MapMaxDim = imax(mapWidth, mapHeight);
    App.GameState->Difficulty = difficulty;
    InitializeAiConstants();
    App.GameState->ViewportPos.X = viewportX;
    App.GameState->ViewportPos.Y = viewportY;
    App.GameState->TeamCount = teamCount;
    if (!EnsureTeamMemoryBuffers(App.GameState->MapWidth, App.GameState->MapHeight, App.GameState->TeamCount)) {
        App.GameState = newState;
        CleanupGame();
        App.GameState = oldState;
        return FALSE;
    }
    for (I32 i = 0; i < MAX_TEAMS; i++) {
        App.GameState->TeamData[i].Resources = res[i];
        I32 attitude = aiAttitudes[i];
        I32 mindset = aiMindsets[i];
        if (attitude != AI_ATTITUDE_AGGRESSIVE && attitude != AI_ATTITUDE_DEFENSIVE) {
            attitude = (RandomFloat() > 0.5f) ? AI_ATTITUDE_AGGRESSIVE : AI_ATTITUDE_DEFENSIVE;
        }
        if (mindset != AI_MINDSET_IDLE && mindset != AI_MINDSET_URGENCY && mindset != AI_MINDSET_PANIC) {
            mindset = AI_MINDSET_IDLE;
        }
            App.GameState->TeamData[i].AiAttitude = attitude;
            App.GameState->TeamData[i].AiMindset = mindset;
            App.GameState->TeamData[i].AiLastClusterUpdate = 0;
            App.GameState->TeamData[i].AiLastShuffleTime = 0;
        }
    App.GameState->GameTime = gameTime;
    App.GameState->LastUpdate = GetSystemTime();
    App.GameState->GameSpeed = gameSpeed;
    App.GameState->IsPaused = (isPaused != 0);
    App.GameState->IsPlacingBuilding = (isPlacing != 0);
    App.GameState->PendingBuildingTypeId = pendingType;
    App.GameState->PlacementX = placementX;
    App.GameState->PlacementY = placementY;
    App.GameState->PlacingFromQueue = FALSE;
    App.GameState->PendingQueueIndex = -1;
    App.GameState->ProductionMenuActive = FALSE;
    if (App.GameState->IsPlacingBuilding && GetBuildingTypeById(App.GameState->PendingBuildingTypeId) == NULL) {
        App.GameState->IsPlacingBuilding = FALSE;
        App.GameState->PendingBuildingTypeId = 0;
    }

    {
        I32 maxUnitId = 0;
        I32 maxBuildingId = 0;
        for (I32 team = 0; team < App.GameState->TeamCount; team++) {
            BUILDING* building = App.GameState->TeamData[team].Buildings;
            while (building != NULL) {
                if (building->Id > maxBuildingId) maxBuildingId = building->Id;
                building = building->Next;
            }
            UNIT* unit = App.GameState->TeamData[team].Units;
            while (unit != NULL) {
                if (unit->Id > maxUnitId) maxUnitId = unit->Id;
                unit = unit->Next;
            }
        }
        App.GameState->NextUnitId = maxUnitId + 1;
        App.GameState->NextBuildingId = maxBuildingId + 1;
    }

    RebuildOccupancy();
    RecalculateEnergy();
    App.GameState->IsRunning = TRUE;
    App.GameState->MenuPage = menuPage;
    App.GameState->ShowGrid = (showGrid != 0);
    App.GameState->ShowCoordinates = (showCoordinates != 0);
    App.GameState->SeeEverything = FALSE;
    App.GameState->LastFogUpdate = 0;
    App.GameState->FogDirty = TRUE;
    App.GameState->SelectedUnit = NULL;
    App.GameState->SelectedBuilding = NULL;
    App.GameState->ProductionMenuActive = FALSE;
    App.GameState->PlacingFromQueue = FALSE;
    App.GameState->PendingQueueIndex = -1;
    App.GameState->IsCommandMode = FALSE;
    App.GameState->CommandType = COMMAND_NONE;
    App.GameState->CommandX = 0;
    App.GameState->CommandY = 0;
    App.Render.BorderDrawn = FALSE;
    ResetRenderCache();

    if (oldState != NULL) {
        App.GameState = oldState;
        CleanupGame();
    }

    App.GameState = newState;
    return TRUE;
}
