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


    Desktop internal test module

\************************************************************************/

#include "Desktop-InternalTest.h"
#include "Desktop-ClockWidget.h"
#include "Kernel.h"
#include "Log.h"

/***************************************************************************/
// Macros

#define DESKTOP_INTERNAL_TEST_WINDOW_ID_A 0x000085A1
#define DESKTOP_INTERNAL_TEST_WINDOW_ID_B 0x000085A2

/***************************************************************************/

/**
 * @brief Internal test window procedure.
 * @param Window Window handle.
 * @param Message Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return Message-specific result.
 */
static U32 DesktopInternalTestWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    LPWINDOW This;

    This = (LPWINDOW)Window;

    SAFE_USE_VALID_ID(This, KOID_WINDOW) {
        if (Message == EWM_CREATE) {
            DEBUG(TEXT("[DesktopInternalTestWindowFunc] EWM_CREATE id=%x style=%x status=%x"),
                This->WindowID,
                This->Style,
                This->Status);
        } else if (Message == EWM_DRAW) {
            DEBUG(TEXT("[DesktopInternalTestWindowFunc] EWM_DRAW id=%x style=%x status=%x"),
                This->WindowID,
                This->Style,
                This->Status);
        }
    }

    return DefWindowFunc(Window, Message, Param1, Param2);
}

/***************************************************************************/

/**
 * @brief Find one direct desktop child window by identifier.
 * @param Desktop Desktop whose root children are inspected.
 * @param WindowID Target window identifier.
 * @return Matching child window pointer or NULL.
 */
static LPWINDOW DesktopInternalFindTestWindow(LPDESKTOP Desktop, U32 WindowID) {
    LPWINDOW RootWindow;
    LPLISTNODE Node;
    LPWINDOW Candidate;
    LPWINDOW Found = NULL;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return NULL;

    RootWindow = Desktop->Window;
    if (RootWindow == NULL || RootWindow->TypeID != KOID_WINDOW) return NULL;

    LockMutex(&(RootWindow->Mutex), INFINITY);

    for (Node = RootWindow->Children != NULL ? RootWindow->Children->First : NULL; Node != NULL; Node = Node->Next) {
        Candidate = (LPWINDOW)Node;
        if (Candidate == NULL || Candidate->TypeID != KOID_WINDOW) continue;
        if (Candidate->WindowID != WindowID) continue;
        Found = Candidate;
        break;
    }

    UnlockMutex(&(RootWindow->Mutex));
    return Found;
}

/***************************************************************************/

/**
 * @brief Ensure one internal test window exists and is visible.
 * @param Desktop Target desktop.
 * @param WindowID Window identifier.
 * @param Title Internal title tag used for diagnostics.
 * @param X Left position in desktop coordinates.
 * @param Y Top position in desktop coordinates.
 * @param Width Width in pixels.
 * @param Height Height in pixels.
 * @return TRUE on success.
 */
static BOOL DesktopInternalEnsureSingleWindow(
    LPDESKTOP Desktop,
    U32 WindowID,
    LPCSTR Title,
    WINDOWFUNC WindowFunc,
    I32 X,
    I32 Y,
    I32 Width,
    I32 Height
) {
    LPWINDOW Window;
    WINDOWINFO WindowInfo;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    Window = DesktopInternalFindTestWindow(Desktop, WindowID);
    if (Window != NULL && Window->TypeID == KOID_WINDOW) {
        (void)ShowWindow((HANDLE)Window, TRUE);
        DEBUG(TEXT("[DesktopInternalEnsureSingleWindow] Existing test window visible title=%s id=%x"), Title, WindowID);
        return TRUE;
    }

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = (HANDLE)Desktop->Window;
    WindowInfo.Function = WindowFunc;
    WindowInfo.Style = EWS_VISIBLE | EWS_SYSTEM_DECORATED;
    WindowInfo.ID = WindowID;
    WindowInfo.WindowPosition.X = X;
    WindowInfo.WindowPosition.Y = Y;
    WindowInfo.WindowSize.X = Width;
    WindowInfo.WindowSize.Y = Height;
    WindowInfo.ShowHide = TRUE;

    Window = CreateWindow(&WindowInfo);
    if (Window == NULL) {
        WARNING(TEXT("[DesktopInternalEnsureSingleWindow] Test window creation failed title=%s id=%x"), Title, WindowID);
        return FALSE;
    }

    (void)ShowWindow((HANDLE)Window, TRUE);
    DEBUG(TEXT("[DesktopInternalEnsureSingleWindow] Test window created title=%s id=%x"), Title, WindowID);
    return TRUE;
}

/***************************************************************************/

BOOL DesktopInternalTestEnsureWindowsVisible(LPDESKTOP Desktop) {
    BOOL FirstCreated;
    BOOL SecondCreated;

    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    FirstCreated = DesktopInternalEnsureSingleWindow(
        Desktop,
        DESKTOP_INTERNAL_TEST_WINDOW_ID_A,
        TEXT("Kernel Test Alpha"),
        DesktopInternalTestWindowFunc,
        48,
        56,
        360,
        220);

    SecondCreated = DesktopInternalEnsureSingleWindow(
        Desktop,
        DESKTOP_INTERNAL_TEST_WINDOW_ID_B,
        TEXT("Kernel Clock Widget"),
        DesktopClockWidgetWindowFunc,
        460,
        140,
        500,
        320);

    return (FirstCreated && SecondCreated);
}

/***************************************************************************/
