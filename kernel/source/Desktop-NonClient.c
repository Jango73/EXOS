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


    Desktop non-client rendering

\************************************************************************/

#include "Desktop-NonClient.h"

/***************************************************************************/

/**
 * @brief Resolve decoration mode from a window style bitfield.
 * @param Style Window style bitfield.
 * @return One of WINDOW_DECORATION_MODE_* values.
 */
static U32 GetDecorationModeFromStyle(U32 Style) {
    if (Style & EWS_BARE_SURFACE) return WINDOW_DECORATION_MODE_BARE;
    if (Style & EWS_CLIENT_DECORATED) return WINDOW_DECORATION_MODE_CLIENT;

    // Compatibility: undecorated style bitfield defaults to system decorations.
    return WINDOW_DECORATION_MODE_SYSTEM;
}

/***************************************************************************/

/**
 * @brief Resolve the decoration mode configured on a window.
 * @param Window Window to inspect.
 * @return One of WINDOW_DECORATION_MODE_* values.
 */
U32 GetWindowDecorationMode(LPWINDOW Window) {
    if (Window == NULL) return WINDOW_DECORATION_MODE_SYSTEM;
    if (Window->TypeID != KOID_WINDOW) return WINDOW_DECORATION_MODE_SYSTEM;

    return GetDecorationModeFromStyle(Window->Style);
}

/***************************************************************************/

/**
 * @brief Tell whether the kernel non-client renderer should draw this window.
 * @param Window Window to inspect.
 * @return TRUE when system decorations are enabled.
 */
BOOL ShouldDrawWindowNonClient(LPWINDOW Window) {
    return (GetWindowDecorationMode(Window) == WINDOW_DECORATION_MODE_SYSTEM);
}

/***************************************************************************/

/**
 * @brief Draw the default non-client visuals for a window.
 * @param Window Window handle.
 * @param GC Graphics context handle.
 * @param Rect Window-local rectangle to draw.
 * @return TRUE when drawing was performed.
 */
BOOL DrawWindowNonClient(HANDLE Window, HANDLE GC, LPRECT Rect) {
    RECTINFO RectInfo;

    if (Window == NULL) return FALSE;
    if (GC == NULL) return FALSE;
    if (Rect == NULL) return FALSE;

    RectInfo.Header.Size = sizeof(RectInfo);
    RectInfo.Header.Version = EXOS_ABI_VERSION;
    RectInfo.Header.Flags = 0;
    RectInfo.GC = GC;
    RectInfo.X1 = Rect->X1;
    RectInfo.Y1 = Rect->Y1;
    RectInfo.X2 = Rect->X2;
    RectInfo.Y2 = Rect->Y2;

    // Keep visuals identical to existing default window rendering.
    SelectBrush(GC, GetSystemBrush(SM_COLOR_NORMAL));
    Rectangle(&RectInfo);

    return TRUE;
}

/***************************************************************************/
