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


    Desktop clock widget

\************************************************************************/

#include "Desktop-ClockWidget.h"
#include "Desktop-NonClient.h"

#include "Clock.h"
#include "GFX.h"
#include "Kernel.h"

/***************************************************************************/

#define CLOCK_WIDGET_TIMER_ID 1
#define CLOCK_WIDGET_TIMER_INTERVAL_MS 1000
#define CLOCK_UNIT_SCALE 1024
#define CLOCK_CIRCLE_MARGIN 10

/***************************************************************************/

static const I32 ClockDirection60[60][2] = {
    {0, -1024},      {107, -1018},   {213, -1002},   {316, -974},   {416, -935},
    {512, -887},     {602, -828},    {685, -761},    {761, -685},   {828, -602},
    {887, -512},     {935, -416},    {974, -316},    {1002, -213},  {1018, -107},
    {1024, 0},       {1018, 107},    {1002, 213},    {974, 316},    {935, 416},
    {887, 512},      {828, 602},     {761, 685},     {685, 761},    {602, 828},
    {512, 887},      {416, 935},     {316, 974},     {213, 1002},   {107, 1018},
    {0, 1024},       {-107, 1018},   {-213, 1002},   {-316, 974},   {-416, 935},
    {-512, 887},     {-602, 828},    {-685, 761},    {-761, 685},   {-828, 602},
    {-887, 512},     {-935, 416},    {-974, 316},    {-1002, 213},  {-1018, 107},
    {-1024, 0},      {-1018, -107},  {-1002, -213},  {-974, -316},  {-935, -416},
    {-887, -512},    {-828, -602},   {-761, -685},   {-685, -761},  {-602, -828},
    {-512, -887},    {-416, -935},   {-316, -974},   {-213, -1002}, {-107, -1018}
};

/***************************************************************************/

static BOOL DrawClockHandTriangle(HANDLE GC, I32 CenterX, I32 CenterY, U32 Slot60, I32 Length, I32 HalfWidth, I32 Tail, COLOR Color) {
    TRIANGLEINFO TriangleInfo;
    BRUSH Brush;
    I32 DirX;
    I32 DirY;
    I32 PerpX;
    I32 PerpY;
    I32 BaseX;
    I32 BaseY;
    HANDLE OldBrush;
    HANDLE OldPen;

    if (GC == NULL) return FALSE;
    Slot60 %= 60;

    DirX = ClockDirection60[Slot60][0];
    DirY = ClockDirection60[Slot60][1];
    PerpX = -DirY;
    PerpY = DirX;

    BaseX = CenterX - ((DirX * Tail) / CLOCK_UNIT_SCALE);
    BaseY = CenterY - ((DirY * Tail) / CLOCK_UNIT_SCALE);

    MemorySet(&Brush, 0, sizeof(Brush));
    Brush.TypeID = KOID_BRUSH;
    Brush.References = 1;
    Brush.Color = Color;
    Brush.Pattern = MAX_U32;

    TriangleInfo.Header.Size = sizeof(TriangleInfo);
    TriangleInfo.Header.Version = EXOS_ABI_VERSION;
    TriangleInfo.Header.Flags = 0;
    TriangleInfo.GC = GC;
    TriangleInfo.P1.X = BaseX + ((PerpX * HalfWidth) / CLOCK_UNIT_SCALE);
    TriangleInfo.P1.Y = BaseY + ((PerpY * HalfWidth) / CLOCK_UNIT_SCALE);
    TriangleInfo.P2.X = BaseX - ((PerpX * HalfWidth) / CLOCK_UNIT_SCALE);
    TriangleInfo.P2.Y = BaseY - ((PerpY * HalfWidth) / CLOCK_UNIT_SCALE);
    TriangleInfo.P3.X = CenterX + ((DirX * Length) / CLOCK_UNIT_SCALE);
    TriangleInfo.P3.Y = CenterY + ((DirY * Length) / CLOCK_UNIT_SCALE);

    OldBrush = SelectBrush(GC, (HANDLE)&Brush);
    OldPen = SelectPen(GC, NULL);
    (void)Triangle(&TriangleInfo);
    (void)SelectPen(GC, OldPen);
    (void)SelectBrush(GC, OldBrush);

    return TRUE;
}

/***************************************************************************/

U32 DesktopClockWidgetWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    RECT Rect;
    RECT ClientRect;
    HANDLE GC;
    ARCINFO ArcInfo;
    DATETIME LocalTime;
    I32 ClientWidth;
    I32 ClientHeight;
    I32 CenterX;
    I32 CenterY;
    I32 Radius;
    U32 HourSlot;
    U32 MinuteSlot;
    U32 SecondSlot;
    U32 Hour;

    switch (Message) {
        case EWM_CREATE:
            (void)SetWindowTimer(Window, CLOCK_WIDGET_TIMER_ID, CLOCK_WIDGET_TIMER_INTERVAL_MS);
            return 1;

        case EWM_DELETE:
            (void)KillWindowTimer(Window, CLOCK_WIDGET_TIMER_ID);
            break;

        case EWM_TIMER:
            if (Param1 == CLOCK_WIDGET_TIMER_ID) {
                (void)InvalidateWindowRect(Window, NULL);
                (void)RequestWindowDraw(Window);
            }
            return 1;

        case EWM_DRAW:
            (void)DefWindowFunc(Window, Message, Param1, Param2);

            GC = BeginWindowDraw(Window);
            if (GC == NULL) return 1;

            if (GetWindowRect(Window, &Rect) == FALSE) {
                EndWindowDraw(Window);
                return 1;
            }

            if (GetWindowClientRect((LPWINDOW)Window, &Rect, &ClientRect) == FALSE) {
                EndWindowDraw(Window);
                return 1;
            }

            ClientWidth = ClientRect.X2 - ClientRect.X1 + 1;
            ClientHeight = ClientRect.Y2 - ClientRect.Y1 + 1;
            if (ClientWidth <= 0 || ClientHeight <= 0) {
                EndWindowDraw(Window);
                return 1;
            }

            CenterX = ClientRect.X1 + (ClientWidth / 2);
            CenterY = ClientRect.Y1 + (ClientHeight / 2);
            Radius = ClientWidth < ClientHeight ? ClientWidth : ClientHeight;
            Radius = (Radius / 2) - CLOCK_CIRCLE_MARGIN;
            if (Radius < 8) Radius = 8;

            ArcInfo.Header.Size = sizeof(ArcInfo);
            ArcInfo.Header.Version = EXOS_ABI_VERSION;
            ArcInfo.Header.Flags = 0;
            ArcInfo.GC = GC;
            ArcInfo.CenterX = CenterX;
            ArcInfo.CenterY = CenterY;
            ArcInfo.StartAngle = 0;
            ArcInfo.EndAngle = 360;

            (void)SelectBrush(GC, NULL);
            (void)SelectPen(GC, GetSystemPen(SM_COLOR_TEXT_NORMAL));
            ArcInfo.Radius = Radius;
            (void)Arc(&ArcInfo);
            ArcInfo.Radius = Radius - 1;
            (void)Arc(&ArcInfo);

            GetLocalTime(&LocalTime);
            Hour = LocalTime.Hour % 12;
            HourSlot = (Hour * 5) + (LocalTime.Minute / 12);
            MinuteSlot = LocalTime.Minute % 60;
            SecondSlot = LocalTime.Second % 60;

            (void)DrawClockHandTriangle(GC, CenterX, CenterY, HourSlot, (Radius * 50) / 100, 4, 8, COLOR_BLACK);
            (void)DrawClockHandTriangle(GC, CenterX, CenterY, MinuteSlot, (Radius * 66) / 100, 3, 8, COLOR_BLACK);
            (void)DrawClockHandTriangle(GC, CenterX, CenterY, SecondSlot, (Radius * 78) / 100, 2, 10, COLOR_BLACK);

            EndWindowDraw(Window);
            return 1;
    }

    return DefWindowFunc(Window, Message, Param1, Param2);
}

/***************************************************************************/
