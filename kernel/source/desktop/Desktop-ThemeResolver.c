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
#include "Desktop-ThemeCommon.h"
#include "Desktop-ThemeRuntime.h"
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
    {TEXT("window.client"), TEXT("normal"), TEXT("background"), TEXT("token:color.client.background")},
    {TEXT("window.client"), TEXT("active"), TEXT("background"), TEXT("token:color.client.background")},
    {TEXT("window.client"), TEXT("focused"), TEXT("background"), TEXT("token:color.client.background")},
    {TEXT("button.body"), TEXT("normal"), TEXT("background"), TEXT("token:color.button.background")},
    {TEXT("button.body"), TEXT("hover"), TEXT("background"), TEXT("token:color.button.background.hover")},
    {TEXT("button.body"), TEXT("pressed"), TEXT("background"), TEXT("token:color.button.background.pressed")},
    {TEXT("button.body"), TEXT("disabled"), TEXT("background"), TEXT("token:color.button.background.disabled")},
    {TEXT("button.body"), TEXT("normal"), TEXT("border_color"), TEXT("token:color.button.border")},
    {TEXT("button.body"), TEXT("hover"), TEXT("border_color"), TEXT("token:color.button.border.hover")},
    {TEXT("button.body"), TEXT("pressed"), TEXT("border_color"), TEXT("token:color.button.border.pressed")},
    {TEXT("button.body"), TEXT("disabled"), TEXT("border_color"), TEXT("token:color.button.border")},
    {TEXT("button.body"), TEXT("normal"), TEXT("border_thickness"), TEXT("0")},
    {TEXT("button.body"), TEXT("hover"), TEXT("border_thickness"), TEXT("0")},
    {TEXT("button.body"), TEXT("pressed"), TEXT("border_thickness"), TEXT("0")},
    {TEXT("button.body"), TEXT("disabled"), TEXT("border_thickness"), TEXT("0")},
    {TEXT("button.text"), TEXT("normal"), TEXT("foreground"), TEXT("token:color.text.normal")},
    {TEXT("button.text"), TEXT("hover"), TEXT("foreground"), TEXT("token:color.text.normal")},
    {TEXT("button.text"), TEXT("pressed"), TEXT("foreground"), TEXT("token:color.text.normal")},
    {TEXT("button.text"), TEXT("disabled"), TEXT("foreground"), TEXT("token:color.button.text.disabled")},
    {TEXT("window.border"), TEXT("normal"), TEXT("border_color"), TEXT("token:color.window.border")},
    {TEXT("window.border"), TEXT("active"), TEXT("border_color"), TEXT("token:color.window.border")},
    {TEXT("window.border"), TEXT("focused"), TEXT("border_color"), TEXT("token:color.window.border")},
    {TEXT("window.border"), TEXT("normal"), TEXT("border_thickness"), TEXT("2")},
    {TEXT("window.border"), TEXT("active"), TEXT("border_thickness"), TEXT("2")},
    {TEXT("window.border"), TEXT("focused"), TEXT("border_thickness"), TEXT("2")},
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

/**
 * @brief Try resolving one runtime level 1 value for element/state/property.
 * @param ElementID Element identifier.
 * @param StateID State identifier.
 * @param PropertyName Property name.
 * @param Value Receives textual property value.
 * @param ValueBufferSize Output buffer size.
 * @return TRUE when runtime contains the requested key.
 */
static BOOL ResolveRuntimeLevel1Text(
    LPCSTR ElementID,
    LPCSTR StateID,
    LPCSTR PropertyName,
    LPSTR Value,
    UINT ValueBufferSize
) {
    STR Key[256];
    LPCSTR RuntimeValue = NULL;
    UINT ValueLength;

    if (ElementID == NULL || StateID == NULL || PropertyName == NULL || Value == NULL) return FALSE;

    Key[0] = STR_NULL;

    if (StringCompareNC(StateID, TEXT("normal")) == 0) {
        StringCopy(Key, ElementID);
        StringConcat(Key, TEXT("."));
        StringConcat(Key, PropertyName);
        if (DesktopThemeLookupElementPropertyValue(NULL, Key, &RuntimeValue)) {
            ValueLength = StringLength(RuntimeValue);
            if (ValueLength + 1 > ValueBufferSize) return FALSE;
            StringCopy(Value, RuntimeValue);
            return TRUE;
        }
    }

    StringCopy(Key, ElementID);
    StringConcat(Key, TEXT(".states."));
    StringConcat(Key, StateID);
    StringConcat(Key, TEXT("."));
    StringConcat(Key, PropertyName);

    if (DesktopThemeLookupElementPropertyValue(NULL, Key, &RuntimeValue) == FALSE) return FALSE;

    ValueLength = StringLength(RuntimeValue);
    if (ValueLength + 1 > ValueBufferSize) return FALSE;
    StringCopy(Value, RuntimeValue);
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

    if (ResolveRuntimeLevel1Text(ElementID, State1, PropertyName, Value, ValueBufferSize)) return TRUE;
    if (ResolveRuntimeLevel1Text(ElementID, State2, PropertyName, Value, ValueBufferSize)) return TRUE;
    if (ResolveRuntimeLevel1Text(ElementID, State3, PropertyName, Value, ValueBufferSize)) return TRUE;

    for (Index = 0; Index < (sizeof(BuiltinLevel1Properties) / sizeof(BuiltinLevel1Properties[0])); Index++) {
        if (IsMatchingPropertyEntry(&BuiltinLevel1Properties[Index], ElementID, State1, PropertyName) == FALSE) continue;
        ValueLength = StringLength(BuiltinLevel1Properties[Index].Value);
        if (ValueLength + 1 > ValueBufferSize) return FALSE;
        StringCopy(Value, BuiltinLevel1Properties[Index].Value);
        return TRUE;
    }

    for (Index = 0; Index < (sizeof(BuiltinLevel1Properties) / sizeof(BuiltinLevel1Properties[0])); Index++) {
        if (IsMatchingPropertyEntry(&BuiltinLevel1Properties[Index], ElementID, State2, PropertyName) == FALSE) continue;
        ValueLength = StringLength(BuiltinLevel1Properties[Index].Value);
        if (ValueLength + 1 > ValueBufferSize) return FALSE;
        StringCopy(Value, BuiltinLevel1Properties[Index].Value);
        return TRUE;
    }

    for (Index = 0; Index < (sizeof(BuiltinLevel1Properties) / sizeof(BuiltinLevel1Properties[0])); Index++) {
        if (IsMatchingPropertyEntry(&BuiltinLevel1Properties[Index], ElementID, State3, PropertyName) == FALSE) continue;
        ValueLength = StringLength(BuiltinLevel1Properties[Index].Value);
        if (ValueLength + 1 > ValueBufferSize) return FALSE;
        StringCopy(Value, BuiltinLevel1Properties[Index].Value);
        return TRUE;
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

    return DesktopThemeParseColorLiteral(Value, Color);
}

/***************************************************************************/

BOOL DesktopThemeResolveLevel1Metric(LPCSTR ElementID, LPCSTR StateID, LPCSTR PropertyName, U32* Metric) {
    STR Value[128];

    if (Metric == NULL) return FALSE;
    if (!DesktopThemeResolveLevel1Text(ElementID, StateID, PropertyName, Value, sizeof(Value))) return FALSE;

    if (ThemeStartsWith(Value, TEXT("token:"))) {
        return DesktopThemeResolveTokenMetricByName(Value + 6, Metric);
    }

    *Metric = StringToU32(Value);
    return TRUE;
}

/***************************************************************************/
