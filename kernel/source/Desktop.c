
// Desktop.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#include "GFX.h"
#include "Kernel.h"
#include "Mouse.h"
#include "Process.h"

/***************************************************************************/

extern GRAPHICSCONTEXT VESAContext;

/***************************************************************************/

LPWINDOW NewWindow();
void DeleteWindow(LPWINDOW);
U32 DesktopWindowFunc(HANDLE, U32, U32, U32);

/***************************************************************************/

static LIST MainDesktopChildren = {NULL,           NULL,          NULL, 0,
                                   KernelMemAlloc, KernelMemFree, NULL};

/***************************************************************************/

static WINDOW MainDesktopWindow = {
    ID_WINDOW,
    1,  // ID, references
    NULL,
    NULL,                   // Next, previous
    EMPTY_MUTEX,        // Window semaphore
    &KernelTask,            // Task
    &DesktopWindowFunc,     // Function
    NULL,                   // Parent
    &MainDesktopChildren,   // Children
    NULL,                   // Properties
    {0, 0, 639, 479},       // Rect
    {0, 0, 639, 479},       // ScreenRect
    {0, 0, 0, 0},           // InvalidRect
    0,                      // WindowID
    0,                      // Style
    WINDOW_STATUS_VISIBLE,  // Status
    0,                      // Level
    0                       // Order
};

/***************************************************************************/

DESKTOP MainDesktop = {
    ID_DESKTOP,
    1,  // ID, references
    NULL,
    NULL,                // Next, previous
    EMPTY_MUTEX,     // Desktop semaphore
    &KernelTask,         // This desktop's owner task
    &VESADriver,         // This desktop's graphics driver
    &MainDesktopWindow,  // Window
    NULL,                // Capture
    NULL,                // Focus
    0                    // Order
};

/***************************************************************************/

BRUSH Brush_Desktop = {ID_BRUSH, 1, NULL, NULL, COLOR_DARK_CYAN, MAX_U32};
BRUSH Brush_High = {ID_BRUSH, 1, NULL, NULL, 0x00FFFFFF, MAX_U32};
BRUSH Brush_Normal = {ID_BRUSH, 1, NULL, NULL, 0x00A0A0A0, MAX_U32};
BRUSH Brush_HiShadow = {ID_BRUSH, 1, NULL, NULL, 0x00404040, MAX_U32};
BRUSH Brush_LoShadow = {ID_BRUSH, 1, NULL, NULL, 0x00000000, MAX_U32};
BRUSH Brush_Client = {ID_BRUSH, 1, NULL, NULL, COLOR_WHITE, MAX_U32};
BRUSH Brush_Text_Normal = {ID_BRUSH, 1, NULL, NULL, COLOR_BLACK, MAX_U32};
BRUSH Brush_Text_Select = {ID_BRUSH, 1, NULL, NULL, COLOR_WHITE, MAX_U32};
BRUSH Brush_Selection = {ID_BRUSH, 1, NULL, NULL, COLOR_DARK_BLUE, MAX_U32};
BRUSH Brush_Title_Bar = {ID_BRUSH, 1, NULL, NULL, COLOR_DARK_BLUE, MAX_U32};
BRUSH Brush_Title_Bar_2 = {ID_BRUSH, 1, NULL, NULL, COLOR_CYAN, MAX_U32};
BRUSH Brush_Title_Text = {ID_BRUSH, 1, NULL, NULL, COLOR_WHITE, MAX_U32};

/***************************************************************************/

PEN Pen_Desktop = {ID_PEN, 1, NULL, NULL, COLOR_DARK_CYAN, MAX_U32};
PEN Pen_High = {ID_PEN, 1, NULL, NULL, 0x00FFFFFF, MAX_U32};
PEN Pen_Normal = {ID_PEN, 1, NULL, NULL, 0x00A0A0A0, MAX_U32};
PEN Pen_HiShadow = {ID_PEN, 1, NULL, NULL, 0x00404040, MAX_U32};
PEN Pen_LoShadow = {ID_PEN, 1, NULL, NULL, 0x00000000, MAX_U32};
PEN Pen_Client = {ID_PEN, 1, NULL, NULL, COLOR_WHITE, MAX_U32};
PEN Pen_Text_Normal = {ID_PEN, 1, NULL, NULL, COLOR_BLACK, MAX_U32};
PEN Pen_Text_Select = {ID_PEN, 1, NULL, NULL, COLOR_WHITE, MAX_U32};
PEN Pen_Selection = {ID_PEN, 1, NULL, NULL, COLOR_DARK_BLUE, MAX_U32};
PEN Pen_Title_Bar = {ID_PEN, 1, NULL, NULL, COLOR_DARK_BLUE, MAX_U32};
PEN Pen_Title_Bar_2 = {ID_PEN, 1, NULL, NULL, COLOR_CYAN, MAX_U32};
PEN Pen_Title_Text = {ID_PEN, 1, NULL, NULL, COLOR_WHITE, MAX_U32};

/***************************************************************************/

BOOL ResetGraphicsContext(LPGRAPHICSCONTEXT This) {
    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != ID_GRAPHICSCONTEXT) return FALSE;

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

I32 SortDesktops_Order(LPCVOID Item1, LPCVOID Item2) {
    LPDESKTOP* Ptr1 = (LPDESKTOP*)Item1;
    LPDESKTOP* Ptr2 = (LPDESKTOP*)Item2;
    LPDESKTOP Dsk1 = *Ptr1;
    LPDESKTOP Dsk2 = *Ptr2;

    return (Dsk1->Order - Dsk2->Order);
}

/***************************************************************************/

I32 SortWindows_Order(LPCVOID Item1, LPCVOID Item2) {
    LPWINDOW* Ptr1 = (LPWINDOW*)Item1;
    LPWINDOW* Ptr2 = (LPWINDOW*)Item2;
    LPWINDOW Win1 = *Ptr1;
    LPWINDOW Win2 = *Ptr2;

    return (Win1->Order - Win2->Order);
}

/***************************************************************************/

LPDESKTOP CreateDesktop() {
    LPDESKTOP This;
    WINDOWINFO WindowInfo;

    This = (LPDESKTOP)KernelMemAlloc(sizeof(DESKTOP));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(DESKTOP));

    InitMutex(&(This->Mutex));

    This->ID = ID_DESKTOP;
    This->References = ID_DESKTOP;
    This->Task = GetCurrentTask();
    This->Graphics = &VESADriver;

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
        KernelMemFree(This);
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

void DeleteDesktop(LPDESKTOP This) {
    if (This == NULL) return;

    LockMutex(&(This->Mutex), INFINITY);

    if (This->Window != NULL) {
        DeleteWindow(This->Window);
    }

    KernelMemFree(This);
}

/***************************************************************************/

BOOL ShowDesktop(LPDESKTOP This) {
    GRAPHICSMODEINFO ModeInfo;
    LPDESKTOP Desktop;
    LPLISTNODE Node;
    I32 Order;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != ID_DESKTOP) return FALSE;

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

    ModeInfo.Width = 1024;
    ModeInfo.Height = 768;
    ModeInfo.BitsPerPixel = 24;

    This->Graphics->Command(DF_GFX_SETMODE, (U32)&ModeInfo);

    // PostMessage((HANDLE) This->Window, EWM_DRAW, 0, 0);

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));
    UnlockMutex(MUTEX_KERNEL);

    return TRUE;
}

/***************************************************************************/

LPWINDOW NewWindow() {
    LPWINDOW This = (LPWINDOW)KernelMemAlloc(sizeof(WINDOW));
    if (This == NULL) return NULL;

    MemorySet(This, 0, sizeof(WINDOW));

    InitMutex(&(This->Mutex));

    This->ID = ID_WINDOW;
    This->References = 1;
    This->Properties = NewList(NULL, KernelMemAlloc, KernelMemFree);
    This->Children = NewList(NULL, KernelMemAlloc, KernelMemFree);

    return This;
}

/***************************************************************************/

void DeleteWindow(LPWINDOW This) {
    LPPROCESS Process;
    LPTASK Task;
    LPDESKTOP Desktop;
    LPLISTNODE Node;

    //-------------------------------------
    // Check validity of parameters

    if (This->ID != ID_WINDOW) return;
    if (This->Parent == NULL) return;

    Task = This->Task;
    if (Task == NULL) return;
    Process = Task->Process;
    if (Process == NULL) return;
    Desktop = Process->Desktop;
    if (Desktop == NULL) return;

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

    //-------------------------------------
    // Invalidate it's ID

    This->ID = ID_NONE;
    This->References = 0;

    KernelMemFree(This);
}

/***************************************************************************/

LPWINDOW FindWindow(LPWINDOW Start, LPWINDOW Target) {
    LPLISTNODE Node = NULL;
    LPWINDOW Current = NULL;
    LPWINDOW Child = NULL;

    if (Start == NULL) return NULL;
    if (Start->ID != ID_WINDOW) return NULL;

    if (Target == NULL) return NULL;
    if (Target->ID != ID_WINDOW) return NULL;

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

    if (Parent != NULL) {
        if (Parent->ID != ID_WINDOW) return NULL;
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
        if (Desktop != NULL) This->Parent = Desktop->Window;
    }

    if (This->Parent != NULL) {
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

LPDESKTOP GetWindowDesktop(LPWINDOW This) {
    LPPROCESS Process = NULL;
    LPTASK Task = NULL;
    LPDESKTOP Desktop = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != ID_WINDOW) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);

    Task = This->Task;
    if (Task != NULL && Task->ID == ID_TASK) {
        Process = Task->Process;
        if (Process != NULL && Process->ID == ID_PROCESS) {
            Desktop = Process->Desktop;
            if (Desktop != NULL) {
                if (Desktop->ID != ID_DESKTOP) Desktop = NULL;
            }
        }
    }

    UnlockMutex(&(This->Mutex));

    return Desktop;
}

/***************************************************************************/

BOOL BroadCastMessage(LPWINDOW This, U32 Msg, U32 Param1, U32 Param2) {
    LPLISTNODE Node;

    if (This == NULL) return NULL;
    if (This->ID != ID_WINDOW) return NULL;

    LockMutex(&(This->Mutex), INFINITY);

    PostMessage((HANDLE)This, Msg, Param1, Param2);

    for (Node = This->Children->First; Node; Node = Node->Next) {
        BroadCastMessage((LPWINDOW)Node, Msg, Param1, Param2);
    }

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

static BOOL ComputeWindowRegions(LPWINDOW This) {}

/***************************************************************************/

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

BOOL WindowRectToScreenRect(HANDLE Handle, LPRECT Src, LPRECT Dst) {
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL) return FALSE;
    if (This->ID != ID_WINDOW) return FALSE;

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

BOOL ScreenRectToWindowRect(HANDLE Handle, LPRECT Src, LPRECT Dst) {
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL) return FALSE;
    if (This->ID != ID_WINDOW) return FALSE;

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

BOOL InvalidateWindowRect(HANDLE Handle, LPRECT Src) {
    LPWINDOW This = (LPWINDOW)Handle;
    RECT Rect;

    if (This == NULL) return FALSE;
    if (This->ID != ID_WINDOW) return FALSE;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    if (Src != NULL) {
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

BOOL BringWindowToFront(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPWINDOW That;
    LPLISTNODE Node;
    RECT Rect;
    I32 Order;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != ID_WINDOW) return FALSE;

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

    for (Node = This->Parent->Children->First, Order = 1; Node;
         Node = Node->Next) {
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

BOOL ShowWindow(HANDLE Handle, BOOL ShowHide) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPWINDOW Child;
    LPLISTNODE Node;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != ID_WINDOW) return FALSE;

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

BOOL GetWindowRect(HANDLE Handle, LPRECT Rect) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != ID_WINDOW) return FALSE;

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

BOOL MoveWindow(HANDLE Handle, LPPOINT Position) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != ID_WINDOW) return FALSE;

    if (Position == NULL) return FALSE;

    return TRUE;
}

/***************************************************************************/

BOOL SizeWindow(HANDLE Handle, LPPOINT Size) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != ID_WINDOW) return FALSE;

    if (Size == NULL) return FALSE;

    return TRUE;
}

/***************************************************************************/

HANDLE GetWindowParent(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != ID_WINDOW) return FALSE;

    return (HANDLE)This->Parent;
}

/***************************************************************************/

U32 SetWindowProp(HANDLE Handle, LPCSTR Name, U32 Value) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPLISTNODE Node;
    LPPROPERTY Prop;
    U32 OldValue = 0;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return 0;
    if (This->ID != ID_WINDOW) return 0;

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

    Prop = (LPPROPERTY)KernelMemAlloc(sizeof(PROPERTY));

    if (Prop != NULL) {
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

U32 GetWindowProp(HANDLE Handle, LPCSTR Name) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPLISTNODE Node = NULL;
    LPPROPERTY Prop = NULL;
    U32 Value = 0;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != ID_WINDOW) return FALSE;

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

HANDLE GetWindowGC(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPGRAPHICSCONTEXT Context;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->ID != ID_WINDOW) return NULL;

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

BOOL ReleaseWindowGC(HANDLE Handle) {
    LPGRAPHICSCONTEXT This = (LPGRAPHICSCONTEXT)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->ID != ID_GRAPHICSCONTEXT) return FALSE;

    return TRUE;
}

/***************************************************************************/

HANDLE BeginWindowDraw(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;
    HANDLE GC = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->ID != ID_WINDOW) return NULL;

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

BOOL EndWindowDraw(HANDLE Handle) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->ID != ID_WINDOW) return NULL;

    //-------------------------------------
    // Lock access to resources

    LockMutex(&(This->Mutex), INFINITY);

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    return TRUE;
}

/***************************************************************************/

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

HANDLE SelectBrush(HANDLE GC, HANDLE Brush) {
    LPGRAPHICSCONTEXT Context;
    LPBRUSH NewBrush;
    LPBRUSH OldBrush;

    if (GC == NULL) return NULL;

    Context = (LPGRAPHICSCONTEXT)GC;
    NewBrush = (LPBRUSH)Brush;

    if (Context->ID != ID_GRAPHICSCONTEXT) return NULL;

    LockMutex(&(Context->Mutex), INFINITY);

    OldBrush = Context->Brush;
    Context->Brush = NewBrush;

    UnlockMutex(&(Context->Mutex));

    return (HANDLE)OldBrush;
}

/***************************************************************************/

HANDLE SelectPen(HANDLE GC, HANDLE Pen) {
    LPGRAPHICSCONTEXT Context;
    LPPEN NewPen;
    LPPEN OldPen;

    if (GC == NULL) return NULL;

    Context = (LPGRAPHICSCONTEXT)GC;
    NewPen = (LPPEN)Pen;

    if (Context->ID != ID_GRAPHICSCONTEXT) return NULL;

    LockMutex(&(Context->Mutex), INFINITY);

    OldPen = Context->Pen;
    Context->Pen = NewPen;

    UnlockMutex(&(Context->Mutex));

    return (HANDLE)OldPen;
}

/***************************************************************************/

HANDLE CreateBrush(LPBRUSHINFO BrushInfo) {
    LPBRUSH Brush = NULL;

    if (BrushInfo == NULL) return NULL;

    Brush = (LPBRUSH)HeapAlloc(sizeof(BRUSH));
    if (Brush == NULL) return NULL;

    MemorySet(Brush, 0, sizeof(BRUSH));

    Brush->ID = ID_BRUSH;
    Brush->References = 1;
    Brush->Color = BrushInfo->Color;
    Brush->Pattern = BrushInfo->Pattern;

    return (HANDLE)Brush;
}

/***************************************************************************/

HANDLE CreatePen(LPPENINFO PenInfo) {
    LPPEN Pen = NULL;

    if (PenInfo == NULL) return NULL;

    Pen = (LPPEN)HeapAlloc(sizeof(PEN));
    if (Pen == NULL) return NULL;

    MemorySet(Pen, 0, sizeof(PEN));

    Pen->ID = ID_PEN;
    Pen->References = 1;
    Pen->Color = PenInfo->Color;
    Pen->Pattern = PenInfo->Pattern;

    return (HANDLE)Pen;
}

/***************************************************************************/

BOOL SetPixel(LPPIXELINFO PixelInfo) {
    LPGRAPHICSCONTEXT Context;

    //-------------------------------------
    // Check validity of parameters

    if (PixelInfo == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)PixelInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->ID != ID_GRAPHICSCONTEXT) return FALSE;

    PixelInfo->X = Context->Origin.X + PixelInfo->X;
    PixelInfo->Y = Context->Origin.Y + PixelInfo->Y;

    Context->Driver->Command(DF_GFX_SETPIXEL, (U32)PixelInfo);

    return TRUE;
}

/***************************************************************************/

BOOL GetPixel(LPPIXELINFO PixelInfo) {
    LPGRAPHICSCONTEXT Context;

    //-------------------------------------
    // Check validity of parameters

    if (PixelInfo == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)PixelInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->ID != ID_GRAPHICSCONTEXT) return FALSE;

    PixelInfo->X = Context->Origin.X + PixelInfo->X;
    PixelInfo->Y = Context->Origin.Y + PixelInfo->Y;

    Context->Driver->Command(DF_GFX_GETPIXEL, (U32)PixelInfo);

    return TRUE;
}

/***************************************************************************/

BOOL Line(LPLINEINFO LineInfo) {
    LPGRAPHICSCONTEXT Context;

    //-------------------------------------
    // Check validity of parameters

    if (LineInfo == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)LineInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->ID != ID_GRAPHICSCONTEXT) return FALSE;

    LineInfo->X1 = Context->Origin.X + LineInfo->X1;
    LineInfo->Y1 = Context->Origin.Y + LineInfo->Y1;
    LineInfo->X2 = Context->Origin.X + LineInfo->X2;
    LineInfo->Y2 = Context->Origin.Y + LineInfo->Y2;

    Context->Driver->Command(DF_GFX_LINE, (U32)LineInfo);

    return TRUE;
}

/***************************************************************************/

BOOL Rectangle(LPRECTINFO RectInfo) {
    LPGRAPHICSCONTEXT Context;

    //-------------------------------------
    // Check validity of parameters

    if (RectInfo == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)RectInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->ID != ID_GRAPHICSCONTEXT) return FALSE;

    RectInfo->X1 = Context->Origin.X + RectInfo->X1;
    RectInfo->Y1 = Context->Origin.Y + RectInfo->Y1;
    RectInfo->X2 = Context->Origin.X + RectInfo->X2;
    RectInfo->Y2 = Context->Origin.Y + RectInfo->Y2;

    Context->Driver->Command(DF_GFX_RECTANGLE, (U32)RectInfo);

    return TRUE;
}

/***************************************************************************/

HANDLE WindowHitTest(HANDLE Handle, LPPOINT Position) {
    LPWINDOW This = (LPWINDOW)Handle;
    LPWINDOW Target = NULL;
    LPWINDOW Child = NULL;
    LPLISTNODE Node = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->ID != ID_WINDOW) return NULL;

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

    if (Position->X >= This->ScreenRect.X1 &&
        Position->X <= This->ScreenRect.X2 &&
        Position->Y >= This->ScreenRect.Y1 &&
        Position->Y <= This->ScreenRect.Y2) {
        Target = This;
    }

Out:

    //-------------------------------------
    // Unlock access to resources

    UnlockMutex(&(This->Mutex));

    return (HANDLE)Target;
}

/***************************************************************************/

U32 DefWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
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

                RectInfo.Size = sizeof(RectInfo);
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

    /*
      PIXELINFO PixelInfo;
      COLOR Color;

      if (OnOff)
      {
    Color = 0x00FFFFFF;
      }
      else
      {
    Color = 0x00000000;
      }

      PixelInfo.GC = GC;
      PixelInfo.X = X;
      PixelInfo.Y = Y;
      PixelInfo.Y = Y;
      SetPixel(&PixelInfo);
    */

    return 0;
}

/***************************************************************************/

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

/***************************************************************************/

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

                RectInfo.Size = sizeof(RectInfo);
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
            I32 MouseX = SIGNED(Param1);
            I32 MouseY = SIGNED(Param2);
            // LPWINDOW Target;
            // POINT Position;
            HANDLE GC;

            if (GC = GetWindowGC(Window)) {
                // DrawMouseCursor(GC, SIGNED(OldMouseX), SIGNED(OldMouseY),
                // FALSE); DrawMouseCursor(GC, MouseX, MouseY, TRUE);
                ReleaseWindowGC(GC);
            }

            /*
              SetWindowProp(Window, Prop_MouseX, Param1);
              SetWindowProp(Window, Prop_MouseY, Param2);

              Target = (LPWINDOW) WindowHitTest(Window, &Position);

              if (Target != NULL)
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
            RECT Rect;
            POINT Position;
            LPWINDOW Target;
            HANDLE GC;
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

/***************************************************************************/
