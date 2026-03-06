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


    Desktop theme schema contract

\************************************************************************/

#include "Desktop-ThemeSchema.h"
#include "CoreString.h"

/***************************************************************************/
// Type definitions

typedef struct tag_THEME_SCHEMA_NAME_ID_ENTRY {
    LPCSTR Name;
    U32 ID;
} THEME_SCHEMA_NAME_ID_ENTRY, *LPTHEME_SCHEMA_NAME_ID_ENTRY;

typedef struct tag_THEME_SCHEMA_PROPERTY_ENTRY {
    LPCSTR Name;
    U32 Type;
    U64 FamilyMask;
} THEME_SCHEMA_PROPERTY_ENTRY, *LPTHEME_SCHEMA_PROPERTY_ENTRY;

/***************************************************************************/
// Other declarations

static const THEME_SCHEMA_NAME_ID_ENTRY TopLevelSections[] = {
    {TEXT("theme"), DESKTOP_THEME_SECTION_THEME},
    {TEXT("tokens"), DESKTOP_THEME_SECTION_TOKENS},
    {TEXT("elements"), DESKTOP_THEME_SECTION_ELEMENTS},
    {TEXT("recipes"), DESKTOP_THEME_SECTION_RECIPES},
    {TEXT("bindings"), DESKTOP_THEME_SECTION_BINDINGS},
};

static const THEME_SCHEMA_NAME_ID_ENTRY Elements[] = {
    {TEXT("desktop.root"), DESKTOP_THEME_FAMILY_DESKTOP_ROOT},
    {TEXT("window.frame"), DESKTOP_THEME_FAMILY_WINDOW_FRAME},
    {TEXT("window.client"), DESKTOP_THEME_FAMILY_WINDOW_CLIENT},
    {TEXT("window.titlebar"), DESKTOP_THEME_FAMILY_WINDOW_TITLEBAR},
    {TEXT("window.title.text"), DESKTOP_THEME_FAMILY_WINDOW_TITLE_TEXT},
    {TEXT("window.border"), DESKTOP_THEME_FAMILY_WINDOW_FRAME},
    {TEXT("window.button.close"), DESKTOP_THEME_FAMILY_WINDOW_BUTTON},
    {TEXT("window.button.maximize"), DESKTOP_THEME_FAMILY_WINDOW_BUTTON},
    {TEXT("window.button.minimize"), DESKTOP_THEME_FAMILY_WINDOW_BUTTON},
    {TEXT("window.resize.left"), DESKTOP_THEME_FAMILY_WINDOW_RESIZE},
    {TEXT("window.resize.right"), DESKTOP_THEME_FAMILY_WINDOW_RESIZE},
    {TEXT("window.resize.top"), DESKTOP_THEME_FAMILY_WINDOW_RESIZE},
    {TEXT("window.resize.bottom"), DESKTOP_THEME_FAMILY_WINDOW_RESIZE},
    {TEXT("window.resize.top_left"), DESKTOP_THEME_FAMILY_WINDOW_RESIZE},
    {TEXT("window.resize.top_right"), DESKTOP_THEME_FAMILY_WINDOW_RESIZE},
    {TEXT("window.resize.bottom_left"), DESKTOP_THEME_FAMILY_WINDOW_RESIZE},
    {TEXT("window.resize.bottom_right"), DESKTOP_THEME_FAMILY_WINDOW_RESIZE},
    {TEXT("button.body"), DESKTOP_THEME_FAMILY_CONTROL_BUTTON},
    {TEXT("button.text"), DESKTOP_THEME_FAMILY_CONTROL_BUTTON},
    {TEXT("textbox.body"), DESKTOP_THEME_FAMILY_TEXTBOX},
    {TEXT("textbox.text"), DESKTOP_THEME_FAMILY_TEXTBOX},
    {TEXT("textbox.caret"), DESKTOP_THEME_FAMILY_TEXTBOX},
    {TEXT("menu.background"), DESKTOP_THEME_FAMILY_MENU},
    {TEXT("menu.item"), DESKTOP_THEME_FAMILY_MENU_ITEM},
    {TEXT("menu.item.text"), DESKTOP_THEME_FAMILY_MENU_ITEM},
};

static const THEME_SCHEMA_NAME_ID_ENTRY States[] = {
    {TEXT("normal"), 0},
    {TEXT("hover"), 1},
    {TEXT("pressed"), 2},
    {TEXT("focused"), 3},
    {TEXT("active"), 4},
    {TEXT("disabled"), 5},
    {TEXT("checked"), 6},
    {TEXT("selected"), 7},
};

static const THEME_SCHEMA_PROPERTY_ENTRY Properties[] = {
    {TEXT("background"), DESKTOP_THEME_PROPERTY_TYPE_COLOR,
     DESKTOP_THEME_FAMILY_MASK_DESKTOP_ROOT | DESKTOP_THEME_FAMILY_MASK_WINDOW_FRAME | DESKTOP_THEME_FAMILY_MASK_WINDOW_CLIENT |
         DESKTOP_THEME_FAMILY_MASK_WINDOW_TITLEBAR | DESKTOP_THEME_FAMILY_MASK_WINDOW_BUTTON | DESKTOP_THEME_FAMILY_MASK_CONTROL_BUTTON |
         DESKTOP_THEME_FAMILY_MASK_TEXTBOX | DESKTOP_THEME_FAMILY_MASK_MENU | DESKTOP_THEME_FAMILY_MASK_MENU_ITEM},
    {TEXT("background2"), DESKTOP_THEME_PROPERTY_TYPE_COLOR, DESKTOP_THEME_FAMILY_MASK_WINDOW_TITLEBAR | DESKTOP_THEME_FAMILY_MASK_WINDOW_BUTTON},
    {TEXT("foreground"), DESKTOP_THEME_PROPERTY_TYPE_COLOR,
     DESKTOP_THEME_FAMILY_MASK_WINDOW_TITLE_TEXT | DESKTOP_THEME_FAMILY_MASK_CONTROL_BUTTON | DESKTOP_THEME_FAMILY_MASK_TEXTBOX |
         DESKTOP_THEME_FAMILY_MASK_MENU_ITEM},
    {TEXT("border_color"), DESKTOP_THEME_PROPERTY_TYPE_COLOR,
     DESKTOP_THEME_FAMILY_MASK_WINDOW_FRAME | DESKTOP_THEME_FAMILY_MASK_WINDOW_BUTTON | DESKTOP_THEME_FAMILY_MASK_CONTROL_BUTTON |
         DESKTOP_THEME_FAMILY_MASK_TEXTBOX | DESKTOP_THEME_FAMILY_MASK_MENU | DESKTOP_THEME_FAMILY_MASK_MENU_ITEM},
    {TEXT("glyph_color"), DESKTOP_THEME_PROPERTY_TYPE_COLOR, DESKTOP_THEME_FAMILY_MASK_WINDOW_BUTTON | DESKTOP_THEME_FAMILY_MASK_TEXTBOX},
    {TEXT("title_height"), DESKTOP_THEME_PROPERTY_TYPE_METRIC, DESKTOP_THEME_FAMILY_MASK_WINDOW_TITLEBAR},
    {TEXT("border_thickness"), DESKTOP_THEME_PROPERTY_TYPE_METRIC,
     DESKTOP_THEME_FAMILY_MASK_WINDOW_FRAME | DESKTOP_THEME_FAMILY_MASK_WINDOW_BUTTON | DESKTOP_THEME_FAMILY_MASK_CONTROL_BUTTON |
         DESKTOP_THEME_FAMILY_MASK_TEXTBOX | DESKTOP_THEME_FAMILY_MASK_MENU},
    {TEXT("padding"), DESKTOP_THEME_PROPERTY_TYPE_METRIC,
     DESKTOP_THEME_FAMILY_MASK_WINDOW_TITLEBAR | DESKTOP_THEME_FAMILY_MASK_WINDOW_BUTTON | DESKTOP_THEME_FAMILY_MASK_CONTROL_BUTTON |
         DESKTOP_THEME_FAMILY_MASK_TEXTBOX | DESKTOP_THEME_FAMILY_MASK_MENU_ITEM},
    {TEXT("corner_radius"), DESKTOP_THEME_PROPERTY_TYPE_METRIC,
     DESKTOP_THEME_FAMILY_MASK_WINDOW_FRAME | DESKTOP_THEME_FAMILY_MASK_WINDOW_BUTTON | DESKTOP_THEME_FAMILY_MASK_CONTROL_BUTTON |
         DESKTOP_THEME_FAMILY_MASK_TEXTBOX | DESKTOP_THEME_FAMILY_MASK_MENU},
    {TEXT("button_size"), DESKTOP_THEME_PROPERTY_TYPE_METRIC, DESKTOP_THEME_FAMILY_MASK_WINDOW_BUTTON},
    {TEXT("button_spacing"), DESKTOP_THEME_PROPERTY_TYPE_METRIC, DESKTOP_THEME_FAMILY_MASK_WINDOW_TITLEBAR},
    {TEXT("visible"), DESKTOP_THEME_PROPERTY_TYPE_BOOLEAN, DESKTOP_THEME_FAMILY_MASK_WINDOW_BUTTON | DESKTOP_THEME_FAMILY_MASK_WINDOW_RESIZE},
    {TEXT("glyph"), DESKTOP_THEME_PROPERTY_TYPE_STRING, DESKTOP_THEME_FAMILY_MASK_WINDOW_BUTTON},
    {TEXT("font"), DESKTOP_THEME_PROPERTY_TYPE_STRING,
     DESKTOP_THEME_FAMILY_MASK_WINDOW_TITLE_TEXT | DESKTOP_THEME_FAMILY_MASK_CONTROL_BUTTON | DESKTOP_THEME_FAMILY_MASK_TEXTBOX |
         DESKTOP_THEME_FAMILY_MASK_MENU_ITEM},
};

/***************************************************************************/

/**
 * @brief Map a family identifier to schema mask bit.
 * @param FamilyID One DESKTOP_THEME_FAMILY_* value.
 * @return Matching family mask bit or 0 when unknown.
 */
static U64 GetFamilyMask(U32 FamilyID) {
    switch (FamilyID) {
        case DESKTOP_THEME_FAMILY_DESKTOP_ROOT:
            return DESKTOP_THEME_FAMILY_MASK_DESKTOP_ROOT;
        case DESKTOP_THEME_FAMILY_WINDOW_FRAME:
            return DESKTOP_THEME_FAMILY_MASK_WINDOW_FRAME;
        case DESKTOP_THEME_FAMILY_WINDOW_CLIENT:
            return DESKTOP_THEME_FAMILY_MASK_WINDOW_CLIENT;
        case DESKTOP_THEME_FAMILY_WINDOW_TITLEBAR:
            return DESKTOP_THEME_FAMILY_MASK_WINDOW_TITLEBAR;
        case DESKTOP_THEME_FAMILY_WINDOW_TITLE_TEXT:
            return DESKTOP_THEME_FAMILY_MASK_WINDOW_TITLE_TEXT;
        case DESKTOP_THEME_FAMILY_WINDOW_BUTTON:
            return DESKTOP_THEME_FAMILY_MASK_WINDOW_BUTTON;
        case DESKTOP_THEME_FAMILY_WINDOW_RESIZE:
            return DESKTOP_THEME_FAMILY_MASK_WINDOW_RESIZE;
        case DESKTOP_THEME_FAMILY_CONTROL_BUTTON:
            return DESKTOP_THEME_FAMILY_MASK_CONTROL_BUTTON;
        case DESKTOP_THEME_FAMILY_TEXTBOX:
            return DESKTOP_THEME_FAMILY_MASK_TEXTBOX;
        case DESKTOP_THEME_FAMILY_MENU:
            return DESKTOP_THEME_FAMILY_MASK_MENU;
        case DESKTOP_THEME_FAMILY_MENU_ITEM:
            return DESKTOP_THEME_FAMILY_MASK_MENU_ITEM;
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Find a name in a schema table.
 * @param Table Name/identifier table.
 * @param TableCount Number of table entries.
 * @param Name Name to search.
 * @param ID Receives identifier on success.
 * @return TRUE when name is found.
 */
static BOOL FindNameID(const THEME_SCHEMA_NAME_ID_ENTRY* Table, UINT TableCount, LPCSTR Name, U32* ID) {
    UINT Index;

    if (Table == NULL || Name == NULL || ID == NULL) return FALSE;

    for (Index = 0; Index < TableCount; Index++) {
        if (StringCompareNC(Table[Index].Name, Name) == 0) {
            *ID = Table[Index].ID;
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Tell whether a section name is part of the frozen top-level schema.
 * @param Name Section name.
 * @return TRUE when recognized.
 */
BOOL DesktopThemeSchemaIsTopLevelSection(LPCSTR Name) {
    U32 SectionID;

    return DesktopThemeSchemaGetTopLevelSectionID(Name, &SectionID);
}

/***************************************************************************/

/**
 * @brief Resolve top-level section identifier from its name.
 * @param Name Section name.
 * @param SectionID Receives DESKTOP_THEME_SECTION_* identifier.
 * @return TRUE when recognized.
 */
BOOL DesktopThemeSchemaGetTopLevelSectionID(LPCSTR Name, U32* SectionID) {
    return FindNameID(TopLevelSections, sizeof(TopLevelSections) / sizeof(TopLevelSections[0]), Name, SectionID);
}

/***************************************************************************/

/**
 * @brief Tell whether an element identifier is part of the frozen schema.
 * @param Name Element identifier.
 * @return TRUE when recognized.
 */
BOOL DesktopThemeSchemaIsElementID(LPCSTR Name) {
    U32 FamilyID;

    return DesktopThemeSchemaGetElementFamily(Name, &FamilyID);
}

/***************************************************************************/

/**
 * @brief Resolve an element identifier to its family.
 * @param Name Element identifier.
 * @param FamilyID Receives DESKTOP_THEME_FAMILY_*.
 * @return TRUE when recognized.
 */
BOOL DesktopThemeSchemaGetElementFamily(LPCSTR Name, U32* FamilyID) {
    return FindNameID(Elements, sizeof(Elements) / sizeof(Elements[0]), Name, FamilyID);
}

/***************************************************************************/

/**
 * @brief Tell whether a state identifier is part of the frozen schema.
 * @param Name State identifier.
 * @return TRUE when recognized.
 */
BOOL DesktopThemeSchemaIsStateID(LPCSTR Name) {
    U32 StateID;

    return FindNameID(States, sizeof(States) / sizeof(States[0]), Name, &StateID);
}

/***************************************************************************/

/**
 * @brief Resolve allowed property type for one family/property pair.
 * @param FamilyID Target family (DESKTOP_THEME_FAMILY_*).
 * @param PropertyName Property name.
 * @param PropertyType Receives DESKTOP_THEME_PROPERTY_TYPE_*.
 * @return TRUE when property is allowed for family.
 */
BOOL DesktopThemeSchemaGetPropertyType(U32 FamilyID, LPCSTR PropertyName, U32* PropertyType) {
    U64 FamilyMask;
    UINT Index;

    if (PropertyName == NULL || PropertyType == NULL) return FALSE;

    FamilyMask = GetFamilyMask(FamilyID);
    if (FamilyMask == 0) return FALSE;

    for (Index = 0; Index < (sizeof(Properties) / sizeof(Properties[0])); Index++) {
        if (StringCompareNC(Properties[Index].Name, PropertyName) != 0) continue;
        if ((Properties[Index].FamilyMask & FamilyMask) == 0) return FALSE;

        *PropertyType = Properties[Index].Type;
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Return frozen parser limits for desktop theme files.
 * @param Limits Receives parser limits.
 * @return TRUE on success.
 */
BOOL DesktopThemeSchemaGetLimits(LPDESKTOP_THEME_SCHEMA_LIMITS Limits) {
    if (Limits == NULL) return FALSE;

    Limits->MaxFileSize = DESKTOP_THEME_MAX_FILE_SIZE;
    Limits->MaxTokenCount = DESKTOP_THEME_MAX_TOKEN_COUNT;
    Limits->MaxRecipeCount = DESKTOP_THEME_MAX_RECIPE_COUNT;
    Limits->MaxPrimitiveCount = DESKTOP_THEME_MAX_PRIMITIVE_COUNT;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Validate a three-step state fallback sequence.
 * @param First First state (expected exact match).
 * @param Second Second state (expected partial fallback).
 * @param Third Third state (expected final fallback).
 * @return TRUE when sequence is valid.
 */
BOOL DesktopThemeSchemaIsValidStateFallbackOrder(LPCSTR First, LPCSTR Second, LPCSTR Third) {
    if (DesktopThemeSchemaIsStateID(First) == FALSE) return FALSE;
    if (DesktopThemeSchemaIsStateID(Second) == FALSE) return FALSE;
    if (StringCompareNC(Third, TEXT("normal")) != 0) return FALSE;

    return TRUE;
}

/***************************************************************************/
