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


    Desktop button component

\************************************************************************/

#include "ui/Button.h"

#include "CoreString.h"

/***************************************************************************/
// Macros

#define DESKTOP_BUTTON_PROP_HOVER TEXT("ui.button.hover")
#define DESKTOP_BUTTON_PROP_PRESSED TEXT("ui.button.pressed")
#define DESKTOP_BUTTON_CAPTION_BUFFER_SIZE 128

/***************************************************************************/

/**
 * @brief Resolve whether one local point is inside one window rectangle.
 * @param Window Target window handle.
 * @param WindowX Window-relative X coordinate.
 * @param WindowY Window-relative Y coordinate.
 * @return TRUE when the point is inside the window rectangle.
 */
static BOOL ButtonIsPointInside(HANDLE Window, I32 WindowX, I32 WindowY) {
    RECT WindowRect;

    if (Window == NULL) return FALSE;
    if (GetWindowRect(Window, &WindowRect) == FALSE) return FALSE;

    return WindowX >= WindowRect.X1 && WindowX <= WindowRect.X2 && WindowY >= WindowRect.Y1 && WindowY <= WindowRect.Y2;
}

/***************************************************************************/

/**
 * @brief Resolve the themed background token for one button state.
 * @param Window Target button window.
 * @return One THEME_TOKEN_WINDOW_BACKGROUND_* identifier.
 */
static U32 ButtonResolveBackgroundToken(HANDLE Window) {
    if (GetWindowProp(Window, DESKTOP_BUTTON_PROP_DISABLED) != 0) return THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_DISABLED;
    if (GetWindowProp(Window, DESKTOP_BUTTON_PROP_PRESSED) != 0) return THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_PRESSED;
    if (GetWindowProp(Window, DESKTOP_BUTTON_PROP_HOVER) != 0) return THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_HOVER;

    return THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_NORMAL;
}

/***************************************************************************/

/**
 * @brief Update one button state property and request redraw.
 * @param Window Target button window.
 * @param Name Property name.
 * @param Value New property value.
 */
static void ButtonSetStateProp(HANDLE Window, LPCSTR Name, U32 Value) {
    if (Window == NULL || Name == NULL) return;
    if (GetWindowProp(Window, Name) == Value) return;

    (void)SetWindowProp(Window, Name, Value);
    (void)InvalidateWindowRect(Window, NULL);
}

/***************************************************************************/

/**
 * @brief Draw centered button caption.
 * @param Window Button window handle.
 * @param GC Target graphics context.
 * @param ClientRect Button client rectangle.
 */
static void ButtonDrawCaption(HANDLE Window, HANDLE GC, LPRECT ClientRect) {
    TEXT_MEASURE_INFO MeasureInfo;
    TEXT_DRAW_INFO DrawInfo;
    STR Caption[DESKTOP_BUTTON_CAPTION_BUFFER_SIZE];
    U32 ThemeToken;
    I32 TextX;
    I32 TextY;

    if (Window == NULL || GC == NULL || ClientRect == NULL) return;
    if (GetWindowCaption(Window, Caption, sizeof(Caption)) == FALSE) return;
    if (Caption[0] == STR_NULL) return;

    MeasureInfo = (TEXT_MEASURE_INFO){
        .Header = {.Size = sizeof(TEXT_MEASURE_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .Text = Caption,
        .Font = NULL,
        .Width = 0,
        .Height = 0};
    (void)MeasureText(&MeasureInfo);

    TextX = ClientRect->X1 + (((ClientRect->X2 - ClientRect->X1 + 1) - (I32)MeasureInfo.Width) / 2);
    TextY = ClientRect->Y1 + (((ClientRect->Y2 - ClientRect->Y1 + 1) - (I32)MeasureInfo.Height) / 2);

    ThemeToken = ButtonResolveBackgroundToken(Window);
    if (ThemeToken == THEME_TOKEN_WINDOW_BACKGROUND_BUTTON_DISABLED) {
        (void)SelectPen(GC, GetSystemPen(SM_COLOR_NORMAL));
    } else {
        (void)SelectPen(GC, GetSystemPen(SM_COLOR_TEXT_NORMAL));
    }
    (void)SelectBrush(GC, NULL);

    DrawInfo = (TEXT_DRAW_INFO){
        .Header = {.Size = sizeof(TEXT_DRAW_INFO), .Version = EXOS_ABI_VERSION, .Flags = 0},
        .GC = GC,
        .X = TextX,
        .Y = TextY,
        .Text = Caption,
        .Font = NULL};
    (void)DrawText(&DrawInfo);
}

/***************************************************************************/

/**
 * @brief Dispatch one button click notify to the parent window.
 * @param Window Source button window.
 */
static void ButtonNotifyClicked(HANDLE Window) {
    HANDLE ParentWindow;

    if (Window == NULL) return;

    ParentWindow = GetWindowParent(Window);
    if (ParentWindow == NULL) return;

    (void)PostMessage(ParentWindow, EWM_NOTIFY, EWN_UI_BUTTON_CLICKED, GetWindowProp(Window, DESKTOP_BUTTON_PROP_NOTIFY_VALUE));
}

/***************************************************************************/

/**
 * @brief Ensure the button window class is registered.
 * @return TRUE on success.
 */
BOOL ButtonEnsureClassRegistered(void) {
    if (FindWindowClass(DESKTOP_BUTTON_WINDOW_CLASS_NAME) != NULL) return TRUE;
    return RegisterWindowClass(DESKTOP_BUTTON_WINDOW_CLASS_NAME, 0, NULL, ButtonWindowFunc, 0) != NULL;
}

/***************************************************************************/

/**
 * @brief Create one button child window.
 * @param ParentWindow Parent window handle.
 * @param WindowID Button window identifier.
 * @param WindowRect Initial window rectangle.
 * @param Caption Optional button caption.
 * @return Button window handle on success, NULL on failure.
 */
HANDLE ButtonCreate(HANDLE ParentWindow, U32 WindowID, LPRECT WindowRect, LPCSTR Caption) {
    WINDOWINFO WindowInfo;
    HANDLE Window;

    if (ParentWindow == NULL || WindowRect == NULL) return NULL;
    if (ButtonEnsureClassRegistered() == FALSE) return NULL;

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = ParentWindow;
    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = DESKTOP_BUTTON_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_VISIBLE | EWS_CLIENT_DECORATED;
    WindowInfo.ID = WindowID;
    WindowInfo.WindowPosition.X = WindowRect->X1;
    WindowInfo.WindowPosition.Y = WindowRect->Y1;
    WindowInfo.WindowSize.X = WindowRect->X2 - WindowRect->X1 + 1;
    WindowInfo.WindowSize.Y = WindowRect->Y2 - WindowRect->Y1 + 1;
    WindowInfo.ShowHide = TRUE;

    Window = (HANDLE)CreateWindow(&WindowInfo);
    if (Window == NULL) return NULL;

    (void)SetWindowProp(Window, DESKTOP_BUTTON_PROP_NOTIFY_VALUE, WindowID);
    if (Caption != NULL) {
        (void)SetWindowCaption(Window, Caption);
    }

    return Window;
}

/***************************************************************************/

/**
 * @brief Button window procedure.
 * @param Window Button window handle.
 * @param Message Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return Message-specific result.
 */
U32 ButtonWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    RECT ClientRect;
    HANDLE GraphicsContext;
    POINT MousePosition;
    POINT WindowPoint;
    I32 MouseX;
    I32 MouseY;
    BOOL IsInside;
    BOOL WasPressed;

    switch (Message) {
        case EWM_CREATE:
            (void)SetWindowProp(Window, DESKTOP_BUTTON_PROP_HOVER, 0);
            (void)SetWindowProp(Window, DESKTOP_BUTTON_PROP_PRESSED, 0);
            return 1;

        case EWM_DELETE:
            (void)ReleaseMouse();
            return 1;

        case EWM_MOUSEDOWN:
            if ((Param1 & MB_LEFT) == 0) return 1;
            if (GetWindowProp(Window, DESKTOP_BUTTON_PROP_DISABLED) != 0) return 1;

            if (GetMousePosition(&MousePosition) == FALSE) return 1;
            if (ScreenPointToWindowPoint(Window, &MousePosition, &WindowPoint) == FALSE) return 1;
            MouseX = WindowPoint.X;
            MouseY = WindowPoint.Y;
            if (ButtonIsPointInside(Window, MouseX, MouseY) == FALSE) return 1;
            (void)CaptureMouse(Window);
            ButtonSetStateProp(Window, DESKTOP_BUTTON_PROP_HOVER, 1);
            ButtonSetStateProp(Window, DESKTOP_BUTTON_PROP_PRESSED, 1);
            return 1;

        case EWM_MOUSEMOVE:
            MouseX = SIGNED(Param1);
            MouseY = SIGNED(Param2);
            IsInside = ButtonIsPointInside(Window, MouseX, MouseY);

            if (GetWindowProp(Window, DESKTOP_BUTTON_PROP_PRESSED) != 0) {
                ButtonSetStateProp(Window, DESKTOP_BUTTON_PROP_HOVER, IsInside ? 1 : 0);
                ButtonSetStateProp(Window, DESKTOP_BUTTON_PROP_PRESSED, IsInside ? 1 : 0);
            } else if (GetWindowProp(Window, DESKTOP_BUTTON_PROP_DISABLED) == 0) {
                ButtonSetStateProp(Window, DESKTOP_BUTTON_PROP_HOVER, IsInside ? 1 : 0);
            }
            return 1;

        case EWM_MOUSEUP:
            if ((Param1 & MB_LEFT) == 0) return 1;

            if (GetMousePosition(&MousePosition) == FALSE) return 1;
            if (ScreenPointToWindowPoint(Window, &MousePosition, &WindowPoint) == FALSE) return 1;
            MouseX = WindowPoint.X;
            MouseY = WindowPoint.Y;
            IsInside = ButtonIsPointInside(Window, MouseX, MouseY);
            WasPressed = GetWindowProp(Window, DESKTOP_BUTTON_PROP_PRESSED) != 0;

            (void)ReleaseMouse();
            ButtonSetStateProp(Window, DESKTOP_BUTTON_PROP_PRESSED, 0);
            ButtonSetStateProp(Window, DESKTOP_BUTTON_PROP_HOVER, IsInside ? 1 : 0);

            if (WasPressed != FALSE && IsInside != FALSE && GetWindowProp(Window, DESKTOP_BUTTON_PROP_DISABLED) == 0) {
                ButtonNotifyClicked(Window);
            }
            return 1;

        case EWM_DRAW:
            (void)BaseWindowFunc(Window, EWM_CLEAR, ButtonResolveBackgroundToken(Window), Param2);

            GraphicsContext = BeginWindowDraw(Window);
            if (GraphicsContext == NULL) return 1;
            if (GetWindowClientRect(Window, &ClientRect) == FALSE) {
                (void)EndWindowDraw(Window);
                return 1;
            }

            ButtonDrawCaption(Window, GraphicsContext, &ClientRect);
            (void)EndWindowDraw(Window);
            return 1;
    }

    return BaseWindowFunc(Window, Message, Param1, Param2);
}

/***************************************************************************/
