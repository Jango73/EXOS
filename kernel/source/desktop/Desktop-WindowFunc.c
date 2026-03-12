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


    Desktop default and base window procedures

\************************************************************************/

#include "Desktop-Private.h"
#include "Desktop-NonClient.h"
#include "Desktop-ThemeResolver.h"
#include "Desktop-ThemeTokens.h"
#include "Kernel.h"
#include "Log.h"
#include "Desktop.h"
#include "input/Mouse.h"
#include "input/MouseDispatcher.h"
#include "utils/Graphics-Utils.h"

/***************************************************************************/

#define DESKTOP_WINDOW_FUNC_TRACE_SHELLBAR_WINDOW_ID 0x53484252

/***************************************************************************/

/**
 * @brief Default window procedure for unhandled messages.
 * @param Window Window handle.
 * @param Message Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return Message-specific result.
 */
static U32 DefaultWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    switch (Message) {
        case EWM_CREATE: {
        } break;

        case EWM_DELETE: {
        } break;

        case EWM_MOUSEDOWN: {
            LPWINDOW This = (LPWINDOW)Window;
            POINT MousePosition;
            I32 MouseX;
            I32 MouseY;
            RECT ScreenRect;

            if ((Param1 & MB_LEFT) == 0) break;
            if (This == NULL || This->TypeID != KOID_WINDOW) break;
            if (GetMouseScreenPosition(&MouseX, &MouseY) == FALSE) break;

            MousePosition.X = MouseX;
            MousePosition.Y = MouseY;

            if (IsPointInWindowTitleBar(This, &MousePosition) == FALSE) break;

            ScreenRect = This->ScreenRect;
            (void)BringWindowToFront(Window);
            (void)SetDesktopCaptureState(This, This, MousePosition.X - ScreenRect.X1, MousePosition.Y - ScreenRect.Y1);
        } break;

        case EWM_MOUSEMOVE: {
            LPWINDOW This = (LPWINDOW)Window;
            LPWINDOW CaptureWindow = NULL;
            RECT ParentScreenRect;
            POINT NewPosition;
            U32 Buttons;
            I32 OffsetX = 0;
            I32 OffsetY = 0;
            BOOL ParentHasRect = FALSE;

            if (This == NULL || This->TypeID != KOID_WINDOW) break;
            if (GetDesktopCaptureState(This, &CaptureWindow, &OffsetX, &OffsetY) == FALSE) break;
            if (CaptureWindow != This) break;

            Buttons = GetMouseDriver()->Command(DF_MOUSE_GETBUTTONS, 0);
            if ((Buttons & MB_LEFT) == 0) {
                (void)SetDesktopCaptureState(This, NULL, 0, 0);
                break;
            }

            NewPosition.X = SIGNED(Param1) - OffsetX;
            NewPosition.Y = SIGNED(Param2) - OffsetY;

            SAFE_USE_VALID_ID(This->ParentWindow, KOID_WINDOW) {
                ParentHasRect = GetWindowScreenRectSnapshot(This->ParentWindow, &ParentScreenRect);
            }

            if (ParentHasRect != FALSE) {
                NewPosition.X -= ParentScreenRect.X1;
                NewPosition.Y -= ParentScreenRect.Y1;
            }

            {
                RECT WindowRect;
                if (BuildWindowRectAtPosition(This, &NewPosition, &WindowRect) != FALSE) {
                    (void)MoveWindow(Window, &WindowRect);
                }
            }
        } break;

        case EWM_MOUSEUP: {
            LPWINDOW This = (LPWINDOW)Window;
            LPWINDOW CaptureWindow = NULL;

            if (This == NULL || This->TypeID != KOID_WINDOW) break;
            if (GetDesktopCaptureState(This, &CaptureWindow, NULL, NULL) == FALSE) break;
            if (CaptureWindow != This) break;

            (void)SetDesktopCaptureState(This, NULL, 0, 0);
        } break;

        case EWM_MOVE: {
            LPWINDOW This = (LPWINDOW)Window;
            POINT Position;
            RECT WindowRect;

            Position.X = SIGNED(Param1);
            Position.Y = SIGNED(Param2);

            if (BuildWindowRectAtPosition(This, &Position, &WindowRect) != FALSE &&
                DefaultSetWindowRect(This, &WindowRect)) {
                return 1;
            }
        } break;

        case EWM_DRAW: {
            return BaseWindowFunc(Window, EWM_CLEAR, Param1, Param2);
        }

        case EWM_CLEAR: {
            HANDLE GC;
            RECT SurfaceRect;
            RECT ClipScreenRect;
            RECT ClipLocalRect;
            RECT SurfaceScreenRect;
            LPWINDOW This = (LPWINDOW)Window;
            WINDOW_DRAW_CONTEXT_SNAPSHOT DrawContext;

            if (DesktopGetWindowDrawSurfaceRect(This, &SurfaceRect) == FALSE) {
                if (GetWindowDrawableRect((HANDLE)This, &SurfaceRect) == FALSE) break;
            } else if (GetWindowDrawContextSnapshot(This, &DrawContext) != FALSE &&
                       (DrawContext.Flags & WINDOW_DRAW_CONTEXT_ACTIVE) != 0 &&
                       DesktopGetWindowDrawClipRect(This, &ClipScreenRect) != FALSE) {
                SurfaceScreenRect.X1 = DrawContext.Origin.X + SurfaceRect.X1;
                SurfaceScreenRect.Y1 = DrawContext.Origin.Y + SurfaceRect.Y1;
                SurfaceScreenRect.X2 = DrawContext.Origin.X + SurfaceRect.X2;
                SurfaceScreenRect.Y2 = DrawContext.Origin.Y + SurfaceRect.Y2;
                GraphicsScreenRectToWindowRect(&SurfaceScreenRect, &ClipScreenRect, &ClipLocalRect);

                if (IntersectRect(&SurfaceRect, &ClipLocalRect, &SurfaceRect) == FALSE) {
                    return 1;
                }
            }

            if (This != NULL && This->TypeID == KOID_WINDOW && This->WindowID == DESKTOP_WINDOW_FUNC_TRACE_SHELLBAR_WINDOW_ID) {
            }

            GC = BeginWindowDraw(Window);
            if (GC == NULL) break;

            (void)DrawWindowBackground(Window, GC, &SurfaceRect, THEME_TOKEN_WINDOW_BACKGROUND_CLIENT);

            EndWindowDraw(Window);
            return 1;
        }
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Resolve one class pointer from one function in one inheritance chain.
 * @param WindowClass Root class.
 * @param Function Function to match.
 * @return Matched class or NULL.
 */
static LPWINDOW_CLASS ResolveClassByFunction(LPWINDOW_CLASS WindowClass, WINDOWFUNC Function) {
    LPWINDOW_CLASS This;

    if (WindowClass == NULL || Function == NULL) return NULL;

    for (This = WindowClass; This != NULL; This = This->BaseClass) {
        if (This->Function == Function) return This;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Call the base class window function for one dispatch context.
 * @param Window Window handle.
 * @param Message Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return Result from base class callback or default behavior.
 */
U32 BaseWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    LPWINDOW This = (LPWINDOW)Window;
    LPTASK Task = GetCurrentTask();
    LPWINDOW_CLASS CurrentClass = NULL;
    LPWINDOW_CLASS BaseClass = NULL;
    LPVOID PreviousWindow = NULL;
    LPVOID PreviousClass = NULL;
    WINDOWFUNC PreviousFunction = NULL;
    U32 Result;

    SAFE_USE_VALID_ID(This, KOID_WINDOW) {
        SAFE_USE_VALID_ID(Task, KOID_TASK) {
            if (Task->WindowDispatchDepth > 0 && Task->WindowDispatchWindow == This) {
                CurrentClass = (LPWINDOW_CLASS)Task->WindowDispatchClass;

                if (CurrentClass == NULL || CurrentClass->TypeID != KOID_WINDOW_CLASS) {
                    CurrentClass = ResolveClassByFunction(This->Class, Task->WindowDispatchFunction);
                }

                SAFE_USE_VALID_ID(CurrentClass, KOID_WINDOW_CLASS) { BaseClass = CurrentClass->BaseClass; }
            }

            if (BaseClass != NULL && BaseClass->TypeID == KOID_WINDOW_CLASS && BaseClass->Function != NULL) {
                PreviousWindow = Task->WindowDispatchWindow;
                PreviousClass = Task->WindowDispatchClass;
                PreviousFunction = Task->WindowDispatchFunction;

                Task->WindowDispatchWindow = This;
                Task->WindowDispatchClass = BaseClass;
                Task->WindowDispatchFunction = BaseClass->Function;
                Task->WindowDispatchDepth++;

                Result = BaseClass->Function(Window, Message, Param1, Param2);

                Task->WindowDispatchWindow = PreviousWindow;
                Task->WindowDispatchClass = PreviousClass;
                Task->WindowDispatchFunction = PreviousFunction;
                if (Task->WindowDispatchDepth > 0) Task->WindowDispatchDepth--;

                return Result;
            }
        }
    }

    return DefaultWindowFunc(Window, Message, Param1, Param2);
}

/***************************************************************************/
