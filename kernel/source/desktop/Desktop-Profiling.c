/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2026 Jango73

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


    Desktop profiling helpers

\************************************************************************/

#include "Desktop-Private.h"
#include "log/Profile.h"
#include "text/CoreString.h"
#include "ui/Button.h"
#include "ui/ClockWidget.h"
#include "ui/Cube3D.h"
#include "ui/LogViewer.h"
#include "ui/ShellBar.h"

/************************************************************************/

#define DESKTOP_PROFILE_ROOT_WINDOW_CLASS_NAME TEXT("RootWindowClass")
#define DESKTOP_PROFILE_SHELL_BAR_SLOT_WINDOW_CLASS_NAME TEXT("ShellBarSlotWindowClass")
#define DESKTOP_PROFILE_SHELL_BAR_WINDOW_ID 0x53484252
#define DESKTOP_PROFILE_INTERNAL_TEST_WINDOW_ID 0x000085A1
#define DESKTOP_PROFILE_CLASS_ROOT 1
#define DESKTOP_PROFILE_CLASS_SHELL_BAR 2
#define DESKTOP_PROFILE_CLASS_SHELL_BAR_SLOT 3
#define DESKTOP_PROFILE_CLASS_BUTTON 4
#define DESKTOP_PROFILE_CLASS_CLOCK_WIDGET 5
#define DESKTOP_PROFILE_CLASS_CUBE3D 6
#define DESKTOP_PROFILE_CLASS_LOG_VIEWER 7
#define DESKTOP_PROFILE_CLASS_DOCK_HOST 8
#define DESKTOP_PROFILE_CLASS_DOCKABLE 9
#define DESKTOP_PROFILE_CLASS_OTHER 10

/************************************************************************/

/**
 * @brief Tell whether one class name matches a window class.
 * @param WindowClass Window class to inspect.
 * @param ClassName Expected class name.
 * @return TRUE when the class name matches.
 */
static BOOL DesktopProfileClassNameMatches(LPWINDOW_CLASS WindowClass, LPCSTR ClassName) {
    if (ClassName == NULL) return FALSE;

    SAFE_USE_VALID_ID(WindowClass, KOID_WINDOW_CLASS) {
        return (StringCompareNC(WindowClass->Name, ClassName) == 0);
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Resolve one profile class category.
 * @param Window Target window.
 * @return Profile class category.
 */
static U32 DesktopProfileResolveWindowClassCategory(LPWINDOW Window) {
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return DESKTOP_PROFILE_CLASS_OTHER;

    if (DesktopProfileClassNameMatches(Snapshot.Class, DESKTOP_PROFILE_ROOT_WINDOW_CLASS_NAME) != FALSE) {
        return DESKTOP_PROFILE_CLASS_ROOT;
    }
    if (DesktopProfileClassNameMatches(Snapshot.Class, SHELL_BAR_WINDOW_CLASS_NAME) != FALSE) {
        return DESKTOP_PROFILE_CLASS_SHELL_BAR;
    }
    if (DesktopProfileClassNameMatches(Snapshot.Class, DESKTOP_PROFILE_SHELL_BAR_SLOT_WINDOW_CLASS_NAME) != FALSE) {
        return DESKTOP_PROFILE_CLASS_SHELL_BAR_SLOT;
    }
    if (DesktopProfileClassNameMatches(Snapshot.Class, DESKTOP_BUTTON_WINDOW_CLASS_NAME) != FALSE) {
        return DESKTOP_PROFILE_CLASS_BUTTON;
    }
    if (DesktopProfileClassNameMatches(Snapshot.Class, DESKTOP_CLOCK_WIDGET_WINDOW_CLASS_NAME) != FALSE) {
        return DESKTOP_PROFILE_CLASS_CLOCK_WIDGET;
    }
    if (DesktopProfileClassNameMatches(Snapshot.Class, DESKTOP_CUBE3D_WINDOW_CLASS_NAME) != FALSE) {
        return DESKTOP_PROFILE_CLASS_CUBE3D;
    }
    if (DesktopProfileClassNameMatches(Snapshot.Class, DESKTOP_LOG_VIEWER_WINDOW_CLASS_NAME) != FALSE) {
        return DESKTOP_PROFILE_CLASS_LOG_VIEWER;
    }
    if (DesktopProfileClassNameMatches(Snapshot.Class, WINDOW_DOCK_HOST_CLASS_NAME) != FALSE) {
        return DESKTOP_PROFILE_CLASS_DOCK_HOST;
    }
    if (DesktopProfileClassNameMatches(Snapshot.Class, WINDOW_DOCKABLE_CLASS_NAME) != FALSE) {
        return DESKTOP_PROFILE_CLASS_DOCKABLE;
    }

    return DESKTOP_PROFILE_CLASS_OTHER;
}

/************************************************************************/

/**
 * @brief Resolve a client draw profile counter for one window class.
 * @param ClassCategory Window class category.
 * @return Static profile counter name.
 */
static LPCSTR DesktopProfileResolveClientDrawCounter(U32 ClassCategory) {
    switch (ClassCategory) {
        case DESKTOP_PROFILE_CLASS_ROOT:
            return TEXT("Desktop.ClientDrawCallback.Class.Root");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR:
            return TEXT("Desktop.ClientDrawCallback.Class.ShellBar");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR_SLOT:
            return TEXT("Desktop.ClientDrawCallback.Class.ShellBarSlot");
        case DESKTOP_PROFILE_CLASS_BUTTON:
            return TEXT("Desktop.ClientDrawCallback.Class.Button");
        case DESKTOP_PROFILE_CLASS_CLOCK_WIDGET:
            return TEXT("Desktop.ClientDrawCallback.Class.ClockWidget");
        case DESKTOP_PROFILE_CLASS_CUBE3D:
            return TEXT("Desktop.ClientDrawCallback.Class.Cube3D");
        case DESKTOP_PROFILE_CLASS_LOG_VIEWER:
            return TEXT("Desktop.ClientDrawCallback.Class.LogViewer");
        case DESKTOP_PROFILE_CLASS_DOCK_HOST:
            return TEXT("Desktop.ClientDrawCallback.Class.DockHost");
        case DESKTOP_PROFILE_CLASS_DOCKABLE:
            return TEXT("Desktop.ClientDrawCallback.Class.Dockable");
    }

    return TEXT("Desktop.ClientDrawCallback.Class.Other");
}

/************************************************************************/

/**
 * @brief Resolve a draw dispatch profile counter for one window class.
 * @param ClassCategory Window class category.
 * @return Static profile counter name.
 */
static LPCSTR DesktopProfileResolveDrawDispatchCounter(U32 ClassCategory) {
    switch (ClassCategory) {
        case DESKTOP_PROFILE_CLASS_ROOT:
            return TEXT("Desktop.DrawDispatch.Class.Root");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR:
            return TEXT("Desktop.DrawDispatch.Class.ShellBar");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR_SLOT:
            return TEXT("Desktop.DrawDispatch.Class.ShellBarSlot");
        case DESKTOP_PROFILE_CLASS_BUTTON:
            return TEXT("Desktop.DrawDispatch.Class.Button");
        case DESKTOP_PROFILE_CLASS_CLOCK_WIDGET:
            return TEXT("Desktop.DrawDispatch.Class.ClockWidget");
        case DESKTOP_PROFILE_CLASS_CUBE3D:
            return TEXT("Desktop.DrawDispatch.Class.Cube3D");
        case DESKTOP_PROFILE_CLASS_LOG_VIEWER:
            return TEXT("Desktop.DrawDispatch.Class.LogViewer");
        case DESKTOP_PROFILE_CLASS_DOCK_HOST:
            return TEXT("Desktop.DrawDispatch.Class.DockHost");
        case DESKTOP_PROFILE_CLASS_DOCKABLE:
            return TEXT("Desktop.DrawDispatch.Class.Dockable");
    }

    return TEXT("Desktop.DrawDispatch.Class.Other");
}

/************************************************************************/

/**
 * @brief Resolve a draw queue wait profile counter for one window class.
 * @param ClassCategory Window class category.
 * @return Static profile counter name.
 */
static LPCSTR DesktopProfileResolveDrawRequestCounter(U32 ClassCategory) {
    switch (ClassCategory) {
        case DESKTOP_PROFILE_CLASS_ROOT:
            return TEXT("Desktop.DrawRequestToDispatch.Class.Root");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR:
            return TEXT("Desktop.DrawRequestToDispatch.Class.ShellBar");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR_SLOT:
            return TEXT("Desktop.DrawRequestToDispatch.Class.ShellBarSlot");
        case DESKTOP_PROFILE_CLASS_BUTTON:
            return TEXT("Desktop.DrawRequestToDispatch.Class.Button");
        case DESKTOP_PROFILE_CLASS_CLOCK_WIDGET:
            return TEXT("Desktop.DrawRequestToDispatch.Class.ClockWidget");
        case DESKTOP_PROFILE_CLASS_CUBE3D:
            return TEXT("Desktop.DrawRequestToDispatch.Class.Cube3D");
        case DESKTOP_PROFILE_CLASS_LOG_VIEWER:
            return TEXT("Desktop.DrawRequestToDispatch.Class.LogViewer");
        case DESKTOP_PROFILE_CLASS_DOCK_HOST:
            return TEXT("Desktop.DrawRequestToDispatch.Class.DockHost");
        case DESKTOP_PROFILE_CLASS_DOCKABLE:
            return TEXT("Desktop.DrawRequestToDispatch.Class.Dockable");
    }

    return TEXT("Desktop.DrawRequestToDispatch.Class.Other");
}

/************************************************************************/

/**
 * @brief Resolve a coalesced draw request age profile counter for one window class.
 * @param ClassCategory Window class category.
 * @return Static profile counter name.
 */
static LPCSTR DesktopProfileResolveDrawRequestCoalescedAgeCounter(U32 ClassCategory) {
    switch (ClassCategory) {
        case DESKTOP_PROFILE_CLASS_ROOT:
            return TEXT("Desktop.DrawRequestCoalescedAge.Class.Root");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR:
            return TEXT("Desktop.DrawRequestCoalescedAge.Class.ShellBar");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR_SLOT:
            return TEXT("Desktop.DrawRequestCoalescedAge.Class.ShellBarSlot");
        case DESKTOP_PROFILE_CLASS_BUTTON:
            return TEXT("Desktop.DrawRequestCoalescedAge.Class.Button");
        case DESKTOP_PROFILE_CLASS_CLOCK_WIDGET:
            return TEXT("Desktop.DrawRequestCoalescedAge.Class.ClockWidget");
        case DESKTOP_PROFILE_CLASS_CUBE3D:
            return TEXT("Desktop.DrawRequestCoalescedAge.Class.Cube3D");
        case DESKTOP_PROFILE_CLASS_LOG_VIEWER:
            return TEXT("Desktop.DrawRequestCoalescedAge.Class.LogViewer");
        case DESKTOP_PROFILE_CLASS_DOCK_HOST:
            return TEXT("Desktop.DrawRequestCoalescedAge.Class.DockHost");
        case DESKTOP_PROFILE_CLASS_DOCKABLE:
            return TEXT("Desktop.DrawRequestCoalescedAge.Class.Dockable");
    }

    return TEXT("Desktop.DrawRequestCoalescedAge.Class.Other");
}

/************************************************************************/

/**
 * @brief Resolve a stale draw request profile counter for one window class.
 * @param ClassCategory Window class category.
 * @return Static profile counter name.
 */
static LPCSTR DesktopProfileResolveDrawRequestAfterCursorMoveCounter(U32 ClassCategory) {
    switch (ClassCategory) {
        case DESKTOP_PROFILE_CLASS_ROOT:
            return TEXT("Desktop.DrawRequestAfterCursorMove.Class.Root");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR:
            return TEXT("Desktop.DrawRequestAfterCursorMove.Class.ShellBar");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR_SLOT:
            return TEXT("Desktop.DrawRequestAfterCursorMove.Class.ShellBarSlot");
        case DESKTOP_PROFILE_CLASS_BUTTON:
            return TEXT("Desktop.DrawRequestAfterCursorMove.Class.Button");
        case DESKTOP_PROFILE_CLASS_CLOCK_WIDGET:
            return TEXT("Desktop.DrawRequestAfterCursorMove.Class.ClockWidget");
        case DESKTOP_PROFILE_CLASS_CUBE3D:
            return TEXT("Desktop.DrawRequestAfterCursorMove.Class.Cube3D");
        case DESKTOP_PROFILE_CLASS_LOG_VIEWER:
            return TEXT("Desktop.DrawRequestAfterCursorMove.Class.LogViewer");
        case DESKTOP_PROFILE_CLASS_DOCK_HOST:
            return TEXT("Desktop.DrawRequestAfterCursorMove.Class.DockHost");
        case DESKTOP_PROFILE_CLASS_DOCKABLE:
            return TEXT("Desktop.DrawRequestAfterCursorMove.Class.Dockable");
    }

    return TEXT("Desktop.DrawRequestAfterCursorMove.Class.Other");
}

/************************************************************************/

/**
 * @brief Resolve a posted draw request profile counter for one window class.
 * @param ClassCategory Window class category.
 * @return Static profile counter name.
 */
static LPCSTR DesktopProfileResolveDrawRequestPostedCounter(U32 ClassCategory) {
    switch (ClassCategory) {
        case DESKTOP_PROFILE_CLASS_ROOT:
            return TEXT("Desktop.DrawRequestPosted.Class.Root");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR:
            return TEXT("Desktop.DrawRequestPosted.Class.ShellBar");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR_SLOT:
            return TEXT("Desktop.DrawRequestPosted.Class.ShellBarSlot");
        case DESKTOP_PROFILE_CLASS_BUTTON:
            return TEXT("Desktop.DrawRequestPosted.Class.Button");
        case DESKTOP_PROFILE_CLASS_CLOCK_WIDGET:
            return TEXT("Desktop.DrawRequestPosted.Class.ClockWidget");
        case DESKTOP_PROFILE_CLASS_CUBE3D:
            return TEXT("Desktop.DrawRequestPosted.Class.Cube3D");
        case DESKTOP_PROFILE_CLASS_LOG_VIEWER:
            return TEXT("Desktop.DrawRequestPosted.Class.LogViewer");
        case DESKTOP_PROFILE_CLASS_DOCK_HOST:
            return TEXT("Desktop.DrawRequestPosted.Class.DockHost");
        case DESKTOP_PROFILE_CLASS_DOCKABLE:
            return TEXT("Desktop.DrawRequestPosted.Class.Dockable");
    }

    return TEXT("Desktop.DrawRequestPosted.Class.Other");
}

/************************************************************************/

/**
 * @brief Resolve a coalesced draw request profile counter for one window class.
 * @param ClassCategory Window class category.
 * @return Static profile counter name.
 */
static LPCSTR DesktopProfileResolveDrawRequestCoalescedCounter(U32 ClassCategory) {
    switch (ClassCategory) {
        case DESKTOP_PROFILE_CLASS_ROOT:
            return TEXT("Desktop.DrawRequestCoalesced.Class.Root");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR:
            return TEXT("Desktop.DrawRequestCoalesced.Class.ShellBar");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR_SLOT:
            return TEXT("Desktop.DrawRequestCoalesced.Class.ShellBarSlot");
        case DESKTOP_PROFILE_CLASS_BUTTON:
            return TEXT("Desktop.DrawRequestCoalesced.Class.Button");
        case DESKTOP_PROFILE_CLASS_CLOCK_WIDGET:
            return TEXT("Desktop.DrawRequestCoalesced.Class.ClockWidget");
        case DESKTOP_PROFILE_CLASS_CUBE3D:
            return TEXT("Desktop.DrawRequestCoalesced.Class.Cube3D");
        case DESKTOP_PROFILE_CLASS_LOG_VIEWER:
            return TEXT("Desktop.DrawRequestCoalesced.Class.LogViewer");
        case DESKTOP_PROFILE_CLASS_DOCK_HOST:
            return TEXT("Desktop.DrawRequestCoalesced.Class.DockHost");
        case DESKTOP_PROFILE_CLASS_DOCKABLE:
            return TEXT("Desktop.DrawRequestCoalesced.Class.Dockable");
    }

    return TEXT("Desktop.DrawRequestCoalesced.Class.Other");
}

/************************************************************************/

/**
 * @brief Resolve a stale draw request event profile counter for one window class.
 * @param ClassCategory Window class category.
 * @return Static profile counter name.
 */
static LPCSTR DesktopProfileResolveDrawRequestStaleAfterCursorMoveCounter(U32 ClassCategory) {
    switch (ClassCategory) {
        case DESKTOP_PROFILE_CLASS_ROOT:
            return TEXT("Desktop.DrawRequestStaleAfterCursorMove.Class.Root");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR:
            return TEXT("Desktop.DrawRequestStaleAfterCursorMove.Class.ShellBar");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR_SLOT:
            return TEXT("Desktop.DrawRequestStaleAfterCursorMove.Class.ShellBarSlot");
        case DESKTOP_PROFILE_CLASS_BUTTON:
            return TEXT("Desktop.DrawRequestStaleAfterCursorMove.Class.Button");
        case DESKTOP_PROFILE_CLASS_CLOCK_WIDGET:
            return TEXT("Desktop.DrawRequestStaleAfterCursorMove.Class.ClockWidget");
        case DESKTOP_PROFILE_CLASS_CUBE3D:
            return TEXT("Desktop.DrawRequestStaleAfterCursorMove.Class.Cube3D");
        case DESKTOP_PROFILE_CLASS_LOG_VIEWER:
            return TEXT("Desktop.DrawRequestStaleAfterCursorMove.Class.LogViewer");
        case DESKTOP_PROFILE_CLASS_DOCK_HOST:
            return TEXT("Desktop.DrawRequestStaleAfterCursorMove.Class.DockHost");
        case DESKTOP_PROFILE_CLASS_DOCKABLE:
            return TEXT("Desktop.DrawRequestStaleAfterCursorMove.Class.Dockable");
    }

    return TEXT("Desktop.DrawRequestStaleAfterCursorMove.Class.Other");
}

/************************************************************************/

/**
 * @brief Resolve a current-cursor draw request event profile counter for one window class.
 * @param ClassCategory Window class category.
 * @return Static profile counter name.
 */
static LPCSTR DesktopProfileResolveDrawRequestCurrentCursorMoveCounter(U32 ClassCategory) {
    switch (ClassCategory) {
        case DESKTOP_PROFILE_CLASS_ROOT:
            return TEXT("Desktop.DrawRequestCurrentCursorMove.Class.Root");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR:
            return TEXT("Desktop.DrawRequestCurrentCursorMove.Class.ShellBar");
        case DESKTOP_PROFILE_CLASS_SHELL_BAR_SLOT:
            return TEXT("Desktop.DrawRequestCurrentCursorMove.Class.ShellBarSlot");
        case DESKTOP_PROFILE_CLASS_BUTTON:
            return TEXT("Desktop.DrawRequestCurrentCursorMove.Class.Button");
        case DESKTOP_PROFILE_CLASS_CLOCK_WIDGET:
            return TEXT("Desktop.DrawRequestCurrentCursorMove.Class.ClockWidget");
        case DESKTOP_PROFILE_CLASS_CUBE3D:
            return TEXT("Desktop.DrawRequestCurrentCursorMove.Class.Cube3D");
        case DESKTOP_PROFILE_CLASS_LOG_VIEWER:
            return TEXT("Desktop.DrawRequestCurrentCursorMove.Class.LogViewer");
        case DESKTOP_PROFILE_CLASS_DOCK_HOST:
            return TEXT("Desktop.DrawRequestCurrentCursorMove.Class.DockHost");
        case DESKTOP_PROFILE_CLASS_DOCKABLE:
            return TEXT("Desktop.DrawRequestCurrentCursorMove.Class.Dockable");
    }

    return TEXT("Desktop.DrawRequestCurrentCursorMove.Class.Other");
}

/************************************************************************/

/**
 * @brief Resolve a draw queue wait profile counter for one stable window id.
 * @param Window Target window.
 * @return Static profile counter name.
 */
static LPCSTR DesktopProfileResolveDrawRequestWindowCounter(LPWINDOW Window) {
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) {
        return TEXT("Desktop.DrawRequestToDispatch.Window.Invalid");
    }

    if (Snapshot.WindowID == DESKTOP_PROFILE_SHELL_BAR_WINDOW_ID) {
        return TEXT("Desktop.DrawRequestToDispatch.Window.ShellBar");
    }
    if (Snapshot.WindowID == DESKTOP_PROFILE_INTERNAL_TEST_WINDOW_ID) {
        return TEXT("Desktop.DrawRequestToDispatch.Window.InternalTest");
    }

    return TEXT("Desktop.DrawRequestToDispatch.Window.Other");
}

/************************************************************************/

/**
 * @brief Record one desktop duration on a window-specific counter.
 * @param Window Target window.
 * @param Counter Counter family identifier.
 * @param DurationMicros Duration in microseconds.
 */
void DesktopProfileRecordWindowDuration(LPWINDOW Window, U32 Counter, UINT DurationMicros) {
    LPCSTR CounterName;
    LPCSTR WindowCounterName;
    U32 ClassCategory;

    SAFE_USE_VALID_ID(Window, KOID_WINDOW) {
        ClassCategory = DesktopProfileResolveWindowClassCategory(Window);

        switch (Counter) {
            case DESKTOP_PROFILE_WINDOW_DURATION_CLIENT_DRAW:
                CounterName = DesktopProfileResolveClientDrawCounter(ClassCategory);
                break;

            case DESKTOP_PROFILE_WINDOW_DURATION_DRAW_DISPATCH:
                CounterName = DesktopProfileResolveDrawDispatchCounter(ClassCategory);
                break;

            case DESKTOP_PROFILE_WINDOW_DURATION_DRAW_REQUEST_TO_DISPATCH:
                CounterName = DesktopProfileResolveDrawRequestCounter(ClassCategory);
                WindowCounterName = DesktopProfileResolveDrawRequestWindowCounter(Window);
                ProfileRecordDuration(WindowCounterName, DurationMicros);
                break;

            case DESKTOP_PROFILE_WINDOW_DURATION_DRAW_REQUEST_COALESCED_AGE:
                CounterName = DesktopProfileResolveDrawRequestCoalescedAgeCounter(ClassCategory);
                break;

            case DESKTOP_PROFILE_WINDOW_DURATION_DRAW_REQUEST_AFTER_CURSOR_MOVE:
                CounterName = DesktopProfileResolveDrawRequestAfterCursorMoveCounter(ClassCategory);
                break;

            default:
                return;
        }

        ProfileRecordDuration(CounterName, DurationMicros);
    }
}

/************************************************************************/

/**
 * @brief Count one desktop profiling event on a window-specific counter.
 * @param Window Target window.
 * @param Event Event family identifier.
 */
void DesktopProfileCountWindowEvent(LPWINDOW Window, U32 Event) {
    LPCSTR CounterName;
    U32 ClassCategory;

    SAFE_USE_VALID_ID(Window, KOID_WINDOW) {
        ClassCategory = DesktopProfileResolveWindowClassCategory(Window);

        switch (Event) {
            case DESKTOP_PROFILE_WINDOW_EVENT_DRAW_REQUEST_POSTED:
                CounterName = DesktopProfileResolveDrawRequestPostedCounter(ClassCategory);
                break;

            case DESKTOP_PROFILE_WINDOW_EVENT_DRAW_REQUEST_COALESCED:
                CounterName = DesktopProfileResolveDrawRequestCoalescedCounter(ClassCategory);
                break;

            case DESKTOP_PROFILE_WINDOW_EVENT_DRAW_REQUEST_STALE_AFTER_CURSOR_MOVE:
                CounterName = DesktopProfileResolveDrawRequestStaleAfterCursorMoveCounter(ClassCategory);
                break;

            case DESKTOP_PROFILE_WINDOW_EVENT_DRAW_REQUEST_CURRENT_CURSOR_MOVE:
                CounterName = DesktopProfileResolveDrawRequestCurrentCursorMoveCounter(ClassCategory);
                break;

            default:
                return;
        }

        ProfileCountCall(CounterName);
    }
}

/************************************************************************/
