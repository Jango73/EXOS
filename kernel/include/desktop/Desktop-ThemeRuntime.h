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


    Desktop theme runtime API

\************************************************************************/

#ifndef DESKTOP_THEME_RUNTIME_H_INCLUDED
#define DESKTOP_THEME_RUNTIME_H_INCLUDED

/************************************************************************/

#include "Desktop-ThemeParser.h"
#include "process/Process.h"

/************************************************************************/

#define DESKTOP_THEME_FALLBACK_REASON_NONE 0x00000000
#define DESKTOP_THEME_FALLBACK_REASON_FILE_READ_FAILED 0x00000001
#define DESKTOP_THEME_FALLBACK_REASON_PARSE_FAILED 0x00000002
#define DESKTOP_THEME_FALLBACK_REASON_ACTIVATION_FAILED 0x00000003
#define DESKTOP_THEME_FALLBACK_REASON_NO_STAGED_THEME 0x00000004
#define DESKTOP_THEME_FALLBACK_REASON_RESET_TO_DEFAULT 0x00000005

/************************************************************************/

typedef struct tag_DESKTOP_THEME_RUNTIME_INFO {
    BOOL IsBuiltinActive;
    BOOL IsLoadedActive;
    BOOL HasStagedTheme;
    STR ActiveThemePath[MAX_FILE_NAME];
    STR StagedThemePath[MAX_FILE_NAME];
    U32 LastStatus;
    U32 LastFallbackReason;
    U32 ActiveTokenCount;
    U32 ActiveElementPropertyCount;
    U32 ActiveRecipeCount;
    U32 ActiveBindingCount;
} DESKTOP_THEME_RUNTIME_INFO, *LPDESKTOP_THEME_RUNTIME_INFO;

/************************************************************************/

BOOL LoadTheme(LPCSTR Path);
BOOL ActivateTheme(LPCSTR NameOrHandle);
BOOL GetActiveThemeInfo(LPDESKTOP_THEME_RUNTIME_INFO Info);
BOOL ResetThemeToDefault(void);
BOOL ApplyDesktopTheme(LPCSTR Target);

LPDESKTOP_THEME_RUNTIME DesktopThemeGetActiveRuntime(LPDESKTOP Desktop);
BOOL DesktopThemeLookupTokenValue(LPDESKTOP Desktop, LPCSTR TokenName, LPCSTR* Value);
BOOL DesktopThemeLookupElementPropertyValue(LPDESKTOP Desktop, LPCSTR ElementPropertyKey, LPCSTR* Value);

/************************************************************************/

#endif  // DESKTOP_THEME_RUNTIME_H_INCLUDED
