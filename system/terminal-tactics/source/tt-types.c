
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

#include "tt-types.h"

/************************************************************************/

APP_STATE App = {
    .Menu = {
        .CurrentMenu = MENU_MAIN,
        .SelectedOption = 0,
        .MenuPage = 0,
        .ExitRequested = FALSE,
        .PrevMenu = -1,
        .PendingMapWidth = DEFAULT_MAP_SIZE,
        .PendingMapHeight = DEFAULT_MAP_SIZE,
        .PendingDifficulty = DIFFICULTY_NORMAL,
        .PendingTeamCount = 2,
        .SaveFileName = "terminal-tactics.sav",
        .SavedGameCount = 0,
        .SelectedSaveIndex = 0
    },
    .Render = {
        .ViewBlitInfo = {0, TOP_BAR_HEIGHT, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, NULL, CONSOLE_WHITE, CONSOLE_BLACK, VIEWPORT_WIDTH + 1, NULL, VIEWPORT_WIDTH},
        .BorderDrawn = FALSE,
        .MainMenuDrawn = FALSE,
        .CachedNGWidth = -1,
        .CachedNGHeight = -1,
        .CachedNGDifficulty = -1,
        .CachedNGTeams = -1,
        .CachedNGSelection = -1,
        .CachedLoadSelected = -1,
        .CachedLoadCount = -1,
        .DebugDrawn = FALSE
    },
    .Input = {
        .LastKeyVK = -1,
        .LastKeyASCII = -1,
        .LastKeyModifiers = 0
    }
};

const U8 TeamColors[MAX_TEAMS] = {
    CONSOLE_LIGHT_BLUE,
    CONSOLE_RED,
    CONSOLE_YELLOW,
    CONSOLE_MAGENTA,
    CONSOLE_GREEN
};

const BUILDING_TYPE BuildingTypes[] = {
    {
        .Id = BUILDING_TYPE_CONSTRUCTION_YARD,
        .Symbol = 'A',
        .Name = "Construction Yard",
        .Icon = "o---o\n| C |\no---o",
        .Width = 5,
        .Height = 3,
        .MaxHp = 500,
        .Armor = 10,
        .CostPlasma = 1000,
        .EnergyConsumption = 0,
        .EnergyProduction = 50,
        .TechLevel = 1,
        .BuildTime = 45000
    },
    {
        .Id = BUILDING_TYPE_BARRACKS,
        .Symbol = 'B',
        .Name = "Barracks",
        .Icon = "+---+\n| B |\n+---+",
        .Width = 5,
        .Height = 3,
        .MaxHp = 400,
        .Armor = 10,
        .CostPlasma = 500,
        .EnergyConsumption = 25,
        .EnergyProduction = 0,
        .TechLevel = 1,
        .BuildTime = 25000
    },
    {
        .Id = BUILDING_TYPE_POWER_PLANT,
        .Symbol = 'P',
        .Name = "Power Plant",
        .Icon = "/+\\\n+P+\n\\+/",
        .Width = 3,
        .Height = 3,
        .MaxHp = 600,
        .Armor = 10,
        .CostPlasma = 800,
        .EnergyConsumption = 0,
        .EnergyProduction = 100,
        .TechLevel = 1,
        .BuildTime = 30000
    },
    {
        .Id = BUILDING_TYPE_FACTORY,
        .Symbol = 'F',
        .Name = "Factory",
        .Icon = ".:::.\n: F :\n.:::.",
        .Width = 5,
        .Height = 3,
        .MaxHp = 800,
        .Armor = 10,
        .CostPlasma = 1000,
        .EnergyConsumption = 150,
        .EnergyProduction = 0,
        .TechLevel = 1,
        .BuildTime = 40000
    },
    {
        .Id = BUILDING_TYPE_TECH_CENTER,
        .Symbol = 'T',
        .Name = "Tech Center",
        .Icon = "|---|\n| T |\n|---|",
        .Width = 5,
        .Height = 3,
        .MaxHp = 1000,
        .Armor = 10,
        .CostPlasma = 1500,
        .EnergyConsumption = 200,
        .EnergyProduction = 0,
        .TechLevel = 1,
        .BuildTime = 55000
    },
    {
        .Id = BUILDING_TYPE_TURRET,
        .Symbol = 'U',
        .Name = "Turret",
        .Icon = "<o>\n |",
        .Width = 3,
        .Height = 2,
        .MaxHp = 600,
        .Armor = 10,
        .CostPlasma = 500,
        .EnergyConsumption = 25,
        .EnergyProduction = 0,
        .TechLevel = 2,
        .BuildTime = 20000
    },
    {
        .Id = BUILDING_TYPE_WALL,
        .Symbol = '#',
        .Name = "Wall",
        .Icon = "#",
        .Width = 1,
        .Height = 1,
        .MaxHp = 200,
        .Armor = 10,
        .CostPlasma = 25,
        .EnergyConsumption = 0,
        .EnergyProduction = 0,
        .TechLevel = 2,
        .BuildTime = 2000
    }
};

const UNIT_TYPE UnitTypes[] = {
    {
        .Id = UNIT_TYPE_TROOPER,
        .Symbol = 't',
        .Name = "Trooper",
        .Icon = "__\n/\\",
        .Width = 2,
        .Height = 2,
        .MaxHp = 100,
        .Speed = 3,
        .Damage = 10,
        .Range = 1,
        .Sight = 5,
        .MoveTimeMs = 2000,
        .CostPlasma = 50,
        .Armor = 5,
        .TechLevel = 1,
        .BuildTime = 10000,
        .AttackSpeed = 1000
    },
    {
        .Id = UNIT_TYPE_SOLDIER,
        .Symbol = 's',
        .Name = "Soldier",
        .Icon = "o|\n/\\",
        .Width = 2,
        .Height = 2,
        .MaxHp = 150,
        .Speed = 3,
        .Damage = 20,
        .Range = 1,
        .Sight = 5,
        .MoveTimeMs = 2000,
        .CostPlasma = 120,
        .Armor = 5,
        .TechLevel = 1,
        .BuildTime = 20000,
        .AttackSpeed = 1000
    },
    {
        .Id = UNIT_TYPE_ENGINEER,
        .Symbol = 'e',
        .Name = "Engineer",
        .Icon = "--\n/\\",
        .Width = 2,
        .Height = 2,
        .MaxHp = 120,
        .Speed = 2,
        .Damage = 5,
        .Range = 1,
        .Sight = 4,
        .MoveTimeMs = 2000,
        .CostPlasma = 200,
        .Armor = 5,
        .TechLevel = 1,
        .BuildTime = 25000,
        .AttackSpeed = 1000
    },
    {
        .Id = UNIT_TYPE_SCOUT,
        .Symbol = 'c',
        .Name = "Scout",
        .Icon = "oo\n/\\",
        .Width = 2,
        .Height = 2,
        .MaxHp = 90,
        .Speed = 4,
        .Damage = 5,
        .Range = 1,
        .Sight = 6,
        .MoveTimeMs = 1000,
        .CostPlasma = 80,
        .Armor = 5,
        .TechLevel = 1,
        .BuildTime = 10000,
        .AttackSpeed = 1000
    },
    {
        .Id = UNIT_TYPE_MOBILE_ARTILLERY,
        .Symbol = 'a',
        .Name = "Mobile Artillery",
        .Icon = " ||\n[==]",
        .Width = 4,
        .Height = 2,
        .MaxHp = 350,
        .Speed = 1,
        .Damage = 40,
        .Range = 4,
        .Sight = 4,
        .MoveTimeMs = 2000,
        .CostPlasma = 300,
        .Armor = 15,
        .TechLevel = 2,
        .BuildTime = 60000,
        .AttackSpeed = 1000
    },
    {
        .Id = UNIT_TYPE_TANK,
        .Symbol = 'k',
        .Name = "Tank",
        .Icon = " /o\\\n[===]",
        .Width = 5,
        .Height = 2,
        .MaxHp = 500,
        .Speed = 2,
        .Damage = 10,
        .Range = 3,
        .Sight = 3,
        .MoveTimeMs = 2000,
        .CostPlasma = 400,
        .Armor = 30,
        .TechLevel = 2,
        .BuildTime = 90000,
        .AttackSpeed = 1000
    },
    {
        .Id = UNIT_TYPE_TRANSPORT,
        .Symbol = 'x',
        .Name = "Transport",
        .Icon = "/==\\\n|  |",
        .Width = 4,
        .Height = 2,
        .MaxHp = 300,
        .Speed = 3,
        .Damage = 10,
        .Range = 1,
        .Sight = 4,
        .MoveTimeMs = 2000,
        .CostPlasma = 200,
        .Armor = 10,
        .TechLevel = 1,
        .BuildTime = 120000,
        .AttackSpeed = 1000
    },
    {
        .Id = UNIT_TYPE_DRILLER,
        .Symbol = 'd',
        .Name = "Driller",
        .Icon = "[###]\nvvvvv",
        .Width = 5,
        .Height = 2,
        .MaxHp = 400,
        .Speed = 1,
        .Damage = 0,
        .Range = 1,
        .Sight = 3,
        .MoveTimeMs = 2000,
        .CostPlasma = 600,
        .Armor = 10,
        .TechLevel = 1,
        .BuildTime = 30000,
        .AttackSpeed = 1000
    }
};

/************************************************************************/

U8 MakeAttr(U8 fore, U8 back) {
    return (U8)((fore & 0x0F) | ((back & 0x0F) << 4));
}

/************************************************************************/

I32 abs(I32 x) {
    if (x >= 0)
        return x;
    return -x;
}

/************************************************************************/

U32 SimpleRandom(void) {
    if (App.GameState == NULL) return 0;
    App.GameState->NoiseSeed = App.GameState->NoiseSeed * 1103515245 + 12345;
    return App.GameState->NoiseSeed;
}

/************************************************************************/

float RandomFloat(void) {
    return (float)SimpleRandom() / 4294967295.0f;
}

/************************************************************************/

U32 RandomIndex(U32 max) {
    if (max == 0) return 0;
    return SimpleRandom() % max;
}

/************************************************************************/

I32 GetTeamCountSafe(void) {
    if (App.GameState == NULL) return 0;
    if (App.GameState->TeamCount <= 0 || App.GameState->TeamCount > MAX_TEAMS) {
        return MAX_TEAMS;
    }
    return App.GameState->TeamCount;
}

/************************************************************************/

I32 GetMaxUnitSight(void) {
    I32 maxSight = 0;
    for (I32 i = 0; i < UNIT_TYPE_COUNT; i++) {
        I32 sight = UnitTypes[i].Sight;
        if (sight > maxSight) {
            maxSight = sight;
        }
    }
    if (maxSight <= 0) maxSight = 1;
    return maxSight;
}

/************************************************************************/

void CopyName(char* destination, const char* source) {
    if (destination == NULL) return;
    if (source == NULL) {
        destination[0] = '\0';
        return;
    }
    snprintf(destination, NAME_MAX_LENGTH, "%s", source);
}

/************************************************************************/

/* No memory cell encode/decode: MemoryMap stores named fields directly. */
