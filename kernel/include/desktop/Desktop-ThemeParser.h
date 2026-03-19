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


    Desktop theme parser

\************************************************************************/

#ifndef DESKTOP_THEME_PARSER_H_INCLUDED
#define DESKTOP_THEME_PARSER_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "Desktop-ThemeSchema.h"

/************************************************************************/

#define DESKTOP_THEME_STATUS_SUCCESS 0x00000000
#define DESKTOP_THEME_STATUS_BAD_PARAMETER 0x00000001
#define DESKTOP_THEME_STATUS_TOO_LARGE 0x00000002
#define DESKTOP_THEME_STATUS_INVALID_TOML 0x00000003
#define DESKTOP_THEME_STATUS_UNKNOWN_SECTION 0x00000004
#define DESKTOP_THEME_STATUS_INVALID_KEY 0x00000005
#define DESKTOP_THEME_STATUS_DUPLICATE_KEY 0x00000006
#define DESKTOP_THEME_STATUS_INVALID_PROPERTY 0x00000007
#define DESKTOP_THEME_STATUS_INVALID_PROPERTY_TYPE 0x00000008
#define DESKTOP_THEME_STATUS_INVALID_REFERENCE 0x00000009
#define DESKTOP_THEME_STATUS_LIMIT_EXCEEDED 0x0000000A
#define DESKTOP_THEME_STATUS_NO_MEMORY 0x0000000B

/************************************************************************/

typedef struct tag_DESKTOP_THEME_TABLE_ENTRY {
    LPSTR Key;
    LPSTR Value;
} DESKTOP_THEME_TABLE_ENTRY, *LPDESKTOP_THEME_TABLE_ENTRY;

typedef struct tag_DESKTOP_THEME_RUNTIME {
    BOOL IsBuiltin;
    U32 TokenCount;
    U32 ElementPropertyCount;
    U32 RecipeCount;
    U32 BindingCount;
    U32 PrimitiveCount;
    LPDESKTOP_THEME_TABLE_ENTRY Tokens;
    LPDESKTOP_THEME_TABLE_ENTRY ElementProperties;
    LPDESKTOP_THEME_TABLE_ENTRY Recipes;
    LPDESKTOP_THEME_TABLE_ENTRY Bindings;
} DESKTOP_THEME_RUNTIME, *LPDESKTOP_THEME_RUNTIME;

/************************************************************************/

BOOL DesktopThemeParseStrict(LPCSTR Source, LPDESKTOP_THEME_RUNTIME* Runtime, U32* Status);
void DesktopThemeFreeRuntime(LPDESKTOP_THEME_RUNTIME Runtime);
LPDESKTOP_THEME_RUNTIME DesktopThemeCreateBuiltinRuntime(void);
BOOL DesktopThemeActivateParsed(LPDESKTOP_THEME_RUNTIME Candidate, LPDESKTOP_THEME_RUNTIME Fallback, LPDESKTOP_THEME_RUNTIME* ActiveRuntime);

/************************************************************************/

#endif  // DESKTOP_THEME_PARSER_H_INCLUDED
