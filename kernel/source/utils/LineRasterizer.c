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

#include "utils/LineRasterizer.h"

/************************************************************************/

/**
 * @brief Rasterize one line and invoke a plot callback for visible pixels.
 *
 * @param Context Callback-specific drawing context.
 * @param X1 Start X.
 * @param Y1 Start Y.
 * @param X2 End X.
 * @param Y2 End Y.
 * @param Color Source color.
 * @param Pattern Optional 32-bit dash pattern, 0 means solid.
 * @param PlotCallback Pixel plot callback.
 */
void LineRasterizerDraw(LPVOID Context, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR Color, U32 Pattern,
                        LINE_RASTERIZER_PLOT_CALLBACK PlotCallback) {
    I32 Dx;
    I32 Sx;
    I32 Dy;
    I32 Sy;
    I32 Error;
    U32 PatternBit;

    if (PlotCallback == NULL) {
        return;
    }

    if (Pattern == 0) {
        Pattern = MAX_U32;
    }

    Dx = (X2 >= X1) ? (X2 - X1) : (X1 - X2);
    Sx = X1 < X2 ? 1 : -1;
    Dy = -((Y2 >= Y1) ? (Y2 - Y1) : (Y1 - Y2));
    Sy = Y1 < Y2 ? 1 : -1;
    Error = Dx + Dy;
    PatternBit = 0;

    for (;;) {
        if (((Pattern >> (PatternBit & 31)) & 1) != 0) {
            COLOR PixelColor = Color;
            (void)PlotCallback(Context, X1, Y1, &PixelColor);
        }
        PatternBit++;

        if (X1 == X2 && Y1 == Y2) {
            break;
        }

        I32 DoubleError = Error << 1;
        if (DoubleError >= Dy) {
            Error += Dy;
            X1 += Sx;
        }
        if (DoubleError <= Dx) {
            Error += Dx;
            Y1 += Sy;
        }
    }
}
