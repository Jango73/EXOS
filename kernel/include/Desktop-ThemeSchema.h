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

#ifndef DESKTOP_THEME_SCHEMA_H_INCLUDED
#define DESKTOP_THEME_SCHEMA_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/
// Theme parser limits

#define DESKTOP_THEME_MAX_FILE_SIZE N_256KB
#define DESKTOP_THEME_MAX_TOKEN_COUNT 512
#define DESKTOP_THEME_MAX_RECIPE_COUNT 256
#define DESKTOP_THEME_MAX_PRIMITIVE_COUNT 4096

/************************************************************************/
// Top-level sections

#define DESKTOP_THEME_SECTION_THEME 0x00000001
#define DESKTOP_THEME_SECTION_TOKENS 0x00000002
#define DESKTOP_THEME_SECTION_ELEMENTS 0x00000003
#define DESKTOP_THEME_SECTION_RECIPES 0x00000004
#define DESKTOP_THEME_SECTION_BINDINGS 0x00000005

/************************************************************************/
// Element families

#define DESKTOP_THEME_FAMILY_DESKTOP_ROOT 0x00000001
#define DESKTOP_THEME_FAMILY_WINDOW_FRAME 0x00000002
#define DESKTOP_THEME_FAMILY_WINDOW_CLIENT 0x00000003
#define DESKTOP_THEME_FAMILY_WINDOW_TITLEBAR 0x00000004
#define DESKTOP_THEME_FAMILY_WINDOW_TITLE_TEXT 0x00000005
#define DESKTOP_THEME_FAMILY_WINDOW_BUTTON 0x00000006
#define DESKTOP_THEME_FAMILY_WINDOW_RESIZE 0x00000007
#define DESKTOP_THEME_FAMILY_CONTROL_BUTTON 0x00000008
#define DESKTOP_THEME_FAMILY_TEXTBOX 0x00000009
#define DESKTOP_THEME_FAMILY_MENU 0x0000000A
#define DESKTOP_THEME_FAMILY_MENU_ITEM 0x0000000B

/************************************************************************/
// Property value types

#define DESKTOP_THEME_PROPERTY_TYPE_COLOR 0x00000001
#define DESKTOP_THEME_PROPERTY_TYPE_METRIC 0x00000002
#define DESKTOP_THEME_PROPERTY_TYPE_BOOLEAN 0x00000003
#define DESKTOP_THEME_PROPERTY_TYPE_STRING 0x00000004

/************************************************************************/
// Family bit masks used by property schema

#define DESKTOP_THEME_FAMILY_MASK_DESKTOP_ROOT (((U32)1) << 0)
#define DESKTOP_THEME_FAMILY_MASK_WINDOW_FRAME (((U32)1) << 1)
#define DESKTOP_THEME_FAMILY_MASK_WINDOW_CLIENT (((U32)1) << 2)
#define DESKTOP_THEME_FAMILY_MASK_WINDOW_TITLEBAR (((U32)1) << 3)
#define DESKTOP_THEME_FAMILY_MASK_WINDOW_TITLE_TEXT (((U32)1) << 4)
#define DESKTOP_THEME_FAMILY_MASK_WINDOW_BUTTON (((U32)1) << 5)
#define DESKTOP_THEME_FAMILY_MASK_WINDOW_RESIZE (((U32)1) << 6)
#define DESKTOP_THEME_FAMILY_MASK_CONTROL_BUTTON (((U32)1) << 7)
#define DESKTOP_THEME_FAMILY_MASK_TEXTBOX (((U32)1) << 8)
#define DESKTOP_THEME_FAMILY_MASK_MENU (((U32)1) << 9)
#define DESKTOP_THEME_FAMILY_MASK_MENU_ITEM (((U32)1) << 10)

/************************************************************************/
// Type definitions

typedef struct tag_DESKTOP_THEME_SCHEMA_LIMITS {
    U32 MaxFileSize;
    U32 MaxTokenCount;
    U32 MaxRecipeCount;
    U32 MaxPrimitiveCount;
} DESKTOP_THEME_SCHEMA_LIMITS, *LPDESKTOP_THEME_SCHEMA_LIMITS;

/************************************************************************/
// External functions

BOOL DesktopThemeSchemaIsTopLevelSection(LPCSTR Name);
BOOL DesktopThemeSchemaGetTopLevelSectionID(LPCSTR Name, U32* SectionID);
BOOL DesktopThemeSchemaIsElementID(LPCSTR Name);
BOOL DesktopThemeSchemaGetElementFamily(LPCSTR Name, U32* FamilyID);
BOOL DesktopThemeSchemaIsStateID(LPCSTR Name);
BOOL DesktopThemeSchemaGetPropertyType(U32 FamilyID, LPCSTR PropertyName, U32* PropertyType);
BOOL DesktopThemeSchemaGetLimits(LPDESKTOP_THEME_SCHEMA_LIMITS Limits);
BOOL DesktopThemeSchemaIsValidStateFallbackOrder(LPCSTR First, LPCSTR Second, LPCSTR Third);

/************************************************************************/

#endif  // DESKTOP_THEME_SCHEMA_H_INCLUDED
