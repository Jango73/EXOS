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


    Desktop graphics and drawing

\************************************************************************/

#include "Desktop-Private.h"
#include "Desktop-Cursor.h"
#include "Desktop-NonClient.h"
#include "Desktop-ThemeResolver.h"
#include "Desktop-ThemeTokens.h"
#include "CoreString.h"
#include "Kernel.h"
#include "Log.h"
#include "Profile.h"
#include "Desktop.h"
#include "input/Mouse.h"
#include "input/MouseDispatcher.h"
#include "process/Task-Messaging.h"
#include "utils/Graphics-Utils.h"

/***************************************************************************/

#define DESKTOP_USE_TEMPORARY_FAST_ROOT_FILL 0

/***************************************************************************/

typedef struct tag_SYSTEM_DRAW_OBJECT_ENTRY {
    U32 SystemColor;
    LPBRUSH Brush;
    LPPEN Pen;
} SYSTEM_DRAW_OBJECT_ENTRY, *LPSYSTEM_DRAW_OBJECT_ENTRY;

/***************************************************************************/

static SYSTEM_DRAW_OBJECT_ENTRY SystemDrawObjects[] = {
    {SM_COLOR_DESKTOP, &Brush_Desktop, &Pen_Desktop},
    {SM_COLOR_HIGHLIGHT, &Brush_High, &Pen_High},
    {SM_COLOR_NORMAL, &Brush_Normal, &Pen_Normal},
    {SM_COLOR_LIGHT_SHADOW, &Brush_HiShadow, &Pen_HiShadow},
    {SM_COLOR_DARK_SHADOW, &Brush_LoShadow, &Pen_LoShadow},
    {SM_COLOR_CLIENT, &Brush_Client, &Pen_Client},
    {SM_COLOR_TEXT_NORMAL, &Brush_Text_Normal, &Pen_Text_Normal},
    {SM_COLOR_TEXT_SELECTED, &Brush_Text_Select, &Pen_Text_Select},
    {SM_COLOR_SELECTION, &Brush_Selection, &Pen_Selection},
    {SM_COLOR_TITLE_BAR, &Brush_Title_Bar, &Pen_Title_Bar},
    {SM_COLOR_TITLE_BAR_2, &Brush_Title_Bar_2, &Pen_Title_Bar_2},
    {SM_COLOR_TITLE_TEXT, &Brush_Title_Text, &Pen_Title_Text},
};

/***************************************************************************/

/**
 * @brief Convert one screen rectangle into coordinates relative to one window.
 * @param Window Window used as origin.
 * @param ScreenRect Rectangle in screen coordinates.
 * @param WindowRect Receives rectangle in window coordinates.
 * @return TRUE on success.
 */
static BOOL ConvertScreenRectToWindowRect(LPWINDOW Window, LPRECT ScreenRect, LPRECT WindowRect) {
    RECT WindowScreenRect;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (ScreenRect == NULL || WindowRect == NULL) return FALSE;

    if (GetWindowScreenRectSnapshot(Window, &WindowScreenRect) == FALSE) return FALSE;

    GraphicsScreenRectToWindowRect(&WindowScreenRect, ScreenRect, WindowRect);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Invalidate visible sibling windows intersecting one uncovered screen rectangle.
 * @param Window Moved window.
 * @param Parent Parent window containing sibling list.
 * @param UncoveredRect Screen rectangle uncovered by the move.
 */
static void InvalidateSiblingWindowsOnUncoveredRect(LPWINDOW Window, LPWINDOW Parent, LPRECT UncoveredRect) {
    LPWINDOW* Siblings;
    RECT SiblingScreenRect;
    RECT Intersection;
    RECT SiblingLocalRect;
    LPWINDOW Sibling;
    UINT Count;
    UINT Index;
    I32 WindowOrder;
    I32 SiblingOrder;
    BOOL IsVisible;
    WINDOW_STATE_SNAPSHOT SiblingSnapshot;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;
    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return;
    if (UncoveredRect == NULL) return;

    Count = 0;
    Siblings = NULL;
    WindowOrder = 0;
    (void)GetWindowOrderSnapshot(Window, &WindowOrder);
    (void)DesktopSnapshotWindowChildren(Parent, &Siblings, &Count);

    for (Index = 0; Index < Count; Index++) {
        Sibling = Siblings[Index];

        if (Sibling == NULL || Sibling->TypeID != KOID_WINDOW || Sibling == Window) continue;

        if (GetWindowStateSnapshot(Sibling, &SiblingSnapshot) == FALSE) continue;
        IsVisible = ((SiblingSnapshot.Status & WINDOW_STATUS_VISIBLE) != 0);
        SiblingOrder = SiblingSnapshot.Order;
        SiblingScreenRect = SiblingSnapshot.ScreenRect;

        if (IsVisible == FALSE) continue;
        if (SiblingOrder <= WindowOrder) continue;
        if (IntersectRect(&SiblingScreenRect, UncoveredRect, &Intersection) == FALSE) continue;

        GraphicsScreenRectToWindowRect(&SiblingScreenRect, &Intersection, &SiblingLocalRect);
        (void)InvalidateWindowRect((HANDLE)Sibling, &SiblingLocalRect);
    }

    if (Siblings != NULL) {
        KernelHeapFree(Siblings);
    }
}

/***************************************************************************/

/**
 * @brief Set whether one window bypasses its parent work rectangle clamp.
 * @param Window Target window.
 * @param Enabled TRUE to bypass parent work rect clamping.
 * @return TRUE on success.
 */
BOOL DesktopSetWindowBypassParentWorkRectState(LPWINDOW Window, BOOL Enabled) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    if (Enabled != FALSE)
        Window->Status |= WINDOW_STATUS_BYPASS_PARENT_WORK_RECT;
    else
        Window->Status &= ~WINDOW_STATUS_BYPASS_PARENT_WORK_RECT;
    UnlockMutex(&(Window->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Build one window rectangle preserving current size at one position.
 * @param Window Target window.
 * @param Position New top-left position relative to parent.
 * @param Rect Receives full window rectangle in parent coordinates.
 * @return TRUE on success.
 */
BOOL BuildWindowRectAtPosition(LPWINDOW Window, LPPOINT Position, LPRECT Rect) {
    I32 Width;
    I32 Height;
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Position == NULL || Rect == NULL) return FALSE;

    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return FALSE;
    Width = Snapshot.Rect.X2 - Snapshot.Rect.X1 + 1;
    Height = Snapshot.Rect.Y2 - Snapshot.Rect.Y1 + 1;

    if (Width <= 0 || Height <= 0) return FALSE;

    Rect->X1 = Position->X;
    Rect->Y1 = Position->Y;
    Rect->X2 = Position->X + Width - 1;
    Rect->Y2 = Position->Y + Height - 1;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Apply default move/resize behavior and enqueue bounded damage.
 * @param Window Target window.
 * @param WindowRect New window rectangle relative to parent.
 * @return TRUE on success.
 */
BOOL DefaultSetWindowRect(LPWINDOW Window, LPRECT WindowRect) {
    LPWINDOW Parent;
    RECT OldScreenRect;
    RECT NewScreenRect;
    RECT FullWindowRect;
    RECT ParentOldRect;
    RECT ParentNewRect;
    RECT OldRect;
    RECT ParentScreenRect;
    WINDOW_STATE_SNAPSHOT Snapshot;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (WindowRect == NULL) return FALSE;
    if (WindowRect->X1 > WindowRect->X2 || WindowRect->Y1 > WindowRect->Y2) return FALSE;

    if (GetWindowStateSnapshot(Window, &Snapshot) == FALSE) return FALSE;
    Parent = Snapshot.ParentWindow;
    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return FALSE;

    OldScreenRect = Snapshot.ScreenRect;
    OldRect = Snapshot.Rect;
    if (DesktopResolveWindowPlacementRect(Window, WindowRect) == FALSE) return FALSE;
    if (WindowRect->X1 == OldRect.X1 && WindowRect->Y1 == OldRect.Y1 && WindowRect->X2 == OldRect.X2 &&
        WindowRect->Y2 == OldRect.Y2) {
        return TRUE;
    }
    if (GetWindowScreenRectSnapshot(Parent, &ParentScreenRect) == FALSE) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    Window->Rect = *WindowRect;
    GraphicsWindowRectToScreenRect(&ParentScreenRect, &(Window->Rect), &(Window->ScreenRect));
    NewScreenRect = Window->ScreenRect;
    UnlockMutex(&(Window->Mutex));

    FullWindowRect.X1 = 0;
    FullWindowRect.Y1 = 0;
    FullWindowRect.X2 = WindowRect->X2 - WindowRect->X1;
    FullWindowRect.Y2 = WindowRect->Y2 - WindowRect->Y1;

    if (ConvertScreenRectToWindowRect(Parent, &OldScreenRect, &ParentOldRect)) {
        (void)InvalidateWindowRect((HANDLE)Parent, &ParentOldRect);
    }

    InvalidateSiblingWindowsOnUncoveredRect(Window, Parent, &OldScreenRect);

    if (ConvertScreenRectToWindowRect(Parent, &NewScreenRect, &ParentNewRect)) {
        (void)InvalidateWindowRect((HANDLE)Parent, &ParentNewRect);
    }

    (void)InvalidateWindowRect((HANDLE)Window, &FullWindowRect);
    (void)PostMessage((HANDLE)Window, EWM_NOTIFY, EWN_WINDOW_RECT_CHANGED, 0);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Broadcast one rectangle-change notify to all direct desktop children.
 * @param DesktopWindow Desktop root window.
 */
static void DesktopBroadcastRectChangedNotifyToDirectChildren(HANDLE DesktopWindow) {
    UINT ChildCount;
    UINT ChildIndex;
    HANDLE ChildWindow;

    if (DesktopWindow == NULL) return;

    ChildCount = GetWindowChildCount(DesktopWindow);
    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        ChildWindow = GetWindowChild(DesktopWindow, ChildIndex);
        if (ChildWindow == NULL) continue;
        (void)PostMessage(ChildWindow, EWM_NOTIFY, EWN_WINDOW_RECT_CHANGED, (U32)(UINT)(LINEAR)DesktopWindow);
    }
}

/***************************************************************************/

/**
 * @brief Set one graphics-context clip rectangle in screen coordinates.
 * @param GC Graphics context handle.
 * @param ClipRect Clip rectangle in screen coordinates.
 * @return TRUE on success.
 */
BOOL SetGraphicsContextClipScreenRect(HANDLE GC, LPRECT ClipRect) {
    LPGRAPHICSCONTEXT Context = (LPGRAPHICSCONTEXT)GC;
    RECT ClampedClip;
    I32 MaxX;
    I32 MaxY;

    if (Context == NULL || ClipRect == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;
    if (ClipRect->X1 > ClipRect->X2 || ClipRect->Y1 > ClipRect->Y2) return FALSE;

    MaxX = Context->Width - 1;
    MaxY = Context->Height - 1;
    if (MaxX < 0 || MaxY < 0) return FALSE;

    ClampedClip = *ClipRect;
    if (ClampedClip.X1 < 0) ClampedClip.X1 = 0;
    if (ClampedClip.Y1 < 0) ClampedClip.Y1 = 0;
    if (ClampedClip.X2 > MaxX) ClampedClip.X2 = MaxX;
    if (ClampedClip.Y2 > MaxY) ClampedClip.Y2 = MaxY;
    if (ClampedClip.X1 > ClampedClip.X2 || ClampedClip.Y1 > ClampedClip.Y2) return FALSE;

    LockMutex(&(Context->Mutex), INFINITY);
    Context->LoClip.X = ClampedClip.X1;
    Context->LoClip.Y = ClampedClip.Y1;
    Context->HiClip.X = ClampedClip.X2;
    Context->HiClip.Y = ClampedClip.Y2;
    UnlockMutex(&(Context->Mutex));

    return TRUE;
}

/**
 * @brief Build and consume one window clip region from accumulated dirty rectangles.
 * @param This Window whose dirty region is consumed.
 * @param ClipRegion Destination clip region.
 * @param ClipStorage Backing storage for destination clip region.
 * @param ClipCapacity Clip storage capacity.
 * @return TRUE on success.
 */
BOOL BuildWindowDrawClipRegion(
    LPWINDOW This,
    LPRECT_REGION ClipRegion,
    LPRECT ClipStorage,
    UINT ClipCapacity
) {
    RECT DirtyStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT VisibleStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT_REGION DirtyRegion;
    RECT_REGION VisibleRegion;
    RECT DirtyRect;
    RECT VisibleRect;
    RECT WindowScreenRect;
    I32 ThisOrder;
    LPWINDOW ParentWindow = NULL;
    UINT DirtyCount;
    UINT DirtyIndex;
    UINT VisibleCount;
    UINT VisibleIndex;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (ClipRegion == NULL || ClipStorage == NULL || ClipCapacity == 0) return FALSE;
    if (RectRegionInit(ClipRegion, ClipStorage, ClipCapacity) == FALSE) return FALSE;
    RectRegionReset(ClipRegion);
    if (RectRegionInit(&DirtyRegion, DirtyStorage, WINDOW_DIRTY_REGION_CAPACITY) == FALSE) return FALSE;

    if (DesktopConsumeWindowDirtyRegionSnapshot(
            This, &DirtyRegion, DirtyStorage, WINDOW_DIRTY_REGION_CAPACITY, &WindowScreenRect, &ThisOrder, &ParentWindow) ==
        FALSE) {
        return FALSE;
    }
    UNUSED(ThisOrder);
    UNUSED(ParentWindow);

    DirtyCount = RectRegionGetCount(&DirtyRegion);
    for (DirtyIndex = 0; DirtyIndex < DirtyCount; DirtyIndex++) {
        if (RectRegionGetRect(&DirtyRegion, DirtyIndex, &DirtyRect) == FALSE) continue;
        if (DesktopBuildWindowVisibleRegion(
                This, &DirtyRect, TRUE, &VisibleRegion, VisibleStorage, WINDOW_DIRTY_REGION_CAPACITY) == FALSE) {
            RectRegionReset(ClipRegion);
            (void)DesktopBuildWindowVisibleRegion(This, &WindowScreenRect, TRUE, ClipRegion, ClipStorage, ClipCapacity);
            return TRUE;
        }

        VisibleCount = RectRegionGetCount(&VisibleRegion);
        for (VisibleIndex = 0; VisibleIndex < VisibleCount; VisibleIndex++) {
            if (RectRegionGetRect(&VisibleRegion, VisibleIndex, &VisibleRect) == FALSE) continue;
            if (RectRegionAddRect(ClipRegion, &VisibleRect) == FALSE) {
                RectRegionReset(ClipRegion);
                (void)DesktopBuildWindowVisibleRegion(This, &WindowScreenRect, TRUE, ClipRegion, ClipStorage, ClipCapacity);
                return TRUE;
            }
        }
    }

    if (RectRegionIsOverflowed(&DirtyRegion) || RectRegionIsOverflowed(ClipRegion)) {
        RectRegionReset(ClipRegion);
        (void)DesktopBuildWindowVisibleRegion(This, &WindowScreenRect, TRUE, ClipRegion, ClipStorage, ClipCapacity);
    }

    return TRUE;
}

/**
 * @brief Resolve shared brush and pen objects for a system color index.
 * @param Index SM_COLOR_* identifier.
 * @param Brush Receives brush object pointer.
 * @param Pen Receives pen object pointer.
 * @return TRUE when mapping exists.
 */
static BOOL ResolveSystemDrawObjects(U32 Index, LPBRUSH* Brush, LPPEN* Pen) {
    UINT EntryIndex;

    if (Brush == NULL || Pen == NULL) return FALSE;

    *Brush = NULL;
    *Pen = NULL;

    for (EntryIndex = 0; EntryIndex < (sizeof(SystemDrawObjects) / sizeof(SystemDrawObjects[0])); EntryIndex++) {
        if (SystemDrawObjects[EntryIndex].SystemColor == Index) {
            *Brush = SystemDrawObjects[EntryIndex].Brush;
            *Pen = SystemDrawObjects[EntryIndex].Pen;
            return TRUE;
        }
    }

    return FALSE;
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
    LPWINDOW* Children = NULL;
    LPWINDOW Child;
    UINT ChildCount = 0;
    UINT ChildIndex;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    //-------------------------------------
    // Send appropriate messages to the window

    (void)DesktopSetWindowVisibleState(This, ShowHide);
    (void)DesktopRevalidateSiblingPlacementConstraints(This);

    PostMessage(Handle, EWM_SHOW, 0, 0);
    (void)RequestWindowDraw(Handle);

    (void)DesktopSnapshotWindowChildren(This, &Children, &ChildCount);
    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        WINDOW_STATE_SNAPSHOT ChildSnapshot;

        Child = Children[ChildIndex];
        if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;
        if (GetWindowStateSnapshot(Child, &ChildSnapshot) == FALSE) continue;
        if ((ChildSnapshot.Style & EWS_VISIBLE) != 0) {
            ShowWindow((HANDLE)Child, ShowHide);
        }
    }
    if (Children != NULL) {
        KernelHeapFree(Children);
    }

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
    if (This->TypeID != KOID_WINDOW) return FALSE;

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
 * @brief Move and/or resize one window.
 * @param Handle Window handle.
 * @param Rect New window rectangle in parent coordinates.
 * @return TRUE on success.
 */
BOOL MoveWindow(HANDLE Handle, LPRECT Rect) {
    LPWINDOW This = (LPWINDOW)Handle;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (Rect == NULL) return FALSE;

    return DefaultSetWindowRect(This, Rect);
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
    RECT NewRect;
    I32 Width;
    I32 Height;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (Size == NULL) return FALSE;
    if (Size->X <= 0 || Size->Y <= 0) return FALSE;

    Width = Size->X;
    Height = Size->Y;

    LockMutex(&(This->Mutex), INFINITY);
    NewRect.X1 = This->Rect.X1;
    NewRect.Y1 = This->Rect.Y1;
    NewRect.X2 = NewRect.X1 + Width - 1;
    NewRect.Y2 = NewRect.Y1 + Height - 1;
    UnlockMutex(&(This->Mutex));

    return DefaultSetWindowRect(This, &NewRect);
}

/***************************************************************************/

/**
 * @brief Set or clear one masked style on one window.
 * @param Handle Window handle.
 * @param StyleMask Style bits to update.
 * @param Enabled TRUE to set bits, FALSE to clear bits.
 * @return TRUE on success.
 */
BOOL SetWindowStyleState(HANDLE Handle, U32 StyleMask, BOOL Enabled) {
    LPWINDOW This = (LPWINDOW)Handle;
    BOOL Result;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (StyleMask == 0) return FALSE;

    Result = DesktopSetWindowStyleState(This, StyleMask, Enabled);
    if (Result == FALSE) return FALSE;

    if ((StyleMask & (EWS_EXCLUDE_SIBLING_PLACEMENT | EWS_VISIBLE)) != 0) {
        (void)DesktopRevalidateSiblingPlacementConstraints(This);
    }
    if ((StyleMask & (EWS_ALWAYS_IN_FRONT | EWS_ALWAYS_AT_BOTTOM)) != 0) {
        (void)DesktopRefreshWindowZOrder(This);
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve raw style bits from one window.
 * @param Handle Window handle.
 * @param Style Receives raw style bits.
 * @return TRUE on success.
 */
BOOL GetWindowStyle(HANDLE Handle, U32* Style) {
    WINDOW_STATE_SNAPSHOT Snapshot;
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (Style == NULL) return FALSE;
    if (GetWindowStateSnapshot(This, &Snapshot) == FALSE) return FALSE;

    *Style = Snapshot.Style;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Set one window caption string.
 * @param Handle Window handle.
 * @param Caption New caption text, or NULL for an empty caption.
 * @return TRUE on success.
 */
BOOL SetWindowCaption(HANDLE Handle, LPCSTR Caption) {
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (DesktopSetWindowCaption(This, Caption) == FALSE) return FALSE;

    return RequestWindowDraw(Handle);
}

/***************************************************************************/

/**
 * @brief Retrieve one window caption string.
 * @param Handle Window handle.
 * @param Caption Receives a null-terminated caption string.
 * @param CaptionLength Destination buffer length.
 * @return TRUE on success.
 */
BOOL GetWindowCaption(HANDLE Handle, LPSTR Caption, UINT CaptionLength) {
    WINDOW_STATE_SNAPSHOT Snapshot;
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (Caption == NULL || CaptionLength == 0) return FALSE;
    if (GetWindowStateSnapshot(This, &Snapshot) == FALSE) return FALSE;

    StringCopyLimit(Caption, Snapshot.Caption, CaptionLength);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Set one window work rectangle in parent coordinates.
 * @param Handle Window handle.
 * @param WorkRect Work rectangle to assign.
 * @return TRUE on success.
 */
BOOL SetWindowWorkRect(HANDLE Handle, LPRECT WorkRect) {
    LPWINDOW This = (LPWINDOW)Handle;
    RECT Candidate;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (WorkRect == NULL) return FALSE;
    if (WorkRect->X1 > WorkRect->X2 || WorkRect->Y1 > WorkRect->Y2) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);
    Candidate = *WorkRect;
    if (Candidate.X1 < 0) Candidate.X1 = 0;
    if (Candidate.Y1 < 0) Candidate.Y1 = 0;
    if (Candidate.X2 > (This->Rect.X2 - This->Rect.X1)) Candidate.X2 = This->Rect.X2 - This->Rect.X1;
    if (Candidate.Y2 > (This->Rect.Y2 - This->Rect.Y1)) Candidate.Y2 = This->Rect.Y2 - This->Rect.Y1;
    if (Candidate.X1 > Candidate.X2 || Candidate.Y1 > Candidate.Y2) {
        UnlockMutex(&(This->Mutex));
        return FALSE;
    }

    This->WorkRect = Candidate;
    This->Status |= WINDOW_STATUS_HAS_WORK_RECT;
    UnlockMutex(&(This->Mutex));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Get one window work rectangle in parent coordinates.
 * @param Handle Window handle.
 * @param WorkRect Receives work rectangle.
 * @return TRUE on success.
 */
BOOL GetWindowWorkRect(HANDLE Handle, LPRECT WorkRect) {
    LPWINDOW This = (LPWINDOW)Handle;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (WorkRect == NULL) return FALSE;

    LockMutex(&(This->Mutex), INFINITY);
    if ((This->Status & WINDOW_STATUS_HAS_WORK_RECT) != 0) {
        *WorkRect = This->WorkRect;
    } else {
        WorkRect->X1 = 0;
        WorkRect->Y1 = 0;
        WorkRect->X2 = This->Rect.X2 - This->Rect.X1;
        WorkRect->Y2 = This->Rect.Y2 - This->Rect.Y1;
    }
    UnlockMutex(&(This->Mutex));
    return TRUE;
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
    if (This->TypeID != KOID_WINDOW) return 0;

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
    (void)PostMessage(Handle, EWM_NOTIFY, EWN_WINDOW_PROPERTY_CHANGED, 0);

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
    if (This->TypeID != KOID_WINDOW) return FALSE;

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
    LPDRIVER GraphicsDriver;
    LPGRAPHICSCONTEXT Context;
    UINT ContextPointer;
    WINDOW_STATE_SNAPSHOT WindowSnapshot;
    WINDOW_DRAW_CONTEXT_SNAPSHOT DrawSnapshot;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->TypeID != KOID_WINDOW) return NULL;

    GraphicsDriver = GetGraphicsDriver();
    if (GraphicsDriver == NULL || GraphicsDriver->Command == NULL) return NULL;

    ContextPointer = GraphicsDriver->Command(DF_GFX_CREATECONTEXT, 0);
    if (ContextPointer == 0) return NULL;

    Context = (LPGRAPHICSCONTEXT)(LPVOID)ContextPointer;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return NULL;

    ResetGraphicsContext(Context);
    if (GetWindowStateSnapshot(This, &WindowSnapshot) == FALSE) return NULL;
    if (GetWindowDrawContextSnapshot(This, &DrawSnapshot) == FALSE) {
        MemorySet(&DrawSnapshot, 0, sizeof(DrawSnapshot));
    }

    //-------------------------------------
    // Set the origin of the context

    LockMutex(&(Context->Mutex), INFINITY);

    Context->Origin.X = WindowSnapshot.ScreenRect.X1;
    Context->Origin.Y = WindowSnapshot.ScreenRect.Y1;

    if ((DrawSnapshot.Flags & WINDOW_DRAW_CONTEXT_ACTIVE) != 0) {
        Context->Origin.X = DrawSnapshot.Origin.X;
        Context->Origin.Y = DrawSnapshot.Origin.Y;
        Context->LoClip.X = DrawSnapshot.ClipRect.X1;
        Context->LoClip.Y = DrawSnapshot.ClipRect.Y1;
        Context->HiClip.X = DrawSnapshot.ClipRect.X2;
        Context->HiClip.Y = DrawSnapshot.ClipRect.Y2;
    }

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
    if (This->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

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
    if (This->TypeID != KOID_WINDOW) return NULL;

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
    if (This->TypeID != KOID_WINDOW) return NULL;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Retrieve a system brush by index.
 * @param Index Brush identifier.
 * @return Handle to the brush.
 */
HANDLE GetSystemBrush(U32 Index) {
    LPBRUSH Brush;
    LPPEN Pen;
    COLOR Color;

    if (ResolveSystemDrawObjects(Index, &Brush, &Pen) == FALSE) return NULL;

    if (DesktopThemeResolveSystemColor(Index, &Color)) {
        SAFE_USE_VALID_ID(Brush, KOID_BRUSH) { Brush->Color = Color; }
        SAFE_USE_VALID_ID(Pen, KOID_PEN) { Pen->Color = Color; }
    }

    return (HANDLE)Brush;
}

/***************************************************************************/

/**
 * @brief Retrieve a system pen by index.
 * @param Index Pen identifier.
 * @return Handle to the pen.
 */
HANDLE GetSystemPen(U32 Index) {
    LPBRUSH Brush;
    LPPEN Pen;
    COLOR Color;

    if (ResolveSystemDrawObjects(Index, &Brush, &Pen) == FALSE) return NULL;

    if (DesktopThemeResolveSystemColor(Index, &Color)) {
        SAFE_USE_VALID_ID(Brush, KOID_BRUSH) { Brush->Color = Color; }
        SAFE_USE_VALID_ID(Pen, KOID_PEN) { Pen->Color = Color; }
    }

    return (HANDLE)Pen;
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

    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return NULL;

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

    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return NULL;

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

    Brush->TypeID = KOID_BRUSH;
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

    Pen->TypeID = KOID_PEN;
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
    PIXELINFO Pixel;

    //-------------------------------------
    // Check validity of parameters

    if (PixelInfo == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)PixelInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    Pixel = *PixelInfo;
    Pixel.X = Context->Origin.X + Pixel.X;
    Pixel.Y = Context->Origin.Y + Pixel.Y;

    Context->Driver->Command(DF_GFX_SETPIXEL, (UINT)&Pixel);

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
    PIXELINFO Pixel;

    //-------------------------------------
    // Check validity of parameters

    if (PixelInfo == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)PixelInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    Pixel = *PixelInfo;
    Pixel.X = Context->Origin.X + Pixel.X;
    Pixel.Y = Context->Origin.Y + Pixel.Y;

    Context->Driver->Command(DF_GFX_GETPIXEL, (UINT)&Pixel);
    PixelInfo->Color = Pixel.Color;

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
    LINEINFO Line;

    //-------------------------------------
    // Check validity of parameters

    if (LineInfo == NULL) return FALSE;
    if (LineInfo->Header.Size < sizeof(LINEINFO)) return FALSE;

    Context = (LPGRAPHICSCONTEXT)LineInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    Line = *LineInfo;
    Line.X1 = Context->Origin.X + Line.X1;
    Line.Y1 = Context->Origin.Y + Line.Y1;
    Line.X2 = Context->Origin.X + Line.X2;
    Line.Y2 = Context->Origin.Y + Line.Y2;

    Context->Driver->Command(DF_GFX_LINE, (UINT)&Line);

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
    RECTINFO RectangleInfo;
    PROFILE_SCOPE Scope;
    PROFILE_SCOPE DispatchScope;

    //-------------------------------------
    // Check validity of parameters

    if (RectInfo == NULL) return FALSE;
    if (RectInfo->Header.Size < sizeof(RECTINFO)) return FALSE;

    Context = (LPGRAPHICSCONTEXT)RectInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    ProfileStart(&Scope, TEXT("Desktop.Rectangle"));
    RectangleInfo = *RectInfo;
    RectangleInfo.X1 = Context->Origin.X + RectangleInfo.X1;
    RectangleInfo.Y1 = Context->Origin.Y + RectangleInfo.Y1;
    RectangleInfo.X2 = Context->Origin.X + RectangleInfo.X2;
    RectangleInfo.Y2 = Context->Origin.Y + RectangleInfo.Y2;

    ProfileStart(&DispatchScope, TEXT("Desktop.RectangleDispatch"));
    Context->Driver->Command(DF_GFX_RECTANGLE, (UINT)&RectangleInfo);
    ProfileStop(&DispatchScope);
    ProfileStop(&Scope);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Temporary fast rectangle fill path for desktop root drawing.
 * @param GC Graphics context handle.
 * @param ScreenRect Rectangle in screen coordinates.
 * @param FillColor Fill color.
 * @return TRUE on success.
 */
static BOOL DesktopFillRectangleTemporaryFast(HANDLE GC, LPRECT ScreenRect, COLOR FillColor) {
    LPGRAPHICSCONTEXT Context;
    I32 DrawX1;
    I32 DrawY1;
    I32 DrawX2;
    I32 DrawY2;
    I32 Y;
    U32 Width;
    U32 X;
    U32* Pixel;

    if (GC == NULL || ScreenRect == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)GC;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;
    if (Context->MemoryBase == NULL) return FALSE;
    if (Context->BitsPerPixel != 32) return FALSE;

    DrawX1 = ScreenRect->X1;
    DrawY1 = ScreenRect->Y1;
    DrawX2 = ScreenRect->X2;
    DrawY2 = ScreenRect->Y2;

    LockMutex(&(Context->Mutex), INFINITY);

    if (DrawX1 < Context->LoClip.X) DrawX1 = Context->LoClip.X;
    if (DrawY1 < Context->LoClip.Y) DrawY1 = Context->LoClip.Y;
    if (DrawX2 > Context->HiClip.X) DrawX2 = Context->HiClip.X;
    if (DrawY2 > Context->HiClip.Y) DrawY2 = Context->HiClip.Y;

    if (DrawX1 < 0) DrawX1 = 0;
    if (DrawY1 < 0) DrawY1 = 0;
    if (DrawX2 >= Context->Width) DrawX2 = Context->Width - 1;
    if (DrawY2 >= Context->Height) DrawY2 = Context->Height - 1;

    if (DrawX2 < DrawX1 || DrawY2 < DrawY1) {
        UnlockMutex(&(Context->Mutex));
        return TRUE;
    }

    Width = (U32)(DrawX2 - DrawX1 + 1);
    for (Y = DrawY1; Y <= DrawY2; Y++) {
        Pixel = (U32*)(Context->MemoryBase + (U32)(Y * (I32)Context->BytesPerScanLine) + ((U32)DrawX1 << 2));
        for (X = 0; X < Width; X++) {
            Pixel[X] = FillColor;
        }
    }

    UnlockMutex(&(Context->Mutex));
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw an arc using current pen.
 * @param ArcInfo Arc parameters.
 * @return TRUE on success.
 */
BOOL Arc(LPARCINFO ArcInfo) {
    LPGRAPHICSCONTEXT Context;
    ARCINFO Arc;

    if (ArcInfo == NULL) return FALSE;
    if (ArcInfo->Header.Size < sizeof(ARCINFO)) return FALSE;

    Context = (LPGRAPHICSCONTEXT)ArcInfo->GC;
    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    Arc = *ArcInfo;
    Arc.CenterX = Context->Origin.X + Arc.CenterX;
    Arc.CenterY = Context->Origin.Y + Arc.CenterY;

    Context->Driver->Command(DF_GFX_ARC, (UINT)&Arc);
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Draw a triangle using current pen and brush.
 * @param TriangleInfo Triangle parameters.
 * @return TRUE on success.
 */
BOOL Triangle(LPTRIANGLEINFO TriangleInfo) {
    LPGRAPHICSCONTEXT Context;
    TRIANGLEINFO Triangle;

    if (TriangleInfo == NULL) return FALSE;
    if (TriangleInfo->Header.Size < sizeof(TRIANGLEINFO)) return FALSE;

    Context = (LPGRAPHICSCONTEXT)TriangleInfo->GC;
    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    Triangle = *TriangleInfo;
    Triangle.P1.X = Context->Origin.X + Triangle.P1.X;
    Triangle.P1.Y = Context->Origin.Y + Triangle.P1.Y;
    Triangle.P2.X = Context->Origin.X + Triangle.P2.X;
    Triangle.P2.Y = Context->Origin.Y + Triangle.P2.Y;
    Triangle.P3.X = Context->Origin.X + Triangle.P3.X;
    Triangle.P3.Y = Context->Origin.Y + Triangle.P3.Y;

    Context->Driver->Command(DF_GFX_TRIANGLE, (UINT)&Triangle);
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
    LPWINDOW* Children = NULL;
    UINT ChildCount = 0;
    UINT ChildIndex;
    WINDOW_STATE_SNAPSHOT Snapshot;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->TypeID != KOID_WINDOW) return NULL;

    (void)DesktopSnapshotWindowChildren(This, &Children, &ChildCount);
    for (ChildIndex = 0; ChildIndex < ChildCount; ChildIndex++) {
        Target = (LPWINDOW)WindowHitTest((HANDLE)Children[ChildIndex], Position);
        if (Target != NULL) goto Out;
    }

    //-------------------------------------
    // Test if this window passes hit test

    Target = NULL;

    if (GetWindowStateSnapshot(This, &Snapshot) == FALSE) goto Out;
    if ((Snapshot.Status & WINDOW_STATUS_VISIBLE) == 0) goto Out;

    if (Position->X >= Snapshot.ScreenRect.X1 && Position->X <= Snapshot.ScreenRect.X2 &&
        Position->Y >= Snapshot.ScreenRect.Y1 && Position->Y <= Snapshot.ScreenRect.Y2) {
        Target = This;
    }

Out:
    if (Children != NULL) {
        KernelHeapFree(Children);
    }

    return (HANDLE)Target;
}

/***************************************************************************/

/**
 * @brief Copy one window screen rectangle under mutex.
 * @param Window Source window.
 * @param Rect Receives screen rectangle.
 * @return TRUE on success.
 */
BOOL GetWindowScreenRectSnapshot(LPWINDOW Window, LPRECT Rect) {
    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Rect == NULL) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);
    *Rect = Window->ScreenRect;
    UnlockMutex(&(Window->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Get desktop capture state for one window desktop.
 * @param Window Any window on the target desktop.
 * @param CaptureWindow Receives captured window (optional).
 * @param OffsetX Receives drag offset X (optional).
 * @param OffsetY Receives drag offset Y (optional).
 * @return TRUE on success.
 */
BOOL GetDesktopCaptureState(LPWINDOW Window, LPWINDOW* CaptureWindow, I32* OffsetX, I32* OffsetY) {
    LPDESKTOP Desktop;

    if (CaptureWindow != NULL) *CaptureWindow = NULL;
    if (OffsetX != NULL) *OffsetX = 0;
    if (OffsetY != NULL) *OffsetY = 0;

    Desktop = GetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    LockMutex(&(Desktop->Mutex), INFINITY);
    if (CaptureWindow != NULL) *CaptureWindow = Desktop->Capture;
    if (OffsetX != NULL) *OffsetX = Desktop->CaptureOffsetX;
    if (OffsetY != NULL) *OffsetY = Desktop->CaptureOffsetY;
    UnlockMutex(&(Desktop->Mutex));

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Set desktop capture state for one window desktop.
 * @param Window Any window on the target desktop.
 * @param CaptureWindow Captured window or NULL.
 * @param OffsetX Drag offset X in captured window coordinates.
 * @param OffsetY Drag offset Y in captured window coordinates.
 * @return TRUE on success.
 */
BOOL SetDesktopCaptureState(LPWINDOW Window, LPWINDOW CaptureWindow, I32 OffsetX, I32 OffsetY) {
    LPDESKTOP Desktop;

    Desktop = GetWindowDesktop(Window);
    if (Desktop == NULL || Desktop->TypeID != KOID_DESKTOP) return FALSE;

    LockMutex(&(Desktop->Mutex), INFINITY);
    Desktop->Capture = CaptureWindow;
    Desktop->CaptureOffsetX = OffsetX;
    Desktop->CaptureOffsetY = OffsetY;
    UnlockMutex(&(Desktop->Mutex));

    return TRUE;
}

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
    U32 Buttons = GetMouseDriver().Command(DF_MOUSE_GETBUTTONS, 0);

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
        case EWM_NOTIFY:
            if (Param1 == EWN_WINDOW_RECT_CHANGED) {
                DesktopBroadcastRectChangedNotifyToDirectChildren(Window);
            }
            return BaseWindowFunc(Window, Message, Param1, Param2);

        case EWM_DRAW: {
            HANDLE GC;
            RECTINFO RectInfo;
            RECT Rect;
            RECT RootVisibleStorage[WINDOW_DIRTY_REGION_CAPACITY];
            RECT_REGION RootVisibleRegion;
            RECT RootVisibleRect;
            UINT RootVisibleIndex;
            BRUSH Brush;
            COLOR Background;
            BOOL HasBackground;
            RECT ClipRect;

            GC = BeginWindowDraw(Window);

            if (GC) {
                GetWindowClientRect(Window, &Rect);

                RectInfo.Header.Size = sizeof(RectInfo);
                RectInfo.Header.Version = EXOS_ABI_VERSION;
                RectInfo.Header.Flags = 0;
                RectInfo.GC = GC;
                RectInfo.X1 = Rect.X1;
                RectInfo.Y1 = Rect.Y1;
                RectInfo.X2 = Rect.X2;
                RectInfo.Y2 = Rect.Y2;

                SelectPen(GC, NULL);

                HasBackground = DesktopThemeResolveLevel1Color(TEXT("desktop.root"), TEXT("normal"), TEXT("background"), &Background);
                if (HasBackground) {
                    MemorySet(&Brush, 0, sizeof(Brush));
                    Brush.TypeID = KOID_BRUSH;
                    Brush.References = 1;
                    Brush.Color = Background;
                    Brush.Pattern = MAX_U32;
                    SelectBrush(GC, (HANDLE)&Brush);
                } else {
                    SelectBrush(GC, GetSystemBrush(SM_COLOR_DESKTOP));
                }

                Rectangle(&RectInfo);

                ProfileDump();
                EndWindowDraw(Window);
            }
        } break;

        case EWM_MOUSEMOVE: {
            POINT Position;
            LPWINDOW Target;
            LPWINDOW CaptureWindow = NULL;

            Position.X = SIGNED(Param1);
            Position.Y = SIGNED(Param2);

            if (GetDesktopCaptureState((LPWINDOW)Window, &CaptureWindow, NULL, NULL) != FALSE) {
                SAFE_USE_VALID_ID(CaptureWindow, KOID_WINDOW) {
                    if (CaptureWindow != (LPWINDOW)Window) {
                        (void)PostMessage((HANDLE)CaptureWindow, EWM_MOUSEMOVE, Param1, Param2);
                        break;
                    }
                }
            }

            Target = (LPWINDOW)WindowHitTest(Window, &Position);
            if (Target != NULL && Target != (LPWINDOW)Window) {
                (void)PostMessage((HANDLE)Target, EWM_MOUSEMOVE, Param1, Param2);
            }
        } break;

        case EWM_MOUSEDOWN: {
            POINT Position;
            LPWINDOW Target;
            I32 MouseX;
            I32 MouseY;

            if (GetMousePosition(&MouseX, &MouseY) == FALSE) {
                break;
            }

            Position.X = MouseX;
            Position.Y = MouseY;
            Target = (LPWINDOW)WindowHitTest(Window, &Position);
            if (Target != NULL && Target != (LPWINDOW)Window) {
                (void)PostMessage((HANDLE)Target, EWM_MOUSEDOWN, Param1, Param2);
            }
        } break;

        case EWM_MOUSEUP: {
            POINT Position;
            LPWINDOW Target;
            LPWINDOW CaptureWindow = NULL;
            I32 MouseX;
            I32 MouseY;

            if (GetDesktopCaptureState((LPWINDOW)Window, &CaptureWindow, NULL, NULL) != FALSE) {
                SAFE_USE_VALID_ID(CaptureWindow, KOID_WINDOW) {
                    if (CaptureWindow != (LPWINDOW)Window) {
                        (void)PostMessage((HANDLE)CaptureWindow, EWM_MOUSEUP, Param1, Param2);
                        break;
                    }
                }
            }

            if (GetMousePosition(&MouseX, &MouseY) == FALSE) {
                break;
            }

            Position.X = MouseX;
            Position.Y = MouseY;
            Target = (LPWINDOW)WindowHitTest(Window, &Position);
            if (Target != NULL && Target != (LPWINDOW)Window) {
                (void)PostMessage((HANDLE)Target, EWM_MOUSEUP, Param1, Param2);
            }
        } break;

        default:
            return BaseWindowFunc(Window, Message, Param1, Param2);
    }

    return 0;
}
