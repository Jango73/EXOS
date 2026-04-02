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


    Graphics-related system calls

\************************************************************************/

#include "Base.h"
#include "SYSCall.h"
#include "desktop/Desktop-ModeSelector.h"
#include "desktop/DisplaySession.h"

/************************************************************************/

/**
 * @brief Force one graphics backend alias and apply one requested graphics mode.
 *
 * The caller provides the backend alias inline in the syscall payload, so no
 * secondary user pointer needs to be dereferenced.
 *
 * @param Parameter Pointer to GRAPHICS_DRIVER_SELECTION_INFO.
 * @return UINT DF_RETURN_SUCCESS on success, or one driver-style error code.
 */
UINT SysCall_SetGraphicsDriver(UINT Parameter) {
    LPGRAPHICS_DRIVER_SELECTION_INFO SelectionInfo = (LPGRAPHICS_DRIVER_SELECTION_INFO)Parameter;
    GRAPHICS_MODE_INFO ModeInfo;

    SAFE_USE_INPUT_POINTER(SelectionInfo, GRAPHICS_DRIVER_SELECTION_INFO) {
        if (StringLength(SelectionInfo->DriverAlias) == 0) {
            return DF_RETURN_BAD_PARAMETER;
        }

        DesktopInitializeGraphicsModeInfo(
            &ModeInfo,
            INFINITY,
            SelectionInfo->Width,
            SelectionInfo->Height,
            SelectionInfo->BitsPerPixel);

        return DisplaySessionApplyGraphicsDriverByAlias(SelectionInfo->DriverAlias, &ModeInfo, NULL);
    }

    return DF_RETURN_BAD_PARAMETER;
}
