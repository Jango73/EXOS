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

#ifndef DESKTOP_THEME_RESOLVER_H_INCLUDED
#define DESKTOP_THEME_RESOLVER_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

BOOL DesktopThemeResolveLevel1Text(LPCSTR ElementID, LPCSTR StateID, LPCSTR PropertyName, LPSTR Value, UINT ValueBufferSize);
BOOL DesktopThemeResolveLevel1Color(LPCSTR ElementID, LPCSTR StateID, LPCSTR PropertyName, COLOR* Color);
BOOL DesktopThemeResolveLevel1Metric(LPCSTR ElementID, LPCSTR StateID, LPCSTR PropertyName, U32* Metric);
BOOL DesktopThemeResolveLevel1CornerStyle(LPCSTR ElementID, LPCSTR StateID, LPCSTR PropertyName, U32* CornerStyle);

/************************************************************************/

#endif  // DESKTOP_THEME_RESOLVER_H_INCLUDED
