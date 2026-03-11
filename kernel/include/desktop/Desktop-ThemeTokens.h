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

#ifndef DESKTOP_THEME_TOKENS_H_INCLUDED
#define DESKTOP_THEME_TOKENS_H_INCLUDED

/************************************************************************/

#include "Desktop.h"

/************************************************************************/
// Built-in color token identifiers

#define THEME_TOKEN_COLOR_DESKTOP_BACKGROUND 0x00001000
#define THEME_TOKEN_COLOR_HIGHLIGHT 0x00001001
#define THEME_TOKEN_COLOR_NORMAL 0x00001002
#define THEME_TOKEN_COLOR_LIGHT_SHADOW 0x00001003
#define THEME_TOKEN_COLOR_DARK_SHADOW 0x00001004
#define THEME_TOKEN_COLOR_CLIENT_BACKGROUND 0x00001005
#define THEME_TOKEN_COLOR_TEXT_NORMAL 0x00001006
#define THEME_TOKEN_COLOR_TEXT_SELECTED 0x00001007
#define THEME_TOKEN_COLOR_SELECTION 0x00001008
#define THEME_TOKEN_COLOR_TITLE_BAR 0x00001009
#define THEME_TOKEN_COLOR_TITLE_BAR_2 0x0000100A
#define THEME_TOKEN_COLOR_TITLE_TEXT 0x0000100B
#define THEME_TOKEN_COLOR_WINDOW_BORDER 0x0000100C

/************************************************************************/
// Built-in metric token identifiers

#define THEME_TOKEN_METRIC_MINIMUM_WINDOW_WIDTH 0x00002000
#define THEME_TOKEN_METRIC_MINIMUM_WINDOW_HEIGHT 0x00002001
#define THEME_TOKEN_METRIC_MAXIMUM_WINDOW_WIDTH 0x00002002
#define THEME_TOKEN_METRIC_MAXIMUM_WINDOW_HEIGHT 0x00002003
#define THEME_TOKEN_METRIC_TITLE_BAR_HEIGHT 0x00002004

/************************************************************************/

BOOL DesktopThemeResolveSystemColor(U32 SystemColorIndex, COLOR* Color);
BOOL DesktopThemeResolveSystemMetric(U32 SystemMetricIndex, U32* Value);
BOOL DesktopThemeResolveTokenColorByName(LPCSTR TokenName, COLOR* Color);
BOOL DesktopThemeResolveTokenMetricByName(LPCSTR TokenName, U32* Value);
void DesktopThemeSyncSystemObjects(void);

/************************************************************************/

#endif  // DESKTOP_THEME_TOKENS_H_INCLUDED
