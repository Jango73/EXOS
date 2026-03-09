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


    Desktop non-client rendering

\************************************************************************/

#include "Desktop-NonClient.h"
#include "Desktop-ThemeRecipes.h"
#include "Desktop-ThemeResolver.h"
#include "Desktop-ThemeTokens.h"
#include "GFX.h"
#include "Kernel.h"

/***************************************************************************/

/**
 * @brief Draw one filled rectangle using an explicit color.
 * @param GC Graphics context.
 * @param X1 Left.
 * @param Y1 Top.
 * @param X2 Right.
 * @param Y2 Bottom.
 * @param Color Fill color.
 * @return TRUE on success.
 */
static BOOL DrawSolidRect(HANDLE GC, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR Color) {
    RECTINFO RectInfo;
    BRUSH Brush;
    HANDLE OldPen;
    HANDLE OldBrush;

    if (GC == NULL) return FALSE;
    if (X1 > X2 || Y1 > Y2) return FALSE;

    MemorySet(&Brush, 0, sizeof(Brush));
    Brush.TypeID = KOID_BRUSH;
    Brush.References = 1;
    Brush.Color = Color;
    Brush.Pattern = MAX_U32;

    RectInfo.Header.Size = sizeof(RectInfo);
    RectInfo.Header.Version = EXOS_ABI_VERSION;
    RectInfo.Header.Flags = 0;
    RectInfo.GC = GC;
    RectInfo.X1 = X1;
    RectInfo.Y1 = Y1;
    RectInfo.X2 = X2;
    RectInfo.Y2 = Y2;

    OldPen = SelectPen(GC, NULL);
    OldBrush = SelectBrush(GC, (HANDLE)&Brush);
    (void)Rectangle(&RectInfo);
    (void)SelectBrush(GC, OldBrush);
    (void)SelectPen(GC, OldPen);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw one vertical gradient rectangle.
 * @param GC Graphics context.
 * @param X1 Left.
 * @param Y1 Top.
 * @param X2 Right.
 * @param Y2 Bottom.
 * @param StartColor Gradient start color (top).
 * @param EndColor Gradient end color (bottom).
 * @return TRUE on success.
 */
static BOOL DrawVerticalGradientRect(HANDLE GC, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR StartColor, COLOR EndColor) {
    LINEINFO BaseLineInfo;
    LINEINFO LineInfo;
    PEN Pen;
    HANDLE OldPen;
    I32 Height;
    I32 Offset;
    U32 Numerator;
    U32 Denominator;
    U32 StartA;
    U32 StartR;
    U32 StartG;
    U32 StartB;
    U32 EndA;
    U32 EndR;
    U32 EndG;
    U32 EndB;
    COLOR RowColor;

    if (GC == NULL) return FALSE;
    if (X1 > X2 || Y1 > Y2) return FALSE;

    Height = Y2 - Y1 + 1;
    if (Height <= 1) {
        return DrawSolidRect(GC, X1, Y1, X2, Y2, StartColor);
    }

    StartA = (StartColor >> 24) & 0xFF;
    StartR = (StartColor >> 16) & 0xFF;
    StartG = (StartColor >> 8) & 0xFF;
    StartB = StartColor & 0xFF;
    EndA = (EndColor >> 24) & 0xFF;
    EndR = (EndColor >> 16) & 0xFF;
    EndG = (EndColor >> 8) & 0xFF;
    EndB = EndColor & 0xFF;

    MemorySet(&Pen, 0, sizeof(Pen));
    Pen.TypeID = KOID_PEN;
    Pen.References = 1;
    Pen.Pattern = MAX_U32;

    BaseLineInfo.Header.Size = sizeof(BaseLineInfo);
    BaseLineInfo.Header.Version = EXOS_ABI_VERSION;
    BaseLineInfo.Header.Flags = 0;
    BaseLineInfo.GC = GC;
    BaseLineInfo.X1 = X1;
    BaseLineInfo.X2 = X2;

    OldPen = SelectPen(GC, (HANDLE)&Pen);
    Denominator = (U32)(Height - 1);

    for (Offset = 0; Offset < Height; Offset++) {
        Numerator = (U32)Offset;

        RowColor =
            ((((StartA + (((I32)EndA - (I32)StartA) * Numerator) / Denominator) & 0xFF) << 24)) |
            ((((StartR + (((I32)EndR - (I32)StartR) * Numerator) / Denominator) & 0xFF) << 16)) |
            ((((StartG + (((I32)EndG - (I32)StartG) * Numerator) / Denominator) & 0xFF) << 8)) |
            (((StartB + (((I32)EndB - (I32)StartB) * Numerator) / Denominator) & 0xFF));

        Pen.Color = RowColor;
        LineInfo = BaseLineInfo;
        LineInfo.Y1 = Y1 + Offset;
        LineInfo.Y2 = Y1 + Offset;
        (void)Line(&LineInfo);
    }

    (void)SelectPen(GC, OldPen);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve themed border thickness for window frame rendering.
 * @param ThicknessOut Receives thickness in pixels.
 * @return TRUE on success.
 */
static BOOL ResolveWindowBorderThickness(U32* ThicknessOut) {
    U32 BorderThickness;

    if (ThicknessOut == NULL) return FALSE;

    if (!DesktopThemeResolveLevel1Metric(TEXT("window.border"), TEXT("normal"), TEXT("border_thickness"), &BorderThickness)) {
        if (!DesktopThemeResolveLevel1Metric(TEXT("window.frame"), TEXT("normal"), TEXT("border_thickness"), &BorderThickness)) {
            BorderThickness = 2;
        }
    }

    if (BorderThickness == 0) BorderThickness = 1;
    *ThicknessOut = BorderThickness;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw themed window borders around a window rectangle.
 * @param GC Graphics context.
 * @param Rect Target window rectangle in window coordinates.
 */
static void DrawWindowBorderFromTheme(HANDLE GC, LPRECT Rect) {
    U32 BorderThickness = 2;
    COLOR BorderColor = 0;
    LINEINFO LineInfo;
    PEN Pen;
    HANDLE OldPen;
    I32 Width;
    I32 Height;
    I32 MaxThickness;
    I32 Thickness;
    I32 Offset;

    if (GC == NULL || Rect == NULL) return;

    (void)ResolveWindowBorderThickness(&BorderThickness);

    if (!DesktopThemeResolveLevel1Color(TEXT("window.border"), TEXT("normal"), TEXT("border_color"), &BorderColor)) {
        if (!DesktopThemeResolveLevel1Color(TEXT("window.frame"), TEXT("normal"), TEXT("border_color"), &BorderColor)) {
            HANDLE Pen = GetSystemPen(SM_COLOR_DARK_SHADOW);
            SAFE_USE_VALID_ID((LPPEN)Pen, KOID_PEN) {
                BorderColor = ((LPPEN)Pen)->Color;
            }
        }
    }

    Width = Rect->X2 - Rect->X1 + 1;
    Height = Rect->Y2 - Rect->Y1 + 1;
    if (Width <= 0 || Height <= 0) return;

    MaxThickness = Width < Height ? Width : Height;
    MaxThickness /= 2;
    if (MaxThickness <= 0) return;

    Thickness = (I32)BorderThickness;
    if (Thickness <= 0) return;
    if (Thickness > MaxThickness) Thickness = MaxThickness;

    MemorySet(&Pen, 0, sizeof(Pen));
    Pen.TypeID = KOID_PEN;
    Pen.References = 1;
    Pen.Color = BorderColor;
    Pen.Pattern = MAX_U32;

    LineInfo.Header.Size = sizeof(LineInfo);
    LineInfo.Header.Version = EXOS_ABI_VERSION;
    LineInfo.Header.Flags = 0;
    LineInfo.GC = GC;

    OldPen = SelectPen(GC, (HANDLE)&Pen);

    // Top border lines
    for (Offset = 0; Offset < Thickness; Offset++) {
        LineInfo.X1 = Rect->X1;
        LineInfo.Y1 = Rect->Y1 + Offset;
        LineInfo.X2 = Rect->X2;
        LineInfo.Y2 = Rect->Y1 + Offset;
        (void)Line(&LineInfo);
    }

    // Bottom border lines
    for (Offset = 0; Offset < Thickness; Offset++) {
        LineInfo.X1 = Rect->X1;
        LineInfo.Y1 = Rect->Y2 - Offset;
        LineInfo.X2 = Rect->X2;
        LineInfo.Y2 = Rect->Y2 - Offset;
        (void)Line(&LineInfo);
    }

    // Left border lines
    for (Offset = 0; Offset < Thickness; Offset++) {
        LineInfo.X1 = Rect->X1 + Offset;
        LineInfo.Y1 = Rect->Y1 + Thickness;
        LineInfo.X2 = Rect->X1 + Offset;
        LineInfo.Y2 = Rect->Y2 - Thickness;
        if (LineInfo.Y1 <= LineInfo.Y2) (void)Line(&LineInfo);
    }

    // Right border lines
    for (Offset = 0; Offset < Thickness; Offset++) {
        LineInfo.X1 = Rect->X2 - Offset;
        LineInfo.Y1 = Rect->Y1 + Thickness;
        LineInfo.X2 = Rect->X2 - Offset;
        LineInfo.Y2 = Rect->Y2 - Thickness;
        if (LineInfo.Y1 <= LineInfo.Y2) (void)Line(&LineInfo);
    }

    (void)SelectPen(GC, OldPen);
}

/***************************************************************************/

/**
 * @brief Draw a themed title bar in the non-client frame area.
 * @param GC Graphics context.
 * @param Rect Window-local rectangle.
 * @return TRUE when title bar was drawn.
 */
static BOOL DrawWindowTitleBarFromTheme(HANDLE GC, LPRECT Rect) {
    HANDLE TitleBrush;
    HANDLE TitleBrush2;
    LPBRUSH TitleBrushPtr;
    LPBRUSH TitleBrush2Ptr;
    COLOR Background = 0;
    COLOR Background2 = 0;
    U32 BorderThickness;
    U32 TitleHeight = 22;
    I32 InnerX1;
    I32 InnerX2;
    I32 InnerY1;
    I32 InnerY2;
    I32 MaxTitleHeight;
    I32 BottomLineY;
    COLOR SeparatorColor = 0;

    if (GC == NULL || Rect == NULL) return FALSE;
    if (ResolveWindowBorderThickness(&BorderThickness) == FALSE) return FALSE;

    if (!DesktopThemeResolveLevel1Metric(TEXT("window.titlebar"), TEXT("normal"), TEXT("title_height"), &TitleHeight)) {
        if (!DesktopThemeResolveTokenMetricByName(TEXT("metric.window.title_height"), &TitleHeight)) {
            TitleHeight = 22;
        }
    }
    if (TitleHeight == 0) return FALSE;

    InnerX1 = Rect->X1 + (I32)BorderThickness;
    InnerX2 = Rect->X2 - (I32)BorderThickness;
    InnerY1 = Rect->Y1 + (I32)BorderThickness;
    if (InnerX1 > InnerX2 || InnerY1 > Rect->Y2) return FALSE;

    MaxTitleHeight = Rect->Y2 - InnerY1 + 1;
    if (MaxTitleHeight <= 0) return FALSE;
    if ((I32)TitleHeight > MaxTitleHeight) TitleHeight = (U32)MaxTitleHeight;
    InnerY2 = InnerY1 + (I32)TitleHeight - 1;

    if (!DesktopThemeResolveLevel1Color(TEXT("window.titlebar"), TEXT("normal"), TEXT("background"), &Background)) {
        TitleBrush = GetSystemBrush(SM_COLOR_TITLE_BAR);
        SAFE_USE_VALID_ID((LPBRUSH)TitleBrush, KOID_BRUSH) {
            TitleBrushPtr = (LPBRUSH)TitleBrush;
            Background = TitleBrushPtr->Color;
        }
    }

    if (!DesktopThemeResolveLevel1Color(TEXT("window.titlebar"), TEXT("normal"), TEXT("background2"), &Background2)) {
        TitleBrush2 = GetSystemBrush(SM_COLOR_TITLE_BAR_2);
        SAFE_USE_VALID_ID((LPBRUSH)TitleBrush2, KOID_BRUSH) {
            TitleBrush2Ptr = (LPBRUSH)TitleBrush2;
            Background2 = TitleBrush2Ptr->Color;
        }
    }

    if (Background != 0 || Background2 != 0) {
        if (Background2 != 0 && Background2 != Background) {
            (void)DrawVerticalGradientRect(GC, InnerX1, InnerY1, InnerX2, InnerY2, Background, Background2);
        } else {
            (void)DrawSolidRect(GC, InnerX1, InnerY1, InnerX2, InnerY2, Background);
        }
    }

    if (!DesktopThemeResolveLevel1Color(TEXT("window.border"), TEXT("normal"), TEXT("border_color"), &SeparatorColor)) {
        if (!DesktopThemeResolveLevel1Color(TEXT("window.frame"), TEXT("normal"), TEXT("border_color"), &SeparatorColor)) {
            HANDLE Pen = GetSystemPen(SM_COLOR_DARK_SHADOW);
            SAFE_USE_VALID_ID((LPPEN)Pen, KOID_PEN) {
                SeparatorColor = ((LPPEN)Pen)->Color;
            }
        }
    }

    BottomLineY = InnerY2;
    if (BottomLineY >= Rect->Y1 && BottomLineY <= Rect->Y2) {
        (void)DrawSolidRect(GC, InnerX1, BottomLineY, InnerX2, BottomLineY, SeparatorColor);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw themed client area inside the non-client frame.
 * @param Window Window handle.
 * @param GC Graphics context.
 * @param Rect Full window rectangle in window coordinates.
 * @return TRUE when a client area was drawn.
 */
static BOOL DrawWindowClientAreaFromTheme(HANDLE Window, HANDLE GC, LPRECT Rect) {
    RECT ClientRect;
    HANDLE ClientBrush;
    LPBRUSH ClientBrushPtr;
    COLOR ClientBackground = COLOR_WHITE;

    if (Window == NULL || GC == NULL || Rect == NULL) return FALSE;
    if (GetWindowClientRect((LPWINDOW)Window, Rect, &ClientRect) == FALSE) return FALSE;

    if (DesktopThemeDrawRecipeForElementState(Window, GC, &ClientRect, TEXT("window.client"), TEXT("normal"))) {
        return TRUE;
    }

    if (!DesktopThemeResolveLevel1Color(TEXT("window.client"), TEXT("normal"), TEXT("background"), &ClientBackground)) {
        if (!DesktopThemeResolveTokenColorByName(TEXT("color.client.background"), &ClientBackground)) {
            ClientBrush = GetSystemBrush(SM_COLOR_CLIENT);
            SAFE_USE_VALID_ID((LPBRUSH)ClientBrush, KOID_BRUSH) {
                ClientBrushPtr = (LPBRUSH)ClientBrush;
                ClientBackground = ClientBrushPtr->Color;
            }
        }
    }

    return DrawSolidRect(GC, ClientRect.X1, ClientRect.Y1, ClientRect.X2, ClientRect.Y2, ClientBackground);
}

/***************************************************************************/

/**
 * @brief Resolve decoration mode from a window style bitfield.
 * @param Style Window style bitfield.
 * @return One of WINDOW_DECORATION_MODE_* values.
 */
static U32 GetDecorationModeFromStyle(U32 Style) {
    if (Style & EWS_BARE_SURFACE) return WINDOW_DECORATION_MODE_BARE;
    if (Style & EWS_CLIENT_DECORATED) return WINDOW_DECORATION_MODE_CLIENT;

    // Compatibility: undecorated style bitfield defaults to system decorations.
    return WINDOW_DECORATION_MODE_SYSTEM;
}

/***************************************************************************/

/**
 * @brief Resolve the decoration mode configured on a window.
 * @param Window Window to inspect.
 * @return One of WINDOW_DECORATION_MODE_* values.
 */
U32 GetWindowDecorationMode(LPWINDOW Window) {
    if (Window == NULL) return WINDOW_DECORATION_MODE_SYSTEM;
    if (Window->TypeID != KOID_WINDOW) return WINDOW_DECORATION_MODE_SYSTEM;

    return GetDecorationModeFromStyle(Window->Style);
}

/***************************************************************************/

/**
 * @brief Tell whether the kernel non-client renderer should draw this window.
 * @param Window Window to inspect.
 * @return TRUE when system decorations are enabled.
 */
BOOL ShouldDrawWindowNonClient(LPWINDOW Window) {
    return (GetWindowDecorationMode(Window) == WINDOW_DECORATION_MODE_SYSTEM);
}

/***************************************************************************/

/**
 * @brief Tell whether one screen point lies in one window title bar.
 * @param Window Target window.
 * @param ScreenPoint Point in screen coordinates.
 * @return TRUE when the point is inside the window title bar area.
 */
BOOL IsPointInWindowTitleBar(LPWINDOW Window, LPPOINT ScreenPoint) {
    RECT ScreenRect;
    U32 BorderThickness;
    U32 TitleHeight = 22;
    I32 InnerX1;
    I32 InnerX2;
    I32 InnerY1;
    I32 InnerY2;
    I32 MaxTitleHeight;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ScreenPoint == NULL) return FALSE;
    if (ShouldDrawWindowNonClient(Window) == FALSE) return FALSE;
    if (ResolveWindowBorderThickness(&BorderThickness) == FALSE) return FALSE;

    if (!DesktopThemeResolveLevel1Metric(TEXT("window.titlebar"), TEXT("normal"), TEXT("title_height"), &TitleHeight)) {
        if (!DesktopThemeResolveTokenMetricByName(TEXT("metric.window.title_height"), &TitleHeight)) {
            TitleHeight = 22;
        }
    }
    if (TitleHeight == 0) return FALSE;

    ScreenRect = Window->ScreenRect;

    InnerX1 = ScreenRect.X1 + (I32)BorderThickness;
    InnerX2 = ScreenRect.X2 - (I32)BorderThickness;
    InnerY1 = ScreenRect.Y1 + (I32)BorderThickness;
    if (InnerX1 > InnerX2 || InnerY1 > ScreenRect.Y2) return FALSE;

    MaxTitleHeight = ScreenRect.Y2 - InnerY1 + 1;
    if (MaxTitleHeight <= 0) return FALSE;
    if ((I32)TitleHeight > MaxTitleHeight) TitleHeight = (U32)MaxTitleHeight;
    InnerY2 = InnerY1 + (I32)TitleHeight - 1;

    if (ScreenPoint->X < InnerX1 || ScreenPoint->X > InnerX2) return FALSE;
    if (ScreenPoint->Y < InnerY1 || ScreenPoint->Y > InnerY2) return FALSE;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Compute the client rectangle from a window-rectangle.
 * @param Window Target window.
 * @param WindowRect Full window rectangle (window coordinates).
 * @param ClientRect Receives client rectangle (window coordinates).
 * @return TRUE when a valid client area was produced.
 */
BOOL GetWindowClientRect(LPWINDOW Window, LPRECT WindowRect, LPRECT ClientRect) {
    U32 BorderThickness;
    U32 TitleHeight = 22;
    I32 Left;
    I32 Top;
    I32 Right;
    I32 Bottom;
    I32 MaxTitleHeight;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (WindowRect == NULL || ClientRect == NULL) return FALSE;

    Left = WindowRect->X1;
    Top = WindowRect->Y1;
    Right = WindowRect->X2;
    Bottom = WindowRect->Y2;

    if (Left > Right || Top > Bottom) return FALSE;

    if (ShouldDrawWindowNonClient(Window) == FALSE) {
        *ClientRect = *WindowRect;
        return TRUE;
    }

    if (ResolveWindowBorderThickness(&BorderThickness) == FALSE) return FALSE;

    if (!DesktopThemeResolveLevel1Metric(TEXT("window.titlebar"), TEXT("normal"), TEXT("title_height"), &TitleHeight)) {
        if (!DesktopThemeResolveTokenMetricByName(TEXT("metric.window.title_height"), &TitleHeight)) {
            TitleHeight = 22;
        }
    }

    Left += (I32)BorderThickness;
    Right -= (I32)BorderThickness;
    Top += (I32)BorderThickness;
    Bottom -= (I32)BorderThickness;

    if (Left > Right || Top > Bottom) return FALSE;

    MaxTitleHeight = Bottom - Top + 1;
    if (MaxTitleHeight <= 0) return FALSE;
    if ((I32)TitleHeight > MaxTitleHeight) TitleHeight = (U32)MaxTitleHeight;
    Top += (I32)TitleHeight;

    if (Top > Bottom) return FALSE;

    ClientRect->X1 = Left;
    ClientRect->Y1 = Top;
    ClientRect->X2 = Right;
    ClientRect->Y2 = Bottom;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw the default non-client visuals for a window.
 * @param Window Window handle.
 * @param GC Graphics context handle.
 * @param Rect Window-local rectangle to draw.
 * @return TRUE when drawing was performed.
 */
BOOL DrawWindowNonClient(HANDLE Window, HANDLE GC, LPRECT Rect) {
    if (Window == NULL) return FALSE;
    if (GC == NULL) return FALSE;
    if (Rect == NULL) return FALSE;

    (void)DrawWindowClientAreaFromTheme(Window, GC, Rect);

    if (DesktopThemeDrawRecipeForElementState(Window, GC, Rect, TEXT("window.frame"), TEXT("normal"))) {
        (void)DrawWindowTitleBarFromTheme(GC, Rect);
        DrawWindowBorderFromTheme(GC, Rect);
        return TRUE;
    }
    (void)DrawWindowTitleBarFromTheme(GC, Rect);
    DrawWindowBorderFromTheme(GC, Rect);

    return TRUE;
}

/***************************************************************************/
