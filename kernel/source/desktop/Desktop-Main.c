
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

#include "Clock.h"
#include "CoreString.h"
#include "Desktop-Cursor.h"
#include "Desktop-Dispatcher.h"
#include "Desktop-ModeSelector.h"
#include "Desktop-NonClient.h"
#include "Desktop-OverlayInvalidation.h"
#include "Desktop-Private.h"
#include "Desktop-ThemeTokens.h"
#include "Desktop-Timer.h"
#include "Desktop-WindowClass.h"
#include "Desktop.h"
#include "DisplaySession.h"
#include "DriverGetters.h"
#include "GFX.h"
#include "Kernel.h"
#include "Log.h"
#include "console/Console.h"
#include "input/Mouse.h"
#include "process/Process.h"
#include "process/Task-Messaging.h"
#include "ui/RootWindowClass.h"
#include "ui/Startup-Desktop-Components.h"
#include "utils/Graphics-Utils.h"
#include "utils/RateLimiter.h"

/***************************************************************************/

extern DRIVER ConsoleDriver;

/***************************************************************************/

typedef struct tag_Z_ORDER_CHILD_SNAPSHOT {
    LPWINDOW Window;
    I32 Order;
} Z_ORDER_CHILD_SNAPSHOT, *LPZ_ORDER_CHILD_SNAPSHOT;

/***************************************************************************/

LPWINDOW NewWindow(void);
BOOL DeleteWindow(LPWINDOW);
U32 DesktopWindowFunc(HANDLE, U32, U32, U32);

/***************************************************************************/

/**
 * @brief Notify one window ancestry that one descendant was appended.
 * @param Window Newly attached descendant window.
 */
static void NotifyWindowChildAppended(LPWINDOW Window) {
    LPWINDOW Current;
    HANDLE ParentWindow;
    U32 ChildWindowID;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;

    ChildWindowID = Window->WindowID;
    Current = Window;
    FOREVER {
        ParentWindow = GetWindowParent((HANDLE)Current);
        if (ParentWindow == NULL) break;

        (void)PostMessage(ParentWindow, EWM_CHILD_APPENDED, ChildWindowID, 0);
        Current = (LPWINDOW)ParentWindow;
    }
}

/***************************************************************************/

/**
 * @brief Notify one window ancestry that one descendant was removed.
 * @param Parent Parent from which the child was detached.
 * @param ChildWindowID Removed child window identifier.
 */
static void NotifyWindowChildRemoved(LPWINDOW Parent, U32 ChildWindowID) {
    LPWINDOW Current;
    HANDLE ParentWindow;

    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return;

    Current = Parent;
    FOREVER {
        (void)PostMessage((HANDLE)Current, EWM_CHILD_REMOVED, ChildWindowID, 0);

        ParentWindow = GetWindowParent((HANDLE)Current);
        if (ParentWindow == NULL) break;
        Current = (LPWINDOW)ParentWindow;
    }
}

/***************************************************************************/

/**
 * @brief Convert a window-relative rectangle to screen coordinates while the window is already locked.
 * @param This Locked window instance.
 * @param Src Source rectangle in window coordinates.
 * @param Dst Destination rectangle in screen coordinates.
 */
static void WindowRectToScreenRectLocked(LPWINDOW This, LPRECT WindowRect, LPRECT ScreenRect) {
    GraphicsWindowRectToScreenRect(&(This->ScreenRect), WindowRect, ScreenRect);
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
    (void)PostMessage((HANDLE)Window, EWM_NOTIFY, EWN_WINDOW_RECT_CHANGED, 0);
    return TRUE;
}

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

/**
 * @brief Check whether one desktop already owns one persisted display selection.
 * @param Desktop Desktop instance to inspect.
 * @return TRUE when backend alias and mode are valid.
 */
static BOOL DesktopHasDisplaySelection(LPDESKTOP Desktop) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) {
        return FALSE;
    }

    if (Desktop->DisplaySelection.IsAssigned == FALSE) {
        return FALSE;
    }

    if (StringLength(Desktop->DisplaySelection.BackendAlias) == 0) {
        return FALSE;
    }

    return DesktopIsValidGraphicsModeInfo(&Desktop->DisplaySelection.ModeInfo);
}

/***************************************************************************/

/**
 * @brief Persist one backend and mode selection on the desktop.
 * @param Desktop Desktop receiving the persisted selection.
 * @param BackendAlias Selected backend alias.
 * @param ModeInfo Selected graphics mode.
 */
static void DesktopSetDisplaySelection(LPDESKTOP Desktop, LPCSTR BackendAlias, LPGRAPHICSMODEINFO ModeInfo) {
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP || BackendAlias == NULL || ModeInfo == NULL) {
        return;
    }

    if (StringLength(BackendAlias) == 0 || DesktopIsValidGraphicsModeInfo(ModeInfo) == FALSE) {
        return;
    }

    MemorySet(&(Desktop->DisplaySelection), 0, sizeof(Desktop->DisplaySelection));
    StringCopy(Desktop->DisplaySelection.BackendAlias, BackendAlias);
    Desktop->DisplaySelection.ModeInfo = *ModeInfo;
    Desktop->DisplaySelection.ModeInfo.ModeIndex = INFINITY;
    Desktop->DisplaySelection.IsAssigned = TRUE;
}

/***************************************************************************/

/**
 * @brief Apply the desktop persisted backend and mode selection.
 * @param Desktop Desktop owning the selection.
 * @param AppliedModeInfo Receives the effective graphics mode.
 * @return TRUE when the stored backend and mode were applied successfully.
 */
static BOOL DesktopApplyDisplaySelection(LPDESKTOP Desktop, LPGRAPHICSMODEINFO AppliedModeInfo) {
    GRAPHICSMODEINFO RequestedModeInfo;
    GRAPHICSMODEINFO QueriedModeInfo;
    UINT ModeSetResult;

    if (DesktopHasDisplaySelection(Desktop) == FALSE || AppliedModeInfo == NULL) {
        return FALSE;
    }

    if (GraphicsSelectorForceBackendByName(Desktop->DisplaySelection.BackendAlias) == FALSE) {
        WARNING(TEXT("[DesktopApplyDisplaySelection] Stored backend unavailable (%s)"),
            Desktop->DisplaySelection.BackendAlias);
        return FALSE;
    }

    RequestedModeInfo = Desktop->DisplaySelection.ModeInfo;
    ModeSetResult = GetGraphicsDriver()->Command(DF_GFX_SETMODE, (UINT)&RequestedModeInfo);
    if (ModeSetResult != DF_RETURN_SUCCESS) {
        WARNING(TEXT("[DesktopApplyDisplaySelection] Stored mode apply failed (%u)"), ModeSetResult);
        return FALSE;
    }

    DesktopInitializeGraphicsModeInfo(&QueriedModeInfo, INFINITY, 0, 0, 0);
    if (GetGraphicsDriver()->Command(DF_GFX_GETMODEINFO, (UINT)&QueriedModeInfo) == DF_RETURN_SUCCESS &&
        DesktopIsValidGraphicsModeInfo(&QueriedModeInfo) != FALSE) {
        *AppliedModeInfo = QueriedModeInfo;
    } else {
        *AppliedModeInfo = RequestedModeInfo;
    }

    return TRUE;
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
    InitMutex(&(This->TimerMutex));
    This->Timers = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    if (This->Timers == NULL) {
        KernelHeapFree(This);
        return NULL;
    }

    This->TypeID = KOID_DESKTOP;
    This->References = KOID_DESKTOP;
    This->Task = GetCurrentTask();
    if (EnsureAllMessageQueues(This->Task, TRUE) == FALSE) {
        DeleteList(This->Timers);
        KernelHeapFree(This);
        return NULL;
    }
    This->Graphics = &ConsoleDriver;
    This->Mode = DESKTOP_MODE_CONSOLE;

    if (DesktopEnsureDispatcherTask(This) == FALSE) {
        DeleteList(This->Timers);
        KernelHeapFree(This);
        return NULL;
    }

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = NULL;
    if (WindowDockHostClassEnsureDerivedRegistered(ROOT_WINDOW_CLASS_NAME, DesktopWindowFunc) == FALSE) {
        DeleteList(This->Timers);
        KernelHeapFree(This);
        return NULL;
    }

    WindowInfo.WindowClass = 0;
    WindowInfo.WindowClassName = ROOT_WINDOW_CLASS_NAME;
    WindowInfo.Function = NULL;
    WindowInfo.Style = EWS_BARE_SURFACE;
    WindowInfo.ID = 0;
    WindowInfo.WindowPosition.X = 0;
    WindowInfo.WindowPosition.Y = 0;
    WindowInfo.WindowSize.X = (I32)Console.Width;
    WindowInfo.WindowSize.Y = (I32)Console.Height;
    WindowInfo.ShowHide = TRUE;

    PreviousDesktop = GetCurrentProcess()->Desktop;
    GetCurrentProcess()->Desktop = This;

    This->Window = CreateWindow(&WindowInfo);

    if (This->Window == NULL) {
        GetCurrentProcess()->Desktop = PreviousDesktop;
        KernelHeapFree(This);
        return NULL;
    }

    (void)StartupDesktopComponentsInitialize(This);
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

    SAFE_USE(This->Timers) {
        DeleteList(This->Timers);
        This->Timers = NULL;
    }

    SAFE_USE_VALID_ID(This->Window, KOID_WINDOW) { DeleteWindow(This->Window); }

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
    GRAPHICSMODEINFO SelectedModeInfo;
    GRAPHICSMODEINFO RequestedModeInfo;
    UINT ModeSetResult;
    LPDESKTOP Desktop;
    LPLISTNODE Node;
    I32 Order;
    LPDRIVER SelectedBackendDriver;
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

    if (DesktopApplyDisplaySelection(This, &ModeInfo) != FALSE) {
        This->Graphics = GetGraphicsDriver();
    } else {
        HasSelectedMode = DesktopSelectGraphicsMode(This->Graphics, &SelectedModeInfo);
        UsedLegacyAutoSelect = FALSE;

        if (HasSelectedMode != FALSE) {
            RequestedModeInfo = SelectedModeInfo;
            ModeSetResult = This->Graphics->Command(DF_GFX_SETMODE, (UINT)&RequestedModeInfo);
            if (ModeSetResult == DF_RETURN_SUCCESS) {
                ModeInfo = RequestedModeInfo;
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
        }
    }

    DesktopInitializeGraphicsModeInfo(&QueriedModeInfo, INFINITY, 0, 0, 0);

    if (This->Graphics->Command(DF_GFX_GETMODEINFO, (UINT)&QueriedModeInfo) == DF_RETURN_SUCCESS &&
        QueriedModeInfo.Width != 0 && QueriedModeInfo.Height != 0) {
        ModeInfo = QueriedModeInfo;
    }

    ModeReady = DesktopIsValidGraphicsModeInfo(&ModeInfo);
    if (ModeReady == FALSE) {
        WARNING(TEXT("[ShowDesktop] No valid graphics mode available after selection"));
        This->Mode = DESKTOP_MODE_CONSOLE;
        UnlockMutex(&(This->Mutex));
        UnlockMutex(MUTEX_KERNEL);
        return FALSE;
    }

    SelectedBackendDriver = GraphicsSelectorGetActiveBackendDriver();
    if (SelectedBackendDriver != NULL && StringLength(SelectedBackendDriver->Alias) != 0) {
        DesktopSetDisplaySelection(This, SelectedBackendDriver->Alias, &ModeInfo);
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

    SetActiveDesktop(This);
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
    LPWINDOW RootWindow;

    if (Rect == NULL) return FALSE;
    if (DesktopGetRootWindow(Desktop, &RootWindow) == FALSE) return FALSE;

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

        UnlockMutex(&(Desktop->Mutex));
    }

    return GetWindowScreenRectSnapshot(RootWindow, Rect);
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
    LPWINDOW ParentWindow;
    LPWINDOW ChildWindow;
    U32 ChildWindowID;

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

    DesktopTimerRemoveWindowTimers(Desktop, This);
    (void)DesktopClearWindowReferences(Desktop, This);
    (void)SendMessage((HANDLE)This, EWM_DELETE, 0, 0);

    LockMutex(&(This->Mutex), INFINITY);
    ParentWindow = This->ParentWindow;
    ChildWindowID = This->WindowID;
    UnlockMutex(&(This->Mutex));

    FOREVER {
        ChildWindow = (LPWINDOW)GetWindowChild((HANDLE)This, 0);
        if (ChildWindow == NULL || ChildWindow->TypeID != KOID_WINDOW) break;
        DeleteWindow(ChildWindow);
    }

    LockMutex(&(This->Mutex), INFINITY);

    if (This->ClassData != NULL) {
        KernelHeapFree(This->ClassData);
        This->ClassData = NULL;
    }
    UnlockMutex(&(This->Mutex));

    (void)DesktopDetachWindowChild(ParentWindow, This);
    NotifyWindowChildRemoved(ParentWindow, ChildWindowID);

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
    LPWINDOW Current = NULL;
    LPWINDOW Child = NULL;
    UINT ChildCount;
    UINT Index;

    if (Start == NULL) return NULL;
    if (Start->TypeID != KOID_WINDOW) return NULL;

    if (Target == NULL) return NULL;
    if (Target->TypeID != KOID_WINDOW) return NULL;

    if (Start == Target) return Start;

    ChildCount = GetWindowChildCount((HANDLE)Start);
    for (Index = 0; Index < ChildCount; Index++) {
        Child = (LPWINDOW)GetWindowChild((HANDLE)Start, Index);
        if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;
        Current = FindWindow(Child, Target);
        if (Current != NULL) return Current;
    }

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
    LPWINDOW Parent;
    LPDESKTOP Desktop;
    LPTASK OwnerTask;
    RECT InitialRect;
    RECT ParentScreenRect;
    U32 ParentLevel;

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

    if (WindowClassInitializeRegistry() == FALSE) {
        KernelHeapFree(This);
        return NULL;
    }

    if (Info->WindowClass != 0) {
        This->Class = WindowClassFindByHandle((U32)Info->WindowClass);
    } else if (Info->WindowClassName != NULL) {
        This->Class = WindowClassFindByName(Info->WindowClassName);
    } else {
        This->Class = WindowClassGetDefault();
    }
    if (This->Class == NULL) {
        KernelHeapFree(This);
        return NULL;
    }

    if (This->Class->ClassDataSize > 0) {
        This->ClassData = KernelHeapAlloc(This->Class->ClassDataSize);
        if (This->ClassData == NULL) {
            KernelHeapFree(This);
            return NULL;
        }
        MemorySet(This->ClassData, 0, This->Class->ClassDataSize);
    }

    if (This->Function == NULL) {
        This->Function = This->Class->Function;
    }

    if (EnsureAllMessageQueues(OwnerTask, TRUE) == FALSE) {
        if (This->ClassData != NULL) {
            KernelHeapFree(This->ClassData);
            This->ClassData = NULL;
        }
        KernelHeapFree(This);
        return NULL;
    }
    if (This->ParentWindow == NULL) {
        SAFE_USE(Desktop) {
            if (Desktop->Window == NULL) {
                Desktop->Window = This;
            } else {
                This->ParentWindow = Desktop->Window;
            }
        }
    }

    InitialRect.X1 = Info->WindowPosition.X;
    InitialRect.Y1 = Info->WindowPosition.Y;
    InitialRect.X2 = Info->WindowPosition.X + (Info->WindowSize.X - 1);
    InitialRect.Y2 = Info->WindowPosition.Y + (Info->WindowSize.Y - 1);

    if (DesktopResolveWindowPlacementRect(This, &InitialRect) == FALSE) {
        if (This->ClassData != NULL) {
            KernelHeapFree(This->ClassData);
            This->ClassData = NULL;
        }
        KernelHeapFree(This);
        return NULL;
    }

    This->Rect = InitialRect;
    This->ScreenRect = This->Rect;
    RectRegionReset(&This->DirtyRegion);
    (void)RectRegionAddRect(&This->DirtyRegion, &This->ScreenRect);

    SAFE_USE(This->ParentWindow) {
        if (GetWindowScreenRectSnapshot(This->ParentWindow, &ParentScreenRect) != FALSE) {
            GraphicsWindowRectToScreenRect(&ParentScreenRect, &(This->Rect), &(This->ScreenRect));
            RectRegionReset(&This->DirtyRegion);
            (void)RectRegionAddRect(&This->DirtyRegion, &This->ScreenRect);
        }

        ParentLevel = 0;
        (void)GetWindowLevelSnapshot(This->ParentWindow, &ParentLevel);
        This->Level = ParentLevel + 1;
        (void)DesktopAttachWindowChild(This->ParentWindow, This);
    }

    NotifyWindowChildAppended(This);

    //-------------------------------------
    // Tell the window it is being created

    PostMessage((HANDLE)This, EWM_CREATE, 0, 0);

    //-------------------------------------
    // Ensure the freshly created window gets a full local draw request

    {
        RECT FullWindowRect;

        FullWindowRect.X1 = 0;
        FullWindowRect.Y1 = 0;
        FullWindowRect.X2 = This->Rect.X2 - This->Rect.X1;
        FullWindowRect.Y2 = This->Rect.Y2 - This->Rect.Y1;
        InvalidateWindowRect((HANDLE)This, &FullWindowRect);
    }

    if (Info->ShowHide != FALSE || (This->Style & EWS_VISIBLE) != 0) {
        (void)ShowWindow((HANDLE)This, TRUE);
    }

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
 * @brief Append one window pointer to a dynamic array.
 * @param Windows Pointer to dynamic array pointer.
 * @param Count Number of valid entries.
 * @param Capacity Allocated capacity.
 * @param Window Window to append.
 * @return TRUE on success.
 */
static BOOL AppendWindowPointer(LPWINDOW** Windows, UINT* Count, UINT* Capacity, LPWINDOW Window) {
    UINT NewCapacity;
    LPWINDOW* NewWindows;

    if (Windows == NULL || Count == NULL || Capacity == NULL) return FALSE;

    if (*Count >= *Capacity) {
        NewCapacity = (*Capacity == 0) ? 8 : (*Capacity * 2);
        NewWindows = (LPWINDOW*)KernelHeapRealloc(*Windows, sizeof(LPWINDOW) * NewCapacity);
        if (NewWindows == NULL) return FALSE;
        *Windows = NewWindows;
        *Capacity = NewCapacity;
    }

    (*Windows)[*Count] = Window;
    (*Count)++;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Snapshot direct children of one window.
 * @param Parent Parent window.
 * @param Children Receives allocated child pointer array.
 * @param ChildCount Receives number of children.
 * @return TRUE on success.
 */
/**
 * @brief Post a message to a window and all of its children.
 * @param This Starting window for broadcast.
 * @param Msg Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return TRUE on success.
 */
BOOL BroadcastMessageToWindow(LPWINDOW This, U32 Msg, U32 Param1, U32 Param2) {
    LPWINDOW* Pending = NULL;
    UINT PendingCount = 0;
    UINT PendingCapacity = 0;
    LPWINDOW* Recipients = NULL;
    UINT RecipientCount = 0;
    UINT RecipientCapacity = 0;
    LPWINDOW Window;
    LPWINDOW* Children = NULL;
    UINT ChildCount = 0;
    UINT Index;
    BOOL Success = TRUE;

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (AppendWindowPointer(&Pending, &PendingCount, &PendingCapacity, This) == FALSE) {
        return FALSE;
    }

    while (PendingCount > 0) {
        PendingCount--;
        Window = Pending[PendingCount];

        if (Window == NULL || Window->TypeID != KOID_WINDOW) continue;

        if (AppendWindowPointer(&Recipients, &RecipientCount, &RecipientCapacity, Window) == FALSE) {
            Success = FALSE;
            break;
        }

        if (DesktopSnapshotWindowChildren(Window, &Children, &ChildCount) == FALSE) {
            Success = FALSE;
            break;
        }

        for (Index = 0; Index < ChildCount; Index++) {
            if (AppendWindowPointer(&Pending, &PendingCount, &PendingCapacity, Children[Index]) == FALSE) {
                Success = FALSE;
                break;
            }
        }

        if (Children != NULL) {
            KernelHeapFree(Children);
            Children = NULL;
        }
        ChildCount = 0;

        if (Success == FALSE) {
            break;
        }
    }

    if (Success != FALSE) {
        for (Index = 0; Index < RecipientCount; Index++) {
            Window = Recipients[Index];

            if (Msg == EWM_DRAW) {
                (void)RequestWindowDraw((HANDLE)Window);
            } else {
                (void)PostMessage((HANDLE)Window, Msg, Param1, Param2);
            }
        }
    }

    if (Children != NULL) KernelHeapFree(Children);
    if (Pending != NULL) KernelHeapFree(Pending);
    if (Recipients != NULL) KernelHeapFree(Recipients);

    return Success;
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
BOOL WindowRectToScreenRect(HANDLE Handle, LPRECT WindowRect, LPRECT ScreenRect) {
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (WindowRect == NULL) return FALSE;
    if (ScreenRect == NULL) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    WindowRectToScreenRectLocked(This, WindowRect, ScreenRect);

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
BOOL ScreenRectToWindowRect(HANDLE Handle, LPRECT ScreenRect, LPRECT WindowRect) {
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (ScreenRect == NULL) return FALSE;
    if (WindowRect == NULL) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    GraphicsScreenRectToWindowRect(&(This->ScreenRect), ScreenRect, WindowRect);

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
    RECT LocalRect;
    RECT WindowRect;
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
    }
    else {
        WindowRect.X1 = 0;
        WindowRect.Y1 = 0;
        WindowRect.X2 = This->Rect.X2 - This->Rect.X1;
        WindowRect.Y2 = This->Rect.Y2 - This->Rect.Y1;

        if (GetWindowDrawableRectFromWindowRect(This, &WindowRect, &LocalRect) == FALSE) {
            UnlockMutex(&(This->Mutex));
            return FALSE;
        }

        FullWindow = TRUE;
        WindowRectToScreenRectLocked(This, &LocalRect, &Rect);
        RectRegionReset(&This->DirtyRegion);
        (void)RectRegionAddRect(&This->DirtyRegion, &Rect);
    }

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));
    UNUSED(FullWindow);

    if (This->WindowID == 0x53484252) {
    }

    return RequestWindowDraw(Handle);
}

/***************************************************************************/

/**
 * @brief Request one coalesced draw message for a window.
 * @param Handle Window handle.
 * @return TRUE on success.
 */
BOOL RequestWindowDraw(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    BOOL ShouldPost = FALSE;

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    if ((This->Status & WINDOW_STATUS_NEED_DRAW) == 0) {
        This->Status |= WINDOW_STATUS_NEED_DRAW;
        ShouldPost = TRUE;
    }

    UnlockMutex(&(This->Mutex));

    if (ShouldPost) {
        PostMessage(Handle, EWM_DRAW, 0, 0);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Snapshot one parent child order list while the parent mutex is held.
 * @param Parent Parent window whose children are snapshotted.
 * @param Entries Receives allocated child snapshots.
 * @param Count Receives number of child snapshots.
 * @return TRUE on success.
 */
static BOOL SnapshotWindowChildOrderLocked(LPWINDOW Parent, LPZ_ORDER_CHILD_SNAPSHOT* Entries, UINT* Count) {
    LPLISTNODE Node;
    LPZ_ORDER_CHILD_SNAPSHOT Snapshot;
    UINT Capacity = 0;
    UINT Index = 0;

    if (Entries == NULL || Count == NULL) return FALSE;

    *Entries = NULL;
    *Count = 0;

    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return FALSE;
    if (Parent->Children == NULL) return TRUE;

    for (Node = Parent->Children->First; Node != NULL; Node = Node->Next) {
        Capacity++;
    }

    if (Capacity == 0) return TRUE;

    Snapshot = KernelHeapAlloc(sizeof(Z_ORDER_CHILD_SNAPSHOT) * Capacity);
    if (Snapshot == NULL) return FALSE;

    for (Node = Parent->Children->First; Node != NULL; Node = Node->Next) {
        LPWINDOW Child = (LPWINDOW)Node;

        if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;

        Snapshot[Index].Window = Child;
        Snapshot[Index].Order = Child->Order;
        Index++;
    }

    *Entries = Snapshot;
    *Count = Index;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Invalidate one window subtree on the intersection with one screen rectangle.
 * @param Window Window whose subtree is invalidated.
 * @param ScreenRect Damage rectangle in screen coordinates.
 * @return TRUE when one intersection was invalidated.
 */
static BOOL InvalidateWindowTreeOnScreenIntersection(LPWINDOW Window, LPRECT ScreenRect) {
    RECT WindowScreenRect;
    RECT Intersection;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ScreenRect == NULL) return FALSE;
    if (GetWindowScreenRectSnapshot(Window, &WindowScreenRect) == FALSE) return FALSE;
    if (IntersectRect(&WindowScreenRect, ScreenRect, &Intersection) == FALSE) return FALSE;

    DesktopOverlayInvalidateWindowTreeRect(Window, &Intersection, FALSE);
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
    LPWINDOW Parent;
    LPZ_ORDER_CHILD_SNAPSHOT AffectedWindows = NULL;
    UINT AffectedWindowCount = 0;
    UINT Index;
    LPLISTNODE Node;
    I32 Order;
    I32 OldOrder;
    RECT DamageScreenRect;
    BOOL IsChild = FALSE;
    BOOL Reordered = FALSE;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;
    if (This->ParentWindow == NULL) return FALSE;

    Parent = This->ParentWindow;
    if (Parent->TypeID != KOID_WINDOW) return FALSE;
    if (GetWindowOrderSnapshot(This, &OldOrder) == FALSE) return FALSE;
    if (OldOrder == 0) return TRUE;
    if (GetWindowScreenRectSnapshot(This, &DamageScreenRect) == FALSE) return FALSE;

    LockMutex(&(Parent->Mutex), INFINITY);

    if (SnapshotWindowChildOrderLocked(Parent, &AffectedWindows, &AffectedWindowCount) == FALSE) {
        UnlockMutex(&(Parent->Mutex));
        return FALSE;
    }

    for (Node = Parent->Children->First, Order = 1; Node; Node = Node->Next) {
        That = (LPWINDOW)Node;
        if (That == This) {
            IsChild = TRUE;
            That->Order = 0;
        } else {
            That->Order = Order++;
        }
    }

    if (IsChild != FALSE) {
        ListSort(Parent->Children, SortWindows_Order);
        Reordered = TRUE;
    }

    UnlockMutex(&(Parent->Mutex));

    if (Reordered == FALSE) {
        if (AffectedWindows != NULL) {
            KernelHeapFree(AffectedWindows);
        }
        return FALSE;
    }

    for (Index = 0; Index < AffectedWindowCount; Index++) {
        LPWINDOW Window = AffectedWindows[Index].Window;

        if (Window == NULL || Window->TypeID != KOID_WINDOW) continue;

        if (Window == This) {
            (void)InvalidateWindowTreeOnScreenIntersection(Window, &DamageScreenRect);
            continue;
        }

        if (AffectedWindows[Index].Order >= OldOrder) continue;
        (void)InvalidateWindowTreeOnScreenIntersection(Window, &DamageScreenRect);
    }

    if (AffectedWindows != NULL) {
        KernelHeapFree(AffectedWindows);
    }

    return TRUE;
}

/***************************************************************************/
