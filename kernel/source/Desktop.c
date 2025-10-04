
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

#include "../include/GFX.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Mouse.h"
#include "../include/Process.h"

/***************************************************************************/

extern GRAPHICSCONTEXT VESAContext;

/***************************************************************************/

LPWINDOW NewWindow(void);
BOOL DeleteWindow(LPWINDOW);
U32 DesktopWindowFunc(HANDLE, U32, U32, U32);

/***************************************************************************/

static LIST MainDesktopChildren = {NULL, NULL, NULL, 0, KernelHeapAlloc, KernelHeapFree, NULL};

/***************************************************************************/

WINDOW MainDesktopWindow = {
    .ID = KOID_WINDOW,
    .References = 1,
    .OwnerProcess = &KernelProcess,
    .Next = NULL,
    .Prev = NULL,
    .Mutex = EMPTY_MUTEX,
    .Task = NULL,
    .Function = &DesktopWindowFunc,
    .Parent = NULL,
    .Children = &MainDesktopChildren,
    .Properties = NULL,
    .Rect = {0, 0, 639, 479},
    .ScreenRect = {0, 0, 639, 479},
    .InvalidRect = {0, 0, 0, 0},
    .WindowID = 0,
    .Style = 0,
    .Status = WINDOW_STATUS_VISIBLE,
    .Level = 0,
    .Order = 0
};

/***************************************************************************/

DESKTOP MainDesktop = {
    .ID = KOID_DESKTOP,
    .References = 1,
    .OwnerProcess = &KernelProcess,
    .Next = NULL,
    .Prev = NULL,
    .Mutex = EMPTY_MUTEX,
    .Task = NULL,
    .Graphics = &VESADriver,
    .Window = &MainDesktopWindow,
    .Capture = NULL,
    .Focus = NULL,
    .Order = 0
};

/***************************************************************************/

BRUSH Brush_Desktop = {KOID_BRUSH, 1, &KernelProcess, NULL, NULL, COLOR_DARK_CYAN, MAX_U32};
BRUSH Brush_High = {KOID_BRUSH, 1, &KernelProcess, NULL, NULL, 0x00FFFFFF, MAX_U32};
BRUSH Brush_Normal = {KOID_BRUSH, 1, &KernelProcess, NULL, NULL, 0x00A0A0A0, MAX_U32};
BRUSH Brush_HiShadow = {KOID_BRUSH, 1, &KernelProcess, NULL, NULL, 0x00404040, MAX_U32};
BRUSH Brush_LoShadow = {KOID_BRUSH, 1, &KernelProcess, NULL, NULL, 0x00000000, MAX_U32};
BRUSH Brush_Client = {KOID_BRUSH, 1, &KernelProcess, NULL, NULL, COLOR_WHITE, MAX_U32};
BRUSH Brush_Text_Normal = {KOID_BRUSH, 1, &KernelProcess, NULL, NULL, COLOR_BLACK, MAX_U32};
BRUSH Brush_Text_Select = {KOID_BRUSH, 1, &KernelProcess, NULL, NULL, COLOR_WHITE, MAX_U32};
BRUSH Brush_Selection = {KOID_BRUSH, 1, &KernelProcess, NULL, NULL, COLOR_DARK_BLUE, MAX_U32};
BRUSH Brush_Title_Bar = {KOID_BRUSH, 1, &KernelProcess, NULL, NULL, COLOR_DARK_BLUE, MAX_U32};
BRUSH Brush_Title_Bar_2 = {KOID_BRUSH, 1, &KernelProcess, NULL, NULL, COLOR_CYAN, MAX_U32};
BRUSH Brush_Title_Text = {KOID_BRUSH, 1, &KernelProcess, NULL, NULL, COLOR_WHITE, MAX_U32};

/***************************************************************************/

PEN Pen_Desktop = {KOID_PEN, 1, &KernelProcess, NULL, NULL, COLOR_DARK_CYAN, MAX_U32};
PEN Pen_High = {KOID_PEN, 1, &KernelProcess, NULL, NULL, 0x00FFFFFF, MAX_U32};
PEN Pen_Normal = {KOID_PEN, 1, &KernelProcess, NULL, NULL, 0x00A0A0A0, MAX_U32};
PEN Pen_HiShadow = {KOID_PEN, 1, &KernelProcess, NULL, NULL, 0x00404040, MAX_U32};
PEN Pen_LoShadow = {KOID_PEN, 1, &KernelProcess, NULL, NULL, 0x00000000, MAX_U32};
PEN Pen_Client = {KOID_PEN, 1, &KernelProcess, NULL, NULL, COLOR_WHITE, MAX_U32};
PEN Pen_Text_Normal = {KOID_PEN, 1, &KernelProcess, NULL, NULL, COLOR_BLACK, MAX_U32};
PEN Pen_Text_Select = {KOID_PEN, 1, &KernelProcess, NULL, NULL, COLOR_WHITE, MAX_U32};
PEN Pen_Selection = {KOID_PEN, 1, &KernelProcess, NULL, NULL, COLOR_DARK_BLUE, MAX_U32};
PEN Pen_Title_Bar = {KOID_PEN, 1, &KernelProcess, NULL, NULL, COLOR_DARK_BLUE, MAX_U32};
PEN Pen_Title_Bar_2 = {KOID_PEN, 1, &KernelProcess, NULL, NULL, COLOR_CYAN, MAX_U32};
PEN Pen_Title_Text = {KOID_PEN, 1, &KernelProcess, NULL, NULL, COLOR_WHITE, MAX_U32};

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
    if (This->ID != KOID_GRAPHICSCONTEXT) return FALSE;

    //-------------------------------------
    // Lock access to the context

    LockMutex(&(This->Mutex), INFINITY);

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

    This = (LPDESKTOP)KernelHeapAlloc(sizeof(DESKTOP));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(DESKTOP));

    InitMutex(&(This->Mutex));

    This->ID = KOID_DESKTOP;
    This->References = KOID_DESKTOP;
    This->Task = GetCurrentTask();
    This->Graphics = &VESADriver;

    WindowInfo.Header.Size = sizeof(WINDOWINFO);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Parent = NULL;
    WindowInfo.Function = DesktopWindowFunc;
    WindowInfo.Style = 0;
    WindowInfo.ID = 0;
    WindowInfo.WindowPosition.X = 0;
    WindowInfo.WindowPosition.Y = 0;
    WindowInfo.WindowSize.X = 800;
    WindowInfo.WindowSize.Y = 600;

    This->Window = CreateWindow(&WindowInfo);

    if (This->Window == NULL) {
        KernelHeapFree(This);
        return NULL;
    }

    //-------------------------------------
    // Add the desktop to the kernel's list

    LockMutex(MUTEX_KERNEL, INFINITY);

    ListAddHead(Kernel.Desktop, This);

    GetCurrentProcess()->Desktop = This;

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
    LPDESKTOP Desktop;
    LPLISTNODE Node;
    I32 Order;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != KOID_DESKTOP) return FALSE;

    //-------------------------------------
    // Lock access to resources

    LockMutex(MUTEX_KERNEL, INFINITY);
    LockMutex(&(This->Mutex), INFINITY);

    //-------------------------------------
    // Sort the kernel's desktop list

    for (Node = Kernel.Desktop->First, Order = 1; Node; Node = Node->Next) {
        Desktop = (LPDESKTOP)Node;
        if (Desktop == This)
            Desktop->Order = 0;
        else
            Desktop->Order = Order++;
    }

    ListSort(Kernel.Desktop, SortDesktops_Order);

    ModeInfo.Header.Size = sizeof(ModeInfo);
    ModeInfo.Header.Version = EXOS_ABI_VERSION;
    ModeInfo.Header.Flags = 0;
    ModeInfo.Width = 1024;
    ModeInfo.Height = 768;
    ModeInfo.BitsPerPixel = 24;

    DEBUG(TEXT("[ShowDesktop] Setting gfx mode %ux%u"), ModeInfo.Width, ModeInfo.Height);

    This->Graphics->Command(DF_GFX_SETMODE, (U32)&ModeInfo);

    // PostMessage((HANDLE) This->Window, EWM_DRAW, 0, 0);

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));
    UnlockMutex(MUTEX_KERNEL);

    return TRUE;
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

    This->ID = KOID_WINDOW;
    This->References = 1;
    This->Properties = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
    This->Children = NewList(NULL, KernelHeapAlloc, KernelHeapFree);

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

    if (This->ID != KOID_WINDOW) return FALSE;
    if (This->Parent == NULL) return FALSE;

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

    LockMutex(&(This->Parent->Mutex), INFINITY);

    ListRemove(This->Parent->Children, This);

    UnlockMutex(&(This->Parent->Mutex));

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
    if (Start->ID != KOID_WINDOW) return NULL;

    if (Target == NULL) return NULL;
    if (Target->ID != KOID_WINDOW) return NULL;

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
        if (Parent->ID != KOID_WINDOW) return NULL;
    }

    This = NewWindow();

    if (This == NULL) return NULL;

    This->Task = GetCurrentTask();
    This->Parent = Parent;
    This->Function = Info->Function;
    This->WindowID = Info->ID;
    This->Style = Info->Style;
    This->Rect.X1 = Info->WindowPosition.X;
    This->Rect.Y1 = Info->WindowPosition.Y;
    This->Rect.X2 = Info->WindowPosition.X + (Info->WindowSize.X - 1);
    This->Rect.Y2 = Info->WindowPosition.Y + (Info->WindowSize.Y - 1);
    This->ScreenRect = This->Rect;
    This->InvalidRect = This->Rect;

    if (This->Parent == NULL) {
        SAFE_USE(Desktop) This->Parent = Desktop->Window;
    }

    SAFE_USE(This->Parent) {
        LockMutex(&(This->Parent->Mutex), INFINITY);

        This->ScreenRect.X1 = This->Parent->ScreenRect.X1 + This->Rect.X1;
        This->ScreenRect.Y1 = This->Parent->ScreenRect.Y1 + This->Rect.Y1;
        This->ScreenRect.X2 = This->Parent->ScreenRect.X1 + This->Rect.X2;
        This->ScreenRect.Y2 = This->Parent->ScreenRect.Y1 + This->Rect.Y2;

        This->InvalidRect.X1 = This->Parent->ScreenRect.X1 + This->Rect.X1;
        This->InvalidRect.Y1 = This->Parent->ScreenRect.Y1 + This->Rect.Y1;
        This->InvalidRect.X2 = This->Parent->ScreenRect.X1 + This->Rect.X2;
        This->InvalidRect.Y2 = This->Parent->ScreenRect.Y1 + This->Rect.Y2;

        ListAddHead(This->Parent->Children, This);

        //-------------------------------------
        // Compute the level of the window

        for (Win = This->Parent; Win; Win = Win->Parent) This->Level++;

        UnlockMutex(&(This->Parent->Mutex));
    }

    //-------------------------------------
    // Tell the window it is being created

    SendMessage((HANDLE)This, EWM_CREATE, 0, 0);

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
    if (This->ID != KOID_WINDOW) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    Task = This->Task;
    if (Task != NULL && Task->ID == KOID_TASK) {
        Process = Task->Process;

        SAFE_USE_VALID_ID(Process, KOID_PROCESS) {
            Desktop = Process->Desktop;

            SAFE_USE(Desktop) {
                if (Desktop->ID != KOID_DESKTOP) Desktop = NULL;
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
BOOL BroadCastMessage(LPWINDOW This, U32 Msg, U32 Param1, U32 Param2) {
    LPLISTNODE Node;

    if (This == NULL) return NULL;
    if (This->ID != KOID_WINDOW) return NULL;

    LockMutex(&(This->Mutex), INFINITY);

    PostMessage((HANDLE)This, Msg, Param1, Param2);

    for (Node = This->Children->First; Node; Node = Node->Next) {
        BroadCastMessage((LPWINDOW)Node, Msg, Param1, Param2);
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
    if (This->ID != KOID_WINDOW) return FALSE;

    if (Src == NULL) return FALSE;
    if (Dst == NULL) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    Dst->X1 = This->ScreenRect.X1 + Src->X1;
    Dst->Y1 = This->ScreenRect.Y1 + Src->Y1;
    Dst->X2 = This->ScreenRect.X1 + Src->X2;
    Dst->Y2 = This->ScreenRect.Y1 + Src->Y2;

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
    if (This->ID != KOID_WINDOW) return FALSE;

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

    if (This == NULL) return FALSE;
    if (This->ID != KOID_WINDOW) return FALSE;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    SAFE_USE(Src) {
        WindowRectToScreenRect(Handle, Src, &Rect);

        if (Rect.X1 < This->InvalidRect.X1) This->InvalidRect.X1 = Rect.X1;
        if (Rect.Y1 < This->InvalidRect.Y1) This->InvalidRect.Y1 = Rect.Y1;
        if (Rect.X2 > This->InvalidRect.X2) This->InvalidRect.X2 = Rect.X2;
        if (Rect.Y2 > This->InvalidRect.Y2) This->InvalidRect.Y2 = Rect.Y2;
    } else {
        This->InvalidRect.X1 = This->ScreenRect.X1;
        This->InvalidRect.Y1 = This->ScreenRect.Y1;
        This->InvalidRect.X2 = This->ScreenRect.X2;
        This->InvalidRect.Y2 = This->ScreenRect.Y2;
    }

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
    I32 Order;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != KOID_WINDOW) return FALSE;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    if (This->Parent == NULL) goto Out;

    //-------------------------------------
    // Invalidate hidden regions

    for (Node = This->Prev; Node; Node = Node->Prev) {
        That = (LPWINDOW)Node;
        if (RectInRect(&(This->ScreenRect), &(That->ScreenRect))) {
            ScreenRectToWindowRect((HANDLE)That, &(That->ScreenRect), &Rect);
            InvalidateWindowRect((HANDLE)This, &Rect);
        }
    }

    //-------------------------------------
    // Reorder the windows

    for (Node = This->Parent->Children->First, Order = 1; Node; Node = Node->Next) {
        That = (LPWINDOW)Node;
        if (That == This)
            That->Order = 0;
        else
            That->Order = Order++;
    }

    ListSort(This->Parent->Children, SortWindows_Order);

    //-------------------------------------
    // Tell the window it needs redraw

    BroadCastMessage(This, EWM_DRAW, 0, 0);

Out:

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Show or hide a window and its visible children.
 * @param Handle Window handle.
 * @param ShowHide TRUE to show, FALSE to hide.
 * @return TRUE on success.
 */
BOOL ShowWindow(HANDLE Handle, BOOL ShowHide) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPWINDOW Child;
    LPLISTNODE Node;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != KOID_WINDOW) return FALSE;

    //-------------------------------------
    // Send appropriate messages to the window

    This->Style |= EWS_VISIBLE;
    This->Status |= WINDOW_STATUS_VISIBLE;

    PostMessage(Handle, EWM_SHOW, 0, 0);
    PostMessage(Handle, EWM_DRAW, 0, 0);

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    for (Node = This->Children->First; Node; Node = Node->Next) {
        Child = (LPWINDOW)Node;
        if (Child->Style & EWS_VISIBLE) {
            ShowWindow((HANDLE)Child, ShowHide);
        }
    }

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Obtain the size of a window in its own coordinates.
 * @param Handle Window handle.
 * @param Rect Destination rectangle.
 * @return TRUE on success.
 */
BOOL GetWindowRect(HANDLE Handle, LPRECT Rect) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != KOID_WINDOW) return FALSE;

    if (Rect == NULL) return FALSE;

    //-------------------------------------
    // Lock access to the window

    LockMutex(&(This->Mutex), INFINITY);

    Rect->X1 = 0;
    Rect->Y1 = 0;
    Rect->X2 = This->Rect.X2 - This->Rect.X1;
    Rect->Y2 = This->Rect.Y2 - This->Rect.Y1;

    //-------------------------------------
    // Unlock access to the window

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Move a window to a new position.
 * @param Handle Window handle.
 * @param Position New position.
 * @return TRUE on success.
 */
BOOL MoveWindow(HANDLE Handle, LPPOINT Position) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != KOID_WINDOW) return FALSE;

    if (Position == NULL) return FALSE;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Resize a window.
 * @param Handle Window handle.
 * @param Size New size.
 * @return TRUE on success.
 */
BOOL SizeWindow(HANDLE Handle, LPPOINT Size) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != KOID_WINDOW) return FALSE;

    if (Size == NULL) return FALSE;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve the parent of a window.
 * @param Handle Window handle.
 * @return Handle of the parent window.
 */
HANDLE GetWindowParent(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != KOID_WINDOW) return FALSE;

    return (HANDLE)This->Parent;
}

/***************************************************************************/

/**
 * @brief Set a custom property on a window.
 * @param Handle Window handle.
 * @param Name Property name.
 * @param Value Property value.
 * @return Previous property value or 0.
 */
U32 SetWindowProp(HANDLE Handle, LPCSTR Name, U32 Value) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPLISTNODE Node;
    LPPROPERTY Prop;
    U32 OldValue = 0;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return 0;
    if (This->ID != KOID_WINDOW) return 0;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    for (Node = This->Properties->First; Node; Node = Node->Next) {
        Prop = (LPPROPERTY)Node;
        if (StringCompareNC(Prop->Name, Name) == 0) {
            OldValue = Prop->Value;
            Prop->Value = Value;
            goto Out;
        }
    }

    //-------------------------------------
    // Add the property to the window

    Prop = (LPPROPERTY)KernelHeapAlloc(sizeof(PROPERTY));

    SAFE_USE(Prop) {
        StringCopy(Prop->Name, Name);
        Prop->Value = Value;
        ListAddItem(This->Properties, Prop);
    }

Out:

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    return OldValue;
}

/***************************************************************************/

/**
 * @brief Retrieve a custom property from a window.
 * @param Handle Window handle.
 * @param Name Property name.
 * @return Property value or 0 if not found.
 */
U32 GetWindowProp(HANDLE Handle, LPCSTR Name) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPLISTNODE Node = NULL;
    LPPROPERTY Prop = NULL;
    U32 Value = 0;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != KOID_WINDOW) return FALSE;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    //-------------------------------------
    // Search the list of properties

    for (Node = This->Properties->First; Node; Node = Node->Next) {
        Prop = (LPPROPERTY)Node;
        if (StringCompareNC(Prop->Name, Name) == 0) {
            Value = Prop->Value;
            goto Out;
        }
    }

Out:

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    return Value;
}

/***************************************************************************/

/**
 * @brief Obtain a graphics context for a window.
 * @param Handle Window handle.
 * @return Handle to a graphics context or NULL.
 */
HANDLE GetWindowGC(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPGRAPHICSCONTEXT Context;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->ID != KOID_WINDOW) return NULL;

    Context = &VESAContext;

    ResetGraphicsContext(Context);

    //-------------------------------------
    // Set the origin of the context

    LockMutex(&(Context->Mutex), INFINITY);

    Context->Origin.X = This->ScreenRect.X1;
    Context->Origin.Y = This->ScreenRect.Y1;

    /*
      Context->LoClip.X = This->ScreenRect.X1;
      Context->LoClip.Y = This->ScreenRect.Y1;
      Context->HiClip.X = This->ScreenRect.X2;
      Context->HiClip.Y = This->ScreenRect.Y2;
    */

    UnlockMutex(&(Context->Mutex));

    return (HANDLE)Context;
}

/***************************************************************************/

/**
 * @brief Release a previously obtained graphics context.
 * @param Handle Graphics context handle.
 * @return TRUE on success.
 */
BOOL ReleaseWindowGC(HANDLE Handle) {
    LPGRAPHICSCONTEXT This = (LPGRAPHICSCONTEXT)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != KOID_GRAPHICSCONTEXT) return FALSE;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Prepare a window for drawing and return its graphics context.
 * @param Handle Window handle.
 * @return Graphics context or NULL on failure.
 */
HANDLE BeginWindowDraw(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    HANDLE GC = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->ID != KOID_WINDOW) return NULL;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    GC = GetWindowGC(Handle);

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    return GC;
}

/***************************************************************************/

/**
 * @brief Finish drawing operations on a window.
 * @param Handle Window handle.
 * @return TRUE on success.
 */
BOOL EndWindowDraw(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->ID != KOID_WINDOW) return NULL;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve a system brush by index.
 * @param Index Brush identifier.
 * @return Handle to the brush.
 */
HANDLE GetSystemBrush(U32 Index) {
    switch (Index) {
        case SM_COLOR_DESKTOP:
            return (HANDLE)&Brush_Desktop;
        case SM_COLOR_HIGHLIGHT:
            return (HANDLE)&Brush_High;
        case SM_COLOR_NORMAL:
            return (HANDLE)&Brush_Normal;
        case SM_COLOR_LIGHT_SHADOW:
            return (HANDLE)&Brush_HiShadow;
        case SM_COLOR_DARK_SHADOW:
            return (HANDLE)&Brush_LoShadow;
        case SM_COLOR_CLIENT:
            return (HANDLE)&Brush_Client;
        case SM_COLOR_TEXT_NORMAL:
            return (HANDLE)&Brush_Text_Normal;
        case SM_COLOR_TEXT_SELECTED:
            return (HANDLE)&Brush_Text_Select;
        case SM_COLOR_SELECTION:
            return (HANDLE)&Brush_Selection;
        case SM_COLOR_TITLE_BAR:
            return (HANDLE)&Brush_Title_Bar;
        case SM_COLOR_TITLE_BAR_2:
            return (HANDLE)&Brush_Title_Bar_2;
        case SM_COLOR_TITLE_TEXT:
            return (HANDLE)&Brush_Title_Text;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Retrieve a system pen by index.
 * @param Index Pen identifier.
 * @return Handle to the pen.
 */
HANDLE GetSystemPen(U32 Index) {
    switch (Index) {
        case SM_COLOR_DESKTOP:
            return (HANDLE)&Pen_Desktop;
        case SM_COLOR_HIGHLIGHT:
            return (HANDLE)&Pen_High;
        case SM_COLOR_NORMAL:
            return (HANDLE)&Pen_Normal;
        case SM_COLOR_LIGHT_SHADOW:
            return (HANDLE)&Pen_HiShadow;
        case SM_COLOR_DARK_SHADOW:
            return (HANDLE)&Pen_LoShadow;
        case SM_COLOR_CLIENT:
            return (HANDLE)&Pen_Client;
        case SM_COLOR_TEXT_NORMAL:
            return (HANDLE)&Pen_Text_Normal;
        case SM_COLOR_TEXT_SELECTED:
            return (HANDLE)&Pen_Text_Select;
        case SM_COLOR_SELECTION:
            return (HANDLE)&Pen_Selection;
        case SM_COLOR_TITLE_BAR:
            return (HANDLE)&Pen_Title_Bar;
        case SM_COLOR_TITLE_BAR_2:
            return (HANDLE)&Pen_Title_Bar_2;
        case SM_COLOR_TITLE_TEXT:
            return (HANDLE)&Pen_Title_Text;
    }

    return NULL;
}

/***************************************************************************/

/**
 * @brief Select a brush into a graphics context.
 * @param GC Graphics context handle.
 * @param Brush Brush handle to select.
 * @return Previous brush handle.
 */
HANDLE SelectBrush(HANDLE GC, HANDLE Brush) {
    LPGRAPHICSCONTEXT Context;
    LPBRUSH NewBrush;
    LPBRUSH OldBrush;

    if (GC == NULL) return NULL;

    Context = (LPGRAPHICSCONTEXT)GC;
    NewBrush = (LPBRUSH)Brush;

    if (Context->ID != KOID_GRAPHICSCONTEXT) return NULL;

    LockMutex(&(Context->Mutex), INFINITY);

    OldBrush = Context->Brush;
    Context->Brush = NewBrush;

    UnlockMutex(&(Context->Mutex));

    return (HANDLE)OldBrush;
}

/***************************************************************************/

/**
 * @brief Select a pen into a graphics context.
 * @param GC Graphics context handle.
 * @param Pen Pen handle to select.
 * @return Previous pen handle.
 */
HANDLE SelectPen(HANDLE GC, HANDLE Pen) {
    LPGRAPHICSCONTEXT Context;
    LPPEN NewPen;
    LPPEN OldPen;

    if (GC == NULL) return NULL;

    Context = (LPGRAPHICSCONTEXT)GC;
    NewPen = (LPPEN)Pen;

    if (Context->ID != KOID_GRAPHICSCONTEXT) return NULL;

    LockMutex(&(Context->Mutex), INFINITY);

    OldPen = Context->Pen;
    Context->Pen = NewPen;

    UnlockMutex(&(Context->Mutex));

    return (HANDLE)OldPen;
}

/***************************************************************************/

/**
 * @brief Create a brush from brush information.
 * @param BrushInfo Brush parameters.
 * @return Handle to the created brush or NULL.
 */
HANDLE CreateBrush(LPBRUSHINFO BrushInfo) {
    LPBRUSH Brush = NULL;

    if (BrushInfo == NULL) return NULL;

    Brush = (LPBRUSH)KernelHeapAlloc(sizeof(BRUSH));
    if (Brush == NULL) return NULL;

    MemorySet(Brush, 0, sizeof(BRUSH));

    Brush->ID = KOID_BRUSH;
    Brush->References = 1;
    Brush->Color = BrushInfo->Color;
    Brush->Pattern = BrushInfo->Pattern;

    return (HANDLE)Brush;
}

/***************************************************************************/

/**
 * @brief Create a pen from pen information.
 * @param PenInfo Pen parameters.
 * @return Handle to the created pen or NULL.
 */
HANDLE CreatePen(LPPENINFO PenInfo) {
    LPPEN Pen = NULL;

    if (PenInfo == NULL) return NULL;

    Pen = (LPPEN)KernelHeapAlloc(sizeof(PEN));
    if (Pen == NULL) return NULL;

    MemorySet(Pen, 0, sizeof(PEN));

    Pen->ID = KOID_PEN;
    Pen->References = 1;
    Pen->Color = PenInfo->Color;
    Pen->Pattern = PenInfo->Pattern;

    return (HANDLE)Pen;
}

/***************************************************************************/

/**
 * @brief Set a pixel in a graphics context.
 * @param PixelInfo Pixel parameters.
 * @return TRUE on success.
 */
BOOL SetPixel(LPPIXELINFO PixelInfo) {
    LPGRAPHICSCONTEXT Context;

    //-------------------------------------
    // Check validity of parameters

    if (PixelInfo == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)PixelInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->ID != KOID_GRAPHICSCONTEXT) return FALSE;

    PixelInfo->X = Context->Origin.X + PixelInfo->X;
    PixelInfo->Y = Context->Origin.Y + PixelInfo->Y;

    Context->Driver->Command(DF_GFX_SETPIXEL, (U32)PixelInfo);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve a pixel from a graphics context.
 * @param PixelInfo Pixel parameters.
 * @return TRUE on success.
 */
BOOL GetPixel(LPPIXELINFO PixelInfo) {
    LPGRAPHICSCONTEXT Context;

    //-------------------------------------
    // Check validity of parameters

    if (PixelInfo == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)PixelInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->ID != KOID_GRAPHICSCONTEXT) return FALSE;

    PixelInfo->X = Context->Origin.X + PixelInfo->X;
    PixelInfo->Y = Context->Origin.Y + PixelInfo->Y;

    Context->Driver->Command(DF_GFX_GETPIXEL, (U32)PixelInfo);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw a line using the current pen.
 * @param LineInfo Line parameters.
 * @return TRUE on success.
 */
BOOL Line(LPLINEINFO LineInfo) {
    LPGRAPHICSCONTEXT Context;

    //-------------------------------------
    // Check validity of parameters

    if (LineInfo == NULL) return FALSE;
    if (LineInfo->Header.Size < sizeof(LINEINFO)) return FALSE;

    Context = (LPGRAPHICSCONTEXT)LineInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->ID != KOID_GRAPHICSCONTEXT) return FALSE;

    LineInfo->X1 = Context->Origin.X + LineInfo->X1;
    LineInfo->Y1 = Context->Origin.Y + LineInfo->Y1;
    LineInfo->X2 = Context->Origin.X + LineInfo->X2;
    LineInfo->Y2 = Context->Origin.Y + LineInfo->Y2;

    Context->Driver->Command(DF_GFX_LINE, (U32)LineInfo);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw a rectangle using current pen and brush.
 * @param RectInfo Rectangle parameters.
 * @return TRUE on success.
 */
BOOL Rectangle(LPRECTINFO RectInfo) {
    LPGRAPHICSCONTEXT Context;

    //-------------------------------------
    // Check validity of parameters

    if (RectInfo == NULL) return FALSE;
    if (RectInfo->Header.Size < sizeof(RECTINFO)) return FALSE;

    Context = (LPGRAPHICSCONTEXT)RectInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->ID != KOID_GRAPHICSCONTEXT) return FALSE;

    RectInfo->X1 = Context->Origin.X + RectInfo->X1;
    RectInfo->Y1 = Context->Origin.Y + RectInfo->Y1;
    RectInfo->X2 = Context->Origin.X + RectInfo->X2;
    RectInfo->Y2 = Context->Origin.Y + RectInfo->Y2;

    Context->Driver->Command(DF_GFX_RECTANGLE, (U32)RectInfo);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Determine which window is under a given screen position.
 * @param Handle Starting window handle.
 * @param Position Screen coordinates to test.
 * @return Handle to the window or NULL.
 */
HANDLE WindowHitTest(HANDLE Handle, LPPOINT Position) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPWINDOW Target = NULL;
    LPLISTNODE Node = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->ID != KOID_WINDOW) return NULL;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    //-------------------------------------
    // Test if one child window passes hit test

    for (Node = This->Children->First; Node; Node = Node->Next) {
        Target = (LPWINDOW)WindowHitTest((HANDLE)Node, Position);
        if (Target != NULL) goto Out;
    }

    //-------------------------------------
    // Test if this window passes hit test

    Target = NULL;

    if ((This->Status & WINDOW_STATUS_VISIBLE) == 0) goto Out;

    if (Position->X >= This->ScreenRect.X1 && Position->X <= This->ScreenRect.X2 &&
        Position->Y >= This->ScreenRect.Y1 && Position->Y <= This->ScreenRect.Y2) {
        Target = This;
    }

Out:

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    return (HANDLE)Target;
}

/***************************************************************************/

/**
 * @brief Default window procedure for unhandled messages.
 * @param Window Window handle.
 * @param Message Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return Message-specific result.
 */
U32 DefWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    UNUSED(Param1);
    UNUSED(Param2);

    switch (Message) {
        case EWM_CREATE: {
        } break;

        case EWM_DELETE: {
        } break;

        case EWM_DRAW: {
            HANDLE GC;
            RECTINFO RectInfo;
            RECT Rect;

            GC = BeginWindowDraw(Window);

            if (GC) {
                GetWindowRect(Window, &Rect);

                RectInfo.Header.Size = sizeof(RectInfo);
                RectInfo.Header.Version = EXOS_ABI_VERSION;
                RectInfo.Header.Flags = 0;
                RectInfo.GC = GC;
                RectInfo.X1 = Rect.X1;
                RectInfo.Y1 = Rect.Y1;
                RectInfo.X2 = Rect.X2;
                RectInfo.Y2 = Rect.Y2;

                SelectBrush(GC, GetSystemBrush(SM_COLOR_NORMAL));
                Rectangle(&RectInfo);

                EndWindowDraw(Window);
            }
        } break;
    }

    return 0;
}

/***************************************************************************/

static STR Prop_MouseX[] = "MOUSEX";
static STR Prop_MouseY[] = "MOUSEY";

/***************************************************************************/

/*
static U32 DrawMouseCursor(HANDLE GC, I32 X, I32 Y, BOOL OnOff) {
    LINEINFO LineInfo;

    if (OnOff) {
        SelectPen(GC, GetSystemPen(SM_COLOR_HIGHLIGHT));
    } else {
        SelectPen(GC, GetSystemPen(SM_COLOR_TEXT_NORMAL));
    }

    LineInfo.GC = GC;

    LineInfo.X1 = X - 4;
    LineInfo.Y1 = Y;
    LineInfo.X2 = X + 4;
    LineInfo.Y2 = Y;
    Line(&LineInfo);

    LineInfo.X1 = X;
    LineInfo.Y1 = Y - 4;
    LineInfo.X2 = X;
    LineInfo.Y2 = Y + 4;
    Line(&LineInfo);

    return 0;
}
*/

/***************************************************************************/

/*
static U32 DrawButtons(HANDLE GC) {
    LINEINFO LineInfo;
    U32 Buttons = SerialMouseDriver.Command(DF_MOUSE_GETBUTTONS, 0);

    if (Buttons & MB_LEFT) {
        SelectPen(GC, GetSystemPen(SM_COLOR_TITLE_BAR_2));

        LineInfo.GC = GC;

        LineInfo.X1 = 10;
        LineInfo.Y1 = 0;
        LineInfo.X2 = 20;
        LineInfo.Y2 = 0;
        Line(&LineInfo);

        LineInfo.X1 = 10;
        LineInfo.Y1 = 1;
        LineInfo.X2 = 20;
        LineInfo.Y2 = 1;
        Line(&LineInfo);

        LineInfo.X1 = 10;
        LineInfo.Y1 = 2;
        LineInfo.X2 = 20;
        LineInfo.Y2 = 2;
        Line(&LineInfo);

        LineInfo.X1 = 10;
        LineInfo.Y1 = 3;
        LineInfo.X2 = 20;
        LineInfo.Y2 = 3;
        Line(&LineInfo);
    }

    return 1;
}
*/

/***************************************************************************/

/**
 * @brief Window procedure for the desktop window.
 * @param Window Desktop window handle.
 * @param Message Message identifier.
 * @param Param1 First parameter.
 * @param Param2 Second parameter.
 * @return Message-specific result.
 */
U32 DesktopWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    switch (Message) {
        case EWM_CREATE: {
            SetWindowProp(Window, Prop_MouseX, 0);
            SetWindowProp(Window, Prop_MouseY, 0);
        } break;

        case EWM_DRAW: {
            HANDLE GC;
            RECTINFO RectInfo;
            RECT Rect;

            GC = BeginWindowDraw(Window);

            if (GC) {
                GetWindowRect(Window, &Rect);

                RectInfo.Header.Size = sizeof(RectInfo);
                RectInfo.Header.Version = EXOS_ABI_VERSION;
                RectInfo.Header.Flags = 0;
                RectInfo.GC = GC;
                RectInfo.X1 = Rect.X1;
                RectInfo.Y1 = Rect.Y1;
                RectInfo.X2 = Rect.X2;
                RectInfo.Y2 = Rect.Y2;

                SelectPen(GC, NULL);
                SelectBrush(GC, GetSystemBrush(SM_COLOR_DESKTOP));

                Rectangle(&RectInfo);

                EndWindowDraw(Window);
            }
        } break;

        case EWM_MOUSEMOVE: {
            // U32 OldMouseX = GetWindowProp(Window, Prop_MouseX);
            // U32 OldMouseY = GetWindowProp(Window, Prop_MouseY);
            // I32 MouseX = SIGNED(Param1);
            // I32 MouseY = SIGNED(Param2);
            // LPWINDOW Target;
            // POINT Position;
            HANDLE GC = GC = GetWindowGC(Window);

            SAFE_USE(GC) {
                // DrawMouseCursor(GC, SIGNED(OldMouseX), SIGNED(OldMouseY),
                // FALSE); DrawMouseCursor(GC, MouseX, MouseY, TRUE);
                ReleaseWindowGC(GC);
            }

            /*
              SetWindowProp(Window, Prop_MouseX, Param1);
              SetWindowProp(Window, Prop_MouseY, Param2);

              Target = (LPWINDOW) WindowHitTest(Window, &Position);

              SAFE_USE(Target)
              {
                Position.X = SIGNED(Param1) - Target->ScreenRect.X1;
                Position.Y = SIGNED(Param2) - Target->ScreenRect.Y1;

                SendMessage
                (
                  (HANDLE) Target,
                  EWM_MOUSEMOVE,
                  UNSIGNED(Position.X),
                  UNSIGNED(Position.Y)
                );
              }
            */
        } break;

        case EWM_MOUSEDOWN: {
            POINT Position;
            LPWINDOW Target;
            I32 X, Y;

            X = SerialMouseDriver.Command(DF_MOUSE_GETDELTAX, 0);
            Y = SerialMouseDriver.Command(DF_MOUSE_GETDELTAY, 0);

            Position.X = SIGNED(X);
            Position.Y = SIGNED(Y);

            Target = (LPWINDOW)WindowHitTest(Window, &Position);

            if (Target) {
                SendMessage((HANDLE)Target, EWM_MOUSEDOWN, Param1, Param2);
            }
        } break;

        default:
            return DefWindowFunc(Window, Message, Param1, Param2);
    }

    return 0;
}
