
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
#include "Desktop-ThemeCommon.h"
#include "Desktop-Private.h"
#include "Desktop-ThemeRuntime.h"
#include "CoreString.h"
#include "Kernel.h"

/***************************************************************************/
// Type definitions

typedef struct tag_THEME_COLOR_TOKEN_ENTRY {
    U32 TokenID;
    LPCSTR Name;
    COLOR Value;
} THEME_COLOR_TOKEN_ENTRY, *LPTHEME_COLOR_TOKEN_ENTRY;

typedef struct tag_THEME_METRIC_TOKEN_ENTRY {
    U32 TokenID;
    LPCSTR Name;
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

// Built-in color tokens must be unique by TokenID.
// Duplicate TokenID entries are forbidden.
static const THEME_COLOR_TOKEN_ENTRY BuiltinColorTokens[] = {
    {THEME_TOKEN_COLOR_DESKTOP_BACKGROUND, TEXT("color.desktop.background"), SETALPHA(COLOR_GRAY15, 0xFF)},
    {THEME_TOKEN_COLOR_HIGHLIGHT, TEXT("color.highlight"), SETALPHA(COLOR_GRAY90, 0xFF)},
    {THEME_TOKEN_COLOR_NORMAL, TEXT("color.normal"), SETALPHA(COLOR_GRAY50, 0xFF)},
    {THEME_TOKEN_COLOR_LIGHT_SHADOW, TEXT("color.light_shadow"), SETALPHA((COLOR)0x00404040, 0xFF)},
    {THEME_TOKEN_COLOR_DARK_SHADOW, TEXT("color.dark_shadow"), SETALPHA((COLOR)0x00000000, 0xFF)},
    {THEME_TOKEN_COLOR_CLIENT_BACKGROUND, TEXT("color.client.background"), SETALPHA(COLOR_GRAY20, 0xFF)},
    {THEME_TOKEN_COLOR_WINDOW_BORDER, TEXT("color.window.border"), SETALPHA((COLOR)0x00000000, 0xFF)},
    {THEME_TOKEN_COLOR_SELECTION, TEXT("color.selection"), SETALPHA(COLOR_DARK_BLUE, 0xFF)},
    {THEME_TOKEN_COLOR_TITLE_BAR, TEXT("color.window.title.active.start"), SETALPHA(COLOR_GRAY35, 0xFF)},
    {THEME_TOKEN_COLOR_TITLE_BAR_2, TEXT("color.window.title.active.end"), SETALPHA(COLOR_GRAY25, 0xFF)},
    {THEME_TOKEN_COLOR_TEXT_NORMAL, TEXT("color.text.normal"), SETALPHA(COLOR_GRAY75, 0xFF)},
    {THEME_TOKEN_COLOR_TEXT_SELECTED, TEXT("color.text.selected"), SETALPHA(COLOR_GRAY75, 0xFF)},
    {THEME_TOKEN_COLOR_TITLE_TEXT, TEXT("color.title_text"), SETALPHA(COLOR_GRAY75, 0xFF)},
    {THEME_TOKEN_COLOR_BUTTON_BACKGROUND, TEXT("color.button.background"), SETALPHA(COLOR_GRAY35, 1)},
    {THEME_TOKEN_COLOR_BUTTON_BACKGROUND_HOVER, TEXT("color.button.background.hover"), SETALPHA(COLOR_GRAY40, 0xFF)},
    {THEME_TOKEN_COLOR_BUTTON_BACKGROUND_PRESSED, TEXT("color.button.background.pressed"), SETALPHA(COLOR_GRAY50, 0xFF)},
    {THEME_TOKEN_COLOR_BUTTON_BACKGROUND_DISABLED, TEXT("color.button.background.disabled"), SETALPHA(COLOR_GRAY30, 0xFF)},
    {THEME_TOKEN_COLOR_BUTTON_BORDER, TEXT("color.button.border"), SETALPHA(COLOR_GRAY75, 0xFF)},
    {THEME_TOKEN_COLOR_BUTTON_BORDER_HOVER, TEXT("color.button.border.hover"), SETALPHA(COLOR_GRAY90, 0xFF)},
    {THEME_TOKEN_COLOR_BUTTON_BORDER_PRESSED, TEXT("color.button.border.pressed"), SETALPHA(COLOR_GRAY50, 0xFF)},
    {THEME_TOKEN_COLOR_BUTTON_TEXT_DISABLED, TEXT("color.button.text.disabled"), SETALPHA(COLOR_GRAY50, 0xFF)},
};

// Built-in metric tokens must be unique by TokenID.
// Duplicate TokenID entries are forbidden.
static const THEME_METRIC_TOKEN_ENTRY BuiltinMetricTokens[] = {
    {THEME_TOKEN_METRIC_MINIMUM_WINDOW_WIDTH, TEXT("metric.window.minimum_width"), 32},
    {THEME_TOKEN_METRIC_MINIMUM_WINDOW_HEIGHT, TEXT("metric.window.minimum_height"), 16},
    {THEME_TOKEN_METRIC_MAXIMUM_WINDOW_WIDTH, TEXT("metric.window.maximum_width"), 4096},
    {THEME_TOKEN_METRIC_MAXIMUM_WINDOW_HEIGHT, TEXT("metric.window.maximum_height"), 2160},
    {THEME_TOKEN_METRIC_TITLE_BAR_HEIGHT, TEXT("metric.window.title_height"), 22},
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
 * @brief Compare start of one string with one prefix.
 * @param Text Input text.
 * @param Prefix Prefix text.
 * @return TRUE when text starts with prefix.
 */
static BOOL ThemeStartsWith(LPCSTR Text, LPCSTR Prefix) {
    UINT Index;

    if (Text == NULL || Prefix == NULL) return FALSE;

    for (Index = 0; Prefix[Index] != STR_NULL; Index++) {
        if (Text[Index] != Prefix[Index]) return FALSE;
    }

    return TRUE;
}

/**
 * @brief Parse one metric literal.
 * @param Value Metric value text.
 * @param Metric Receives parsed metric.
 * @return TRUE on success.
 */
static BOOL ParseMetricLiteral(LPCSTR Value, U32* Metric) {
    if (Value == NULL || Metric == NULL) return FALSE;

    *Metric = StringToU32(Value);
    return TRUE;
}

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
 * @brief Resolve a built-in color token name from token identifier.
 * @param TokenID Built-in color token identifier.
 * @param TokenName Receives canonical token name.
 * @return TRUE on success.
 */
static BOOL ResolveBuiltinColorTokenName(U32 TokenID, LPCSTR* TokenName) {
    UINT Index;

    if (TokenName == NULL) return FALSE;

    for (Index = 0; Index < (sizeof(BuiltinColorTokens) / sizeof(BuiltinColorTokens[0])); Index++) {
        if (BuiltinColorTokens[Index].TokenID != TokenID) continue;
        *TokenName = BuiltinColorTokens[Index].Name;
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Resolve one built-in color token by token name.
 * @param TokenName Token name.
 * @param Color Receives color value.
 * @return TRUE on success.
 */
static BOOL ResolveBuiltinColorTokenByName(LPCSTR TokenName, COLOR* Color) {
    UINT Index;

    if (TokenName == NULL || Color == NULL) return FALSE;

    for (Index = 0; Index < (sizeof(BuiltinColorTokens) / sizeof(BuiltinColorTokens[0])); Index++) {
        if (StringCompareNC(BuiltinColorTokens[Index].Name, TokenName) == 0) {
            *Color = BuiltinColorTokens[Index].Value;
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Resolve one built-in metric token by token name.
 * @param TokenName Token name.
 * @param Value Receives metric value.
 * @return TRUE on success.
 */
static BOOL ResolveBuiltinMetricTokenByName(LPCSTR TokenName, U32* Value) {
    UINT Index;

    if (TokenName == NULL || Value == NULL) return FALSE;

    for (Index = 0; Index < (sizeof(BuiltinMetricTokens) / sizeof(BuiltinMetricTokens[0])); Index++) {
        if (StringCompareNC(BuiltinMetricTokens[Index].Name, TokenName) == 0) {
            *Value = BuiltinMetricTokens[Index].Value;
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Resolve token color from active runtime with bounded recursion.
 * @param TokenName Token name.
 * @param Color Receives token color.
 * @param Depth Recursion depth.
 * @return TRUE on success.
 */
static BOOL ResolveRuntimeTokenColorRecursive(LPCSTR TokenName, COLOR* Color, U32 Depth) {
    LPCSTR Value = NULL;

    if (TokenName == NULL || Color == NULL) return FALSE;
    if (Depth > 8) return FALSE;

    if (DesktopThemeLookupTokenValue(NULL, TokenName, &Value) == FALSE) {
        return ResolveBuiltinColorTokenByName(TokenName, Color);
    }
    if (Value == NULL || Value[0] == STR_NULL) {
        return ResolveBuiltinColorTokenByName(TokenName, Color);
    }

    if (ThemeStartsWith(Value, TEXT("token:"))) {
        return ResolveRuntimeTokenColorRecursive(Value + 6, Color, Depth + 1);
    }

    if (DesktopThemeParseColorLiteral(Value, Color)) return TRUE;
    return ResolveBuiltinColorTokenByName(TokenName, Color);
}

/***************************************************************************/

/**
 * @brief Resolve token metric from active runtime with bounded recursion.
 * @param TokenName Token name.
 * @param Metric Receives token metric.
 * @param Depth Recursion depth.
 * @return TRUE on success.
 */
static BOOL ResolveRuntimeTokenMetricRecursive(LPCSTR TokenName, U32* Metric, U32 Depth) {
    LPCSTR Value = NULL;

    if (TokenName == NULL || Metric == NULL) return FALSE;
    if (Depth > 8) return FALSE;

    if (DesktopThemeLookupTokenValue(NULL, TokenName, &Value) == FALSE) {
        return ResolveBuiltinMetricTokenByName(TokenName, Metric);
    }
    if (Value == NULL || Value[0] == STR_NULL) {
        return ResolveBuiltinMetricTokenByName(TokenName, Metric);
    }

    if (ThemeStartsWith(Value, TEXT("token:"))) {
        return ResolveRuntimeTokenMetricRecursive(Value + 6, Metric, Depth + 1);
    }

    if (ParseMetricLiteral(Value, Metric)) return TRUE;
    return ResolveBuiltinMetricTokenByName(TokenName, Metric);
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

BOOL DesktopThemeResolveSystemColor(U32 SystemColorIndex, COLOR* Color) {
    U32 TokenID;
    LPCSTR TokenName = NULL;

    if (Color == NULL) return FALSE;
    if (ResolveSystemColorTokenID(SystemColorIndex, &TokenID) == FALSE) return FALSE;

    if (ResolveBuiltinColorTokenName(TokenID, &TokenName)) {
        if (DesktopThemeResolveTokenColorByName(TokenName, Color)) return TRUE;
    }

    return ResolveBuiltinColorToken(TokenID, Color);
}

BOOL DesktopThemeResolveTokenColorByName(LPCSTR TokenName, COLOR* Color) {
    if (TokenName == NULL || Color == NULL) return FALSE;

    if (ResolveRuntimeTokenColorRecursive(TokenName, Color, 0)) return TRUE;

    return ResolveBuiltinColorTokenByName(TokenName, Color);
}

/***************************************************************************/

BOOL DesktopThemeResolveTokenMetricByName(LPCSTR TokenName, U32* Value) {
    if (TokenName == NULL || Value == NULL) return FALSE;

    if (ResolveRuntimeTokenMetricRecursive(TokenName, Value, 0)) return TRUE;

    return ResolveBuiltinMetricTokenByName(TokenName, Value);
}

/***************************************************************************/

void DesktopThemeSyncSystemObjects(void) {
    UINT Index;
    COLOR Color;

    for (Index = 0; Index < (sizeof(BuiltinSystemColorBindings) / sizeof(BuiltinSystemColorBindings[0])); Index++) {
        if (DesktopThemeResolveSystemColor(BuiltinSystemColorBindings[Index].SystemColor, &Color) == FALSE) continue;

        SAFE_USE_VALID_ID(BuiltinSystemColorBindings[Index].Brush, KOID_BRUSH) { BuiltinSystemColorBindings[Index].Brush->Color = Color; }
        SAFE_USE_VALID_ID(BuiltinSystemColorBindings[Index].Pen, KOID_PEN) { BuiltinSystemColorBindings[Index].Pen->Color = Color; }
    }
}

/***************************************************************************/
