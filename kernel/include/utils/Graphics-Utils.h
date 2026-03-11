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


    Graphics utility helpers

\************************************************************************/

#ifndef GRAPHICS_UTILS_H_INCLUDED
#define GRAPHICS_UTILS_H_INCLUDED

/************************************************************************/

#include "GFX.h"
#include "utils/RectRegion.h"

/************************************************************************/

typedef BOOL (*GRAPHICS_PLOT_PIXEL_ROUTINE)(LPVOID Context, I32 X, I32 Y, COLOR* Color);

/************************************************************************/

BOOL IntersectRect(LPRECT Left, LPRECT Right, LPRECT Result);
BOOL SubtractRectFromRect(LPRECT Source, LPRECT Occluder, LPRECT_REGION Region);
BOOL SubtractRectFromRegion(LPRECT_REGION Region, LPRECT Occluder, LPRECT TempStorage, UINT TempCapacity);
BOOL GraphicsFillSolidRect(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR FillColor);
I32 GraphicsTriangleEdgeFunction(I32 Ax, I32 Ay, I32 Bx, I32 By, I32 Px, I32 Py);
BOOL GraphicsFillTriangleSpans(LPGRAPHICSCONTEXT Context, LPTRIANGLEINFO Info, COLOR FillColor, LPRECT FilledBounds);
BOOL GraphicsStrokeArc(LPVOID Context, GRAPHICS_PLOT_PIXEL_ROUTINE PlotPixel, I32 CenterX, I32 CenterY, I32 Radius, COLOR StrokeColor);

// Coordinate spaces:
// - Screen: absolute desktop pixels.
// - Window: pixels relative to the full window rectangle (frame included).
// - Client: pixels relative to the client rectangle origin.
void GraphicsScreenRectToWindowRect(LPRECT WindowScreenRect, LPRECT ScreenRect, LPRECT WindowRect);
void GraphicsWindowRectToScreenRect(LPRECT WindowScreenRect, LPRECT WindowRect, LPRECT ScreenRect);
void GraphicsScreenPointToWindowPoint(LPRECT WindowScreenRect, LPPOINT ScreenPoint, LPPOINT WindowPoint);
void GraphicsWindowPointToScreenPoint(LPRECT WindowScreenRect, LPPOINT WindowPoint, LPPOINT ScreenPoint);

/************************************************************************/

#endif  // GRAPHICS_UTILS_H_INCLUDED
