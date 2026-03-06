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


    Desktop theme level 1 resolver

\************************************************************************/

#include "Desktop-ThemeResolver.h"
#include "Desktop-ThemeTokens.h"
#include "CoreString.h"

/***************************************************************************/
// Type definitions

typedef struct tag_LEVEL1_PROPERTY_ENTRY {
    LPCSTR ElementID;
    LPCSTR StateID;
    LPCSTR PropertyName;
    LPCSTR Value;
} LEVEL1_PROPERTY_ENTRY, *LPLEVEL1_PROPERTY_ENTRY;

/***************************************************************************/
// Other declarations

static const LEVEL1_PROPERTY_ENTRY BuiltinLevel1Properties[] = {
    {TEXT("desktop.root"), TEXT("normal"), TEXT("background"), TEXT("token:color.desktop.background")},
    {TEXT("window.frame"), TEXT("normal"), TEXT("background"), TEXT("token:color.window.frame")},
    {TEXT("window.frame"), TEXT("active"), TEXT("background"), TEXT("token:color.window.frame")},
    {TEXT("window.frame"), TEXT("focused"), TEXT("background"), TEXT("token:color.window.frame")},
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

/***************************************************************************/

/**
 * @brief Parse one hexadecimal color string.
 * @param Value Color text as "#RRGGBB", "#AARRGGBB", "0xRRGGBB", or "0xAARRGGBB".
 * @param Color Receives parsed color.
 * @return TRUE on success.
 */
static BOOL ParseColorLiteral(LPCSTR Value, COLOR* Color) {
    STR HexBuffer[16];
    UINT Index;
    UINT Length;

    if (Value == NULL || Color == NULL) return FALSE;

    if (ThemeStartsWith(Value, TEXT("0x")) || ThemeStartsWith(Value, TEXT("0X"))) {
        *Color = StringToU32(Value);
        return TRUE;
    }

    if (Value[0] != '#') return FALSE;

    Length = StringLength(Value);
    if (Length != 7 && Length != 9) return FALSE;

    HexBuffer[0] = '0';
    HexBuffer[1] = 'x';
    for (Index = 1; Index < Length; Index++) {
        HexBuffer[Index + 1] = Value[Index];
    }
    HexBuffer[Length + 1] = STR_NULL;
    *Color = StringToU32(HexBuffer);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Build state fallback chain (exact -> partial -> normal).
 * @param InputState Requested state.
 * @param Out1 First fallback candidate.
 * @param Out2 Second fallback candidate.
 * @param Out3 Final fallback candidate.
 */
static void BuildStateFallbackChain(LPCSTR InputState, LPSTR Out1, LPSTR Out2, LPSTR Out3) {
    UINT Length;
    UINT Index;

    StringCopy(Out1, TEXT("normal"));
    StringCopy(Out2, TEXT("normal"));
    StringCopy(Out3, TEXT("normal"));

    if (InputState == NULL || InputState[0] == STR_NULL) return;

    StringCopy(Out1, InputState);
    StringCopy(Out2, InputState);

    Length = StringLength(Out2);
    for (Index = Length; Index > 0; Index--) {
        if (Out2[Index] == '.') {
            Out2[Index] = STR_NULL;
            return;
        }
    }

    StringCopy(Out2, TEXT("normal"));
}

/***************************************************************************/

/**
 * @brief Match one property entry.
 * @param Entry Entry to test.
 * @param ElementID Target element identifier.
 * @param StateID Target state identifier.
 * @param PropertyName Target property name.
 * @return TRUE when entry matches.
 */
static BOOL IsMatchingPropertyEntry(
    const LEVEL1_PROPERTY_ENTRY* Entry,
    LPCSTR ElementID,
    LPCSTR StateID,
    LPCSTR PropertyName
) {
    if (StringCompareNC(Entry->ElementID, ElementID) != 0) return FALSE;
    if (StringCompareNC(Entry->StateID, StateID) != 0) return FALSE;
    if (StringCompareNC(Entry->PropertyName, PropertyName) != 0) return FALSE;
    return TRUE;
}

/***************************************************************************/

BOOL DesktopThemeResolveLevel1Text(
    LPCSTR ElementID,
    LPCSTR StateID,
    LPCSTR PropertyName,
    LPSTR Value,
    UINT ValueBufferSize
) {
    STR State1[64];
    STR State2[64];
    STR State3[64];
    UINT Index;
    UINT ValueLength;

    if (ElementID == NULL || PropertyName == NULL || Value == NULL || ValueBufferSize == 0) return FALSE;

    BuildStateFallbackChain(StateID, State1, State2, State3);

    for (Index = 0; Index < (sizeof(BuiltinLevel1Properties) / sizeof(BuiltinLevel1Properties[0])); Index++) {
        if (IsMatchingPropertyEntry(&BuiltinLevel1Properties[Index], ElementID, State1, PropertyName) ||
            IsMatchingPropertyEntry(&BuiltinLevel1Properties[Index], ElementID, State2, PropertyName) ||
            IsMatchingPropertyEntry(&BuiltinLevel1Properties[Index], ElementID, State3, PropertyName)) {
            ValueLength = StringLength(BuiltinLevel1Properties[Index].Value);
            if (ValueLength + 1 > ValueBufferSize) return FALSE;
            StringCopy(Value, BuiltinLevel1Properties[Index].Value);
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

BOOL DesktopThemeResolveLevel1Color(LPCSTR ElementID, LPCSTR StateID, LPCSTR PropertyName, COLOR* Color) {
    STR Value[128];

    if (Color == NULL) return FALSE;
    if (DesktopThemeResolveLevel1Text(ElementID, StateID, PropertyName, Value, sizeof(Value)) == FALSE) return FALSE;

    if (ThemeStartsWith(Value, TEXT("token:"))) {
        return DesktopThemeResolveTokenColorByName(Value + 6, Color);
    }

    return ParseColorLiteral(Value, Color);
}

/***************************************************************************/

BOOL DesktopThemeResolveLevel1Metric(LPCSTR ElementID, LPCSTR StateID, LPCSTR PropertyName, U32* Metric) {
    STR Value[128];

    if (Metric == NULL) return FALSE;
    if (DesktopThemeResolveLevel1Text(ElementID, StateID, PropertyName, Value, sizeof(Value)) == FALSE) return FALSE;

    if (ThemeStartsWith(Value, TEXT("token:"))) {
        return DesktopThemeResolveTokenMetricByName(Value + 6, Metric);
    }

    *Metric = StringToU32(Value);
    return TRUE;
}

/***************************************************************************/
