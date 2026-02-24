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
#include "drivers/Keyboard.h"
#include "VKey.h"
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
    LPCONSOLE_ACTIVE_REGION_SNAPSHOT Snapshot;
    U32 CellCount;
    U32 CellBytes;
    U32 CellBytesPerRow;
    U32 RegionOffset;
    U32 CellWidth;
    U32 CellHeight;
    U32 BytesPerPixel;
    U32 FramebufferOffset;

    if (OutSnapshot == NULL) {
        return FALSE;
    }

    *OutSnapshot = NULL;

    Snapshot = (LPCONSOLE_ACTIVE_REGION_SNAPSHOT)KernelHeapAlloc(sizeof(CONSOLE_ACTIVE_REGION_SNAPSHOT));
    if (Snapshot == NULL) {
        return FALSE;
    }

    MemorySet(Snapshot, 0, sizeof(CONSOLE_ACTIVE_REGION_SNAPSHOT));

    LockMutex(MUTEX_CONSOLE, INFINITY);

    Snapshot->CursorX = Console.CursorX;
    Snapshot->CursorY = Console.CursorY;
    Snapshot->ForeColor = Console.ForeColor;
    Snapshot->BackColor = Console.BackColor;
    Snapshot->Blink = Console.Blink;
    Snapshot->IsFramebuffer = Console.UseFramebuffer ? TRUE : FALSE;
    Snapshot->RegionX = Console.Regions[0].X;
    Snapshot->RegionY = Console.Regions[0].Y;
    Snapshot->RegionWidth = Console.Regions[0].Width;
    Snapshot->RegionHeight = Console.Regions[0].Height;

    if (Snapshot->IsFramebuffer == FALSE) {
        CellCount = Snapshot->RegionWidth * Snapshot->RegionHeight;
        CellBytes = CellCount * sizeof(U16);
        CellBytesPerRow = Snapshot->RegionWidth * sizeof(U16);
        Snapshot->TextBuffer = (U16*)KernelHeapAlloc(CellBytes);
        if (Snapshot->TextBuffer != NULL) {
            for (U32 Row = 0; Row < Snapshot->RegionHeight; Row++) {
                RegionOffset = ((Snapshot->RegionY + Row) * Console.ScreenWidth) + Snapshot->RegionX;
                MemoryCopy(
                    Snapshot->TextBuffer + (Row * Snapshot->RegionWidth),
                    Console.Memory + RegionOffset,
                    CellBytesPerRow);
            }
            Snapshot->TextCellCount = CellCount;
            Snapshot->IsValid = TRUE;
        }
    } else {
        if (ConsoleEnsureFramebufferMapped() == TRUE) {
            CellWidth = ConsoleGetCellWidth();
            CellHeight = ConsoleGetCellHeight();
            BytesPerPixel = Console.FramebufferBytesPerPixel;

            if (CellWidth > 0 && CellHeight > 0 && BytesPerPixel > 0) {
                Snapshot->FramebufferPixelX = Snapshot->RegionX * CellWidth;
                Snapshot->FramebufferPixelY = Snapshot->RegionY * CellHeight;
                Snapshot->FramebufferPixelHeight = Snapshot->RegionHeight * CellHeight;
                Snapshot->FramebufferRowBytes =
                    (Snapshot->RegionWidth * CellWidth) * BytesPerPixel;
                Snapshot->FramebufferSize =
                    Snapshot->FramebufferRowBytes * Snapshot->FramebufferPixelHeight;
                Snapshot->FramebufferBuffer = (U8*)KernelHeapAlloc(Snapshot->FramebufferSize);

                if (Snapshot->FramebufferBuffer != NULL) {
                    for (U32 Row = 0; Row < Snapshot->FramebufferPixelHeight; Row++) {
                        FramebufferOffset =
                            ((Snapshot->FramebufferPixelY + Row) * Console.FramebufferPitch) +
                            (Snapshot->FramebufferPixelX * BytesPerPixel);
                        MemoryCopy(
                            Snapshot->FramebufferBuffer + (Row * Snapshot->FramebufferRowBytes),
                            Console.FramebufferLinear + FramebufferOffset,
                            Snapshot->FramebufferRowBytes);
                    }
                    Snapshot->IsValid = TRUE;
                }
            }
        }
    }

    UnlockMutex(MUTEX_CONSOLE);

    if (Snapshot->IsValid == FALSE) {
        SAFE_USE(Snapshot->TextBuffer) { KernelHeapFree(Snapshot->TextBuffer); }
        SAFE_USE(Snapshot->FramebufferBuffer) { KernelHeapFree(Snapshot->FramebufferBuffer); }
        KernelHeapFree(Snapshot);
        return FALSE;
    }

    *OutSnapshot = Snapshot;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Restore a previously captured active console region snapshot.
 * @param Snapshot Opaque snapshot pointer.
 * @return TRUE on success.
 */
BOOL ConsoleRestoreActiveRegionSnapshot(LPVOID Snapshot) {
    LPCONSOLE_ACTIVE_REGION_SNAPSHOT State = (LPCONSOLE_ACTIVE_REGION_SNAPSHOT)Snapshot;
    U32 CellBytesPerRow;
    U32 RegionOffset;
    U32 BytesPerPixel;
    U32 FramebufferOffset;

    if (State == NULL || State->IsValid == FALSE) {
        return FALSE;
    }

    LockMutex(MUTEX_CONSOLE, INFINITY);

    if (State->IsFramebuffer == FALSE) {
        if (State->TextBuffer == NULL || State->TextCellCount == 0) {
            UnlockMutex(MUTEX_CONSOLE);
            return FALSE;
        }

        CellBytesPerRow = State->RegionWidth * sizeof(U16);
        for (U32 Row = 0; Row < State->RegionHeight; Row++) {
            RegionOffset = ((State->RegionY + Row) * Console.ScreenWidth) + State->RegionX;
            MemoryCopy(
                Console.Memory + RegionOffset,
                State->TextBuffer + (Row * State->RegionWidth),
                CellBytesPerRow);
        }
    } else {
        if (ConsoleEnsureFramebufferMapped() == FALSE) {
            UnlockMutex(MUTEX_CONSOLE);
            return FALSE;
        }

        BytesPerPixel = Console.FramebufferBytesPerPixel;
        if (State->FramebufferBuffer == NULL ||
            State->FramebufferRowBytes == 0 ||
            State->FramebufferPixelHeight == 0 ||
            BytesPerPixel == 0) {
            UnlockMutex(MUTEX_CONSOLE);
            return FALSE;
        }

        for (U32 Row = 0; Row < State->FramebufferPixelHeight; Row++) {
            FramebufferOffset =
                ((State->FramebufferPixelY + Row) * Console.FramebufferPitch) +
                (State->FramebufferPixelX * BytesPerPixel);
            MemoryCopy(
                Console.FramebufferLinear + FramebufferOffset,
                State->FramebufferBuffer + (Row * State->FramebufferRowBytes),
                State->FramebufferRowBytes);
        }
    }

    Console.ForeColor = State->ForeColor;
    Console.BackColor = State->BackColor;
    Console.Blink = State->Blink;

    UnlockMutex(MUTEX_CONSOLE);

    SetConsoleCursorPosition(State->CursorX, State->CursorY);
    return TRUE;
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
 * @brief Show the console paging prompt for a specific region.
 * @param RegionIndex Region index.
 */
static void ConsolePagerWaitLockedRegion(U32 RegionIndex) {
    CONSOLE_REGION_STATE State;
    KEYCODE KeyCode;
    U32 Row;
    U32 Column;
    U32 Offset;
    U16 Attribute;
    STR Prompt[] = "-- Press a key --";
    U32 PromptLen;
    U32 Start;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if ((*State.PagingEnabled) == FALSE || (*State.PagingActive) == FALSE) return;
    if (State.Width == 0 || State.Height < 2) return;

    Row = State.Height - 1;
    Attribute = (U16)(((*State.ForeColor) | ((*State.BackColor) << 0x04) | ((*State.Blink) << 0x07)) << 0x08);

    if (Console.UseFramebuffer != FALSE) {
        if (ConsoleEnsureFramebufferMapped() == FALSE) {
            return;
        }

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
        goto WaitForKey;
    }

    for (Column = 0; Column < State.Width; Column++) {
        Offset = ((State.Y + Row) * Console.ScreenWidth) + (State.X + Column);
        Console.Memory[Offset] = (U16)STR_SPACE | Attribute;
    }

    PromptLen = StringLength(Prompt);
    if (PromptLen > State.Width) PromptLen = State.Width;
    Start = (State.Width > PromptLen) ? (State.Width - PromptLen) / 2 : 0;
    Offset = ((State.Y + Row) * Console.ScreenWidth) + (State.X + Start);
    for (Column = 0; Column < PromptLen; Column++) {
        Console.Memory[Offset + Column] = (U16)Prompt[Column] | Attribute;
    }

    SetConsoleCursorPosition(0, Row);

WaitForKey:
    while (TRUE) {
        if (PeekChar()) {
            GetKeyCode(&KeyCode);
            if (KeyCode.VirtualKey == VK_SPACE || KeyCode.VirtualKey == VK_ENTER) {
                (*State.PagingRemaining) = State.Height - 1;
                break;
            }
            if (KeyCode.VirtualKey == VK_ESCAPE) {
                (*State.PagingRemaining) = State.Height - 1;
                break;
            }
        }

        Sleep(10);
    }

    for (Column = 0; Column < State.Width; Column++) {
        Offset = ((State.Y + Row) * Console.ScreenWidth) + (State.X + Column);
        Console.Memory[Offset] = (U16)STR_SPACE | Attribute;
    }
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
    U32 Offset;
    U16 Attribute;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if ((*State.CursorX) >= State.Width || (*State.CursorY) >= State.Height) return;

    if (Console.UseFramebuffer != FALSE) {
        if (ConsoleEnsureFramebufferMapped() == FALSE) {
            return;
        }

        U32 PixelX = (State.X + (*State.CursorX)) * ConsoleGetCellWidth();
        U32 PixelY = (State.Y + (*State.CursorY)) * ConsoleGetCellHeight();
        ConsoleDrawGlyph(PixelX, PixelY, Char);
        return;
    }

    Offset = ((State.Y + (*State.CursorY)) * Console.ScreenWidth) + (State.X + (*State.CursorX));
    Attribute = (U16)(((*State.ForeColor) | ((*State.BackColor) << 0x04) | ((*State.Blink) << 0x07)) << 0x08);
    Console.Memory[Offset] = (U16)Char | Attribute;
}

/***************************************************************************/

/**
 * @brief Scroll a region up by one line.
 * @param RegionIndex Region index.
 */
void ConsoleScrollRegion(U32 RegionIndex) {
    CONSOLE_REGION_STATE State;
    U32 Row;
    U32 Column;
    U32 Src;
    U32 Dst;
    U16 Attribute;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) return;

    if ((*State.PagingRemaining) == 0) {
        ConsolePagerWaitLockedRegion(RegionIndex);
    }
    if ((*State.PagingRemaining) > 0) {
        (*State.PagingRemaining)--;
    }

    if (Console.UseFramebuffer != FALSE) {
        ConsoleScrollRegionFramebuffer(RegionIndex);
        return;
    }

    for (Row = 1; Row < State.Height; Row++) {
        for (Column = 0; Column < State.Width; Column++) {
            Src = ((State.Y + Row) * Console.ScreenWidth) + (State.X + Column);
            Dst = ((State.Y + (Row - 1)) * Console.ScreenWidth) + (State.X + Column);
            Console.Memory[Dst] = Console.Memory[Src];
        }
    }

    Attribute = (U16)(((*State.ForeColor) | ((*State.BackColor) << 0x04) | ((*State.Blink) << 0x07)) << 0x08);
    for (Column = 0; Column < State.Width; Column++) {
        Dst = ((State.Y + (State.Height - 1)) * Console.ScreenWidth) + (State.X + Column);
        Console.Memory[Dst] = (U16)STR_SPACE | Attribute;
    }
}

/***************************************************************************/

/**
 * @brief Clear a region and reset its cursor.
 * @param RegionIndex Region index.
 */
void ConsoleClearRegion(U32 RegionIndex) {
    CONSOLE_REGION_STATE State;
    U32 Row;
    U32 Column;
    U32 Offset;
    U16 Attribute;

    if (ConsoleResolveRegionState(RegionIndex, &State) == FALSE) return;
    if (State.Width == 0 || State.Height == 0) return;

    if (Console.UseFramebuffer != FALSE) {
        ConsoleClearRegionFramebuffer(RegionIndex);
        (*State.CursorX) = 0;
        (*State.CursorY) = 0;
        return;
    }

    Attribute = (U16)(((*State.ForeColor) | ((*State.BackColor) << 0x04) | ((*State.Blink) << 0x07)) << 0x08);
    for (Row = 0; Row < State.Height; Row++) {
        for (Column = 0; Column < State.Width; Column++) {
            Offset = ((State.Y + Row) * Console.ScreenWidth) + (State.X + Column);
            Console.Memory[Offset] = (U16)STR_SPACE | Attribute;
        }
    }

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
