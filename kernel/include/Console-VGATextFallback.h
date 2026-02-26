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


    Console VGA text emergency fallback

\************************************************************************/

#ifndef CONSOLE_VGA_TEXT_FALLBACK_H_INCLUDED
#define CONSOLE_VGA_TEXT_FALLBACK_H_INCLUDED

/************************************************************************/

#include "Console.h"

/************************************************************************/

BOOL ConsoleVGATextFallbackActivate(U32 Columns, U32 Rows, LPGRAPHICSMODEINFO AppliedMode);

/************************************************************************/

#endif  // CONSOLE_VGA_TEXT_FALLBACK_H_INCLUDED
