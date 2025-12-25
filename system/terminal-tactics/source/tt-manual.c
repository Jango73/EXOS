
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

#include "tt-manual.h"

/************************************************************************/

const char* TerminalTacticsManual =
"Overview\n"
"Terminal Tactics is a real-time strategy game played on a wraparound map.\n"
"Every team follows the same rules; only the decision logic differs for AI.\n"
"Your goal is to eliminate enemy teams by destroying their Construction Yards\n"
"or by exhausting their plasma and drillers.\n"
"\n"
"Resources\n"
"- Plasma: main currency used to build structures and units.\n"
"- Energy: produced by Power Plants, consumed by most buildings. Unpowered\n"
"  buildings are disabled until energy balance is positive.\n"
"\n"
"Map and Visibility\n"
"- The map wraps on all edges; moving past one side appears on the other.\n"
"- Fog of war hides unexplored tiles.\n"
"\n"
"Selection and Orders\n"
"- Select units/buildings on screen with selection cycling or hotkeys.\n"
"- Units accept Move, Attack, Escort, and Explore commands.\n"
"- Escort targets a friendly unit; Explore sends a unit to scout or, for a\n"
"  Driller, to the nearest plasma field.\n"
"\n"
"Construction and Production\n"
"- Buildings take time to finish after placement.\n"
"- Buildings with production queues can queue up to three items.\n"
"- Cancel a queued production with Delete.\n"
"- Confirm building placement with P, cancel with Escape.\n"
"\n"
"Menu Levels\n"
"- Units: Level 1 shows available orders; Level 2 confirms the order on the map.\n"
"- Buildings: Level 1 shows actions; Level 2 selects the unit type to produce.\n"
"\n"
"Buildings\n"
"- Construction Yard: core base, produces structures and provides energy.\n"
"- Barracks: produces infantry.\n"
"- Power Plant: generates energy for your base.\n"
"- Factory: produces vehicles (tech level 2 units).\n"
"- Tech Center: unlocks tech level 2 options.\n"
"- Turret: static defense, attacks nearby enemies (range 3, damage 10, speed 1000ms).\n"
"- Wall: basic defensive segment.\n"
"\n"
"Units\n"
"- Trooper: basic infantry for early offense and defense.\n"
"- Soldier: tougher infantry with higher damage.\n"
"- Engineer: repairs buildings.\n"
"- Scout: fast unit for exploration and spotting.\n"
"- Mobile Artillery: long-range vehicle with high damage.\n"
"- Tank: armored vehicle for frontline fighting.\n"
"- Transport: utility vehicle for movement and support.\n"
"- Driller: harvests plasma from nearby fields.\n"
"- Attack speed: 1000ms for all units.\n";

/************************************************************************/

/// @brief Return the total number of lines in the manual.
I32 GetManualLineCount(void) {
    const char* manual = TerminalTacticsManual;
    I32 count = 0;

    if (manual == NULL || manual[0] == '\0') return 0;

    count = 1;
    for (const char* p = manual; *p != '\0'; p++) {
        if (*p == '\n') count++;
    }

    return count;
}

/************************************************************************/

/// @brief Return the maximum scroll offset for a given visible line count.
I32 GetManualScrollMax(I32 visibleLines) {
    I32 totalLines = GetManualLineCount();

    if (visibleLines <= 0) return 0;
    if (totalLines <= visibleLines) return 0;
    return totalLines - visibleLines;
}

/************************************************************************/

/// @brief Return the start and length of a manual line by index (0-based).
BOOL GetManualLineSpan(I32 lineIndex, const char** outStart, I32* outLength) {
    const char* manual = TerminalTacticsManual;
    const char* lineStart;
    const char* p;
    I32 current = 0;

    if (manual == NULL) return FALSE;
    if (lineIndex < 0) return FALSE;

    lineStart = manual;
    p = manual;
    while (*p != '\0') {
        if (*p == '\n') {
            if (current == lineIndex) {
                if (outStart != NULL) *outStart = lineStart;
                if (outLength != NULL) *outLength = (I32)(p - lineStart);
                return TRUE;
            }
            current++;
            lineStart = p + 1;
        }
        p++;
    }

    if (current == lineIndex) {
        if (outStart != NULL) *outStart = lineStart;
        if (outLength != NULL) *outLength = (I32)(p - lineStart);
        return TRUE;
    }

    return FALSE;
}
