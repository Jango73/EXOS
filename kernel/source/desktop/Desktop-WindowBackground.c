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


    Desktop themed window background drawing

\************************************************************************/

#include "Desktop.h"

#include "Desktop-ThemeRecipes.h"
#include "Desktop-ThemeResolver.h"
#include "Kernel.h"

/***************************************************************************/
// Type definitions

typedef struct tag_WINDOW_BACKGROUND_THEME_ENTRY {
    U32 ThemeToken;
    LPCSTR ElementID;
    LPCSTR StateID;
    U32 FallbackSystemColor;
} WINDOW_BACKGROUND_THEME_ENTRY, *LPWINDOW_BACKGROUND_THEME_ENTRY;

/***************************************************************************/
// Other declarations

static const WINDOW_BACKGROUND_THEME_ENTRY WindowBackgroundThemeEntries[] = {
    {THEME_TOKEN_WINDOW_BACKGROUND_DESKTOP, TEXT("desktop.root"), TEXT("normal"), SM_COLOR_DESKTOP},
    {THEME_TOKEN_WINDOW_BACKGROUND_CLIENT, TEXT("window.client"), TEXT("normal"), SM_COLOR_CLIENT},
    {THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_NORMAL, TEXT("button.body"), TEXT("normal"), SM_COLOR_NORMAL},
    {THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_HOVER, TEXT("button.body"), TEXT("hover"), SM_COLOR_NORMAL},
    {THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_PRESSED, TEXT("button.body"), TEXT("pressed"), SM_COLOR_NORMAL},
    {THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_DISABLED, TEXT("button.body"), TEXT("disabled"), SM_COLOR_NORMAL},
};

/***************************************************************************/

/**
 * @brief Resolve one background theme token into one themed element binding.
 * @param ThemeToken Public theme token identifier.
 * @param EntryOut Receives the resolved entry.
 * @return TRUE when the token is supported.
 */
static BOOL ResolveWindowBackgroundThemeEntry(U32 ThemeToken, const WINDOW_BACKGROUND_THEME_ENTRY** EntryOut) {
    UINT Index;

    if (EntryOut == NULL) return FALSE;

    for (Index = 0; Index < (sizeof(WindowBackgroundThemeEntries) / sizeof(WindowBackgroundThemeEntries[0])); Index++) {
        if (WindowBackgroundThemeEntries[Index].ThemeToken != ThemeToken) continue;
        *EntryOut = &WindowBackgroundThemeEntries[Index];
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Fill one rectangle with one solid color.
 * @param GC Target graphics context.
 * @param Rect Target rectangle.
 * @param Color Fill color.
 * @return TRUE on success.
 */
static BOOL DrawSolidBackground(HANDLE GC, LPRECT Rect, COLOR Color) {
    BRUSH Brush;
    RECTINFO RectInfo;
    HANDLE OldBrush;
    HANDLE OldPen;

    if (GC == NULL || Rect == NULL) return FALSE;

    MemorySet(&Brush, 0, sizeof(Brush));
    Brush.TypeID = KOID_BRUSH;
    Brush.References = 1;
    Brush.Color = Color;
    Brush.Pattern = MAX_U32;

    RectInfo.Header.Size = sizeof(RectInfo);
    RectInfo.Header.Version = EXOS_ABI_VERSION;
    RectInfo.Header.Flags = 0;
    RectInfo.GC = GC;
    RectInfo.X1 = Rect->X1;
    RectInfo.Y1 = Rect->Y1;
    RectInfo.X2 = Rect->X2;
    RectInfo.Y2 = Rect->Y2;

    OldPen = SelectPen(GC, NULL);
    OldBrush = SelectBrush(GC, (HANDLE)&Brush);
    (void)Rectangle(&RectInfo);
    (void)SelectBrush(GC, OldBrush);
    (void)SelectPen(GC, OldPen);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw one rectangle border with one explicit color and thickness.
 * @param GC Target graphics context.
 * @param Rect Target rectangle.
 * @param Color Border color.
 * @param Thickness Border thickness in pixels.
 * @return TRUE on success.
 */
static BOOL DrawBackgroundBorder(HANDLE GC, LPRECT Rect, COLOR Color, U32 Thickness) {
    PEN Pen;
    LINEINFO LineInfo;
    HANDLE OldPen;
    UINT Index;
    I32 Offset;

    if (GC == NULL || Rect == NULL) return FALSE;
    if (Thickness == 0) return TRUE;

    MemorySet(&Pen, 0, sizeof(Pen));
    Pen.TypeID = KOID_PEN;
    Pen.References = 1;
    Pen.Color = Color;
    Pen.Pattern = MAX_U32;

    LineInfo.Header.Size = sizeof(LineInfo);
    LineInfo.Header.Version = EXOS_ABI_VERSION;
    LineInfo.Header.Flags = 0;
    LineInfo.GC = GC;

    OldPen = SelectPen(GC, (HANDLE)&Pen);
    for (Index = 0; Index < Thickness; Index++) {
        Offset = (I32)Index;
        if ((Rect->X1 + Offset) > (Rect->X2 - Offset) || (Rect->Y1 + Offset) > (Rect->Y2 - Offset)) break;

        LineInfo.X1 = Rect->X1 + Offset;
        LineInfo.Y1 = Rect->Y1 + Offset;
        LineInfo.X2 = Rect->X2 - Offset;
        LineInfo.Y2 = Rect->Y1 + Offset;
        (void)Line(&LineInfo);

        LineInfo.X1 = Rect->X2 - Offset;
        LineInfo.Y1 = Rect->Y1 + Offset;
        LineInfo.X2 = Rect->X2 - Offset;
        LineInfo.Y2 = Rect->Y2 - Offset;
        (void)Line(&LineInfo);

        LineInfo.X1 = Rect->X2 - Offset;
        LineInfo.Y1 = Rect->Y2 - Offset;
        LineInfo.X2 = Rect->X1 + Offset;
        LineInfo.Y2 = Rect->Y2 - Offset;
        (void)Line(&LineInfo);

        LineInfo.X1 = Rect->X1 + Offset;
        LineInfo.Y1 = Rect->Y2 - Offset;
        LineInfo.X2 = Rect->X1 + Offset;
        LineInfo.Y2 = Rect->Y1 + Offset;
        (void)Line(&LineInfo);
    }
    (void)SelectPen(GC, OldPen);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw one themed background using level 1 properties.
 * @param GC Target graphics context.
 * @param Rect Target rectangle.
 * @param ElementID Theme element identifier.
 * @param StateID Theme state identifier.
 * @param FallbackSystemColor Legacy fallback system color.
 * @return TRUE on success.
 */
static BOOL DrawLevel1WindowBackground(HANDLE GC, LPRECT Rect, LPCSTR ElementID, LPCSTR StateID, U32 FallbackSystemColor) {
    COLOR BackgroundColor;
    COLOR BorderColor;
    U32 BorderThickness = 0;
    BOOL HasBackground;
    BOOL HasBorderColor;
    BOOL HasBorderThickness;

    if (GC == NULL || Rect == NULL || ElementID == NULL || StateID == NULL) return FALSE;

    HasBackground = DesktopThemeResolveLevel1Color(ElementID, StateID, TEXT("background"), &BackgroundColor);
    if (HasBackground != FALSE) {
        (void)DrawSolidBackground(GC, Rect, BackgroundColor);
    } else {
        RECTINFO RectInfo;
        HANDLE OldPen = NULL;
        HANDLE OldBrush = NULL;

        RectInfo.Header.Size = sizeof(RectInfo);
        RectInfo.Header.Version = EXOS_ABI_VERSION;
        RectInfo.Header.Flags = 0;
        RectInfo.GC = GC;
        RectInfo.X1 = Rect->X1;
        RectInfo.Y1 = Rect->Y1;
        RectInfo.X2 = Rect->X2;
        RectInfo.Y2 = Rect->Y2;

        OldPen = SelectPen(GC, NULL);
        OldBrush = SelectBrush(GC, GetSystemBrush(FallbackSystemColor));
        (void)Rectangle(&RectInfo);
        (void)SelectBrush(GC, OldBrush);
        (void)SelectPen(GC, OldPen);
    }

    HasBorderColor = DesktopThemeResolveLevel1Color(ElementID, StateID, TEXT("border_color"), &BorderColor);
    HasBorderThickness = DesktopThemeResolveLevel1Metric(ElementID, StateID, TEXT("border_thickness"), &BorderThickness);
    if (HasBorderColor != FALSE && HasBorderThickness != FALSE && BorderThickness > 0) {
        (void)DrawBackgroundBorder(GC, Rect, BorderColor, BorderThickness);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw one themed window background from one public theme token.
 * @param Window Target window handle.
 * @param GC Target graphics context.
 * @param Rect Target rectangle.
 * @param ThemeToken Public theme token identifier.
 * @return TRUE on success.
 */
BOOL DrawWindowBackground(HANDLE Window, HANDLE GC, LPRECT Rect, U32 ThemeToken) {
    const WINDOW_BACKGROUND_THEME_ENTRY* Entry = NULL;

    if (GC == NULL || Rect == NULL) return FALSE;
    if (ResolveWindowBackgroundThemeEntry(ThemeToken, &Entry) == FALSE) return FALSE;

    if (DesktopThemeDrawRecipeForElementState(Window, GC, Rect, Entry->ElementID, Entry->StateID) != FALSE) {
        return TRUE;
    }

    return DrawLevel1WindowBackground(GC, Rect, Entry->ElementID, Entry->StateID, Entry->FallbackSystemColor);
}

/***************************************************************************/
