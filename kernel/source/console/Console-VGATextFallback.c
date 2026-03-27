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

#include "console/Console-VGATextFallback.h"

#include "Console-Internal.h"
#include "DisplaySession.h"
#include "DriverGetters.h"
#include "GFX.h"
#include "Kernel.h"
/************************************************************************/

/**
 * @brief Activate direct VGA text console fallback mode.
 * @param Columns Requested text width.
 * @param Rows Requested text height.
 * @param AppliedMode Optional output for the effective mode.
 * @return TRUE when VGA text mode activation succeeded.
 */
BOOL ConsoleVGATextFallbackActivate(U32 Columns, U32 Rows, LPGRAPHICS_MODE_INFO AppliedMode) {
    U32 RequestedColumns = (Columns != 0) ? Columns : 80;
    U32 RequestedRows = (Rows != 0) ? Rows : 25;
    LPDRIVER VGADriver = VGAGetDriver();
    GRAPHICS_MODE_INFO RequestedMode;
    UINT SetModeResult = DF_RETURN_NOT_IMPLEMENTED;
    GRAPHICS_MODE_INFO ModeInfo;

    if (VGADriver == NULL || VGADriver->Command == NULL) {
        return FALSE;
    }

    if (VGADriver->Command(DF_LOAD, 0) != DF_RETURN_SUCCESS) {
        return FALSE;
    }

    RequestedMode.Header.Size = sizeof(RequestedMode);
    RequestedMode.Header.Version = EXOS_ABI_VERSION;
    RequestedMode.Header.Flags = 0;
    RequestedMode.ModeIndex = INFINITY;
    RequestedMode.Width = RequestedColumns;
    RequestedMode.Height = RequestedRows;
    RequestedMode.BitsPerPixel = 0;

    SetModeResult = VGADriver->Command(DF_GFX_SETMODE, (UINT)(LPVOID)&RequestedMode);
    if (SetModeResult != DF_RETURN_SUCCESS) {
        RequestedColumns = 80;
        RequestedRows = 25;
        RequestedMode.Width = RequestedColumns;
        RequestedMode.Height = RequestedRows;
        SetModeResult = VGADriver->Command(DF_GFX_SETMODE, (UINT)(LPVOID)&RequestedMode);
        if (SetModeResult != DF_RETURN_SUCCESS) {
            return FALSE;
        }
    }

    if (RequestedMode.Width == 0 || RequestedMode.Height == 0) {
        return FALSE;
    }

    Console.UseFramebuffer = FALSE;
    Console.UseTextBackend = TRUE;
    Console.Port = 0;
    Console.Memory = NULL;
    Console.ScreenWidth = RequestedMode.Width;
    Console.ScreenHeight = RequestedMode.Height;
    Console.FramebufferPhysical = 0;
    Console.FramebufferLinear = NULL;
    Console.FramebufferPitch = RequestedMode.Width * sizeof(U16);
    Console.FramebufferWidth = RequestedMode.Width;
    Console.FramebufferHeight = RequestedMode.Height;
    Console.FramebufferBitsPerPixel = 0;
    Console.FramebufferType = MULTIBOOT_FRAMEBUFFER_TEXT;
    Console.FramebufferRedPosition = 0;
    Console.FramebufferRedMaskSize = 0;
    Console.FramebufferGreenPosition = 0;
    Console.FramebufferGreenMaskSize = 0;
    Console.FramebufferBluePosition = 0;
    Console.FramebufferBlueMaskSize = 0;
    Console.FramebufferBytesPerPixel = sizeof(U16);
    ConsoleApplyLayout();

    ClearConsole();
    ConsoleApplyBootCursorHandover();

    ModeInfo.Header.Size = sizeof(ModeInfo);
    ModeInfo.Header.Version = EXOS_ABI_VERSION;
    ModeInfo.Header.Flags = 0;
    ModeInfo.ModeIndex = INFINITY;
    ModeInfo.Width = Console.Width;
    ModeInfo.Height = Console.Height;
    ModeInfo.BitsPerPixel = 0;
    (void)DisplaySessionSetConsoleMode(&ModeInfo);

    if (AppliedMode != NULL) {
        *AppliedMode = ModeInfo;
    }

    return TRUE;
}

/************************************************************************/
