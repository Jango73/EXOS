/************************************************************************\

    EXOS Kernel
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


    Console regions and layout

\************************************************************************/

#include "Console-Internal.h"
#include "Heap.h"
#include "Kernel.h"
#include "Memory.h"
#include "Mutex.h"
#include "CoreString.h"
#include "drivers/input/Keyboard.h"
#include "input/VKey.h"
#include "process/Process-Control.h"
#include "process/Process.h"
#include "process/Task.h"
#include "System.h"

/***************************************************************************/

typedef struct tag_CONSOLE_ACTIVE_REGION_SNAPSHOT {
    BOOL IsValid;
    BOOL IsFramebuffer;
    U32 RegionX;
    U32 RegionY;
    U32 RegionWidth;
    U32 RegionHeight;
    U32 CursorX;
    U32 CursorY;
    U32 ForeColor;
    U32 BackColor;
    U32 Blink;
    U32 TextCellCount;
    U16* TextBuffer;
    U32 FramebufferSize;
    U32 FramebufferRowBytes;
    U32 FramebufferPixelX;
    U32 FramebufferPixelY;
    U32 FramebufferPixelHeight;
    U8* FramebufferBuffer;
} CONSOLE_ACTIVE_REGION_SNAPSHOT, *LPCONSOLE_ACTIVE_REGION_SNAPSHOT;

/***************************************************************************/

/**
 * @brief Capture the active console region as a reusable snapshot.
 * @param OutSnapshot Receives an opaque snapshot pointer.
 * @return TRUE on success.
 */
BOOL ConsoleCaptureActiveRegionSnapshot(LPVOID* OutSnapshot) {
    UNUSED(OutSnapshot);
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Restore a previously captured active console region snapshot.
 * @param Snapshot Opaque snapshot pointer.
 * @return TRUE on success.
 */
BOOL ConsoleRestoreActiveRegionSnapshot(LPVOID Snapshot) {
    UNUSED(Snapshot);
    return FALSE;
}

/***************************************************************************/

/**
 * @brief Release a snapshot created by ConsoleCaptureActiveRegionSnapshot.
 * @param Snapshot Opaque snapshot pointer.
 */
void ConsoleReleaseActiveRegionSnapshot(LPVOID Snapshot) {
    LPCONSOLE_ACTIVE_REGION_SNAPSHOT State = (LPCONSOLE_ACTIVE_REGION_SNAPSHOT)Snapshot;

    if (State == NULL) {
        return;
    }

    SAFE_USE(State->TextBuffer) { KernelHeapFree(State->TextBuffer); }
    SAFE_USE(State->FramebufferBuffer) { KernelHeapFree(State->FramebufferBuffer); }
    KernelHeapFree(State);
}

/***************************************************************************/

/**
 * @brief Resolve a console region into a mutable state descriptor.
 *
 * Provides pointers to the mutable fields for either the standard region
 * or a dedicated region, and copies the immutable layout data.
 *
 * @param Index Region index.
 * @param State Output state descriptor.
 * @return TRUE on success, FALSE on invalid index.
 */
BOOL ConsoleResolveRegionState(U32 Index, LPCONSOLE_REGION_STATE State) {
    LPCONSOLE_REGION Region;

    if (Index >= Console.RegionCount) return FALSE;

    Region = &Console.Regions[Index];

    State->X = Region->X;
    State->Y = Region->Y;
    State->Width = Region->Width;
    State->Height = Region->Height;

    if (Index == 0) {
        State->CursorX = &Console.CursorX;
        State->CursorY = &Console.CursorY;
        State->ForeColor = &Console.ForeColor;
        State->BackColor = &Console.BackColor;
        State->Blink = &Console.Blink;
        State->PagingEnabled = &Console.PagingEnabled;
        State->PagingActive = &Console.PagingActive;
        State->PagingRemaining = &Console.PagingRemaining;
    } else {
        State->CursorX = &Region->CursorX;
        State->CursorY = &Region->CursorY;
        State->ForeColor = &Region->ForeColor;
        State->BackColor = &Region->BackColor;
        State->Blink = &Region->Blink;
        State->PagingEnabled = &Region->PagingEnabled;
        State->PagingActive = &Region->PagingActive;
        State->PagingRemaining = &Region->PagingRemaining;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Initialize a console region layout and runtime fields.
 *
 * @param Index Region index.
 * @param X Left coordinate in screen space.
 * @param Y Top coordinate in screen space.
 * @param Width Region width.
 * @param Height Region height.
 */
static void ConsoleInitializeRegion(U32 Index, U32 X, U32 Y, U32 Width, U32 Height) {
    LPCONSOLE_REGION Region;

    if (Index >= MAX_CONSOLE_REGIONS) return;

    Region = &Console.Regions[Index];
    Region->X = X;
    Region->Y = Y;
    Region->Width = Width;
    Region->Height = Height;
    Region->CursorX = 0;
    Region->CursorY = 0;
    Region->ForeColor = Console.ForeColor;
    Region->BackColor = Console.BackColor;
    Region->Blink = Console.Blink;
    Region->PagingEnabled = FALSE;
    Region->PagingActive = FALSE;
    Region->PagingRemaining = 0;
}

/***************************************************************************/

/**
 * @brief Build a grid of console regions.
 *
 * Regions are laid out in row-major order. The first columns and rows
 * receive any extra width or height if the screen does not divide evenly.
 *
 * @param Columns Number of columns.
 * @param Rows Number of rows.
 */
static void ConsoleConfigureRegions(U32 Columns, U32 Rows) {
    U32 EffectiveColumns;
    U32 EffectiveRows;
    U32 Index;
    U32 Row;
    U32 Column;
    U32 BaseWidth;
    U32 BaseHeight;
    U32 ExtraWidth;
    U32 ExtraHeight;
    U32 CursorX;
    U32 CursorY;

    EffectiveColumns = (Columns == 0) ? 1 : Columns;
    EffectiveRows = (Rows == 0) ? 1 : Rows;

    while ((EffectiveColumns * EffectiveRows) > MAX_CONSOLE_REGIONS) {
        if (EffectiveColumns >= EffectiveRows && EffectiveColumns > 1) {
            EffectiveColumns--;
        } else if (EffectiveRows > 1) {
            EffectiveRows--;
        } else {
            break;
        }
    }

    Console.RegionCount = EffectiveColumns * EffectiveRows;

    BaseWidth = Console.ScreenWidth / EffectiveColumns;
    ExtraWidth = Console.ScreenWidth % EffectiveColumns;
    BaseHeight = Console.ScreenHeight / EffectiveRows;
    ExtraHeight = Console.ScreenHeight % EffectiveRows;

    Index = 0;
    CursorY = 0;
    for (Row = 0; Row < EffectiveRows; Row++) {
        U32 RegionHeight = BaseHeight + ((Row < ExtraHeight) ? 1 : 0);
        CursorX = 0;
        for (Column = 0; Column < EffectiveColumns; Column++) {
            U32 RegionWidth = BaseWidth + ((Column < ExtraWidth) ? 1 : 0);
            ConsoleInitializeRegion(Index, CursorX, CursorY, RegionWidth, RegionHeight);
            CursorX += RegionWidth;
            Index++;
        }
        CursorY += RegionHeight;
    }
}

/***************************************************************************/

/**
 * @brief Apply the console region layout based on build configuration.
 */
void ConsoleApplyLayout(void) {
#if DEBUG_SPLIT == 1
    ConsoleConfigureRegions(2, 1);
    Console.DebugRegion = (Console.RegionCount > 1) ? 1 : 0;
#else
    ConsoleConfigureRegions(1, 1);
    Console.DebugRegion = 0;
#endif

    Console.ActiveRegion = 0;
    Console.Width = Console.Regions[0].Width;
    Console.Height = Console.Regions[0].Height;
}

/***************************************************************************/

/**
 * @brief Clamp the standard console cursor to its region bounds.
 */
void ConsoleClampCursorToRegionZero(void) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(0, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) {
        Console.CursorX = 0;
        Console.CursorY = 0;
        return;
    }

    if (Console.CursorY >= State.Height) {
        Console.CursorY = State.Height - 1;
    }

    if (Console.CursorX >= State.Width) {
        Console.CursorX = 0;
        if ((Console.CursorY + 1) < State.Height) {
            Console.CursorY++;
        }
    }
}

/***************************************************************************/

/**
 * @brief Returns TRUE when the debug split is enabled.
 * @return TRUE if the debug region is active, FALSE otherwise.
 */
BOOL ConsoleIsDebugSplitEnabled(void) {
#if DEBUG_SPLIT == 1
    return (Console.RegionCount > 1 && Console.DebugRegion < Console.RegionCount) ? TRUE : FALSE;
#else
    return FALSE;
#endif
}

/***************************************************************************/

/**
 * @brief Determine whether a keycode requests cooperative interruption.
 *
 * @param KeyCode Keycode to evaluate.
 * @return TRUE when keycode corresponds to Control+C, FALSE otherwise.
 */
static BOOL ConsoleIsInterruptKey(LPKEYCODE KeyCode) {
    U32 Modifiers;

    if (KeyCode == NULL) {
        return FALSE;
    }

    if ((U8)KeyCode->ASCIICode == 0x03) {
        return TRUE;
    }

    if (KeyCode->VirtualKey != VK_C) {
        return FALSE;
    }

    Modifiers = GetKeyModifiers();
    return (Modifiers & KEYMOD_CONTROL) != 0;
}

/***************************************************************************/

/**
 * @brief Show the console paging prompt for a specific region.
 * @param RegionIndex Region index.
 */
static void ConsolePagerWaitLockedRegion(U32 RegionIndex) {
    CONSOLE_REGION_STATE State;
    LPPROCESS CurrentProcess;
    LPTASK CurrentTask;
    KEYCODE KeyCode;
    U32 WaitLoops;
    U32 ReleasedConsoleLocks;
    BOOL ExitByInterrupt;
    U32 Row;
    U32 Column;
    LPCSTR Prompt = TEXT("-- Press a key --");
    U32 PromptLen;
    U32 Start;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if ((*State.PagingEnabled) == FALSE || (*State.PagingActive) == FALSE) return;
    if (State.Width == 0 || State.Height < 2) return;
    if (ConsoleEnsureFramebufferMapped() == FALSE) return;

    Row = State.Height - 1;

    for (Column = 0; Column < State.Width; Column++) {
        U32 PixelX = (State.X + Column) * ConsoleGetCellWidth();
        U32 PixelY = (State.Y + Row) * ConsoleGetCellHeight();
        ConsoleDrawGlyph(PixelX, PixelY, STR_SPACE);
    }

    PromptLen = StringLength(Prompt);
    if (PromptLen > State.Width) PromptLen = State.Width;
    Start = (State.Width > PromptLen) ? (State.Width - PromptLen) / 2 : 0;
    for (Column = 0; Column < PromptLen; Column++) {
        U32 PixelX = (State.X + Start + Column) * ConsoleGetCellWidth();
        U32 PixelY = (State.Y + Row) * ConsoleGetCellHeight();
        ConsoleDrawGlyph(PixelX, PixelY, Prompt[Column]);
    }

    SetConsoleCursorPosition(0, Row);

    CurrentProcess = GetCurrentProcess();
    CurrentTask = GetCurrentTask();
    WaitLoops = 0;
    ReleasedConsoleLocks = 0;
    ExitByInterrupt = FALSE;

    // Release all recursive console mutex holds while waiting for input.
    while (CurrentTask != NULL && ConsoleStateMutex.Task == CurrentTask && ConsoleStateMutex.Lock > 0) {
        if (UnlockMutex(MUTEX_CONSOLE_STATE) == FALSE) {
            break;
        }

        ReleasedConsoleLocks++;
    }

    while (TRUE) {
        if (CurrentProcess != NULL && ProcessControlIsInterruptRequested(CurrentProcess)) {
            (*State.PagingRemaining) = (State.Height > 0) ? (State.Height - 1) : 0;
            ExitByInterrupt = TRUE;
            break;
        }

        if (PeekChar()) {
            GetKeyCode(&KeyCode);

            if (KeyCode.VirtualKey == VK_ESCAPE) {
                (*State.PagingRemaining) = State.Height - 1;
                break;
            }

            if (ConsoleIsInterruptKey(&KeyCode)) {
                if (CurrentProcess != NULL) {
                    ProcessControlRequestInterrupt(CurrentProcess);
                }
                (*State.PagingRemaining) = (State.Height > 0) ? (State.Height - 1) : 0;
                ExitByInterrupt = TRUE;
                break;
            }

            (*State.PagingRemaining) = State.Height - 1;
            break;
        }

        Sleep(10);
        WaitLoops++;
    }

    if (ReleasedConsoleLocks == 0) {
        LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
    } else {
        for (U32 LockIndex = 0; LockIndex < ReleasedConsoleLocks; LockIndex++) {
            LockMutex(MUTEX_CONSOLE_STATE, INFINITY);
        }
    }

    for (Column = 0; Column < State.Width; Column++) {
        U32 PixelX = (State.X + Column) * ConsoleGetCellWidth();
        U32 PixelY = (State.Y + Row) * ConsoleGetCellHeight();
        ConsoleDrawGlyph(PixelX, PixelY, STR_SPACE);
    }

    UNUSED(ExitByInterrupt);
    UNUSED(WaitLoops);
}

/***************************************************************************/

/**
 * @brief Write a character at the current cursor position inside a region.
 *
 * @param RegionIndex Region index.
 * @param Char Character to write.
 */
static void ConsoleSetCharacterRegion(U32 RegionIndex, STR Char) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if ((*State.CursorX) >= State.Width || (*State.CursorY) >= State.Height) return;
    if (ConsoleEnsureFramebufferMapped() == FALSE) return;

    {
        U32 PixelX = (State.X + (*State.CursorX)) * ConsoleGetCellWidth();
        U32 PixelY = (State.Y + (*State.CursorY)) * ConsoleGetCellHeight();
        ConsoleDrawGlyph(PixelX, PixelY, Char);
    }
}

/***************************************************************************/

/**
 * @brief Scroll a region up by one line.
 * @param RegionIndex Region index.
 */
void ConsoleScrollRegion(U32 RegionIndex) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) return;

    if ((*State.PagingRemaining) == 0) {
        ConsolePagerWaitLockedRegion(RegionIndex);
    } else {
        (*State.PagingRemaining)--;
    }

    ConsoleScrollRegionFramebuffer(RegionIndex);
}

/***************************************************************************/

/**
 * @brief Clear a region and reset its cursor.
 * @param RegionIndex Region index.
 */
void ConsoleClearRegion(U32 RegionIndex) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) return;

    ConsoleClearRegionFramebuffer(RegionIndex);
    (*State.CursorX) = 0;
    (*State.CursorY) = 0;
}

/***************************************************************************/

/**
 * @brief Print a character into a region and update its cursor.
 * @param RegionIndex Region index.
 * @param Char Character to print.
 */
void ConsolePrintCharRegion(U32 RegionIndex, STR Char) {
    CONSOLE_REGION_STATE State;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) return;
    if (ConsoleEnsureFramebufferMapped() == FALSE) return;

    if (Char == STR_NEWLINE) {
        (*State.CursorX) = 0;
        (*State.CursorY)++;
        if ((*State.CursorY) >= State.Height) {
            ConsoleScrollRegion(RegionIndex);
            (*State.CursorY) = State.Height - 1;
        }
        if (RegionIndex == 0) {
            SetConsoleCursorPosition(*State.CursorX, *State.CursorY);
        }
        return;
    }

    if (Char == STR_RETURN) {
        return;
    }

    if (Char == STR_TAB) {
        (*State.CursorX) += 4;
        if ((*State.CursorX) >= State.Width) {
            (*State.CursorX) = 0;
            (*State.CursorY)++;
            if ((*State.CursorY) >= State.Height) {
                ConsoleScrollRegion(RegionIndex);
                (*State.CursorY) = State.Height - 1;
            }
        }
        if (RegionIndex == 0) {
            SetConsoleCursorPosition(*State.CursorX, *State.CursorY);
        }
        return;
    }

    ConsoleSetCharacterRegion(RegionIndex, Char);
    (*State.CursorX)++;
    if ((*State.CursorX) >= State.Width) {
        (*State.CursorX) = 0;
        (*State.CursorY)++;
        if ((*State.CursorY) >= State.Height) {
            ConsoleScrollRegion(RegionIndex);
            (*State.CursorY) = State.Height - 1;
        }
    }

    if (RegionIndex == 0) {
        SetConsoleCursorPosition(*State.CursorX, *State.CursorY);
    }
}
