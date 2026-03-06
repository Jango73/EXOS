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


    Desktop built-in theme tokens

\************************************************************************/

#include "Desktop-ThemeTokens.h"
#include "Desktop-Private.h"
#include "Kernel.h"

/***************************************************************************/
// Type definitions

typedef struct tag_THEME_COLOR_TOKEN_ENTRY {
    U32 TokenID;
    COLOR Value;
} THEME_COLOR_TOKEN_ENTRY, *LPTHEME_COLOR_TOKEN_ENTRY;

typedef struct tag_THEME_METRIC_TOKEN_ENTRY {
    U32 TokenID;
    U32 Value;
} THEME_METRIC_TOKEN_ENTRY, *LPTHEME_METRIC_TOKEN_ENTRY;

typedef struct tag_SYSTEM_COLOR_BINDING {
    U32 SystemColor;
    U32 TokenID;
    LPBRUSH Brush;
    LPPEN Pen;
} SYSTEM_COLOR_BINDING, *LPSYSTEM_COLOR_BINDING;

/***************************************************************************/
// Other declarations

static const THEME_COLOR_TOKEN_ENTRY BuiltinColorTokens[] = {
    {THEME_TOKEN_COLOR_DESKTOP_BACKGROUND, COLOR_DARK_CYAN},
    {THEME_TOKEN_COLOR_HIGHLIGHT, 0x00FFFFFF},
    {THEME_TOKEN_COLOR_NORMAL, 0x00A0A0A0},
    {THEME_TOKEN_COLOR_LIGHT_SHADOW, 0x00404040},
    {THEME_TOKEN_COLOR_DARK_SHADOW, 0x00000000},
    {THEME_TOKEN_COLOR_CLIENT_BACKGROUND, COLOR_WHITE},
    {THEME_TOKEN_COLOR_TEXT_NORMAL, COLOR_BLACK},
    {THEME_TOKEN_COLOR_TEXT_SELECTED, COLOR_WHITE},
    {THEME_TOKEN_COLOR_SELECTION, COLOR_DARK_BLUE},
    {THEME_TOKEN_COLOR_TITLE_BAR, COLOR_DARK_BLUE},
    {THEME_TOKEN_COLOR_TITLE_BAR_2, COLOR_CYAN},
    {THEME_TOKEN_COLOR_TITLE_TEXT, COLOR_WHITE},
};

static const THEME_METRIC_TOKEN_ENTRY BuiltinMetricTokens[] = {
    {THEME_TOKEN_METRIC_MINIMUM_WINDOW_WIDTH, 32},
    {THEME_TOKEN_METRIC_MINIMUM_WINDOW_HEIGHT, 16},
    {THEME_TOKEN_METRIC_MAXIMUM_WINDOW_WIDTH, 4096},
    {THEME_TOKEN_METRIC_MAXIMUM_WINDOW_HEIGHT, 2160},
    {THEME_TOKEN_METRIC_TITLE_BAR_HEIGHT, 22},
};

static SYSTEM_COLOR_BINDING BuiltinSystemColorBindings[] = {
    {SM_COLOR_DESKTOP, THEME_TOKEN_COLOR_DESKTOP_BACKGROUND, &Brush_Desktop, &Pen_Desktop},
    {SM_COLOR_HIGHLIGHT, THEME_TOKEN_COLOR_HIGHLIGHT, &Brush_High, &Pen_High},
    {SM_COLOR_NORMAL, THEME_TOKEN_COLOR_NORMAL, &Brush_Normal, &Pen_Normal},
    {SM_COLOR_LIGHT_SHADOW, THEME_TOKEN_COLOR_LIGHT_SHADOW, &Brush_HiShadow, &Pen_HiShadow},
    {SM_COLOR_DARK_SHADOW, THEME_TOKEN_COLOR_DARK_SHADOW, &Brush_LoShadow, &Pen_LoShadow},
    {SM_COLOR_CLIENT, THEME_TOKEN_COLOR_CLIENT_BACKGROUND, &Brush_Client, &Pen_Client},
    {SM_COLOR_TEXT_NORMAL, THEME_TOKEN_COLOR_TEXT_NORMAL, &Brush_Text_Normal, &Pen_Text_Normal},
    {SM_COLOR_TEXT_SELECTED, THEME_TOKEN_COLOR_TEXT_SELECTED, &Brush_Text_Select, &Pen_Text_Select},
    {SM_COLOR_SELECTION, THEME_TOKEN_COLOR_SELECTION, &Brush_Selection, &Pen_Selection},
    {SM_COLOR_TITLE_BAR, THEME_TOKEN_COLOR_TITLE_BAR, &Brush_Title_Bar, &Pen_Title_Bar},
    {SM_COLOR_TITLE_BAR_2, THEME_TOKEN_COLOR_TITLE_BAR_2, &Brush_Title_Bar_2, &Pen_Title_Bar_2},
    {SM_COLOR_TITLE_TEXT, THEME_TOKEN_COLOR_TITLE_TEXT, &Brush_Title_Text, &Pen_Title_Text},
};

/***************************************************************************/

/**
 * @brief Resolve a built-in color token identifier.
 * @param TokenID Built-in THEME_TOKEN_COLOR_* identifier.
 * @param Color Receives the token value.
 * @return TRUE when the token exists.
 */
static BOOL ResolveBuiltinColorToken(U32 TokenID, COLOR* Color) {
    UINT Index;

    if (Color == NULL) return FALSE;

    for (Index = 0; Index < (sizeof(BuiltinColorTokens) / sizeof(BuiltinColorTokens[0])); Index++) {
        if (BuiltinColorTokens[Index].TokenID == TokenID) {
            *Color = BuiltinColorTokens[Index].Value;
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Resolve a built-in metric token identifier.
 * @param TokenID Built-in THEME_TOKEN_METRIC_* identifier.
 * @param Value Receives the token value.
 * @return TRUE when the token exists.
 */
static BOOL ResolveBuiltinMetricToken(U32 TokenID, U32* Value) {
    UINT Index;

    if (Value == NULL) return FALSE;

    for (Index = 0; Index < (sizeof(BuiltinMetricTokens) / sizeof(BuiltinMetricTokens[0])); Index++) {
        if (BuiltinMetricTokens[Index].TokenID == TokenID) {
            *Value = BuiltinMetricTokens[Index].Value;
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Convert SM_COLOR_* identifiers to built-in color token identifiers.
 * @param SystemColorIndex Value from SM_COLOR_*.
 * @param TokenID Receives THEME_TOKEN_COLOR_* identifier.
 * @return TRUE when mapping exists.
 */
static BOOL ResolveSystemColorTokenID(U32 SystemColorIndex, U32* TokenID) {
    UINT Index;

    if (TokenID == NULL) return FALSE;

    for (Index = 0; Index < (sizeof(BuiltinSystemColorBindings) / sizeof(BuiltinSystemColorBindings[0])); Index++) {
        if (BuiltinSystemColorBindings[Index].SystemColor == SystemColorIndex) {
            *TokenID = BuiltinSystemColorBindings[Index].TokenID;
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Convert SM_* metric identifiers to built-in metric token identifiers.
 * @param SystemMetricIndex Value from SM_* metric constants.
 * @param TokenID Receives THEME_TOKEN_METRIC_* identifier.
 * @return TRUE when mapping exists.
 */
static BOOL ResolveSystemMetricTokenID(U32 SystemMetricIndex, U32* TokenID) {
    if (TokenID == NULL) return FALSE;

    switch (SystemMetricIndex) {
        case SM_MINIMUM_WINDOW_WIDTH:
            *TokenID = THEME_TOKEN_METRIC_MINIMUM_WINDOW_WIDTH;
            return TRUE;
        case SM_MINIMUM_WINDOW_HEIGHT:
            *TokenID = THEME_TOKEN_METRIC_MINIMUM_WINDOW_HEIGHT;
            return TRUE;
        case SM_MAXIMUM_WINDOW_WIDTH:
            *TokenID = THEME_TOKEN_METRIC_MAXIMUM_WINDOW_WIDTH;
            return TRUE;
        case SM_MAXIMUM_WINDOW_HEIGHT:
            *TokenID = THEME_TOKEN_METRIC_MAXIMUM_WINDOW_HEIGHT;
            return TRUE;
        case SM_TITLE_BAR_HEIGHT:
            *TokenID = THEME_TOKEN_METRIC_TITLE_BAR_HEIGHT;
            return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

BOOL DesktopThemeResolveSystemColor(U32 SystemColorIndex, COLOR* Color) {
    U32 TokenID;

    if (ResolveSystemColorTokenID(SystemColorIndex, &TokenID) == FALSE) return FALSE;
    return ResolveBuiltinColorToken(TokenID, Color);
}

/***************************************************************************/

BOOL DesktopThemeResolveSystemMetric(U32 SystemMetricIndex, U32* Value) {
    U32 TokenID;

    if (ResolveSystemMetricTokenID(SystemMetricIndex, &TokenID) == FALSE) return FALSE;
    return ResolveBuiltinMetricToken(TokenID, Value);
}

/***************************************************************************/

void DesktopThemeSyncSystemObjects(void) {
    UINT Index;
    COLOR Color;

    for (Index = 0; Index < (sizeof(BuiltinSystemColorBindings) / sizeof(BuiltinSystemColorBindings[0])); Index++) {
        if (ResolveBuiltinColorToken(BuiltinSystemColorBindings[Index].TokenID, &Color) == FALSE) continue;

        SAFE_USE_VALID_ID(BuiltinSystemColorBindings[Index].Brush, KOID_BRUSH) { BuiltinSystemColorBindings[Index].Brush->Color = Color; }
        SAFE_USE_VALID_ID(BuiltinSystemColorBindings[Index].Pen, KOID_PEN) { BuiltinSystemColorBindings[Index].Pen->Color = Color; }
    }
}

/***************************************************************************/
