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
#include "Desktop-ThemeRecipes.h"
#include "Desktop-ThemeResolver.h"
#include "GFX.h"
#include "Kernel.h"

/***************************************************************************/

/**
 * @brief Draw one filled rectangle using an explicit color.
 * @param GC Graphics context.
 * @param X1 Left.
 * @param Y1 Top.
 * @param X2 Right.
 * @param Y2 Bottom.
 * @param Color Fill color.
 * @return TRUE on success.
 */
static BOOL DrawSolidRect(HANDLE GC, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR Color) {
    RECTINFO RectInfo;
    BRUSH Brush;
    HANDLE OldPen;
    HANDLE OldBrush;

    if (GC == NULL) return FALSE;
    if (X1 > X2 || Y1 > Y2) return FALSE;

    MemorySet(&Brush, 0, sizeof(Brush));
    Brush.TypeID = KOID_BRUSH;
    Brush.References = 1;
    Brush.Color = Color;
    Brush.Pattern = MAX_U32;

    RectInfo.Header.Size = sizeof(RectInfo);
    RectInfo.Header.Version = EXOS_ABI_VERSION;
    RectInfo.Header.Flags = 0;
    RectInfo.GC = GC;
    RectInfo.X1 = X1;
    RectInfo.Y1 = Y1;
    RectInfo.X2 = X2;
    RectInfo.Y2 = Y2;

    OldPen = SelectPen(GC, NULL);
    OldBrush = SelectBrush(GC, (HANDLE)&Brush);
    (void)Rectangle(&RectInfo);
    (void)SelectBrush(GC, OldBrush);
    (void)SelectPen(GC, OldPen);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw themed window borders around a window rectangle.
 * @param GC Graphics context.
 * @param Rect Target window-local rectangle.
 */
static void DrawWindowBorderFromTheme(HANDLE GC, LPRECT Rect) {
    U32 BorderThickness = 2;
    COLOR BorderColor = 0;
    I32 Width;
    I32 Height;
    I32 MaxThickness;
    I32 Thickness;

    if (GC == NULL || Rect == NULL) return;

    if (!DesktopThemeResolveLevel1Metric(TEXT("window.border"), TEXT("normal"), TEXT("border_thickness"), &BorderThickness)) {
        if (!DesktopThemeResolveLevel1Metric(TEXT("window.frame"), TEXT("normal"), TEXT("border_thickness"), &BorderThickness)) {
            BorderThickness = 2;
        }
    }

    if (!DesktopThemeResolveLevel1Color(TEXT("window.border"), TEXT("normal"), TEXT("border_color"), &BorderColor)) {
        if (!DesktopThemeResolveLevel1Color(TEXT("window.frame"), TEXT("normal"), TEXT("border_color"), &BorderColor)) {
            HANDLE Pen = GetSystemPen(SM_COLOR_DARK_SHADOW);
            SAFE_USE_VALID_ID((LPPEN)Pen, KOID_PEN) {
                BorderColor = ((LPPEN)Pen)->Color;
            }
        }
    }

    Width = Rect->X2 - Rect->X1 + 1;
    Height = Rect->Y2 - Rect->Y1 + 1;
    if (Width <= 0 || Height <= 0) return;

    MaxThickness = Width < Height ? Width : Height;
    MaxThickness /= 2;
    if (MaxThickness <= 0) return;

    Thickness = (I32)BorderThickness;
    if (Thickness <= 0) return;
    if (Thickness > MaxThickness) Thickness = MaxThickness;

    // Top border
    (void)DrawSolidRect(GC, Rect->X1, Rect->Y1, Rect->X2, Rect->Y1 + Thickness - 1, BorderColor);
    // Bottom border
    (void)DrawSolidRect(GC, Rect->X1, Rect->Y2 - Thickness + 1, Rect->X2, Rect->Y2, BorderColor);
    // Left border
    (void)DrawSolidRect(GC, Rect->X1, Rect->Y1 + Thickness, Rect->X1 + Thickness - 1, Rect->Y2 - Thickness, BorderColor);
    // Right border
    (void)DrawSolidRect(GC, Rect->X2 - Thickness + 1, Rect->Y1 + Thickness, Rect->X2, Rect->Y2 - Thickness, BorderColor);
}

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
    BRUSH Brush;
    COLOR Background;
    BOOL HasBackground = FALSE;

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

    if (DesktopThemeDrawRecipeForElementState(Window, GC, Rect, TEXT("window.frame"), TEXT("normal"))) {
        DrawWindowBorderFromTheme(GC, Rect);
        return TRUE;
    }

    HasBackground = DesktopThemeResolveLevel1Color(TEXT("window.frame"), TEXT("normal"), TEXT("background"), &Background);
    if (HasBackground) {
        MemorySet(&Brush, 0, sizeof(Brush));
        Brush.TypeID = KOID_BRUSH;
        Brush.References = 1;
        Brush.Color = Background;
        Brush.Pattern = MAX_U32;
        SelectBrush(GC, (HANDLE)&Brush);
    } else {
        SelectBrush(GC, GetSystemBrush(SM_COLOR_NORMAL));
    }

    Rectangle(&RectInfo);
    DrawWindowBorderFromTheme(GC, Rect);

    return TRUE;
}

/***************************************************************************/
