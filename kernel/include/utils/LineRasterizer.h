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


    Line rasterizer

\************************************************************************/

#ifndef UTILS_LINERASTERIZER_H_INCLUDED
#define UTILS_LINERASTERIZER_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/
// Type definitions

typedef BOOL (*LINE_RASTERIZER_PLOT_CALLBACK)(LPVOID Context, I32 X, I32 Y, COLOR* Color);

/************************************************************************/
// External functions

void LineRasterizerDraw(LPVOID Context, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR Color, U32 Pattern, U32 Width,
                        LINE_RASTERIZER_PLOT_CALLBACK PlotCallback);

/************************************************************************/

#endif  // UTILS_LINERASTERIZER_H_INCLUDED
