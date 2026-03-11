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


    On-screen debug information component

\************************************************************************/

#include "desktop/components/OnScreenDebugInfo.h"

/***************************************************************************/

#define ON_SCREEN_DEBUG_INFO_LINE_COUNT 5
#define ON_SCREEN_DEBUG_INFO_PADDING_X 10
#define ON_SCREEN_DEBUG_INFO_PADDING_Y 8
#define ON_SCREEN_DEBUG_INFO_LINE_GAP 4
#define ON_SCREEN_DEBUG_INFO_DEFAULT_LINE_HEIGHT 16
#define ON_SCREEN_DEBUG_INFO_DEFAULT_WIDTH 160
#define ON_SCREEN_DEBUG_INFO_DEFAULT_HEIGHT 112

/***************************************************************************/

static const LPCSTR OnScreenDebugInfoLines[ON_SCREEN_DEBUG_INFO_LINE_COUNT] = {
    TEXT("Line 1"),
    TEXT("Line 2"),
    TEXT("Line 3"),
    TEXT("Line 4"),
    TEXT("Line 5")
};

/***************************************************************************/

/**
 * @brief Return the preferred initial size for the on-screen debug component.
 * @param SizeOut Receives the preferred size.
 * @return TRUE on success.
 */
BOOL OnScreenDebugInfoGetPreferredSize(LPPOINT SizeOut) {
    if (SizeOut == NULL) return FALSE;

    SizeOut->X = ON_SCREEN_DEBUG_INFO_DEFAULT_WIDTH;
    SizeOut->Y = ON_SCREEN_DEBUG_INFO_DEFAULT_HEIGHT;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw placeholder debug lines inside the component window.
 * @param Window Component window handle.
 * @param Message Message identifier.
 * @param Param1 First message parameter.
 * @param Param2 Second message parameter.
 * @return Message-specific result.
 */
U32 OnScreenDebugInfoWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    RECT ClientRect;
    HANDLE GraphicsContext = NULL;
    GFX_TEXT_MEASURE_INFO MeasureInfo;
    GFX_TEXT_DRAW_INFO DrawInfo;
    I32 LineY = 0;
    I32 LineHeight = ON_SCREEN_DEBUG_INFO_DEFAULT_LINE_HEIGHT;
    U32 Index = 0;

    switch (Message) {
        case EWM_DRAW:
            (void)BaseWindowFunc(Window, EWM_CLEAR, Param1, Param2);

            GraphicsContext = BeginWindowDraw(Window);
            if (GraphicsContext == NULL) {
                return 1;
            }

            if (GetWindowClientRect(Window, &ClientRect) == FALSE) {
                (void)EndWindowDraw(Window);
                return 1;
            }

            MeasureInfo = (GFX_TEXT_MEASURE_INFO){
                .Header = {.Size = sizeof(GFX_TEXT_MEASURE_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
                .Text = OnScreenDebugInfoLines[0],
                .Font = NULL,
                .Width = 0,
                .Height = 0
            };
            if (MeasureText(&MeasureInfo) != FALSE && MeasureInfo.Height != 0) {
                LineHeight = (I32)MeasureInfo.Height;
            }

            DrawInfo = (GFX_TEXT_DRAW_INFO){
                .Header = {.Size = sizeof(GFX_TEXT_DRAW_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
                .GC = GraphicsContext,
                .X = ClientRect.X1 + ON_SCREEN_DEBUG_INFO_PADDING_X,
                .Y = ClientRect.Y1 + ON_SCREEN_DEBUG_INFO_PADDING_Y,
                .Text = NULL,
                .Font = NULL
            };

            (void)SelectBrush(GraphicsContext, NULL);
            (void)SelectPen(GraphicsContext, GetSystemPen(SM_COLOR_TEXT_NORMAL));

            LineY = DrawInfo.Y;
            for (Index = 0; Index < ON_SCREEN_DEBUG_INFO_LINE_COUNT; Index++) {
                DrawInfo.Y = LineY;
                DrawInfo.Text = OnScreenDebugInfoLines[Index];
                (void)DrawText(&DrawInfo);
                LineY += LineHeight + ON_SCREEN_DEBUG_INFO_LINE_GAP;
            }

            (void)EndWindowDraw(Window);
            return 1;
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/***************************************************************************/
