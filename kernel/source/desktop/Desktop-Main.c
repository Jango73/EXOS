
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


    Desktop

\************************************************************************/

#include "console/Console.h"
#include "DisplaySession.h"
#include "DriverGetters.h"
#include "GFX.h"
#include "Kernel.h"
#include "Log.h"
#include "input/Mouse.h"
#include "Desktop-Dispatcher.h"
#include "Desktop-Cursor.h"
#include "Desktop.h"
#include "Desktop-ModeSelector.h"
#include "Desktop-ThemeTokens.h"
#include "process/Process.h"
#include "process/Task-Messaging.h"
#include "Clock.h"
#include "utils/RateLimiter.h"

/***************************************************************************/

extern DRIVER ConsoleDriver;

/***************************************************************************/

LPWINDOW NewWindow(void);
BOOL DeleteWindow(LPWINDOW);
U32 DesktopWindowFunc(HANDLE, U32, U32, U32);

/***************************************************************************/

static LIST MainDesktopChildren = {NULL, NULL, NULL, 0, KernelHeapAlloc, KernelHeapFree, NULL};

/***************************************************************************/

/**
 * @brief Convert a window-relative rectangle to screen coordinates while the window is already locked.
 * @param This Locked window instance.
 * @param Src Source rectangle in window coordinates.
 * @param Dst Destination rectangle in screen coordinates.
 */
static void WindowRectToScreenRectLocked(LPWINDOW This, LPRECT Src, LPRECT Dst) {
    Dst->X1 = This->ScreenRect.X1 + Src->X1;
    Dst->Y1 = This->ScreenRect.Y1 + Src->Y1;
    Dst->X2 = This->ScreenRect.X1 + Src->X2;
    Dst->Y2 = This->ScreenRect.Y1 + Src->Y2;
}

/***************************************************************************/

/**
 * @brief Ensure dirty region storage is initialized for one window.
 * @param This Target window.
 * @return TRUE on success.
 */
static BOOL EnsureWindowDirtyRegionInitialized(LPWINDOW This) {
    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (This->DirtyRegion.Storage == This->DirtyRects && This->DirtyRegion.Capacity == WINDOW_DIRTY_REGION_CAPACITY) {
        return TRUE;
    }

    return RectRegionInit(&This->DirtyRegion, This->DirtyRects, WINDOW_DIRTY_REGION_CAPACITY);
}

/***************************************************************************/

/**
 * @brief Update one window screen rectangle and reset its dirty region to this rectangle.
 * @param Window Target window.
 * @param Rect New screen rectangle.
 * @return TRUE on success.
 */
BOOL UpdateWindowScreenRectAndDirtyRegion(LPWINDOW Window, LPRECT Rect) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Rect == NULL) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);

    Window->Rect = *Rect;
    Window->ScreenRect = *Rect;

    if (EnsureWindowDirtyRegionInitialized(Window) == FALSE) {
        UnlockMutex(&(Window->Mutex));
        return FALSE;
    }

    RectRegionReset(&Window->DirtyRegion);
    (void)RectRegionAddRect(&Window->DirtyRegion, Rect);

    UnlockMutex(&(Window->Mutex));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Emit one rate-limited invalidation trace for redraw diagnostics.
 * @param This Target window.
 * @param SourceRect Source rectangle in window coordinates.
 * @param ScreenRect Converted invalidated rectangle in screen coordinates.
 * @param FullWindow TRUE when the full window was invalidated.
 */
static void LogInvalidateWindowRectRateLimited(LPWINDOW This, LPRECT SourceRect, LPRECT ScreenRect, BOOL FullWindow) {
    static RATE_LIMITER InvalidateLimiter;
    static BOOL InvalidateLimiterReady = FALSE;
    U32 Suppressed = 0;
    U32 Now = GetSystemTime();
    RECT Source = {0, 0, 0, 0};
    U32 WindowID = 0;
    U32 DirtyCount = 0;

    if (InvalidateLimiterReady == FALSE) {
        if (RateLimiterInit(&InvalidateLimiter, 16, 1000) == FALSE) {
            return;
        }
        InvalidateLimiterReady = TRUE;
    }

    if (SourceRect != NULL) {
        Source = *SourceRect;
    }

    SAFE_USE_VALID_ID(This, KOID_WINDOW) {
        WindowID = This->WindowID;
        DirtyCount = RectRegionGetCount(&This->DirtyRegion);
    }

    if (RateLimiterShouldTrigger(&InvalidateLimiter, Now, &Suppressed) == FALSE) {
        return;
    }

    DEBUG(TEXT("[InvalidateWindowRect] id=%x full=%x src=(%u,%u)-(%u,%u) screen=(%u,%u)-(%u,%u) dirty=%u suppressed=%u"),
        WindowID,
        FullWindow ? 1 : 0,
        UNSIGNED(Source.X1),
        UNSIGNED(Source.Y1),
        UNSIGNED(Source.X2),
        UNSIGNED(Source.Y2),
        UNSIGNED(ScreenRect->X1),
        UNSIGNED(ScreenRect->Y1),
        UNSIGNED(ScreenRect->X2),
        UNSIGNED(ScreenRect->Y2),
        DirtyCount,
        Suppressed);
}

/***************************************************************************/

WINDOW MainDesktopWindow = {
    .TypeID = KOID_WINDOW,
    .References = 1,
    .OwnerProcess = &KernelProcess,
    .Next = NULL,
    .Prev = NULL,
    .Mutex = EMPTY_MUTEX,
    .Task = NULL,
    .Function = &DesktopWindowFunc,
    .ParentWindow = NULL,
    .Children = &MainDesktopChildren,
    .Properties = NULL,
    .Rect = {0, 0, 79, 24},
    .ScreenRect = {0, 0, 79, 24},
    .WindowID = 0,
    .Style = 0,
    .Status = WINDOW_STATUS_VISIBLE,
    .Level = 0,
    .Order = 0
};

/***************************************************************************/

DESKTOP MainDesktop = {
    .TypeID = KOID_DESKTOP,
    .References = 1,
    .OwnerProcess = &KernelProcess,
    .Next = NULL,
    .Prev = NULL,
    .Mutex = EMPTY_MUTEX,
    .Task = NULL,
    .Graphics = &ConsoleDriver,
    .Window = &MainDesktopWindow,
    .Capture = NULL,
    .Focus = NULL,
    .FocusedProcess = &KernelProcess,
    .Mode = DESKTOP_MODE_CONSOLE,
    .Order = 0
};

/***************************************************************************/

/**
 * @brief Update the desktop root window rectangle from a size.
 * @param Desktop Desktop to update.
 * @param Width New width in pixels/cells.
 * @param Height New height in pixels/cells.
 */
static void UpdateDesktopWindowRect(LPDESKTOP Desktop, I32 Width, I32 Height) {
    RECT Rect;

    if (Width <= 0 || Height <= 0) return;

    Rect.X1 = 0;
    Rect.Y1 = 0;
    Rect.X2 = Width - 1;
    Rect.Y2 = Height - 1;

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        SAFE_USE_VALID_ID(Desktop->Window, KOID_WINDOW) {
            (void)UpdateWindowScreenRectAndDirtyRegion(Desktop->Window, &Rect);
        }
    }
}

/***************************************************************************/

BRUSH Brush_Desktop = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_High = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Normal = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_HiShadow = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_LoShadow = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Client = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Text_Normal = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Text_Select = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Selection = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Title_Bar = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Title_Bar_2 = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
BRUSH Brush_Title_Text = { .TypeID = KOID_BRUSH, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };

/***************************************************************************/

PEN Pen_Desktop = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_High = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Normal = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_HiShadow = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_LoShadow = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Client = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Text_Normal = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Text_Select = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Selection = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Title_Bar = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Title_Bar_2 = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };
PEN Pen_Title_Text = { .TypeID = KOID_PEN, .References = 1, .OwnerProcess = &KernelProcess, .Next = NULL, .Prev = NULL, .Color = 0, .Pattern = MAX_U32 };

/***************************************************************************/

/**
 * @brief Reset a graphics context to its default state.
 * @param This Graphics context to reset.
 * @return TRUE on success.
 */
BOOL ResetGraphicsContext(LPGRAPHICSCONTEXT This) {
    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    //-------------------------------------
    // Lock access to the context

    LockMutex(&(This->Mutex), INFINITY);

    DesktopThemeSyncSystemObjects();

    This->LoClip.X = 0;
    This->LoClip.Y = 0;
    This->HiClip.X = This->Width - 1;
    This->HiClip.Y = This->Height - 1;

    This->Brush = &Brush_Normal;
    This->Pen = &Pen_Text_Normal;
    This->Font = NULL;
    This->Bitmap = NULL;

    //-------------------------------------
    // Unlock access to the context

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Comparison routine for sorting desktops by order.
 * @param Item1 First desktop pointer.
 * @param Item2 Second desktop pointer.
 * @return Difference of desktop orders.
 */
I32 SortDesktops_Order(LPCVOID Item1, LPCVOID Item2) {
    LPDESKTOP* Ptr1 = (LPDESKTOP*)Item1;
    LPDESKTOP* Ptr2 = (LPDESKTOP*)Item2;
    LPDESKTOP Dsk1 = *Ptr1;
    LPDESKTOP Dsk2 = *Ptr2;

    return (Dsk1->Order - Dsk2->Order);
}

/***************************************************************************/

/**
 * @brief Comparison routine for sorting windows by order.
 * @param Item1 First window pointer.
 * @param Item2 Second window pointer.
 * @return Difference of window orders.
 */
I32 SortWindows_Order(LPCVOID Item1, LPCVOID Item2) {
    LPWINDOW* Ptr1 = (LPWINDOW*)Item1;
    LPWINDOW* Ptr2 = (LPWINDOW*)Item2;
    LPWINDOW Win1 = *Ptr1;
    LPWINDOW Win2 = *Ptr2;

    return (Win1->Order - Win2->Order);
}

/***************************************************************************/

/**
 * @brief Create a new desktop and its main window.
 * @return Pointer to the created desktop or NULL on failure.
 */
LPDESKTOP CreateDesktop(void) {
    LPDESKTOP This;
    WINDOWINFO WindowInfo;
    LPDESKTOP PreviousDesktop;

    This = (LPDESKTOP)KernelHeapAlloc(sizeof(DESKTOP));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(DESKTOP));

    InitMutex(&(This->Mutex));

    This->TypeID = KOID_DESKTOP;
    This->References = KOID_DESKTOP;
    This->Task = GetCurrentTask();
    if (EnsureAllMessageQueues(This->Task, TRUE) == FALSE) {
        KernelHeapFree(This);
        return NULL;
    }
    This->Graphics = &ConsoleDriver;
    This->FocusedProcess = GetCurrentProcess();
    This->Mode = DESKTOP_MODE_CONSOLE;

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Parent = NULL;
    WindowInfo.Function = DesktopWindowFunc;
    WindowInfo.Style = 0;
    WindowInfo.ID = 0;
    WindowInfo.WindowPosition.X = 0;
    WindowInfo.WindowPosition.Y = 0;
    WindowInfo.WindowSize.X = (I32)Console.Width;
    WindowInfo.WindowSize.Y = (I32)Console.Height;

    PreviousDesktop = GetCurrentProcess()->Desktop;
    GetCurrentProcess()->Desktop = This;

    This->Window = CreateWindow(&WindowInfo);

    if (This->Window == NULL) {
        GetCurrentProcess()->Desktop = PreviousDesktop;
        KernelHeapFree(This);
        return NULL;
    }
    UpdateDesktopWindowRect(This, (I32)Console.Width, (I32)Console.Height);

    //-------------------------------------
    // Add the desktop to the kernel's list

    LockMutex(MUTEX_KERNEL, INFINITY);

    LPLIST DesktopList = GetDesktopList();
    ListAddHead(DesktopList, This);

    // Process already points to this desktop

    UnlockMutex(MUTEX_KERNEL);

    return This;
}

/***************************************************************************/

/**
 * @brief Delete a desktop and release its resources.
 * @param This Desktop to delete.
 */
BOOL DeleteDesktop(LPDESKTOP This) {
    if (This == NULL) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    SAFE_USE_VALID_ID(This->Window, KOID_WINDOW) {
        DeleteWindow(This->Window);
    }

    ReleaseKernelObject(This);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Display a desktop by setting the graphics mode and ordering.
 * @param This Desktop to show.
 * @return TRUE on success.
 */
BOOL ShowDesktop(LPDESKTOP This) {
    GRAPHICSMODEINFO ModeInfo;
    GRAPHICSMODEINFO QueriedModeInfo;
    GRAPHICSMODEINFO ActiveModeInfo;
    GRAPHICSMODEINFO SelectedModeInfo;
    GRAPHICSMODEINFO RequestedModeInfo;
    UINT ModeSetResult;
    LPDESKTOP Desktop;
    LPLISTNODE Node;
    I32 Order;
    BOOL HasActiveMode;
    LPDRIVER ActiveGraphicsDriver;
    BOOL ModeReady;
    BOOL HasSelectedMode;
    BOOL UsedLegacyAutoSelect;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_DESKTOP) return FALSE;

    (void)DesktopEnsureDispatcherTask(This);

    //-------------------------------------
    // Lock access to resources

    LockMutex(MUTEX_KERNEL, INFINITY);
    LockMutex(&(This->Mutex), INFINITY);

    //-------------------------------------
    // Sort the kernel's desktop list

    LPLIST DesktopList = GetDesktopList();
    for (Node = DesktopList->First, Order = 1; Node; Node = Node->Next) {
        Desktop = (LPDESKTOP)Node;
        if (Desktop == This)
            Desktop->Order = 0;
        else
            Desktop->Order = Order++;
    }

    ListSort(DesktopList, SortDesktops_Order);

    This->Graphics = GetGraphicsDriver();
    if (This->Graphics == NULL || This->Graphics->Command == NULL) {
        WARNING(TEXT("[ShowDesktop] Graphics driver unavailable"));
        This->Mode = DESKTOP_MODE_CONSOLE;
        UnlockMutex(&(This->Mutex));
        UnlockMutex(MUTEX_KERNEL);
        return FALSE;
    }

    HasActiveMode = DisplaySessionGetActiveMode(&ActiveModeInfo);
    ActiveGraphicsDriver = DisplaySessionGetActiveGraphicsDriver();
    if (HasActiveMode && ActiveGraphicsDriver != NULL && ActiveGraphicsDriver == This->Graphics && ActiveGraphicsDriver != ConsoleGetDriver() &&
        ActiveModeInfo.Width != 0 && ActiveModeInfo.Height != 0) {
        ModeInfo = ActiveModeInfo;
        DEBUG(TEXT("[ShowDesktop] Reusing active graphics mode %ux%ux%u"), ModeInfo.Width, ModeInfo.Height, ModeInfo.BitsPerPixel);
    } else {
        HasSelectedMode = DesktopSelectGraphicsMode(This->Graphics, &SelectedModeInfo);
        UsedLegacyAutoSelect = FALSE;

        if (HasSelectedMode != FALSE) {
            RequestedModeInfo = SelectedModeInfo;
            ModeSetResult = This->Graphics->Command(DF_GFX_SETMODE, (UINT)&RequestedModeInfo);
            if (ModeSetResult == DF_RETURN_SUCCESS) {
                ModeInfo = RequestedModeInfo;
                DEBUG(TEXT("[ShowDesktop] Applied selected mode %ux%ux%u"),
                    ModeInfo.Width,
                    ModeInfo.Height,
                    ModeInfo.BitsPerPixel);
            } else {
                WARNING(TEXT("[ShowDesktop] DF_GFX_SETMODE selected mode failed (%u)"), ModeSetResult);
            }
        }

        if (HasSelectedMode == FALSE || ModeSetResult != DF_RETURN_SUCCESS) {
            // Legacy fallback while all backends migrate to full mode enumeration.
            DesktopInitializeGraphicsModeInfo(&RequestedModeInfo, INFINITY, 0, 0, 0);
            ModeSetResult = This->Graphics->Command(DF_GFX_SETMODE, (UINT)&RequestedModeInfo);
            if (ModeSetResult != DF_RETURN_SUCCESS) {
                WARNING(TEXT("[ShowDesktop] DF_GFX_SETMODE legacy auto-select failed (%u)"), ModeSetResult);
            } else {
                ModeInfo = RequestedModeInfo;
                UsedLegacyAutoSelect = TRUE;
            }
        }

        if (UsedLegacyAutoSelect != FALSE) {
            DEBUG(TEXT("[ShowDesktop] Legacy auto-select mode fallback applied"));
        }
    }

    DesktopInitializeGraphicsModeInfo(&QueriedModeInfo, INFINITY, 0, 0, 0);

    if (This->Graphics->Command(DF_GFX_GETMODEINFO, (UINT)&QueriedModeInfo) == DF_RETURN_SUCCESS &&
        QueriedModeInfo.Width != 0 && QueriedModeInfo.Height != 0) {
        ModeInfo = QueriedModeInfo;
    }

    ModeReady = DesktopIsValidGraphicsModeInfo(&ModeInfo);
    if (ModeReady == FALSE && HasActiveMode && ActiveModeInfo.Width != 0 && ActiveModeInfo.Height != 0) {
        ModeInfo = ActiveModeInfo;
        ModeReady = TRUE;
    }

    if (ModeReady == FALSE) {
        WARNING(TEXT("[ShowDesktop] No valid graphics mode available after selection"));
        This->Mode = DESKTOP_MODE_CONSOLE;
        UnlockMutex(&(This->Mutex));
        UnlockMutex(MUTEX_KERNEL);
        return FALSE;
    }

    if (DisplaySessionSetDesktopMode(This, This->Graphics, &ModeInfo) == FALSE) {
        WARNING(TEXT("[ShowDesktop] Unable to activate desktop display session"));
        This->Mode = DESKTOP_MODE_CONSOLE;
        UnlockMutex(&(This->Mutex));
        UnlockMutex(MUTEX_KERNEL);
        return FALSE;
    }

    This->Mode = DESKTOP_MODE_GRAPHICS;
    UpdateDesktopWindowRect(This, (I32)ModeInfo.Width, (I32)ModeInfo.Height);

    // PostMessage((HANDLE) This->Window, EWM_DRAW, 0, 0);

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));
    UnlockMutex(MUTEX_KERNEL);

    DesktopCursorOnDesktopActivated(This);

    //-------------------------------------
    // Force the desktop root window to repaint

    SAFE_USE_VALID_ID(This->Window, KOID_WINDOW) { InvalidateWindowRect((HANDLE)This->Window, NULL); }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve the desktop screen rectangle for the current mode.
 * @param Desktop Desktop to query.
 * @param Rect Output rectangle.
 * @return TRUE if the rectangle is returned, FALSE otherwise.
 */
BOOL GetDesktopScreenRect(LPDESKTOP Desktop, LPRECT Rect) {
    if (Rect == NULL) return FALSE;

    SAFE_USE_VALID_ID(Desktop, KOID_DESKTOP) {
        LockMutex(&(Desktop->Mutex), INFINITY);

        if (Desktop->Mode == DESKTOP_MODE_CONSOLE) {
            if (Console.Width == 0 || Console.Height == 0) {
                UnlockMutex(&(Desktop->Mutex));
                return FALSE;
            }
            Rect->X1 = 0;
            Rect->Y1 = 0;
            Rect->X2 = (I32)Console.Width - 1;
            Rect->Y2 = (I32)Console.Height - 1;
            UnlockMutex(&(Desktop->Mutex));
            return TRUE;
        }

        SAFE_USE_VALID_ID(Desktop->Window, KOID_WINDOW) {
            LockMutex(&(Desktop->Window->Mutex), INFINITY);
            *Rect = Desktop->Window->ScreenRect;
            UnlockMutex(&(Desktop->Window->Mutex));
            UnlockMutex(&(Desktop->Mutex));
            return TRUE;
        }

        UnlockMutex(&(Desktop->Mutex));
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Allocate and initialize a new window structure.
 * @return Pointer to the created window or NULL on failure.
 */
LPWINDOW NewWindow(void) {
    LPWINDOW This = (LPWINDOW)KernelHeapAlloc(sizeof(WINDOW));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(WINDOW));

    InitMutex(&(This->Mutex));

    This->TypeID = KOID_WINDOW;
    This->References = 1;
    This->Properties = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    This->Children = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    (void)RectRegionInit(&This->DirtyRegion, This->DirtyRects, WINDOW_DIRTY_REGION_CAPACITY);

    return This;
}

/***************************************************************************/

/**
 * @brief Delete a window and its children.
 * @param This Window to delete.
 */
BOOL DeleteWindow(LPWINDOW This) {
    LPPROCESS Process;
    LPTASK Task;
    LPDESKTOP Desktop;
    LPLISTNODE Node;

    //-------------------------------------
    // Check validity of parameters

    if (This->TypeID != KOID_WINDOW) return FALSE;
    if (This->ParentWindow == NULL) return FALSE;

    Task = This->Task;
    if (Task == NULL) return FALSE;
    Process = Task->Process;
    if (Process == NULL) return FALSE;
    Desktop = Process->Desktop;
    if (Desktop == NULL) return FALSE;

    //-------------------------------------
    // Release desktop related resources

    LockMutex(&(Desktop->Mutex), INFINITY);

    if (Desktop->Capture == This) Desktop->Capture = NULL;
    if (Desktop->Focus == This) Desktop->Focus = NULL;

    UnlockMutex(&(Desktop->Mutex));

    //-------------------------------------
    // Lock access to the window

    LockMutex(&(This->Mutex), INFINITY);

    //-------------------------------------
    // Delete children first

    for (Node = This->Children->First; Node; Node = Node->Next) {
        DeleteWindow((LPWINDOW)Node);
    }

    //-------------------------------------
    // Remove window from it's parent's list

    LockMutex(&(This->ParentWindow->Mutex), INFINITY);

    ListRemove(This->ParentWindow->Children, This);

    UnlockMutex(&(This->ParentWindow->Mutex));

    ReleaseKernelObject(This);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Search recursively for a window starting from another window.
 * @param Start Window to start searching from.
 * @param Target Window to find.
 * @return Pointer to the found window or NULL.
 */
LPWINDOW FindWindow(LPWINDOW Start, LPWINDOW Target) {
    LPLISTNODE Node = NULL;
    LPWINDOW Current = NULL;
    LPWINDOW Child = NULL;

    if (Start == NULL) return NULL;
    if (Start->TypeID != KOID_WINDOW) return NULL;

    if (Target == NULL) return NULL;
    if (Target->TypeID != KOID_WINDOW) return NULL;

    if (Start == Target) return Start;

    LockMutex(&(Start->Mutex), INFINITY);
    LockMutex(&(Target->Mutex), INFINITY);

    for (Node = Start->Children->First; Node; Node = Node->Next) {
        Child = (LPWINDOW)Node;
        Current = FindWindow(Child, Target);
        if (Current != NULL) goto Out;
    }

Out:

    UnlockMutex(&(Target->Mutex));
    UnlockMutex(&(Start->Mutex));

    return Current;
}

/***************************************************************************/

/**
 * @brief Create a window based on provided window information.
 * @param Info Structure describing the window to create.
 * @return Pointer to the created window or NULL on failure.
 */
LPWINDOW CreateWindow(LPWINDOWINFO Info) {
    LPWINDOW This;
    LPWINDOW Win;
    LPWINDOW Parent;
    LPDESKTOP Desktop;
    LPTASK OwnerTask;

    //-------------------------------------
    // Check validity of parameters

    if (Info == NULL) return NULL;

    //-------------------------------------
    // Get the desktop of the current process

    Desktop = GetCurrentProcess()->Desktop;

    //-------------------------------------
    // Check that parent is a valid window
    // and that it belongs to the current desktop

    Parent = (LPWINDOW)Info->Parent;

    SAFE_USE(Parent) {
        if (Parent->TypeID != KOID_WINDOW) return NULL;
    }

    This = NewWindow();

    if (This == NULL) return NULL;

    OwnerTask = DesktopResolveWindowTask(Desktop, GetCurrentTask());
    This->Task = OwnerTask;
    This->ParentWindow = Parent;
    This->Function = Info->Function;
    This->WindowID = Info->ID;
    This->Style = Info->Style;
    if (EnsureAllMessageQueues(OwnerTask, TRUE) == FALSE) {
        KernelHeapFree(This);
        return NULL;
    }
    This->Rect.X1 = Info->WindowPosition.X;
    This->Rect.Y1 = Info->WindowPosition.Y;
    This->Rect.X2 = Info->WindowPosition.X + (Info->WindowSize.X - 1);
    This->Rect.Y2 = Info->WindowPosition.Y + (Info->WindowSize.Y - 1);
    This->ScreenRect = This->Rect;
    RectRegionReset(&This->DirtyRegion);
    (void)RectRegionAddRect(&This->DirtyRegion, &This->ScreenRect);

    if (This->ParentWindow == NULL) {
        SAFE_USE(Desktop) {
            if (Desktop->Window == NULL) {
                Desktop->Window = This;
            } else {
                This->ParentWindow = Desktop->Window;
            }
        }
    }

    SAFE_USE(This->ParentWindow) {
        LockMutex(&(This->ParentWindow->Mutex), INFINITY);

        This->ScreenRect.X1 = This->ParentWindow->ScreenRect.X1 + This->Rect.X1;
        This->ScreenRect.Y1 = This->ParentWindow->ScreenRect.Y1 + This->Rect.Y1;
        This->ScreenRect.X2 = This->ParentWindow->ScreenRect.X1 + This->Rect.X2;
        This->ScreenRect.Y2 = This->ParentWindow->ScreenRect.Y1 + This->Rect.Y2;

        RectRegionReset(&This->DirtyRegion);
        (void)RectRegionAddRect(&This->DirtyRegion, &This->ScreenRect);

        ListAddHead(This->ParentWindow->Children, This);

        //-------------------------------------
        // Compute the level of the window

        for (Win = This->ParentWindow; Win; Win = Win->ParentWindow) This->Level++;

        UnlockMutex(&(This->ParentWindow->Mutex));
    }

    //-------------------------------------
    // Tell the window it is being created

    PostMessage((HANDLE)This, EWM_CREATE, 0, 0);

    //-------------------------------------
    // Ensure the freshly created window gets a draw request

    InvalidateWindowRect((HANDLE)This, NULL);
    PostMessage((HANDLE)This, EWM_DRAW, 0, 0);

    return This;
}

/***************************************************************************/

/**
 * @brief Retrieve the desktop owning a given window.
 * @param This Window whose desktop is requested.
 * @return Pointer to the desktop or NULL.
 */
LPDESKTOP GetWindowDesktop(LPWINDOW This) {
    LPPROCESS Process = NULL;
    LPTASK Task = NULL;
    LPDESKTOP Desktop = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    Task = This->Task;
    if (Task != NULL && Task->TypeID == KOID_TASK) {
        Process = Task->Process;

        SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
            Desktop = Process->Desktop;

            SAFE_USE(Desktop) {
                if (Desktop->TypeID != KOID_DESKTOP) Desktop = NULL;
            }
        }
    }

    UnlockMutex(&(This->Mutex));

    return Desktop;
}

/***************************************************************************/

/**
 * @brief Post a message to a window and all of its children.
 * @param This Starting window for broadcast.
 * @param Msg Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return TRUE on success.
 */
BOOL BroadcastMessageToWindow(LPWINDOW This, U32 Msg, U32 Param1, U32 Param2) {
    LPLISTNODE Node;

    if (This == NULL) return NULL;
    if (This->TypeID != KOID_WINDOW) return NULL;

    LockMutex(&(This->Mutex), INFINITY);

    PostMessage((HANDLE)This, Msg, Param1, Param2);

    for (Node = This->Children->First; Node; Node = Node->Next) {
        BroadcastMessageToWindow((LPWINDOW)Node, Msg, Param1, Param2);
    }

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

/*
static BOOL ComputeWindowRegions(LPWINDOW This) {
    UNUSED(This);

    return FALSE;
}
*/

/***************************************************************************/

/**
 * @brief Determine if a rectangle is fully contained within another.
 * @param Src Source rectangle.
 * @param Dst Destination rectangle.
 * @return TRUE if Src is inside Dst.
 */
BOOL RectInRect(LPRECT Src, LPRECT Dst) {
    if (Src == NULL) return FALSE;
    if (Dst == NULL) return FALSE;

    if (Src->X1 < Dst->X1 && Src->X2 < Dst->X1) return FALSE;
    if (Src->X1 > Dst->X2 && Src->X2 > Dst->X2) return FALSE;
    if (Src->Y1 < Dst->Y1 && Src->Y2 < Dst->Y1) return FALSE;
    if (Src->Y1 > Dst->Y2 && Src->Y2 > Dst->Y2) return FALSE;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Convert a window-relative rectangle to screen coordinates.
 * @param Handle Window handle.
 * @param Src Source rectangle in window coordinates.
 * @param Dst Destination rectangle in screen coordinates.
 * @return TRUE on success.
 */
BOOL WindowRectToScreenRect(HANDLE Handle, LPRECT Src, LPRECT Dst) {
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (Src == NULL) return FALSE;
    if (Dst == NULL) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    WindowRectToScreenRectLocked(This, Src, Dst);

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Convert a screen rectangle to window-relative coordinates.
 * @param Handle Window handle.
 * @param Src Source screen rectangle.
 * @param Dst Destination window rectangle.
 * @return TRUE on success.
 */
BOOL ScreenRectToWindowRect(HANDLE Handle, LPRECT Src, LPRECT Dst) {
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (Src == NULL) return FALSE;
    if (Dst == NULL) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    Dst->X1 = Src->X1 - This->ScreenRect.X1;
    Dst->Y1 = Src->Y1 - This->ScreenRect.Y1;
    Dst->X2 = Src->X2 - This->ScreenRect.X1;
    Dst->Y2 = Src->Y2 - This->ScreenRect.Y1;

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Add a rectangle to a window's invalid region.
 * @param Handle Window handle.
 * @param Src Rectangle to invalidate.
 * @return TRUE on success.
 */
BOOL InvalidateWindowRect(HANDLE Handle, LPRECT Src) {
    LPWINDOW This = (LPWINDOW)Handle;
    RECT Rect;
    BOOL FullWindow = FALSE;

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    if (EnsureWindowDirtyRegionInitialized(This) == FALSE) {
        UnlockMutex(&(This->Mutex));
        return FALSE;
    }

    SAFE_USE(Src) {
        WindowRectToScreenRectLocked(This, Src, &Rect);
        (void)RectRegionAddRect(&This->DirtyRegion, &Rect);
    } else {
        FullWindow = TRUE;
        Rect = This->ScreenRect;
        RectRegionReset(&This->DirtyRegion);
        (void)RectRegionAddRect(&This->DirtyRegion, &Rect);
    }

    LogInvalidateWindowRectRateLimited(This, Src, &Rect, FullWindow);

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    PostMessage(Handle, EWM_DRAW, 0, 0);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Raise a window to the front of the Z order.
 * @param Handle Window handle.
 * @return TRUE on success.
 */
BOOL BringWindowToFront(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPWINDOW That;
    LPLISTNODE Node;
    RECT Rect;
    RECT HiddenRect;
    I32 Order;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    if (EnsureWindowDirtyRegionInitialized(This) == FALSE) goto Out;

    if (This->ParentWindow == NULL) goto Out;

    //-------------------------------------
    // Invalidate hidden regions

    for (Node = This->Prev; Node; Node = Node->Prev) {
        That = (LPWINDOW)Node;
        if (RectInRect(&(This->ScreenRect), &(That->ScreenRect))) {
            ScreenRectToWindowRect((HANDLE)That, &(That->ScreenRect), &Rect);
            WindowRectToScreenRectLocked(This, &Rect, &HiddenRect);
            (void)RectRegionAddRect(&This->DirtyRegion, &HiddenRect);
        }
    }

    //-------------------------------------
    // Reorder the windows

    for (Node = This->ParentWindow->Children->First, Order = 1; Node; Node = Node->Next) {
        That = (LPWINDOW)Node;
        if (That == This)
            That->Order = 0;
        else
            That->Order = Order++;
    }

    ListSort(This->ParentWindow->Children, SortWindows_Order);

    //-------------------------------------
    // Tell the window it needs redraw

    BroadcastMessageToWindow(This, EWM_DRAW, 0, 0);

Out:

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/
