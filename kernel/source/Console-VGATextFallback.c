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


    Console VGA text emergency fallback

\************************************************************************/

#include "Console-VGATextFallback.h"

#include "Console-Internal.h"
#include "DisplaySession.h"
#include "DriverGetters.h"
#include "Kernel.h"
#include "Log.h"
#include "Mutex.h"
#include "drivers/graphics/VGA.h"
#include "process/Process.h"

/************************************************************************/

static void ConsoleVGATextFallbackUpdateDesktopState(U32 Columns, U32 Rows);

/************************************************************************/

/**
 * @brief Keep main desktop metadata coherent for VGA text console mode.
 * @param Columns Console width in character cells.
 * @param Rows Console height in character cells.
 */
static void ConsoleVGATextFallbackUpdateDesktopState(U32 Columns, U32 Rows) {
    RECT Rect;

    if (Columns == 0 || Rows == 0) {
        return;
    }

    Rect.X1 = 0;
    Rect.Y1 = 0;
    Rect.X2 = (I32)Columns - 1;
    Rect.Y2 = (I32)Rows - 1;

    SAFE_USE_VALID_ID(&MainDesktop, KOID_DESKTOP) {
        LockMutex(&(MainDesktop.Mutex), INFINITY);
        MainDesktop.Graphics = ConsoleGetDriver();
        MainDesktop.Mode = DESKTOP_MODE_CONSOLE;

        SAFE_USE_VALID_ID(MainDesktop.Window, KOID_WINDOW) {
            LockMutex(&(MainDesktop.Window->Mutex), INFINITY);
            MainDesktop.Window->Rect = Rect;
            MainDesktop.Window->ScreenRect = Rect;
            MainDesktop.Window->InvalidRect = Rect;
            UnlockMutex(&(MainDesktop.Window->Mutex));
        }

        UnlockMutex(&(MainDesktop.Mutex));
    }
}

/************************************************************************/

/**
 * @brief Activate direct VGA text console fallback mode.
 * @param Columns Requested text width.
 * @param Rows Requested text height.
 * @param AppliedMode Optional output for the effective mode.
 * @return TRUE when VGA text mode activation succeeded.
 */
BOOL ConsoleVGATextFallbackActivate(U32 Columns, U32 Rows, LPGRAPHICSMODEINFO AppliedMode) {
    U32 RequestedColumns = (Columns != 0) ? Columns : 80;
    U32 RequestedRows = (Rows != 0) ? Rows : 25;
    U32 ModeIndex = 0;
    BOOL FallbackUsed = FALSE;
    GRAPHICSMODEINFO ModeInfo;

    if (VGAFindTextMode(RequestedColumns, RequestedRows, &ModeIndex) == FALSE) {
        RequestedColumns = 80;
        RequestedRows = 25;
        FallbackUsed = TRUE;
        if (VGAFindTextMode(RequestedColumns, RequestedRows, &ModeIndex) == FALSE) {
            ERROR(TEXT("[ConsoleVGATextFallbackActivate] No compatible VGA text mode found"));
            return FALSE;
        }
    }

    if (VGASetMode(ModeIndex) == FALSE) {
        ERROR(TEXT("[ConsoleVGATextFallbackActivate] VGASetMode failed for %ux%u"),
              RequestedColumns,
              RequestedRows);
        return FALSE;
    }

    Console.UseFramebuffer = FALSE;
    Console.ScreenWidth = RequestedColumns;
    Console.ScreenHeight = RequestedRows;
    ConsoleApplyLayout();
    Console.CursorX = 0;
    Console.CursorY = 0;
    ClearConsole();

    ConsoleVGATextFallbackUpdateDesktopState(Console.Width, Console.Height);

    ModeInfo.Header.Size = sizeof(ModeInfo);
    ModeInfo.Header.Version = EXOS_ABI_VERSION;
    ModeInfo.Header.Flags = 0;
    ModeInfo.Width = Console.Width;
    ModeInfo.Height = Console.Height;
    ModeInfo.BitsPerPixel = 0;
    (void)DisplaySessionSetConsoleMode(&ModeInfo);

    if (AppliedMode != NULL) {
        *AppliedMode = ModeInfo;
    }

    if (FallbackUsed != FALSE) {
        WARNING(TEXT("[ConsoleVGATextFallbackActivate] Falling back to VGA text mode %ux%u"),
                RequestedColumns,
                RequestedRows);
    } else {
        WARNING(TEXT("[ConsoleVGATextFallbackActivate] Activated VGA text fallback mode %ux%u"),
                RequestedColumns,
                RequestedRows);
    }

    return TRUE;
}

/************************************************************************/
