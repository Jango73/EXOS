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

#include "utils/Graphics-Utils.h"
#include "KernelData.h"
#include "System.h"
#include "math/Math.h"
#include "utils/LineRasterizer.h"

/************************************************************************/

#define GRAPHICS_FIXED_SHIFT 16
#define GRAPHICS_FIXED_ONE (1 << GRAPHICS_FIXED_SHIFT)

/************************************************************************/

typedef struct tag_GRAPHICS_SPAN {
    I32 X1;
    I32 X2;
} GRAPHICS_SPAN, *LPGRAPHICS_SPAN;

/************************************************************************/

typedef enum tag_GRAPHICS_GRADIENT_AXIS {
    GRAPHICS_GRADIENT_AXIS_VERTICAL = 0,
    GRAPHICS_GRADIENT_AXIS_HORIZONTAL = 1
} GRAPHICS_GRADIENT_AXIS;

/************************************************************************/

#define GRAPHICS_ARC_QUADRANT_BOTTOM_RIGHT 0x0001
#define GRAPHICS_ARC_QUADRANT_BOTTOM_LEFT 0x0002
#define GRAPHICS_ARC_QUADRANT_TOP_LEFT 0x0004
#define GRAPHICS_ARC_QUADRANT_TOP_RIGHT 0x0008
#define GRAPHICS_ARC_QUADRANT_ALL                                                              \
    (GRAPHICS_ARC_QUADRANT_BOTTOM_RIGHT | GRAPHICS_ARC_QUADRANT_BOTTOM_LEFT |                 \
        GRAPHICS_ARC_QUADRANT_TOP_LEFT | GRAPHICS_ARC_QUADRANT_TOP_RIGHT)

/************************************************************************/

typedef struct tag_GRAPHICS_FILL_DESCRIPTOR {
    BOOL Enabled;
    BOOL HasGradient;
    GRAPHICS_GRADIENT_AXIS Axis;
    COLOR StartColor;
    COLOR EndColor;
    I32 GradientX1;
    I32 GradientY1;
    I32 GradientX2;
    I32 GradientY2;
} GRAPHICS_FILL_DESCRIPTOR, *LPGRAPHICS_FILL_DESCRIPTOR;

/************************************************************************/

static BOOL GraphicsRenderArc(
    LPGRAPHICSCONTEXT Context,
    I32 CenterX,
    I32 CenterY,
    I32 Radius,
    U32 QuadrantMask,
    LPGRAPHICS_FILL_DESCRIPTOR Fill,
    BOOL HasStroke,
    COLOR StrokeColor);

/************************************************************************/

/**
 * @brief Sleeps between primitive scanlines when debug redraw throttling is enabled.
 */
static void GraphicsSlowRedrawPauseIfNeeded(void) {
    if (GetSlowRedrawEnabled() == FALSE) {
        return;
    }

    Sleep(100);
}

/************************************************************************/

/**
 * @brief Append one rectangle to a region when it is valid.
 * @param Region Destination region.
 * @param Rect Candidate rectangle.
 * @return TRUE on success.
 */
static BOOL GraphicsRegionAppendRectIfValid(LPRECT_REGION Region, LPRECT Rect) {
    if (Region == NULL || Rect == NULL) return FALSE;
    if (Rect->X1 > Rect->X2 || Rect->Y1 > Rect->Y2) return TRUE;
    return RectRegionAddRect(Region, Rect);
}

/************************************************************************/

/**
 * @brief Clamp and normalize one scanline request against context bounds.
 * @param Context Graphics context.
 * @param X1 Input left coordinate.
 * @param X2 Input right coordinate.
 * @param Y Input row coordinate.
 * @param ClippedX1 Receives clipped left coordinate.
 * @param ClippedX2 Receives clipped right coordinate.
 * @return TRUE when at least one pixel remains to draw.
 */
static BOOL GraphicsClipScanline(
    LPGRAPHICSCONTEXT Context, I32 X1, I32 X2, I32 Y, I32* ClippedX1, I32* ClippedX2) {
    I32 DrawX1 = 0;
    I32 DrawX2 = 0;

    if (Context == NULL || ClippedX1 == NULL || ClippedX2 == NULL) return FALSE;
    if (Context->MemoryBase == NULL) return FALSE;
    if (Context->Width <= 0 || Context->Height <= 0) return FALSE;
    if (Y < Context->LoClip.Y || Y > Context->HiClip.Y) return FALSE;
    if (Y < 0 || Y >= Context->Height) return FALSE;

    DrawX1 = X1;
    DrawX2 = X2;

    if (DrawX1 > DrawX2) {
        I32 Temp = DrawX1;
        DrawX1 = DrawX2;
        DrawX2 = Temp;
    }

    if (DrawX1 < Context->LoClip.X) DrawX1 = Context->LoClip.X;
    if (DrawX2 > Context->HiClip.X) DrawX2 = Context->HiClip.X;
    if (DrawX1 < 0) DrawX1 = 0;
    if (DrawX2 >= Context->Width) DrawX2 = Context->Width - 1;
    if (DrawX1 > DrawX2) return FALSE;

    *ClippedX1 = DrawX1;
    *ClippedX2 = DrawX2;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Interpolate one color channel with integer arithmetic.
 * @param Start Start channel value.
 * @param End End channel value.
 * @param Numerator Interpolation numerator.
 * @param Denominator Interpolation denominator.
 * @return Interpolated channel value in range 0..255.
 */
static U32 GraphicsInterpolateChannel(U32 Start, U32 End, U32 Numerator, U32 Denominator) {
    I32 Delta = 0;
    I32 Value = 0;

    if (Denominator == 0) return Start & 0xFF;

    Delta = (I32)End - (I32)Start;
    Value = (I32)Start + ((Delta * (I32)Numerator) / (I32)Denominator);

    if (Value < 0) Value = 0;
    if (Value > 0xFF) Value = 0xFF;
    return (U32)Value;
}

/************************************************************************/

/**
 * @brief Interpolate one color along a linear gradient.
 * @param StartColor Gradient start color.
 * @param EndColor Gradient end color.
 * @param Numerator Interpolation numerator.
 * @param Denominator Interpolation denominator.
 * @return Interpolated ARGB color.
 */
static COLOR GraphicsInterpolateColor(COLOR StartColor, COLOR EndColor, U32 Numerator, U32 Denominator) {
    U32 StartA = 0;
    U32 StartR = 0;
    U32 StartG = 0;
    U32 StartB = 0;
    U32 EndA = 0;
    U32 EndR = 0;
    U32 EndG = 0;
    U32 EndB = 0;
    U32 OutA = 0;
    U32 OutR = 0;
    U32 OutG = 0;
    U32 OutB = 0;

    if (StartColor == EndColor || Denominator == 0) return StartColor;

    StartA = (StartColor >> 24) & 0xFF;
    StartR = (StartColor >> 16) & 0xFF;
    StartG = (StartColor >> 8) & 0xFF;
    StartB = StartColor & 0xFF;
    EndA = (EndColor >> 24) & 0xFF;
    EndR = (EndColor >> 16) & 0xFF;
    EndG = (EndColor >> 8) & 0xFF;
    EndB = EndColor & 0xFF;

    OutA = GraphicsInterpolateChannel(StartA, EndA, Numerator, Denominator);
    OutR = GraphicsInterpolateChannel(StartR, EndR, Numerator, Denominator);
    OutG = GraphicsInterpolateChannel(StartG, EndG, Numerator, Denominator);
    OutB = GraphicsInterpolateChannel(StartB, EndB, Numerator, Denominator);

    return (OutA << 24) | (OutR << 16) | (OutG << 8) | OutB;
}

/************************************************************************/

static void GraphicsNormalizeRectangle(I32* X1, I32* Y1, I32* X2, I32* Y2) {
    I32 Temp = 0;

    if (X1 == NULL || Y1 == NULL || X2 == NULL || Y2 == NULL) return;

    if (*X1 > *X2) {
        Temp = *X1;
        *X1 = *X2;
        *X2 = Temp;
    }

    if (*Y1 > *Y2) {
        Temp = *Y1;
        *Y1 = *Y2;
        *Y2 = Temp;
    }
}

/************************************************************************/

static BOOL GraphicsHasRectangleGradient(LPRECT_INFO Info) {
    if (Info == NULL) return FALSE;
    return (Info->Header.Flags & RECT_FLAG_FILL_GRADIENT_MASK) != 0;
}

/************************************************************************/

static BOOL GraphicsHasArcGradient(LPARC_INFO Info) {
    if (Info == NULL) return FALSE;
    return (Info->Header.Flags & ARC_FLAG_FILL_GRADIENT_MASK) != 0;
}

/************************************************************************/

static BOOL GraphicsSetupSolidFillDescriptor(LPGRAPHICS_FILL_DESCRIPTOR Fill, COLOR FillColor) {
    if (Fill == NULL) return FALSE;

    Fill->Enabled = TRUE;
    Fill->HasGradient = FALSE;
    Fill->Axis = GRAPHICS_GRADIENT_AXIS_VERTICAL;
    Fill->StartColor = FillColor;
    Fill->EndColor = FillColor;
    Fill->GradientX1 = 0;
    Fill->GradientY1 = 0;
    Fill->GradientX2 = 0;
    Fill->GradientY2 = 0;
    return TRUE;
}

/************************************************************************/

static BOOL GraphicsSetupRectangleFillDescriptor(LPGRAPHICSCONTEXT Context, LPRECT_INFO Info, LPGRAPHICS_FILL_DESCRIPTOR Fill) {
    I32 X1 = 0;
    I32 Y1 = 0;
    I32 X2 = 0;
    I32 Y2 = 0;

    if (Context == NULL || Info == NULL || Fill == NULL) return FALSE;

    *Fill = (GRAPHICS_FILL_DESCRIPTOR){0};
    if (GraphicsHasRectangleGradient(Info) != FALSE) {
        X1 = Info->X1;
        Y1 = Info->Y1;
        X2 = Info->X2;
        Y2 = Info->Y2;
        GraphicsNormalizeRectangle(&X1, &Y1, &X2, &Y2);

        Fill->Enabled = TRUE;
        Fill->HasGradient = TRUE;
        Fill->Axis = (Info->Header.Flags & RECT_FLAG_FILL_HORIZONTAL_GRADIENT) != 0
            ? GRAPHICS_GRADIENT_AXIS_HORIZONTAL
            : GRAPHICS_GRADIENT_AXIS_VERTICAL;
        Fill->StartColor = Info->StartColor;
        Fill->EndColor = Info->EndColor;
        Fill->GradientX1 = X1;
        Fill->GradientY1 = Y1;
        Fill->GradientX2 = X2;
        Fill->GradientY2 = Y2;
        return TRUE;
    }

    if (Context->Brush == NULL || Context->Brush->TypeID != KOID_BRUSH) return TRUE;
    return GraphicsSetupSolidFillDescriptor(Fill, Context->Brush->Color);
}

/************************************************************************/

static BOOL GraphicsSetupArcFillDescriptor(LPGRAPHICSCONTEXT Context, LPARC_INFO Info, LPGRAPHICS_FILL_DESCRIPTOR Fill) {
    if (Context == NULL || Info == NULL || Fill == NULL) return FALSE;

    *Fill = (GRAPHICS_FILL_DESCRIPTOR){0};
    if ((Info->Header.Flags & ARC_FLAG_FILL) == 0 && GraphicsHasArcGradient(Info) == FALSE) return TRUE;

    if (GraphicsHasArcGradient(Info) != FALSE) {
        Fill->Enabled = TRUE;
        Fill->HasGradient = TRUE;
        Fill->Axis = (Info->Header.Flags & ARC_FLAG_FILL_HORIZONTAL_GRADIENT) != 0
            ? GRAPHICS_GRADIENT_AXIS_HORIZONTAL
            : GRAPHICS_GRADIENT_AXIS_VERTICAL;
        Fill->StartColor = Info->StartColor;
        Fill->EndColor = Info->EndColor;
        Fill->GradientX1 = Info->CenterX - Info->Radius;
        Fill->GradientY1 = Info->CenterY - Info->Radius;
        Fill->GradientX2 = Info->CenterX + Info->Radius;
        Fill->GradientY2 = Info->CenterY + Info->Radius;
        return TRUE;
    }

    if (Context->Brush == NULL || Context->Brush->TypeID != KOID_BRUSH) return TRUE;
    return GraphicsSetupSolidFillDescriptor(Fill, Context->Brush->Color);
}

/************************************************************************/

static U32 GraphicsResolveRoundedCornerRadius(LPRECT_INFO Info, I32 X1, I32 Y1, I32 X2, I32 Y2) {
    I32 Width = 0;
    I32 Height = 0;
    I32 Radius = 0;

    if (Info == NULL) return 0;
    if (Info->CornerStyle != RECT_CORNER_STYLE_ROUNDED) return 0;
    if (Info->CornerRadius <= 0) return 0;

    Width = X2 - X1 + 1;
    Height = Y2 - Y1 + 1;
    Radius = Info->CornerRadius;
    if (Width <= 0 || Height <= 0) return 0;
    if (Radius > Width / 2) Radius = Width / 2;
    if (Radius > Height / 2) Radius = Height / 2;
    if (Radius < 0) Radius = 0;
    return (U32)Radius;
}

/************************************************************************/

static U32 GraphicsResolvePenWidth(LPGRAPHICSCONTEXT Context) {
    if (Context == NULL || Context->Pen == NULL || Context->Pen->TypeID != KOID_PEN) return 1;
    if (Context->Pen->Width == 0) return 1;
    return Context->Pen->Width;
}

/************************************************************************/

static BOOL GraphicsDrawFillSpan(
    LPGRAPHICSCONTEXT Context, LPGRAPHICS_FILL_DESCRIPTOR Fill, I32 X1, I32 X2, I32 Y) {
    I32 ClipX1 = 0;
    I32 ClipX2 = 0;
    COLOR StartColor = 0;
    COLOR EndColor = 0;
    U32 Denominator = 0;
    U32 StartNumerator = 0;
    U32 EndNumerator = 0;

    if (Context == NULL || Fill == NULL) return FALSE;
    if (Fill->Enabled == FALSE) return TRUE;
    if (GraphicsClipScanline(Context, X1, X2, Y, &ClipX1, &ClipX2) == FALSE) return TRUE;

    if (Fill->HasGradient == FALSE) {
        StartColor = Fill->StartColor;
        EndColor = Fill->StartColor;
    } else if (Fill->Axis == GRAPHICS_GRADIENT_AXIS_HORIZONTAL) {
        Denominator = Fill->GradientX2 > Fill->GradientX1 ? (U32)(Fill->GradientX2 - Fill->GradientX1) : 0;
        StartNumerator = ClipX1 >= Fill->GradientX1 ? (U32)(ClipX1 - Fill->GradientX1) : 0;
        EndNumerator = ClipX2 >= Fill->GradientX1 ? (U32)(ClipX2 - Fill->GradientX1) : 0;
        StartColor = GraphicsInterpolateColor(Fill->StartColor, Fill->EndColor, StartNumerator, Denominator);
        EndColor = GraphicsInterpolateColor(Fill->StartColor, Fill->EndColor, EndNumerator, Denominator);
    } else {
        Denominator = Fill->GradientY2 > Fill->GradientY1 ? (U32)(Fill->GradientY2 - Fill->GradientY1) : 0;
        StartNumerator = Y >= Fill->GradientY1 ? (U32)(Y - Fill->GradientY1) : 0;
        StartColor = GraphicsInterpolateColor(Fill->StartColor, Fill->EndColor, StartNumerator, Denominator);
        EndColor = StartColor;
    }

    return GraphicsDrawScanline(Context, ClipX1, ClipX2, Y, StartColor, EndColor);
}

/************************************************************************/

static I32 GraphicsNormalizeAngle(I32 Angle) {
    while (Angle < 0) Angle += 360;
    while (Angle >= 360) Angle -= 360;
    return Angle;
}

/************************************************************************/

static BOOL GraphicsAngleIsWithinClockwiseSweep(I32 StartAngle, I32 EndAngle, I32 TestAngle) {
    StartAngle = GraphicsNormalizeAngle(StartAngle);
    EndAngle = GraphicsNormalizeAngle(EndAngle);
    TestAngle = GraphicsNormalizeAngle(TestAngle);

    if (StartAngle <= EndAngle) {
        return TestAngle >= StartAngle && TestAngle <= EndAngle;
    }

    return TestAngle >= StartAngle || TestAngle <= EndAngle;
}

/************************************************************************/

static U32 GraphicsResolveArcQuadrantMask(LPARC_INFO Info) {
    I32 Delta = 0;
    U32 Mask = 0;

    if (Info == NULL) return 0;

    Delta = Info->EndAngle - Info->StartAngle;
    if (Delta >= 360 || Delta <= -360) return GRAPHICS_ARC_QUADRANT_ALL;

    if (GraphicsAngleIsWithinClockwiseSweep(Info->StartAngle, Info->EndAngle, 45)) {
        Mask |= GRAPHICS_ARC_QUADRANT_BOTTOM_RIGHT;
    }
    if (GraphicsAngleIsWithinClockwiseSweep(Info->StartAngle, Info->EndAngle, 135)) {
        Mask |= GRAPHICS_ARC_QUADRANT_BOTTOM_LEFT;
    }
    if (GraphicsAngleIsWithinClockwiseSweep(Info->StartAngle, Info->EndAngle, 225)) {
        Mask |= GRAPHICS_ARC_QUADRANT_TOP_LEFT;
    }
    if (GraphicsAngleIsWithinClockwiseSweep(Info->StartAngle, Info->EndAngle, 315)) {
        Mask |= GRAPHICS_ARC_QUADRANT_TOP_RIGHT;
    }

    return Mask;
}

/************************************************************************/

/**
 * @brief Shared fallback used by the assembly scanline entry for gradients.
 * @param Pixel Scanline start pointer.
 * @param PixelCount Number of pixels to write.
 * @param BitsPerPixel Destination pixel format.
 * @param RasterOperation Raster operation.
 * @param StartColor Gradient start color.
 * @param EndColor Gradient end color.
 * @return TRUE on success.
 */
BOOL GraphicsDrawScanlineFallback(
    U8* Pixel, U32 PixelCount, U32 BitsPerPixel, U32 RasterOperation, COLOR StartColor, COLOR EndColor) {
    U32 PixelIndex = 0;
    U32 Denominator = 0;

    if (Pixel == NULL || PixelCount == 0) return FALSE;

    Denominator = PixelCount > 1 ? PixelCount - 1 : 0;

    switch (BitsPerPixel) {
        case 32:
            for (PixelIndex = 0; PixelIndex < PixelCount; PixelIndex++) {
                U32 PixelColor = GraphicsInterpolateColor(StartColor, EndColor, PixelIndex, Denominator);

                switch (RasterOperation) {
                    case ROP_SET:
                        ((U32*)Pixel)[PixelIndex] = PixelColor;
                        break;
                    case ROP_XOR:
                        ((U32*)Pixel)[PixelIndex] ^= PixelColor;
                        break;
                    case ROP_OR:
                        ((U32*)Pixel)[PixelIndex] |= PixelColor;
                        break;
                    case ROP_AND:
                        ((U32*)Pixel)[PixelIndex] &= PixelColor;
                        break;
                    default:
                        return FALSE;
                }
            }
            return TRUE;

        case 24:
            for (PixelIndex = 0; PixelIndex < PixelCount; PixelIndex++) {
                COLOR PixelColor = GraphicsInterpolateColor(StartColor, EndColor, PixelIndex, Denominator);
                U8 Red = (U8)((PixelColor >> 16) & 0xFF);
                U8 Green = (U8)((PixelColor >> 8) & 0xFF);
                U8 Blue = (U8)(PixelColor & 0xFF);
                U8* Current = Pixel + (PixelIndex * 3);

                switch (RasterOperation) {
                    case ROP_SET:
                        Current[0] = Red;
                        Current[1] = Green;
                        Current[2] = Blue;
                        break;
                    case ROP_XOR:
                        Current[0] ^= Red;
                        Current[1] ^= Green;
                        Current[2] ^= Blue;
                        break;
                    case ROP_OR:
                        Current[0] |= Red;
                        Current[1] |= Green;
                        Current[2] |= Blue;
                        break;
                    case ROP_AND:
                        Current[0] &= Red;
                        Current[1] &= Green;
                        Current[2] &= Blue;
                        break;
                    default:
                        return FALSE;
                }
            }
            return TRUE;

        case 16:
            for (PixelIndex = 0; PixelIndex < PixelCount; PixelIndex++) {
                U16 PixelColor = (U16)GraphicsInterpolateColor(StartColor, EndColor, PixelIndex, Denominator);

                switch (RasterOperation) {
                    case ROP_SET:
                        ((U16*)Pixel)[PixelIndex] = PixelColor;
                        break;
                    case ROP_XOR:
                        ((U16*)Pixel)[PixelIndex] ^= PixelColor;
                        break;
                    case ROP_OR:
                        ((U16*)Pixel)[PixelIndex] |= PixelColor;
                        break;
                    case ROP_AND:
                        ((U16*)Pixel)[PixelIndex] &= PixelColor;
                        break;
                    default:
                        return FALSE;
                }
            }
            return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Write one clipped scanline through the shared assembly primitive.
 * @param Context Graphics context.
 * @param X1 Left coordinate.
 * @param X2 Right coordinate.
 * @param Y Row coordinate.
 * @param StartColor Left/start color.
 * @param EndColor Right/end color.
 * @return TRUE on success.
 */
BOOL GraphicsDrawScanline(LPGRAPHICSCONTEXT Context, I32 X1, I32 X2, I32 Y, COLOR StartColor, COLOR EndColor) {
    I32 DrawX1 = 0;
    I32 DrawX2 = 0;
    I32 OriginalX1 = 0;
    I32 OriginalX2 = 0;
    U32 PixelCount = 0;
    U32 Denominator = 0;
    U32 StartNumerator = 0;
    U32 EndNumerator = 0;
    COLOR ClippedStartColor = 0;
    COLOR ClippedEndColor = 0;
    U8* Pixel = NULL;

    if (Context == NULL) return FALSE;
    if (GraphicsClipScanline(Context, X1, X2, Y, &DrawX1, &DrawX2) == FALSE) return FALSE;

    OriginalX1 = X1;
    OriginalX2 = X2;
    if (OriginalX1 > OriginalX2) {
        I32 TempX = OriginalX1;
        COLOR TempColor = StartColor;

        OriginalX1 = OriginalX2;
        OriginalX2 = TempX;
        StartColor = EndColor;
        EndColor = TempColor;
    }

    ClippedStartColor = StartColor;
    ClippedEndColor = EndColor;
    Denominator = (U32)(OriginalX2 - OriginalX1);
    if (Denominator != 0 && (DrawX1 != OriginalX1 || DrawX2 != OriginalX2 || StartColor != EndColor)) {
        StartNumerator = (U32)(DrawX1 - OriginalX1);
        EndNumerator = (U32)(DrawX2 - OriginalX1);
        ClippedStartColor = GraphicsInterpolateColor(StartColor, EndColor, StartNumerator, Denominator);
        ClippedEndColor = GraphicsInterpolateColor(StartColor, EndColor, EndNumerator, Denominator);
    }

    switch (Context->BitsPerPixel) {
        case 16:
            Pixel = Context->MemoryBase + (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)DrawX1 << 1);
            break;
        case 24:
            Pixel = Context->MemoryBase + (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)DrawX1 * 3);
            break;
        case 32:
            Pixel = Context->MemoryBase + (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)DrawX1 << 2);
            break;
        default:
            return FALSE;
    }

    PixelCount = (U32)(DrawX2 - DrawX1 + 1);
    if (ClippedStartColor == ClippedEndColor) {
        return GraphicsDrawScanlineAsm(
            Pixel, PixelCount, Context->BitsPerPixel, Context->RasterOperation, ClippedStartColor, ClippedEndColor);
    }

    return GraphicsDrawHorizontalGradientScanlineAsm(
        Pixel, PixelCount, Context->BitsPerPixel, Context->RasterOperation, ClippedStartColor, ClippedEndColor);
}

/************************************************************************/

/**
 * @brief Fill one rectangle through horizontal scanlines.
 * @param Context Graphics context.
 * @param X1 Left coordinate.
 * @param Y1 Top coordinate.
 * @param X2 Right coordinate.
 * @param Y2 Bottom coordinate.
 * @param FillColor Solid fill color.
 * @return TRUE on success.
 */
BOOL GraphicsFillSolidRect(LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR FillColor) {
    I32 DrawX1 = 0;
    I32 DrawY1 = 0;
    I32 DrawX2 = 0;
    I32 DrawY2 = 0;
    I32 Y = 0;

    if (Context == NULL || Context->MemoryBase == NULL) return FALSE;
    GraphicsSlowRedrawPauseIfNeeded();

    DrawX1 = X1;
    DrawY1 = Y1;
    DrawX2 = X2;
    DrawY2 = Y2;

    if (DrawX1 > DrawX2) {
        I32 Temp = DrawX1;
        DrawX1 = DrawX2;
        DrawX2 = Temp;
    }
    if (DrawY1 > DrawY2) {
        I32 Temp = DrawY1;
        DrawY1 = DrawY2;
        DrawY2 = Temp;
    }

    if (DrawX1 < Context->LoClip.X) DrawX1 = Context->LoClip.X;
    if (DrawY1 < Context->LoClip.Y) DrawY1 = Context->LoClip.Y;
    if (DrawX2 > Context->HiClip.X) DrawX2 = Context->HiClip.X;
    if (DrawY2 > Context->HiClip.Y) DrawY2 = Context->HiClip.Y;
    if (DrawX1 < 0) DrawX1 = 0;
    if (DrawY1 < 0) DrawY1 = 0;
    if (DrawX2 >= Context->Width) DrawX2 = Context->Width - 1;
    if (DrawY2 >= Context->Height) DrawY2 = Context->Height - 1;
    if (DrawX2 < DrawX1 || DrawY2 < DrawY1) return TRUE;

    for (Y = DrawY1; Y <= DrawY2; Y++) {
        if (GraphicsDrawScanline(Context, DrawX1, DrawX2, Y, FillColor, FillColor) == FALSE) return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Fill one rectangle with a vertical gradient through scanlines.
 * @param Context Graphics context.
 * @param X1 Left coordinate.
 * @param Y1 Top coordinate.
 * @param X2 Right coordinate.
 * @param Y2 Bottom coordinate.
 * @param StartColor Top color.
 * @param EndColor Bottom color.
 * @return TRUE on success.
 */
BOOL GraphicsFillVerticalGradientRect(
    LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR StartColor, COLOR EndColor) {
    I32 DrawX1 = 0;
    I32 DrawY1 = 0;
    I32 DrawX2 = 0;
    I32 DrawY2 = 0;
    I32 OriginalY1 = 0;
    I32 OriginalY2 = 0;
    I32 Height = 0;
    U32 PixelCount = 0;
    U32 RowCount = 0;
    U32 Denominator = 0;
    COLOR ClippedStartColor = 0;
    COLOR ClippedEndColor = 0;
    U8* Pixel = NULL;

    if (Context == NULL || Context->MemoryBase == NULL) return FALSE;
    GraphicsSlowRedrawPauseIfNeeded();
    if (StartColor == EndColor) return GraphicsFillSolidRect(Context, X1, Y1, X2, Y2, StartColor);

    DrawX1 = X1;
    DrawY1 = Y1;
    DrawX2 = X2;
    DrawY2 = Y2;

    if (DrawX1 > DrawX2) {
        I32 Temp = DrawX1;
        DrawX1 = DrawX2;
        DrawX2 = Temp;
    }
    if (DrawY1 > DrawY2) {
        I32 Temp = DrawY1;
        DrawY1 = DrawY2;
        DrawY2 = Temp;
    }

    OriginalY1 = DrawY1;
    OriginalY2 = DrawY2;
    Height = OriginalY2 - OriginalY1;
    if (Height <= 0) return GraphicsFillSolidRect(Context, DrawX1, DrawY1, DrawX2, DrawY2, StartColor);

    if (DrawX1 < Context->LoClip.X) DrawX1 = Context->LoClip.X;
    if (DrawY1 < Context->LoClip.Y) DrawY1 = Context->LoClip.Y;
    if (DrawX2 > Context->HiClip.X) DrawX2 = Context->HiClip.X;
    if (DrawY2 > Context->HiClip.Y) DrawY2 = Context->HiClip.Y;
    if (DrawX1 < 0) DrawX1 = 0;
    if (DrawY1 < 0) DrawY1 = 0;
    if (DrawX2 >= Context->Width) DrawX2 = Context->Width - 1;
    if (DrawY2 >= Context->Height) DrawY2 = Context->Height - 1;
    if (DrawX2 < DrawX1) return TRUE;
    if (DrawY2 < DrawY1) return TRUE;

    Denominator = (U32)Height;
    ClippedStartColor = GraphicsInterpolateColor(StartColor, EndColor, (U32)(DrawY1 - OriginalY1), Denominator);
    ClippedEndColor = GraphicsInterpolateColor(StartColor, EndColor, (U32)(DrawY2 - OriginalY1), Denominator);

    switch (Context->BitsPerPixel) {
        case 16:
            Pixel = Context->MemoryBase + (U32)(DrawY1 * (I32)Context->BytesPerScanLine) + ((U32)DrawX1 << 1);
            break;
        case 24:
            Pixel = Context->MemoryBase + (U32)(DrawY1 * (I32)Context->BytesPerScanLine) + ((U32)DrawX1 * 3);
            break;
        case 32:
            Pixel = Context->MemoryBase + (U32)(DrawY1 * (I32)Context->BytesPerScanLine) + ((U32)DrawX1 << 2);
            break;
        default:
            return FALSE;
    }

    PixelCount = (U32)(DrawX2 - DrawX1 + 1);
    RowCount = (U32)(DrawY2 - DrawY1 + 1);
    return GraphicsFillVerticalGradientRectAsm(
        Pixel,
        PixelCount,
        RowCount,
        Context->BitsPerPixel,
        Context->BytesPerScanLine,
        Context->RasterOperation,
        ClippedStartColor,
        ClippedEndColor);
}

/************************************************************************/

/**
 * @brief Fill one rectangle with a horizontal gradient through scanlines.
 * @param Context Graphics context.
 * @param X1 Left coordinate.
 * @param Y1 Top coordinate.
 * @param X2 Right coordinate.
 * @param Y2 Bottom coordinate.
 * @param StartColor Left color.
 * @param EndColor Right color.
 * @return TRUE on success.
 */
BOOL GraphicsFillHorizontalGradientRect(
    LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR StartColor, COLOR EndColor) {
    I32 DrawY1 = 0;
    I32 DrawY2 = 0;
    I32 Y = 0;

    if (Context == NULL || Context->MemoryBase == NULL) return FALSE;
    GraphicsSlowRedrawPauseIfNeeded();
    if (StartColor == EndColor) return GraphicsFillSolidRect(Context, X1, Y1, X2, Y2, StartColor);

    DrawY1 = Y1;
    DrawY2 = Y2;
    if (DrawY1 > DrawY2) {
        I32 Temp = DrawY1;
        DrawY1 = DrawY2;
        DrawY2 = Temp;
    }

    if (DrawY1 < Context->LoClip.Y) DrawY1 = Context->LoClip.Y;
    if (DrawY2 > Context->HiClip.Y) DrawY2 = Context->HiClip.Y;
    if (DrawY1 < 0) DrawY1 = 0;
    if (DrawY2 >= Context->Height) DrawY2 = Context->Height - 1;
    if (DrawY2 < DrawY1) return TRUE;

    for (Y = DrawY1; Y <= DrawY2; Y++) {
        if (GraphicsDrawScanline(Context, X1, X2, Y, StartColor, EndColor) == FALSE) return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Fill one rectangle from one shared descriptor.
 * @param Context Graphics context.
 * @param Info Rectangle descriptor.
 * @return TRUE on success.
 */
BOOL GraphicsFillRectangleFromDescriptor(LPGRAPHICSCONTEXT Context, LPRECT_INFO Info) {
    GRAPHICS_FILL_DESCRIPTOR Fill;
    I32 X1 = 0;
    I32 Y1 = 0;
    I32 X2 = 0;
    I32 Y2 = 0;
    I32 Y = 0;

    if (Context == NULL || Info == NULL) return FALSE;
    if (GraphicsSetupRectangleFillDescriptor(Context, Info, &Fill) == FALSE) return FALSE;
    if (Fill.Enabled == FALSE) return TRUE;

    X1 = Info->X1;
    Y1 = Info->Y1;
    X2 = Info->X2;
    Y2 = Info->Y2;
    GraphicsNormalizeRectangle(&X1, &Y1, &X2, &Y2);

    for (Y = Y1; Y <= Y2; Y++) {
        if (GraphicsDrawFillSpan(Context, &Fill, X1, X2, Y) == FALSE) return FALSE;
    }

    return TRUE;
}

/************************************************************************/

I32 GraphicsTriangleEdgeFunction(I32 Ax, I32 Ay, I32 Bx, I32 By, I32 Px, I32 Py) {
    return (Px - Ax) * (By - Ay) - (Py - Ay) * (Bx - Ax);
}

/************************************************************************/

/**
 * @brief Order three triangle points by increasing Y then X.
 * @param Points Triangle point array.
 */
static void GraphicsSortTrianglePoints(LPPOINT Points) {
    UINT Index = 0;
    UINT Other = 0;

    if (Points == NULL) return;

    for (Index = 0; Index < 2; Index++) {
        for (Other = Index + 1; Other < 3; Other++) {
            if (Points[Other].Y < Points[Index].Y ||
                (Points[Other].Y == Points[Index].Y && Points[Other].X < Points[Index].X)) {
                POINT Temp = Points[Index];
                Points[Index] = Points[Other];
                Points[Other] = Temp;
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Compute one triangle span for a scanline crossing.
 * @param V1 First vertex.
 * @param V2 Second vertex.
 * @param V3 Third vertex.
 * @param Y Scanline row.
 * @param SpanOut Receives [x1, x2].
 * @return TRUE when the row intersects the triangle.
 */
static BOOL GraphicsTriangleSpanForScanline(const POINT* V1, const POINT* V2, const POINT* V3, I32 Y, LPGRAPHICS_SPAN SpanOut) {
    POINT Sorted[3];
    I32 SplitXFixed = 0;
    I32 LeftFixed = 0;
    I32 RightFixed = 0;
    I32 Numerator = 0;
    I32 Denominator = 0;
    I32 RelativeY = 0;
    const POINT* LeftStart = NULL;
    const POINT* LeftEnd = NULL;
    const POINT* RightStart = NULL;
    const POINT* RightEnd = NULL;

    if (V1 == NULL || V2 == NULL || V3 == NULL || SpanOut == NULL) return FALSE;

    Sorted[0] = *V1;
    Sorted[1] = *V2;
    Sorted[2] = *V3;
    GraphicsSortTrianglePoints(Sorted);

    if (Y < Sorted[0].Y || Y > Sorted[2].Y) return FALSE;
    if (Sorted[0].Y == Sorted[2].Y) {
        I32 MinX = Sorted[0].X;
        I32 MaxX = Sorted[0].X;

        if (Sorted[1].X < MinX) MinX = Sorted[1].X;
        if (Sorted[2].X < MinX) MinX = Sorted[2].X;
        if (Sorted[1].X > MaxX) MaxX = Sorted[1].X;
        if (Sorted[2].X > MaxX) MaxX = Sorted[2].X;

        SpanOut->X1 = MinX;
        SpanOut->X2 = MaxX;
        return TRUE;
    }

    if (Sorted[0].Y == Sorted[2].Y) return FALSE;

    SplitXFixed =
        (Sorted[0].X << GRAPHICS_FIXED_SHIFT) +
        (((Sorted[2].X - Sorted[0].X) << GRAPHICS_FIXED_SHIFT) * (Sorted[1].Y - Sorted[0].Y)) /
            (Sorted[2].Y - Sorted[0].Y);

    if (Y < Sorted[1].Y || Sorted[1].Y == Sorted[2].Y) {
        LeftStart = &Sorted[0];
        LeftEnd = &Sorted[1];
        RightStart = &Sorted[0];
        RightEnd = &Sorted[2];
    } else if (Sorted[0].Y == Sorted[1].Y) {
        LeftStart = &Sorted[0];
        LeftEnd = &Sorted[2];
        RightStart = &Sorted[1];
        RightEnd = &Sorted[2];
    } else if (Sorted[1].X < (SplitXFixed >> GRAPHICS_FIXED_SHIFT)) {
        LeftStart = &Sorted[1];
        LeftEnd = &Sorted[2];
        RightStart = &Sorted[0];
        RightEnd = &Sorted[2];
    } else {
        LeftStart = &Sorted[0];
        LeftEnd = &Sorted[2];
        RightStart = &Sorted[1];
        RightEnd = &Sorted[2];
    }

    Denominator = LeftEnd->Y - LeftStart->Y;
    if (Denominator == 0) {
        LeftFixed = LeftStart->X << GRAPHICS_FIXED_SHIFT;
    } else {
        RelativeY = Y - LeftStart->Y;
        Numerator = ((LeftEnd->X - LeftStart->X) << GRAPHICS_FIXED_SHIFT) * RelativeY;
        LeftFixed = (LeftStart->X << GRAPHICS_FIXED_SHIFT) + (Numerator / Denominator);
    }

    Denominator = RightEnd->Y - RightStart->Y;
    if (Denominator == 0) {
        RightFixed = RightStart->X << GRAPHICS_FIXED_SHIFT;
    } else {
        RelativeY = Y - RightStart->Y;
        Numerator = ((RightEnd->X - RightStart->X) << GRAPHICS_FIXED_SHIFT) * RelativeY;
        RightFixed = (RightStart->X << GRAPHICS_FIXED_SHIFT) + (Numerator / Denominator);
    }

    SpanOut->X1 = LeftFixed >> GRAPHICS_FIXED_SHIFT;
    SpanOut->X2 = RightFixed >> GRAPHICS_FIXED_SHIFT;
    if (SpanOut->X1 > SpanOut->X2) {
        I32 Temp = SpanOut->X1;
        SpanOut->X1 = SpanOut->X2;
        SpanOut->X2 = Temp;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Fill one triangle through scanlines with a configurable gradient axis.
 * @param Context Graphics context.
 * @param Info Triangle descriptor.
 * @param StartColor Gradient start color.
 * @param EndColor Gradient end color.
 * @param Axis Gradient direction.
 * @param FilledBounds Receives rasterized bounds when not NULL.
 * @return TRUE when at least one scanline was drawn.
 */
static BOOL GraphicsFillTriangleScanlinesInternal(
    LPGRAPHICSCONTEXT Context,
    LPTRIANGLE_INFO Info,
    COLOR StartColor,
    COLOR EndColor,
    GRAPHICS_GRADIENT_AXIS Axis,
    LPRECT FilledBounds) {
    POINT Sorted[3];
    I32 MinY = 0;
    I32 MaxY = 0;
    I32 MinX = 0;
    I32 MaxX = 0;
    I32 Y = 0;
    I32 GradientStart = 0;
    I32 GradientEnd = 0;
    U32 GradientDenominator = 0;
    BOOL FilledAny = FALSE;

    if (FilledBounds != NULL) {
        *FilledBounds = (RECT){0};
    }

    if (Context == NULL || Info == NULL || Context->MemoryBase == NULL) return FALSE;

    Sorted[0] = Info->P1;
    Sorted[1] = Info->P2;
    Sorted[2] = Info->P3;
    GraphicsSortTrianglePoints(Sorted);

    MinY = Sorted[0].Y;
    MaxY = Sorted[2].Y;
    MinX = Sorted[0].X;
    MaxX = Sorted[0].X;
    if (Sorted[1].X < MinX) MinX = Sorted[1].X;
    if (Sorted[2].X < MinX) MinX = Sorted[2].X;
    if (Sorted[1].X > MaxX) MaxX = Sorted[1].X;
    if (Sorted[2].X > MaxX) MaxX = Sorted[2].X;

    if (MinY < Context->LoClip.Y) MinY = Context->LoClip.Y;
    if (MaxY > Context->HiClip.Y) MaxY = Context->HiClip.Y;
    if (MinY < 0) MinY = 0;
    if (MaxY >= Context->Height) MaxY = Context->Height - 1;
    if (MinY > MaxY) return FALSE;

    if (Axis == GRAPHICS_GRADIENT_AXIS_VERTICAL) {
        GradientStart = Sorted[0].Y;
        GradientEnd = Sorted[2].Y;
    } else {
        GradientStart = MinX;
        GradientEnd = MaxX;
    }
    GradientDenominator = (GradientEnd > GradientStart) ? (U32)(GradientEnd - GradientStart) : 0;

    for (Y = MinY; Y <= MaxY; Y++) {
        GRAPHICS_SPAN Span;
        COLOR SpanStartColor = StartColor;
        COLOR SpanEndColor = EndColor;

        if (GraphicsTriangleSpanForScanline(&Info->P1, &Info->P2, &Info->P3, Y, &Span) == FALSE) continue;
        if (Span.X1 > Context->HiClip.X || Span.X2 < Context->LoClip.X) continue;
        if (Span.X1 >= Context->Width || Span.X2 < 0) continue;

        if (Axis == GRAPHICS_GRADIENT_AXIS_VERTICAL) {
            COLOR RowColor = GraphicsInterpolateColor(StartColor, EndColor, (U32)(Y - GradientStart), GradientDenominator);
            SpanStartColor = RowColor;
            SpanEndColor = RowColor;
        }

        if (GraphicsDrawScanline(Context, Span.X1, Span.X2, Y, SpanStartColor, SpanEndColor) == FALSE) return FALSE;

        if (FilledAny == FALSE) {
            if (FilledBounds != NULL) {
                FilledBounds->X1 = Span.X1;
                FilledBounds->Y1 = Y;
                FilledBounds->X2 = Span.X2;
                FilledBounds->Y2 = Y;
            }
            FilledAny = TRUE;
        } else if (FilledBounds != NULL) {
            if (Span.X1 < FilledBounds->X1) FilledBounds->X1 = Span.X1;
            if (Span.X2 > FilledBounds->X2) FilledBounds->X2 = Span.X2;
            FilledBounds->Y2 = Y;
        }
    }

    return FilledAny;
}

/************************************************************************/

BOOL GraphicsFillTriangleSpans(LPGRAPHICSCONTEXT Context, LPTRIANGLE_INFO Info, COLOR FillColor, LPRECT FilledBounds) {
    return GraphicsFillTriangleScanlinesInternal(
        Context, Info, FillColor, FillColor, GRAPHICS_GRADIENT_AXIS_VERTICAL, FilledBounds);
}

/************************************************************************/

BOOL GraphicsFillTriangleVerticalGradient(
    LPGRAPHICSCONTEXT Context, LPTRIANGLE_INFO Info, COLOR StartColor, COLOR EndColor, LPRECT FilledBounds) {
    return GraphicsFillTriangleScanlinesInternal(
        Context, Info, StartColor, EndColor, GRAPHICS_GRADIENT_AXIS_VERTICAL, FilledBounds);
}

/************************************************************************/

BOOL GraphicsFillTriangleHorizontalGradient(
    LPGRAPHICSCONTEXT Context, LPTRIANGLE_INFO Info, COLOR StartColor, COLOR EndColor, LPRECT FilledBounds) {
    return GraphicsFillTriangleScanlinesInternal(
        Context, Info, StartColor, EndColor, GRAPHICS_GRADIENT_AXIS_HORIZONTAL, FilledBounds);
}

/************************************************************************/

static F64 GraphicsAbsoluteF64(F64 Value) {
    return Value < 0 ? -Value : Value;
}

/************************************************************************/

static I32 GraphicsRoundF64ToI32(F64 Value) {
    return Value >= 0 ? (I32)(Value + 0.5) : (I32)(Value - 0.5);
}

/************************************************************************/

static BOOL GraphicsStrokePoint(LPGRAPHICSCONTEXT Context, I32 X, I32 Y, COLOR StrokeColor);
static BOOL GraphicsPlotStrokePixel(LPVOID Context, I32 X, I32 Y, COLOR* Color);
static BOOL GraphicsDrawSinglePixelLine(
    LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR StrokeColor, U32 Pattern);

/************************************************************************/

static BOOL GraphicsComputeInsetTriangle(
    LPTRIANGLE_INFO Info, U32 Inset, POINT* OutP1, POINT* OutP2, POINT* OutP3) {
    const POINT* Vertices[3];
    F64 NormalX[3];
    F64 NormalY[3];
    F64 Constant[3];
    F64 CentroidX;
    F64 CentroidY;
    UINT Index;

    if (Info == NULL || OutP1 == NULL || OutP2 == NULL || OutP3 == NULL) return FALSE;
    if (Inset == 0) {
        *OutP1 = Info->P1;
        *OutP2 = Info->P2;
        *OutP3 = Info->P3;
        return TRUE;
    }

    Vertices[0] = &(Info->P1);
    Vertices[1] = &(Info->P2);
    Vertices[2] = &(Info->P3);
    CentroidX = ((F64)Info->P1.X + (F64)Info->P2.X + (F64)Info->P3.X) / 3.0;
    CentroidY = ((F64)Info->P1.Y + (F64)Info->P2.Y + (F64)Info->P3.Y) / 3.0;

    for (Index = 0; Index < 3; Index++) {
        const POINT* Start = Vertices[Index];
        const POINT* End = Vertices[(Index + 1) % 3];
        F64 DeltaX = (F64)(End->X - Start->X);
        F64 DeltaY = (F64)(End->Y - Start->Y);
        F64 Length = MathSqrtF64((DeltaX * DeltaX) + (DeltaY * DeltaY));
        F64 MidX;
        F64 MidY;
        F64 Dot;

        if (Length <= MATH_EPSILON_F64) return FALSE;

        NormalX[Index] = DeltaY / Length;
        NormalY[Index] = -DeltaX / Length;
        MidX = ((F64)Start->X + (F64)End->X) * 0.5;
        MidY = ((F64)Start->Y + (F64)End->Y) * 0.5;
        Dot = (CentroidX - MidX) * NormalX[Index] + (CentroidY - MidY) * NormalY[Index];
        if (Dot < 0) {
            NormalX[Index] = -NormalX[Index];
            NormalY[Index] = -NormalY[Index];
        }

        Constant[Index] = (NormalX[Index] * (F64)Start->X) + (NormalY[Index] * (F64)Start->Y) + (F64)Inset;
    }

    {
        F64 Determinant = (NormalX[2] * NormalY[0]) - (NormalY[2] * NormalX[0]);
        F64 X;
        F64 Y;

        if (GraphicsAbsoluteF64(Determinant) <= MATH_EPSILON_F64) return FALSE;
        X = ((Constant[2] * NormalY[0]) - (NormalY[2] * Constant[0])) / Determinant;
        Y = ((NormalX[2] * Constant[0]) - (Constant[2] * NormalX[0])) / Determinant;
        OutP1->X = GraphicsRoundF64ToI32(X);
        OutP1->Y = GraphicsRoundF64ToI32(Y);
    }

    {
        F64 Determinant = (NormalX[0] * NormalY[1]) - (NormalY[0] * NormalX[1]);
        F64 X;
        F64 Y;

        if (GraphicsAbsoluteF64(Determinant) <= MATH_EPSILON_F64) return FALSE;
        X = ((Constant[0] * NormalY[1]) - (NormalY[0] * Constant[1])) / Determinant;
        Y = ((NormalX[0] * Constant[1]) - (Constant[0] * NormalX[1])) / Determinant;
        OutP2->X = GraphicsRoundF64ToI32(X);
        OutP2->Y = GraphicsRoundF64ToI32(Y);
    }

    {
        F64 Determinant = (NormalX[1] * NormalY[2]) - (NormalY[1] * NormalX[2]);
        F64 X;
        F64 Y;

        if (GraphicsAbsoluteF64(Determinant) <= MATH_EPSILON_F64) return FALSE;
        X = ((Constant[1] * NormalY[2]) - (NormalY[1] * Constant[2])) / Determinant;
        Y = ((NormalX[1] * Constant[2]) - (Constant[1] * NormalX[2])) / Determinant;
        OutP3->X = GraphicsRoundF64ToI32(X);
        OutP3->Y = GraphicsRoundF64ToI32(Y);
    }

    return GraphicsTriangleEdgeFunction(OutP1->X, OutP1->Y, OutP2->X, OutP2->Y, OutP3->X, OutP3->Y) != 0;
}

/************************************************************************/

BOOL GraphicsDrawTriangleFromDescriptor(LPGRAPHICSCONTEXT Context, LPTRIANGLE_INFO Info) {
    TRIANGLE_INFO InsetTriangle;
    U32 StrokeWidth = 1;
    U32 Offset = 0;
    BOOL HasFill = FALSE;
    BOOL HasStroke = FALSE;
    I32 Area = 0;

    if (Context == NULL || Info == NULL) return FALSE;
    GraphicsSlowRedrawPauseIfNeeded();

    HasFill = (Context->Brush != NULL && Context->Brush->TypeID == KOID_BRUSH);
    HasStroke = (Context->Pen != NULL && Context->Pen->TypeID == KOID_PEN);
    if (HasFill == FALSE && HasStroke == FALSE) return TRUE;

    Area = GraphicsTriangleEdgeFunction(Info->P1.X, Info->P1.Y, Info->P2.X, Info->P2.Y, Info->P3.X, Info->P3.Y);
    if (Area != 0 && HasFill != FALSE) {
        if (GraphicsFillTriangleSpans(Context, Info, Context->Brush->Color, NULL) == FALSE) return FALSE;
    }

    if (HasStroke == FALSE) return TRUE;

    if (Area == 0) {
        LineRasterizerDraw(
            Context,
            Info->P1.X,
            Info->P1.Y,
            Info->P2.X,
            Info->P2.Y,
            Context->Pen->Color,
            Context->Pen->Pattern,
            GraphicsResolvePenWidth(Context),
            GraphicsPlotStrokePixel);
        LineRasterizerDraw(
            Context,
            Info->P2.X,
            Info->P2.Y,
            Info->P3.X,
            Info->P3.Y,
            Context->Pen->Color,
            Context->Pen->Pattern,
            GraphicsResolvePenWidth(Context),
            GraphicsPlotStrokePixel);
        LineRasterizerDraw(
            Context,
            Info->P3.X,
            Info->P3.Y,
            Info->P1.X,
            Info->P1.Y,
            Context->Pen->Color,
            Context->Pen->Pattern,
            GraphicsResolvePenWidth(Context),
            GraphicsPlotStrokePixel);
        return TRUE;
    }

    StrokeWidth = GraphicsResolvePenWidth(Context);
    InsetTriangle = *Info;
    for (Offset = 0; Offset < StrokeWidth; Offset++) {
        if (GraphicsComputeInsetTriangle(Info, Offset, &(InsetTriangle.P1), &(InsetTriangle.P2), &(InsetTriangle.P3)) == FALSE) break;
        if (GraphicsDrawSinglePixelLine(
                Context,
                InsetTriangle.P1.X,
                InsetTriangle.P1.Y,
                InsetTriangle.P2.X,
                InsetTriangle.P2.Y,
                Context->Pen->Color,
                Context->Pen->Pattern) == FALSE) {
            return FALSE;
        }
        if (GraphicsDrawSinglePixelLine(
                Context,
                InsetTriangle.P2.X,
                InsetTriangle.P2.Y,
                InsetTriangle.P3.X,
                InsetTriangle.P3.Y,
                Context->Pen->Color,
                Context->Pen->Pattern) == FALSE) {
            return FALSE;
        }
        if (GraphicsDrawSinglePixelLine(
                Context,
                InsetTriangle.P3.X,
                InsetTriangle.P3.Y,
                InsetTriangle.P1.X,
                InsetTriangle.P1.Y,
                Context->Pen->Color,
                Context->Pen->Pattern) == FALSE) {
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

static BOOL GraphicsStrokePoint(LPGRAPHICSCONTEXT Context, I32 X, I32 Y, COLOR StrokeColor) {
    return GraphicsDrawFillSpan(
        Context,
        &(GRAPHICS_FILL_DESCRIPTOR){
            .Enabled = TRUE,
            .HasGradient = FALSE,
            .Axis = GRAPHICS_GRADIENT_AXIS_VERTICAL,
            .StartColor = StrokeColor,
            .EndColor = StrokeColor},
        X,
        X,
        Y);
}

/************************************************************************/

static BOOL GraphicsPlotStrokePixel(LPVOID Context, I32 X, I32 Y, COLOR* Color) {
    if (Color == NULL) return FALSE;
    return GraphicsStrokePoint((LPGRAPHICSCONTEXT)Context, X, Y, *Color);
}

/************************************************************************/

static BOOL GraphicsDrawSinglePixelLine(
    LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR StrokeColor, U32 Pattern) {
    LineRasterizerDraw(Context, X1, Y1, X2, Y2, StrokeColor, Pattern, 1, GraphicsPlotStrokePixel);
    return TRUE;
}

/************************************************************************/

static BOOL GraphicsStrokeRectangleInset(
    LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR StrokeColor) {
    if (Context == NULL) return FALSE;
    if (X1 > X2 || Y1 > Y2) return TRUE;

    if (GraphicsDrawScanline(Context, X1, X2, Y1, StrokeColor, StrokeColor) == FALSE) return FALSE;
    if (Y2 != Y1 && GraphicsDrawScanline(Context, X1, X2, Y2, StrokeColor, StrokeColor) == FALSE) return FALSE;
    if (Y2 - Y1 > 1) {
        if (GraphicsFillSolidRect(Context, X1, Y1 + 1, X1, Y2 - 1, StrokeColor) == FALSE) return FALSE;
        if (X2 != X1 && GraphicsFillSolidRect(Context, X2, Y1 + 1, X2, Y2 - 1, StrokeColor) == FALSE) return FALSE;
    }

    return TRUE;
}

/************************************************************************/

static BOOL GraphicsStrokeRoundedRectangleInset(
    LPGRAPHICSCONTEXT Context, I32 X1, I32 Y1, I32 X2, I32 Y2, I32 Radius, COLOR StrokeColor) {
    GRAPHICS_FILL_DESCRIPTOR Fill = {0};

    if (Context == NULL) return FALSE;
    if (X1 > X2 || Y1 > Y2) return TRUE;

    if (Radius <= 0) {
        return GraphicsStrokeRectangleInset(Context, X1, Y1, X2, Y2, StrokeColor);
    }

    if (GraphicsRenderArc(Context, X1 + Radius, Y1 + Radius, Radius, GRAPHICS_ARC_QUADRANT_TOP_LEFT, &Fill, TRUE, StrokeColor) == FALSE) {
        return FALSE;
    }
    if (GraphicsRenderArc(Context, X2 - Radius, Y1 + Radius, Radius, GRAPHICS_ARC_QUADRANT_TOP_RIGHT, &Fill, TRUE, StrokeColor) == FALSE) {
        return FALSE;
    }
    if (GraphicsRenderArc(Context, X2 - Radius, Y2 - Radius, Radius, GRAPHICS_ARC_QUADRANT_BOTTOM_RIGHT, &Fill, TRUE, StrokeColor) == FALSE) {
        return FALSE;
    }
    if (GraphicsRenderArc(Context, X1 + Radius, Y2 - Radius, Radius, GRAPHICS_ARC_QUADRANT_BOTTOM_LEFT, &Fill, TRUE, StrokeColor) == FALSE) {
        return FALSE;
    }
    if (X1 + Radius <= X2 - Radius &&
        GraphicsDrawScanline(Context, X1 + Radius, X2 - Radius, Y1, StrokeColor, StrokeColor) == FALSE) {
        return FALSE;
    }
    if (X1 + Radius <= X2 - Radius &&
        GraphicsDrawScanline(Context, X1 + Radius, X2 - Radius, Y2, StrokeColor, StrokeColor) == FALSE) {
        return FALSE;
    }
    if (Y1 + Radius <= Y2 - Radius &&
        GraphicsFillSolidRect(Context, X1, Y1 + Radius, X1, Y2 - Radius, StrokeColor) == FALSE) {
        return FALSE;
    }
    if (Y1 + Radius <= Y2 - Radius &&
        GraphicsFillSolidRect(Context, X2, Y1 + Radius, X2, Y2 - Radius, StrokeColor) == FALSE) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

static BOOL GraphicsRenderArcSpan(
    LPGRAPHICSCONTEXT Context,
    I32 CenterX,
    I32 CenterY,
    I32 SpanX,
    I32 RowOffset,
    U32 QuadrantMask,
    LPGRAPHICS_FILL_DESCRIPTOR Fill,
    BOOL HasStroke,
    COLOR StrokeColor) {
    I32 TopY = CenterY - RowOffset;
    I32 BottomY = CenterY + RowOffset;

    if (Context == NULL || SpanX < 0) return FALSE;

    if ((QuadrantMask & GRAPHICS_ARC_QUADRANT_TOP_LEFT) != 0) {
        if (GraphicsDrawFillSpan(Context, Fill, CenterX - SpanX, CenterX, TopY) == FALSE) return FALSE;
        if (HasStroke != FALSE && GraphicsStrokePoint(Context, CenterX - SpanX, TopY, StrokeColor) == FALSE) return FALSE;
    }

    if ((QuadrantMask & GRAPHICS_ARC_QUADRANT_TOP_RIGHT) != 0) {
        if (GraphicsDrawFillSpan(Context, Fill, CenterX, CenterX + SpanX, TopY) == FALSE) return FALSE;
        if (HasStroke != FALSE && GraphicsStrokePoint(Context, CenterX + SpanX, TopY, StrokeColor) == FALSE) return FALSE;
    }

    if (BottomY == TopY) return TRUE;

    if ((QuadrantMask & GRAPHICS_ARC_QUADRANT_BOTTOM_LEFT) != 0) {
        if (GraphicsDrawFillSpan(Context, Fill, CenterX - SpanX, CenterX, BottomY) == FALSE) return FALSE;
        if (HasStroke != FALSE && GraphicsStrokePoint(Context, CenterX - SpanX, BottomY, StrokeColor) == FALSE) return FALSE;
    }

    if ((QuadrantMask & GRAPHICS_ARC_QUADRANT_BOTTOM_RIGHT) != 0) {
        if (GraphicsDrawFillSpan(Context, Fill, CenterX, CenterX + SpanX, BottomY) == FALSE) return FALSE;
        if (HasStroke != FALSE && GraphicsStrokePoint(Context, CenterX + SpanX, BottomY, StrokeColor) == FALSE) return FALSE;
    }

    return TRUE;
}

/************************************************************************/

static BOOL GraphicsRenderArc(
    LPGRAPHICSCONTEXT Context,
    I32 CenterX,
    I32 CenterY,
    I32 Radius,
    U32 QuadrantMask,
    LPGRAPHICS_FILL_DESCRIPTOR Fill,
    BOOL HasStroke,
    COLOR StrokeColor) {
    I32 X = 0;
    I32 Y = 0;
    I32 Error = 0;

    if (Context == NULL || Context->MemoryBase == NULL) return FALSE;
    if (Radius <= 0 || QuadrantMask == 0) return FALSE;

    X = Radius;
    Y = 0;
    Error = 1 - Radius;

    while (X >= Y) {
        if (GraphicsRenderArcSpan(Context, CenterX, CenterY, X, Y, QuadrantMask, Fill, HasStroke, StrokeColor) == FALSE) {
            return FALSE;
        }
        if (X != Y) {
            if (GraphicsRenderArcSpan(Context, CenterX, CenterY, Y, X, QuadrantMask, Fill, HasStroke, StrokeColor) == FALSE) {
                return FALSE;
            }
        }

        Y++;
        if (Error < 0) {
            Error += (2 * Y) + 1;
        } else {
            X--;
            Error += 2 * (Y - X) + 1;
        }
    }

    return TRUE;
}

/************************************************************************/

BOOL GraphicsStrokeArc(LPVOID Context, GRAPHICS_PLOT_PIXEL_ROUTINE PlotPixel, I32 CenterX, I32 CenterY, I32 Radius, COLOR StrokeColor) {
    GRAPHICS_FILL_DESCRIPTOR Fill = {0};
    U32 StrokeWidth = GraphicsResolvePenWidth((LPGRAPHICSCONTEXT)Context);
    U32 Offset = 0;

    UNUSED(PlotPixel);
    GraphicsSlowRedrawPauseIfNeeded();

    for (Offset = 0; Offset < StrokeWidth; Offset++) {
        I32 CurrentRadius = Radius - (I32)Offset;

        if (CurrentRadius <= 0) break;
        if (GraphicsRenderArc((LPGRAPHICSCONTEXT)Context, CenterX, CenterY, CurrentRadius, GRAPHICS_ARC_QUADRANT_ALL, &Fill, TRUE, StrokeColor) == FALSE) {
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

BOOL GraphicsDrawArcFromDescriptor(LPGRAPHICSCONTEXT Context, LPARC_INFO Info) {
    GRAPHICS_FILL_DESCRIPTOR Fill;
    GRAPHICS_FILL_DESCRIPTOR EmptyFill = {0};
    COLOR StrokeColor = 0;
    BOOL HasStroke = FALSE;
    U32 QuadrantMask = 0;
    U32 StrokeWidth = 1;
    U32 Offset = 0;

    if (Context == NULL || Info == NULL) return FALSE;
    GraphicsSlowRedrawPauseIfNeeded();
    if (Info->Radius <= 0) return TRUE;
    if (GraphicsSetupArcFillDescriptor(Context, Info, &Fill) == FALSE) return FALSE;

    HasStroke = Context->Pen != NULL && Context->Pen->TypeID == KOID_PEN;
    if (HasStroke != FALSE) {
        StrokeColor = Context->Pen->Color;
        StrokeWidth = GraphicsResolvePenWidth(Context);
    }

    QuadrantMask = GraphicsResolveArcQuadrantMask(Info);
    if (QuadrantMask == 0) return TRUE;
    if (Fill.Enabled != FALSE) {
        if (GraphicsRenderArc(Context, Info->CenterX, Info->CenterY, Info->Radius, QuadrantMask, &Fill, FALSE, 0) == FALSE) {
            return FALSE;
        }
    }
    if (HasStroke == FALSE) return TRUE;

    for (Offset = 0; Offset < StrokeWidth; Offset++) {
        I32 CurrentRadius = Info->Radius - (I32)Offset;

        if (CurrentRadius <= 0) break;
        if (GraphicsRenderArc(Context, Info->CenterX, Info->CenterY, CurrentRadius, QuadrantMask, &EmptyFill, TRUE, StrokeColor) == FALSE) {
            return FALSE;
        }
    }

    return TRUE;
}

/************************************************************************/

BOOL GraphicsDrawRectangleFromDescriptor(LPGRAPHICSCONTEXT Context, LPRECT_INFO Info) {
    GRAPHICS_FILL_DESCRIPTOR Fill;
    I32 X1 = 0;
    I32 Y1 = 0;
    I32 X2 = 0;
    I32 Y2 = 0;
    I32 Radius = 0;
    I32 Y = 0;
    BOOL HasStroke = FALSE;
    U32 StrokeWidth = 1;
    U32 Offset = 0;

    if (Context == NULL || Info == NULL) return FALSE;
    GraphicsSlowRedrawPauseIfNeeded();
    if (GraphicsSetupRectangleFillDescriptor(Context, Info, &Fill) == FALSE) return FALSE;

    X1 = Info->X1;
    Y1 = Info->Y1;
    X2 = Info->X2;
    Y2 = Info->Y2;
    GraphicsNormalizeRectangle(&X1, &Y1, &X2, &Y2);
    Radius = (I32)GraphicsResolveRoundedCornerRadius(Info, X1, Y1, X2, Y2);
    HasStroke = Context->Pen != NULL && Context->Pen->TypeID == KOID_PEN;
    if (HasStroke != FALSE) {
        StrokeWidth = GraphicsResolvePenWidth(Context);
    }

    if (Radius <= 0) {
        if (GraphicsFillRectangleFromDescriptor(Context, Info) == FALSE) return FALSE;

        if (HasStroke != FALSE) {
            for (Offset = 0; Offset < StrokeWidth; Offset++) {
                I32 StrokeX1 = X1 + (I32)Offset;
                I32 StrokeY1 = Y1 + (I32)Offset;
                I32 StrokeX2 = X2 - (I32)Offset;
                I32 StrokeY2 = Y2 - (I32)Offset;

                if (GraphicsStrokeRectangleInset(Context, StrokeX1, StrokeY1, StrokeX2, StrokeY2, Context->Pen->Color) == FALSE) return FALSE;
            }
        }

        return TRUE;
    }

    for (Y = Y1; Y <= Y2; Y++) {
        I32 SpanStart = X1;
        I32 SpanEnd = X2;

        if (Y < Y1 + Radius || Y > Y2 - Radius) {
            SpanStart = X1 + Radius;
            SpanEnd = X2 - Radius;
        }

        if (GraphicsDrawFillSpan(Context, &Fill, SpanStart, SpanEnd, Y) == FALSE) return FALSE;
    }

    if (GraphicsRenderArc(Context, X1 + Radius, Y1 + Radius, Radius, GRAPHICS_ARC_QUADRANT_TOP_LEFT, &Fill, FALSE, 0) == FALSE) {
        return FALSE;
    }
    if (GraphicsRenderArc(Context, X2 - Radius, Y1 + Radius, Radius, GRAPHICS_ARC_QUADRANT_TOP_RIGHT, &Fill, FALSE, 0) == FALSE) {
        return FALSE;
    }
    if (GraphicsRenderArc(Context, X2 - Radius, Y2 - Radius, Radius, GRAPHICS_ARC_QUADRANT_BOTTOM_RIGHT, &Fill, FALSE, 0) == FALSE) {
        return FALSE;
    }
    if (GraphicsRenderArc(Context, X1 + Radius, Y2 - Radius, Radius, GRAPHICS_ARC_QUADRANT_BOTTOM_LEFT, &Fill, FALSE, 0) == FALSE) {
        return FALSE;
    }

    if (HasStroke != FALSE) {
        for (Offset = 0; Offset < StrokeWidth; Offset++) {
            I32 StrokeX1 = X1 + (I32)Offset;
            I32 StrokeY1 = Y1 + (I32)Offset;
            I32 StrokeX2 = X2 - (I32)Offset;
            I32 StrokeY2 = Y2 - (I32)Offset;
            I32 StrokeRadius = Radius - (I32)Offset;

            if (StrokeRadius < 0) StrokeRadius = 0;
            if (GraphicsStrokeRoundedRectangleInset(Context, StrokeX1, StrokeY1, StrokeX2, StrokeY2, StrokeRadius, Context->Pen->Color) == FALSE) {
                return FALSE;
            }
        }
    }

    return TRUE;
}

/************************************************************************/

BOOL IntersectRect(LPRECT Left, LPRECT Right, LPRECT Result) {
    if (Left == NULL || Right == NULL || Result == NULL) return FALSE;

    Result->X1 = Left->X1 > Right->X1 ? Left->X1 : Right->X1;
    Result->Y1 = Left->Y1 > Right->Y1 ? Left->Y1 : Right->Y1;
    Result->X2 = Left->X2 < Right->X2 ? Left->X2 : Right->X2;
    Result->Y2 = Left->Y2 < Right->Y2 ? Left->Y2 : Right->Y2;

    return Result->X1 <= Result->X2 && Result->Y1 <= Result->Y2;
}

/************************************************************************/

BOOL SubtractRectFromRect(LPRECT Source, LPRECT Occluder, LPRECT_REGION Region) {
    RECT Intersection;
    RECT Piece;

    if (Source == NULL || Occluder == NULL || Region == NULL) return FALSE;

    if (IntersectRect(Source, Occluder, &Intersection) == FALSE) {
        return GraphicsRegionAppendRectIfValid(Region, Source);
    }

    Piece.X1 = Source->X1;
    Piece.Y1 = Source->Y1;
    Piece.X2 = Source->X2;
    Piece.Y2 = Intersection.Y1 - 1;
    if (GraphicsRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece.X1 = Source->X1;
    Piece.Y1 = Intersection.Y2 + 1;
    Piece.X2 = Source->X2;
    Piece.Y2 = Source->Y2;
    if (GraphicsRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece.X1 = Source->X1;
    Piece.Y1 = Intersection.Y1;
    Piece.X2 = Intersection.X1 - 1;
    Piece.Y2 = Intersection.Y2;
    if (GraphicsRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece.X1 = Intersection.X2 + 1;
    Piece.Y1 = Intersection.Y1;
    Piece.X2 = Source->X2;
    Piece.Y2 = Intersection.Y2;
    if (GraphicsRegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    return TRUE;
}

/************************************************************************/

BOOL SubtractRectFromRegion(LPRECT_REGION Region, LPRECT Occluder, LPRECT TempStorage, UINT TempCapacity) {
    RECT_REGION TempRegion;
    RECT Current;
    UINT Count;
    UINT Index;

    if (Region == NULL || Occluder == NULL) return FALSE;
    if (RectRegionInit(&TempRegion, TempStorage, TempCapacity) == FALSE) return FALSE;
    RectRegionReset(&TempRegion);

    Count = RectRegionGetCount(Region);
    for (Index = 0; Index < Count; Index++) {
        if (RectRegionGetRect(Region, Index, &Current) == FALSE) return FALSE;
        if (SubtractRectFromRect(&Current, Occluder, &TempRegion) == FALSE) return FALSE;
    }

    RectRegionReset(Region);
    Count = RectRegionGetCount(&TempRegion);
    for (Index = 0; Index < Count; Index++) {
        if (RectRegionGetRect(&TempRegion, Index, &Current) == FALSE) return FALSE;
        if (RectRegionAddRect(Region, &Current) == FALSE) return FALSE;
    }

    return TRUE;
}

/************************************************************************/

void GraphicsScreenRectToWindowRect(LPRECT WindowScreenRect, LPRECT ScreenRect, LPRECT WindowRect) {
    if (WindowScreenRect == NULL || ScreenRect == NULL || WindowRect == NULL) return;

    WindowRect->X1 = ScreenRect->X1 - WindowScreenRect->X1;
    WindowRect->Y1 = ScreenRect->Y1 - WindowScreenRect->Y1;
    WindowRect->X2 = ScreenRect->X2 - WindowScreenRect->X1;
    WindowRect->Y2 = ScreenRect->Y2 - WindowScreenRect->Y1;
}

/************************************************************************/

void GraphicsWindowRectToScreenRect(LPRECT WindowScreenRect, LPRECT WindowRect, LPRECT ScreenRect) {
    if (WindowScreenRect == NULL || WindowRect == NULL || ScreenRect == NULL) return;

    ScreenRect->X1 = WindowScreenRect->X1 + WindowRect->X1;
    ScreenRect->Y1 = WindowScreenRect->Y1 + WindowRect->Y1;
    ScreenRect->X2 = WindowScreenRect->X1 + WindowRect->X2;
    ScreenRect->Y2 = WindowScreenRect->Y1 + WindowRect->Y2;
}

/************************************************************************/

void GraphicsScreenPointToWindowPoint(LPRECT WindowScreenRect, LPPOINT ScreenPoint, LPPOINT WindowPoint) {
    if (WindowScreenRect == NULL || ScreenPoint == NULL || WindowPoint == NULL) return;

    WindowPoint->X = ScreenPoint->X - WindowScreenRect->X1;
    WindowPoint->Y = ScreenPoint->Y - WindowScreenRect->Y1;
}

/************************************************************************/

void GraphicsWindowPointToScreenPoint(LPRECT WindowScreenRect, LPPOINT WindowPoint, LPPOINT ScreenPoint) {
    if (WindowScreenRect == NULL || WindowPoint == NULL || ScreenPoint == NULL) return;

    ScreenPoint->X = WindowScreenRect->X1 + WindowPoint->X;
    ScreenPoint->Y = WindowScreenRect->Y1 + WindowPoint->Y;
}

/************************************************************************/
