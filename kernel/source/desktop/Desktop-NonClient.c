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
#include "Desktop-Private.h"
#include "Desktop-ThemeRecipes.h"
#include "Desktop-ThemeResolver.h"
#include "Desktop-ThemeTokens.h"
#include "CoreString.h"
#include "GFX.h"
#include "Kernel.h"
#include "Log.h"
#include "utils/Graphics-Utils.h"

/***************************************************************************/

#define DESKTOP_NON_CLIENT_TRACE_SHELLBAR_WINDOW_ID 0x53484252
#define DESKTOP_NON_CLIENT_TRACE_TEST_WINDOW_ID 0x000085A1

/***************************************************************************/

/**
 * @brief Draw one filled rectangle using an explicit color.
 * @param GC Graphics context.
 * @param X1 Left.
 * @param Y1 Top.
 * @param X2 Right.
 * @param Y2 Bottom.
 * @param Color Fill color.
 * @param CornerRadius Corner radius.
 * @return TRUE on success.
 */
static BOOL DrawSolidRect(HANDLE GC, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR Color, U32 CornerRadius) {
    RECT_INFO RectInfo;
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
    RectInfo.CornerRadius = (I32)CornerRadius;
    RectInfo.CornerStyle = CornerRadius > 0 ? RECT_CORNER_STYLE_ROUNDED : RECT_CORNER_STYLE_SQUARE;

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
 * @param CornerRadius Corner radius.
 * @return TRUE on success.
 */
static BOOL DrawVerticalGradientRect(
    HANDLE GC, I32 X1, I32 Y1, I32 X2, I32 Y2, COLOR StartColor, COLOR EndColor, U32 CornerRadius) {
    RECT_INFO RectInfo;

    if (GC == NULL) return FALSE;
    if (X1 > X2 || Y1 > Y2) return FALSE;

    RectInfo.Header.Size = sizeof(RectInfo);
    RectInfo.Header.Version = EXOS_ABI_VERSION;
    RectInfo.Header.Flags = RECT_FLAG_FILL_VERTICAL_GRADIENT;
    RectInfo.GC = GC;
    RectInfo.X1 = X1;
    RectInfo.Y1 = Y1;
    RectInfo.X2 = X2;
    RectInfo.Y2 = Y2;
    RectInfo.StartColor = StartColor;
    RectInfo.EndColor = EndColor;
    RectInfo.CornerRadius = (I32)CornerRadius;
    RectInfo.CornerStyle = CornerRadius > 0 ? RECT_CORNER_STYLE_ROUNDED : RECT_CORNER_STYLE_SQUARE;

    return Rectangle(&RectInfo);
}

/***************************************************************************/

/**
 * @brief Resolve themed title bar height.
 * @param TitleHeightOut Receives the title bar height in pixels.
 * @return TRUE on success.
 */
static BOOL ResolveWindowTitleBarHeight(U32* TitleHeightOut) {
    U32 TitleHeight = 22;

    if (TitleHeightOut == NULL) return FALSE;

    if (!DesktopThemeResolveLevel1Metric(TEXT("window.titlebar"), TEXT("normal"), TEXT("title_height"), &TitleHeight)) {
        if (!DesktopThemeResolveTokenMetricByName(TEXT("metric.window.title_height"), &TitleHeight)) {
            TitleHeight = 22;
        }
    }

    *TitleHeightOut = TitleHeight;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resolve themed border thickness for window border rendering.
 * @param ThicknessOut Receives thickness in pixels.
 * @return TRUE on success.
 */
static BOOL ResolveWindowBorderThickness(U32* ThicknessOut) {
    U32 BorderThickness;

    if (ThicknessOut == NULL) return FALSE;

    if (!DesktopThemeResolveLevel1Metric(TEXT("window.border"), TEXT("normal"), TEXT("border_thickness"), &BorderThickness)) {
        BorderThickness = 2;
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
    U32 TitleHeight = 22;
    COLOR BorderColor = 0;
    LINE_INFO LineInfo;
    PEN Pen;
    HANDLE OldPen;
    I32 Width;
    I32 Height;
    I32 MaxThickness;
    I32 Thickness;
    I32 BorderTopY;
    I32 Offset;

    if (GC == NULL || Rect == NULL) return;

    (void)ResolveWindowBorderThickness(&BorderThickness);
    (void)ResolveWindowTitleBarHeight(&TitleHeight);

    if (!DesktopThemeResolveLevel1Color(TEXT("window.border"), TEXT("normal"), TEXT("border_color"), &BorderColor)) {
        HANDLE Pen = GetSystemPen(SM_COLOR_DARK_SHADOW);
        SAFE_USE_VALID_ID((LPPEN)Pen, KOID_PEN) {
            BorderColor = ((LPPEN)Pen)->Color;
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
    BorderTopY = Rect->Y1 + (I32)TitleHeight;
    if (BorderTopY < Rect->Y1) BorderTopY = Rect->Y1;
    if (BorderTopY > Rect->Y2 + 1) BorderTopY = Rect->Y2 + 1;

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
        LineInfo.Y1 = BorderTopY;
        LineInfo.X2 = Rect->X1 + Offset;
        LineInfo.Y2 = Rect->Y2 - Thickness;
        if (LineInfo.Y1 <= LineInfo.Y2) (void)Line(&LineInfo);
    }

    // Right border lines
    for (Offset = 0; Offset < Thickness; Offset++) {
        LineInfo.X1 = Rect->X2 - Offset;
        LineInfo.Y1 = BorderTopY;
        LineInfo.X2 = Rect->X2 - Offset;
        LineInfo.Y2 = Rect->Y2 - Thickness;
        if (LineInfo.Y1 <= LineInfo.Y2) (void)Line(&LineInfo);
    }

    (void)SelectPen(GC, OldPen);
}

/***************************************************************************/

/**
 * @brief Draw a themed title bar in the non-client frame area.
 * @param Window Window handle.
 * @param GC Graphics context.
 * @param Rect Window-local rectangle.
 * @return TRUE when title bar was drawn.
 */
static BOOL DrawWindowTitleBarFromTheme(HANDLE Window, HANDLE GC, LPRECT Rect) {
    WINDOW_STATE_SNAPSHOT Snapshot;
    HANDLE TitleBrush;
    HANDLE TitleBrush2;
    HANDLE TitlePen;
    HANDLE OldPen = NULL;
    HANDLE OldBrush = NULL;
    LPBRUSH TitleBrushPtr;
    LPBRUSH TitleBrush2Ptr;
    COLOR Background = 0;
    COLOR Background2 = 0;
    COLOR TextColor = 0;
    U32 TitleHeight = 22;
    U32 CornerRadius = 6;
    I32 InnerX1;
    I32 InnerX2;
    I32 InnerY1;
    I32 InnerY2;
    I32 MaxTitleHeight;
    I32 BottomLineY;
    I32 TextHeight = 0;
    I32 TextY;
    COLOR SeparatorColor = 0;
    GFX_TEXT_MEASURE_INFO MeasureInfo;
    GFX_TEXT_DRAW_INFO DrawInfo;

    if (GC == NULL || Rect == NULL) return FALSE;
    if (GetWindowStateSnapshot((LPWINDOW)Window, &Snapshot) == FALSE) return FALSE;

    (void)ResolveWindowTitleBarHeight(&TitleHeight);
    if (!DesktopThemeResolveLevel1Metric(TEXT("window.titlebar"), TEXT("normal"), TEXT("corner_radius"), &CornerRadius)) {
        CornerRadius = 6;
    }
    if (TitleHeight == 0) return FALSE;

    InnerX1 = Rect->X1;
    InnerX2 = Rect->X2;
    InnerY1 = Rect->Y1;
    if (InnerX1 > InnerX2 || InnerY1 > Rect->Y2) return FALSE;

    MaxTitleHeight = Rect->Y2 - InnerY1 + 1;
    if (MaxTitleHeight <= 0) return FALSE;
    if ((I32)TitleHeight > MaxTitleHeight) TitleHeight = (U32)MaxTitleHeight;
    InnerY2 = InnerY1 + (I32)TitleHeight - 1;

    SAFE_USE_VALID_ID((LPWINDOW)Window, KOID_WINDOW) {
    }

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
            (void)DrawVerticalGradientRect(GC, InnerX1, InnerY1, InnerX2, InnerY2, Background, Background2, CornerRadius);
        } else {
            (void)DrawSolidRect(GC, InnerX1, InnerY1, InnerX2, InnerY2, Background, CornerRadius);
        }
    }

    if (!DesktopThemeResolveLevel1Color(TEXT("window.border"), TEXT("normal"), TEXT("border_color"), &SeparatorColor)) {
        HANDLE Pen = GetSystemPen(SM_COLOR_DARK_SHADOW);
        SAFE_USE_VALID_ID((LPPEN)Pen, KOID_PEN) {
            SeparatorColor = ((LPPEN)Pen)->Color;
        }
    }

    BottomLineY = InnerY2;
    if (BottomLineY >= Rect->Y1 && BottomLineY <= Rect->Y2) {
        (void)DrawSolidRect(GC, InnerX1, BottomLineY, InnerX2, BottomLineY, SeparatorColor, 0);
    }

    if (StringLength(Snapshot.Caption) == 0) return TRUE;

    if (!DesktopThemeResolveLevel1Color(TEXT("window.titlebar"), TEXT("normal"), TEXT("text_color"), &TextColor)) {
        TitlePen = GetSystemPen(SM_COLOR_TITLE_TEXT);
        SAFE_USE_VALID_ID((LPPEN)TitlePen, KOID_PEN) {
            TextColor = ((LPPEN)TitlePen)->Color;
        }
    }

    MeasureInfo = (GFX_TEXT_MEASURE_INFO){
        .Header = {.Size = sizeof(GFX_TEXT_MEASURE_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .Text = Snapshot.Caption,
        .Font = NULL,
        .Width = 0,
        .Height = 0
    };
    if (DesktopMeasureText(&MeasureInfo) != FALSE && MeasureInfo.Height != 0) {
        TextHeight = (I32)MeasureInfo.Height;
    } else {
        TextHeight = 16;
    }

    TextY = InnerY1 + (((InnerY2 - InnerY1 + 1) - TextHeight) / 2);
    if (TextY < InnerY1) TextY = InnerY1;

    OldPen = SelectPen(GC, GetSystemPen(SM_COLOR_TITLE_TEXT));
    OldBrush = SelectBrush(GC, NULL);
    if (TextColor != 0) {
        PEN TempPen;

        MemorySet(&TempPen, 0, sizeof(TempPen));
        TempPen.TypeID = KOID_PEN;
        TempPen.References = 1;
        TempPen.Color = TextColor;
        TempPen.Pattern = MAX_U32;
        (void)SelectPen(GC, (HANDLE)&TempPen);
    }

    DrawInfo = (GFX_TEXT_DRAW_INFO){
        .Header = {.Size = sizeof(GFX_TEXT_DRAW_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .GC = GC,
        .X = InnerX1 + 8,
        .Y = TextY,
        .Text = Snapshot.Caption,
        .Font = NULL
    };
    (void)DesktopDrawText(&DrawInfo);

    (void)SelectBrush(GC, OldBrush);
    (void)SelectPen(GC, OldPen);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw themed client area inside the non-client border.
 * @param Window Window handle.
 * @param GC Graphics context.
 * @param Rect Full window rectangle in window coordinates.
 * @return TRUE when a client area was drawn.
 */
BOOL DrawWindowClientArea(HANDLE Window, HANDLE GC, LPRECT Rect) {
    RECT ClientRect;

    if (Window == NULL || GC == NULL || Rect == NULL) return FALSE;
    if (GetWindowClientRectFromWindowRect((LPWINDOW)Window, Rect, &ClientRect) == FALSE) return FALSE;

    return DrawWindowBackground(Window, GC, &ClientRect, THEME_TOKEN_WINDOW_BACKGROUND_CLIENT);
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
    U32 DecorationMode;
    BOOL Result;

    DecorationMode = GetWindowDecorationMode(Window);
    Result = (DecorationMode == WINDOW_DECORATION_MODE_SYSTEM);

    if (Window != NULL && Window->TypeID == KOID_WINDOW &&
        (Window->WindowID == DESKTOP_NON_CLIENT_TRACE_SHELLBAR_WINDOW_ID ||
         Window->WindowID == DESKTOP_NON_CLIENT_TRACE_TEST_WINDOW_ID)) {
    }

    return Result;
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
    U32 TitleHeight = 22;
    I32 InnerX1;
    I32 InnerX2;
    I32 InnerY1;
    I32 InnerY2;
    I32 MaxTitleHeight;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ScreenPoint == NULL) return FALSE;
    if (ShouldDrawWindowNonClient(Window) == FALSE) return FALSE;
    (void)ResolveWindowTitleBarHeight(&TitleHeight);
    if (TitleHeight == 0) return FALSE;

    ScreenRect = Window->ScreenRect;

    InnerX1 = ScreenRect.X1;
    InnerX2 = ScreenRect.X2;
    InnerY1 = ScreenRect.Y1;
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
BOOL GetWindowClientRectFromWindowRect(LPWINDOW Window, LPRECT WindowRect, LPRECT ClientRect) {
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
    (void)ResolveWindowTitleBarHeight(&TitleHeight);

    Left += (I32)BorderThickness;
    Right -= (I32)BorderThickness;
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

BOOL GetWindowClientRect(HANDLE Handle, LPRECT ClientRect) {
    RECT WindowRect;
    LPWINDOW Window = (LPWINDOW)Handle;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ClientRect == NULL) return FALSE;
    if (GetWindowRect(Handle, &WindowRect) == FALSE) return FALSE;

    return GetWindowClientRectFromWindowRect(Window, &WindowRect, ClientRect);
}

/***************************************************************************/

BOOL GetWindowDrawableRectFromWindowRect(LPWINDOW Window, LPRECT WindowRect, LPRECT DrawableRect) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (WindowRect == NULL || DrawableRect == NULL) return FALSE;

    if (ShouldDrawWindowNonClient(Window) == FALSE) {
        *DrawableRect = *WindowRect;
        return TRUE;
    }

    return GetWindowClientRectFromWindowRect(Window, WindowRect, DrawableRect);
}

/***************************************************************************/

BOOL GetWindowDrawableRect(HANDLE Handle, LPRECT DrawableRect) {
    RECT WindowRect;
    LPWINDOW Window = (LPWINDOW)Handle;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (DrawableRect == NULL) return FALSE;
    if (GetWindowRect(Handle, &WindowRect) == FALSE) return FALSE;

    return GetWindowDrawableRectFromWindowRect(Window, &WindowRect, DrawableRect);
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
    LPWINDOW This = (LPWINDOW)Window;

    if (Window == NULL) return FALSE;
    if (GC == NULL) return FALSE;
    if (Rect == NULL) return FALSE;

    SAFE_USE_VALID_ID(This, KOID_WINDOW) {
        if (This->WindowID == DESKTOP_NON_CLIENT_TRACE_SHELLBAR_WINDOW_ID ||
            This->WindowID == DESKTOP_NON_CLIENT_TRACE_TEST_WINDOW_ID) {
        }
    }

    (void)DrawWindowTitleBarFromTheme(Window, GC, Rect);
    DrawWindowBorderFromTheme(GC, Rect);

    return TRUE;
}

/***************************************************************************/
