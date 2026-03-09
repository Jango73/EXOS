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
#include "Kernel.h"
#include "Log.h"
#include "Desktop.h"
#include "input/Mouse.h"
#include "input/MouseDispatcher.h"
#include "process/Task-Messaging.h"
#include "utils/Graphics-Utils.h"

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

    LockMutex(&(Window->Mutex), INFINITY);
    WindowScreenRect = Window->ScreenRect;
    UnlockMutex(&(Window->Mutex));

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
    LPLISTNODE Node;
    RECT SiblingScreenRect;
    RECT Intersection;
    RECT SiblingLocalRect;
    LPWINDOW Sibling;
    UINT Count;
    UINT Index;
    BOOL IsVisible;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return;
    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) return;
    if (UncoveredRect == NULL) return;

    Count = 0;
    Siblings = NULL;

    LockMutex(&(Parent->Mutex), INFINITY);
    if (Parent->Children != NULL && Parent->Children->NumItems > 0) {
        Count = Parent->Children->NumItems;
        Siblings = (LPWINDOW*)KernelHeapAlloc(sizeof(LPWINDOW) * Count);
        if (Siblings != NULL) {
            Index = 0;
            for (Node = Parent->Children->First; Node != NULL && Index < Count; Node = Node->Next) {
                Siblings[Index++] = (LPWINDOW)Node;
            }
            Count = Index;
        } else {
            Count = 0;
        }
    }
    UnlockMutex(&(Parent->Mutex));

    for (Index = 0; Index < Count; Index++) {
        Sibling = Siblings[Index];

        if (Sibling == NULL || Sibling->TypeID != KOID_WINDOW || Sibling == Window) continue;

        LockMutex(&(Sibling->Mutex), INFINITY);
        IsVisible = ((Sibling->Status & WINDOW_STATUS_VISIBLE) != 0);
        SiblingScreenRect = Sibling->ScreenRect;
        UnlockMutex(&(Sibling->Mutex));

        if (IsVisible == FALSE) continue;
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
 * @brief Apply default move behavior and enqueue bounded damage.
 * @param Window Target window.
 * @param Position New window position relative to parent.
 * @return TRUE on success.
 */
static BOOL DefaultMoveWindow(LPWINDOW Window, LPPOINT Position) {
    LPWINDOW Parent;
    RECT OldScreenRect;
    RECT NewScreenRect;
    RECT ParentOldRect;
    RECT ParentNewRect;
    I32 Width;
    I32 Height;
    I32 DeltaX;
    I32 DeltaY;

    if (Window == NULL || Window->TypeID != KOID_WINDOW) return FALSE;
    if (Position == NULL) return FALSE;

    LockMutex(&(Window->Mutex), INFINITY);

    Parent = Window->ParentWindow;
    if (Parent == NULL || Parent->TypeID != KOID_WINDOW) {
        UnlockMutex(&(Window->Mutex));
        return FALSE;
    }

    OldScreenRect = Window->ScreenRect;
    Width = Window->Rect.X2 - Window->Rect.X1;
    Height = Window->Rect.Y2 - Window->Rect.Y1;
    DeltaX = Position->X - Window->Rect.X1;
    DeltaY = Position->Y - Window->Rect.Y1;
    if (DeltaX == 0 && DeltaY == 0) {
        UnlockMutex(&(Window->Mutex));
        return TRUE;
    }

    Window->Rect.X1 = Position->X;
    Window->Rect.Y1 = Position->Y;
    Window->Rect.X2 = Position->X + Width;
    Window->Rect.Y2 = Position->Y + Height;

    Window->ScreenRect.X1 += DeltaX;
    Window->ScreenRect.Y1 += DeltaY;
    Window->ScreenRect.X2 += DeltaX;
    Window->ScreenRect.Y2 += DeltaY;

    NewScreenRect = Window->ScreenRect;

    UnlockMutex(&(Window->Mutex));

    if (ConvertScreenRectToWindowRect(Parent, &OldScreenRect, &ParentOldRect)) {
        (void)InvalidateWindowRect((HANDLE)Parent, &ParentOldRect);
    }

    InvalidateSiblingWindowsOnUncoveredRect(Window, Parent, &OldScreenRect);

    if (ConvertScreenRectToWindowRect(Parent, &NewScreenRect, &ParentNewRect)) {
        (void)InvalidateWindowRect((HANDLE)Parent, &ParentNewRect);
    }

    (void)InvalidateWindowRect((HANDLE)Window, NULL);
    return TRUE;
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

    if (Context == NULL || ClipRect == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;
    if (ClipRect->X1 > ClipRect->X2 || ClipRect->Y1 > ClipRect->Y2) return FALSE;

    LockMutex(&(Context->Mutex), INFINITY);
    Context->LoClip.X = ClipRect->X1;
    Context->LoClip.Y = ClipRect->Y1;
    Context->HiClip.X = ClipRect->X2;
    Context->HiClip.Y = ClipRect->Y2;
    UnlockMutex(&(Context->Mutex));

    return TRUE;
}

/***************************************************************************/

static void RootClipSubtractVisibleWindowTree(LPWINDOW Window, LPRECT_REGION Region);

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
    RECT WindowScreenRect;
    RECT DirtyRect;
    UINT DirtyCount;
    UINT DirtyIndex;
    I32 ThisOrder;
    LPWINDOW ParentWindow = NULL;
    LPWINDOW SiblingWindow;
    LPLISTNODE Node;

    if (This == NULL || This->TypeID != KOID_WINDOW) return FALSE;
    if (ClipRegion == NULL) return FALSE;
    if (RectRegionInit(ClipRegion, ClipStorage, ClipCapacity) == FALSE) return FALSE;
    RectRegionReset(ClipRegion);

    LockMutex(&(This->Mutex), INFINITY);

    WindowScreenRect = This->ScreenRect;
    ThisOrder = This->Order;

    if (This->DirtyRegion.Storage != This->DirtyRects || This->DirtyRegion.Capacity != WINDOW_DIRTY_REGION_CAPACITY) {
        (void)RectRegionInit(&This->DirtyRegion, This->DirtyRects, WINDOW_DIRTY_REGION_CAPACITY);
    }

    DirtyCount = RectRegionGetCount(&This->DirtyRegion);
    if (DirtyCount == 0) {
        (void)RectRegionAddRect(ClipRegion, &WindowScreenRect);
    } else {
        for (DirtyIndex = 0; DirtyIndex < DirtyCount; DirtyIndex++) {
            if (RectRegionGetRect(&This->DirtyRegion, DirtyIndex, &DirtyRect) == FALSE) continue;

            if (DirtyRect.X1 < WindowScreenRect.X1) DirtyRect.X1 = WindowScreenRect.X1;
            if (DirtyRect.Y1 < WindowScreenRect.Y1) DirtyRect.Y1 = WindowScreenRect.Y1;
            if (DirtyRect.X2 > WindowScreenRect.X2) DirtyRect.X2 = WindowScreenRect.X2;
            if (DirtyRect.Y2 > WindowScreenRect.Y2) DirtyRect.Y2 = WindowScreenRect.Y2;
            if (DirtyRect.X1 > DirtyRect.X2 || DirtyRect.Y1 > DirtyRect.Y2) continue;

            (void)RectRegionAddRect(ClipRegion, &DirtyRect);
        }
    }

    if (RectRegionIsOverflowed(&This->DirtyRegion) || RectRegionIsOverflowed(ClipRegion)) {
        RectRegionReset(ClipRegion);
        (void)RectRegionAddRect(ClipRegion, &WindowScreenRect);
    }

    if (RectRegionGetCount(ClipRegion) == 0) {
        (void)RectRegionAddRect(ClipRegion, &WindowScreenRect);
    }

    RectRegionReset(&This->DirtyRegion);
    ParentWindow = This->ParentWindow;

    UnlockMutex(&(This->Mutex));

    SAFE_USE_VALID_ID(ParentWindow, KOID_WINDOW) {
        LockMutex(&(ParentWindow->Mutex), INFINITY);

        for (Node = ParentWindow->Children != NULL ? ParentWindow->Children->First : NULL; Node != NULL; Node = Node->Next) {
            SiblingWindow = (LPWINDOW)Node;
            if (SiblingWindow == NULL || SiblingWindow->TypeID != KOID_WINDOW) continue;
            if (SiblingWindow == This) continue;
            if (SiblingWindow->Order >= ThisOrder) continue;

            RootClipSubtractVisibleWindowTree(SiblingWindow, ClipRegion);
            if (RectRegionGetCount(ClipRegion) == 0) break;
        }

        UnlockMutex(&(ParentWindow->Mutex));
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Append one rectangle to a region when bounds are valid.
 * @param Region Destination region.
 * @param Rect Rectangle candidate.
 * @return TRUE on success.
 */
static BOOL RegionAppendRectIfValid(LPRECT_REGION Region, LPRECT Rect) {
    if (Region == NULL || Rect == NULL) return FALSE;
    if (Rect->X1 > Rect->X2 || Rect->Y1 > Rect->Y2) return TRUE;
    return RectRegionAddRect(Region, Rect);
}

/***************************************************************************/

/**
 * @brief Subtract one occluder rectangle from one source rectangle.
 * @param Region Destination region receiving remaining parts.
 * @param Source Source rectangle.
 * @param Occluder Occluding rectangle.
 * @return TRUE on success.
 */
static BOOL RegionSubtractRectFromRect(LPRECT_REGION Region, LPRECT Source, LPRECT Occluder) {
    RECT Inter;
    RECT Piece;

    if (Region == NULL || Source == NULL || Occluder == NULL) return FALSE;

    if (IntersectRect(Source, Occluder, &Inter) == FALSE) {
        return RegionAppendRectIfValid(Region, Source);
    }

    Piece = (RECT){Source->X1, Source->Y1, Source->X2, Inter.Y1 - 1};
    if (RegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece = (RECT){Source->X1, Inter.Y2 + 1, Source->X2, Source->Y2};
    if (RegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece = (RECT){Source->X1, Inter.Y1, Inter.X1 - 1, Inter.Y2};
    if (RegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    Piece = (RECT){Inter.X2 + 1, Inter.Y1, Source->X2, Inter.Y2};
    if (RegionAppendRectIfValid(Region, &Piece) == FALSE) return FALSE;

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Subtract one occluding rectangle from one clip region.
 * @param Region Region updated in place.
 * @param Occluder Occluding rectangle.
 * @return TRUE on success.
 */
static BOOL RegionSubtractOccluder(LPRECT_REGION Region, LPRECT Occluder) {
    RECT ExistingStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT TempStorage[WINDOW_DIRTY_REGION_CAPACITY];
    RECT_REGION TempRegion;
    RECT Existing;
    UINT Count;
    UINT Index;

    if (Region == NULL || Occluder == NULL) return FALSE;
    if (RectRegionInit(&TempRegion, TempStorage, WINDOW_DIRTY_REGION_CAPACITY) == FALSE) return FALSE;
    RectRegionReset(&TempRegion);

    Count = RectRegionGetCount(Region);
    if (Count > WINDOW_DIRTY_REGION_CAPACITY) Count = WINDOW_DIRTY_REGION_CAPACITY;

    for (Index = 0; Index < Count; Index++) {
        if (RectRegionGetRect(Region, Index, &ExistingStorage[Index]) == FALSE) return FALSE;
    }

    for (Index = 0; Index < Count; Index++) {
        Existing = ExistingStorage[Index];
        if (RegionSubtractRectFromRect(&TempRegion, &Existing, Occluder) == FALSE) {
            RectRegionReset(Region);
            return FALSE;
        }
    }

    RectRegionReset(Region);
    Count = RectRegionGetCount(&TempRegion);
    for (Index = 0; Index < Count; Index++) {
        if (RectRegionGetRect(&TempRegion, Index, &Existing) == FALSE) return FALSE;
        if (RectRegionAddRect(Region, &Existing) == FALSE) return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Subtract one visible window subtree from one root clip region.
 * @param Window Window subtree root.
 * @param Region Region to clip.
 */
static void RootClipSubtractVisibleWindowTree(LPWINDOW Window, LPRECT_REGION Region) {
    LPLISTNODE Node;
    LPWINDOW Child;
    RECT WindowRect;
    BOOL IsVisible = FALSE;

    if (Window == NULL || Window->TypeID != KOID_WINDOW || Region == NULL) return;

    LockMutex(&(Window->Mutex), INFINITY);
    IsVisible = ((Window->Status & WINDOW_STATUS_VISIBLE) != 0);
    WindowRect = Window->ScreenRect;

    for (Node = Window->Children != NULL ? Window->Children->First : NULL; Node != NULL; Node = Node->Next) {
        Child = (LPWINDOW)Node;
        if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;
        RootClipSubtractVisibleWindowTree(Child, Region);
    }
    UnlockMutex(&(Window->Mutex));

    if (IsVisible != FALSE) {
        (void)RegionSubtractOccluder(Region, &WindowRect);
    }
}

/***************************************************************************/

/**
 * @brief Build one root-draw clip region excluding visible child windows.
 * @param RootWindow Desktop root window.
 * @param SourceRect Source screen clip rectangle.
 * @param Region Output region.
 * @param Storage Region storage.
 * @param Capacity Region capacity.
 * @return TRUE on success.
 */
static BOOL BuildDesktopRootVisibleClipRegion(
    LPWINDOW RootWindow,
    LPRECT SourceRect,
    LPRECT_REGION Region,
    LPRECT Storage,
    UINT Capacity
) {
    LPLISTNODE Node;
    LPWINDOW Child;

    if (RootWindow == NULL || RootWindow->TypeID != KOID_WINDOW) return FALSE;
    if (SourceRect == NULL || Region == NULL || Storage == NULL || Capacity == 0) return FALSE;

    if (RectRegionInit(Region, Storage, Capacity) == FALSE) return FALSE;
    RectRegionReset(Region);
    if (RectRegionAddRect(Region, SourceRect) == FALSE) return FALSE;

    LockMutex(&(RootWindow->Mutex), INFINITY);
    for (Node = RootWindow->Children != NULL ? RootWindow->Children->First : NULL; Node != NULL; Node = Node->Next) {
        Child = (LPWINDOW)Node;
        if (Child == NULL || Child->TypeID != KOID_WINDOW) continue;
        RootClipSubtractVisibleWindowTree(Child, Region);
        if (RectRegionGetCount(Region) == 0) {
            UnlockMutex(&(RootWindow->Mutex));
            return TRUE;
        }
    }
    UnlockMutex(&(RootWindow->Mutex));

    return TRUE;
}

/***************************************************************************/

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
    LPWINDOW Child;
    LPLISTNODE Node;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    //-------------------------------------
    // Send appropriate messages to the window

    This->Style |= EWS_VISIBLE;
    This->Status |= WINDOW_STATUS_VISIBLE;

    PostMessage(Handle, EWM_SHOW, 0, 0);
    (void)RequestWindowDraw(Handle);

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
 * @brief Move a window to a new position.
 * @param Handle Window handle.
 * @param Position New position.
 * @return TRUE on success.
 */
BOOL MoveWindow(HANDLE Handle, LPPOINT Position) {
    LPWINDOW This = (LPWINDOW)Handle;
    I32 X;
    I32 Y;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

    if (Position == NULL) return FALSE;

    X = Position->X;
    Y = Position->Y;

    if (PostMessage(Handle, EWM_MOVING, (U32)X, (U32)Y) == FALSE) return FALSE;
    return PostMessage(Handle, EWM_MOVE, (U32)X, (U32)Y);
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
    if (This->TypeID != KOID_WINDOW) return FALSE;

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
    if (This->TypeID != KOID_WINDOW) return FALSE;

    return (HANDLE)This->ParentWindow;
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

    DesktopCursorRenderSoftwareOverlayOnWindow(This);

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

    //-------------------------------------
    // Check validity of parameters

    if (RectInfo == NULL) return FALSE;
    if (RectInfo->Header.Size < sizeof(RECTINFO)) return FALSE;

    Context = (LPGRAPHICSCONTEXT)RectInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    RectangleInfo = *RectInfo;
    RectangleInfo.X1 = Context->Origin.X + RectangleInfo.X1;
    RectangleInfo.Y1 = Context->Origin.Y + RectangleInfo.Y1;
    RectangleInfo.X2 = Context->Origin.X + RectangleInfo.X2;
    RectangleInfo.Y2 = Context->Origin.Y + RectangleInfo.Y2;

    Context->Driver->Command(DF_GFX_RECTANGLE, (UINT)&RectangleInfo);

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
    LPLISTNODE Node = NULL;

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return NULL;
    if (This->TypeID != KOID_WINDOW) return NULL;

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

static BOOL GetWindowScreenRectSnapshot(LPWINDOW Window, LPRECT Rect);
static BOOL GetDesktopCaptureState(LPWINDOW Window, LPWINDOW* CaptureWindow, I32* OffsetX, I32* OffsetY);
static BOOL SetDesktopCaptureState(LPWINDOW Window, LPWINDOW CaptureWindow, I32 OffsetX, I32 OffsetY);

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
            if (GetMousePosition(&MouseX, &MouseY) == FALSE) break;

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

            (void)MoveWindow(Window, &NewPosition);
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

            Position.X = SIGNED(Param1);
            Position.Y = SIGNED(Param2);

            if (DefaultMoveWindow(This, &Position)) {
                return 1;
            }
        } break;

        case EWM_DRAW: {
            HANDLE GC;
            RECT Rect;
            RECT ClipStorage[WINDOW_DIRTY_REGION_CAPACITY];
            RECT_REGION ClipRegion;
            RECT ClipRect;
            UINT ClipIndex;
            LPWINDOW This = (LPWINDOW)Window;
            BOOL DrawingMarked = FALSE;

            if (This != NULL && This->TypeID == KOID_WINDOW) {
                LockMutex(&(This->Mutex), INFINITY);
                This->Status &= ~WINDOW_STATUS_NEED_DRAW;
                This->Status |= WINDOW_STATUS_DRAWING;
                DrawingMarked = TRUE;
                UnlockMutex(&(This->Mutex));
            }

            if (BuildWindowDrawClipRegion(This, &ClipRegion, ClipStorage, WINDOW_DIRTY_REGION_CAPACITY) == FALSE) {
                if (DrawingMarked != FALSE) {
                    LockMutex(&(This->Mutex), INFINITY);
                    This->Status &= ~WINDOW_STATUS_DRAWING;
                    UnlockMutex(&(This->Mutex));
                }
                break;
            }

            GC = BeginWindowDraw(Window);

            if (GC) {
                GetWindowRect(Window, &Rect);

                if (ShouldDrawWindowNonClient(This)) {
                    for (ClipIndex = 0; ClipIndex < RectRegionGetCount(&ClipRegion); ClipIndex++) {
                        if (RectRegionGetRect(&ClipRegion, ClipIndex, &ClipRect) == FALSE) continue;
                        (void)SetGraphicsContextClipScreenRect(GC, &ClipRect);
                        DrawWindowNonClient(Window, GC, &Rect);
                    }
                }

                EndWindowDraw(Window);
            }

            if (DrawingMarked != FALSE) {
                LockMutex(&(This->Mutex), INFINITY);
                This->Status &= ~WINDOW_STATUS_DRAWING;
                UnlockMutex(&(This->Mutex));
            }
        } break;
    }

    return 0;
}

/***************************************************************************/

/**
 * @brief Copy one window screen rectangle under mutex.
 * @param Window Source window.
 * @param Rect Receives screen rectangle.
 * @return TRUE on success.
 */
static BOOL GetWindowScreenRectSnapshot(LPWINDOW Window, LPRECT Rect) {
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
static BOOL GetDesktopCaptureState(LPWINDOW Window, LPWINDOW* CaptureWindow, I32* OffsetX, I32* OffsetY) {
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
static BOOL SetDesktopCaptureState(LPWINDOW Window, LPWINDOW CaptureWindow, I32 OffsetX, I32 OffsetY) {
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
        case EWM_CREATE: {
        } break;

        case EWM_DRAW: {
            HANDLE GC;
            RECTINFO RectInfo;
            RECT Rect;
            RECT ClipStorage[WINDOW_DIRTY_REGION_CAPACITY];
            RECT_REGION ClipRegion;
            RECT ClipRect;
            RECT RootVisibleStorage[WINDOW_DIRTY_REGION_CAPACITY];
            RECT_REGION RootVisibleRegion;
            RECT RootVisibleRect;
            UINT RootVisibleIndex;
            UINT ClipIndex;
            BRUSH Brush;
            COLOR Background;
            BOOL HasBackground;
            BOOL DrawingMarked = FALSE;

            SAFE_USE_VALID_ID((LPWINDOW)Window, KOID_WINDOW) {
                LockMutex(&(((LPWINDOW)Window)->Mutex), INFINITY);
                ((LPWINDOW)Window)->Status &= ~WINDOW_STATUS_NEED_DRAW;
                ((LPWINDOW)Window)->Status |= WINDOW_STATUS_DRAWING;
                DrawingMarked = TRUE;
                UnlockMutex(&(((LPWINDOW)Window)->Mutex));
            }

            if (BuildWindowDrawClipRegion((LPWINDOW)Window, &ClipRegion, ClipStorage, WINDOW_DIRTY_REGION_CAPACITY) == FALSE) {
                if (DrawingMarked != FALSE) {
                    SAFE_USE_VALID_ID((LPWINDOW)Window, KOID_WINDOW) {
                        LockMutex(&(((LPWINDOW)Window)->Mutex), INFINITY);
                        ((LPWINDOW)Window)->Status &= ~WINDOW_STATUS_DRAWING;
                        UnlockMutex(&(((LPWINDOW)Window)->Mutex));
                    }
                }
                break;
            }

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

                for (ClipIndex = 0; ClipIndex < RectRegionGetCount(&ClipRegion); ClipIndex++) {
                    if (RectRegionGetRect(&ClipRegion, ClipIndex, &ClipRect) == FALSE) continue;

                    if (BuildDesktopRootVisibleClipRegion((LPWINDOW)Window, &ClipRect, &RootVisibleRegion, RootVisibleStorage, WINDOW_DIRTY_REGION_CAPACITY) ==
                        FALSE) {
                        continue;
                    }

                    for (RootVisibleIndex = 0; RootVisibleIndex < RectRegionGetCount(&RootVisibleRegion); RootVisibleIndex++) {
                        if (RectRegionGetRect(&RootVisibleRegion, RootVisibleIndex, &RootVisibleRect) == FALSE) continue;
                        (void)SetGraphicsContextClipScreenRect(GC, &RootVisibleRect);
                        Rectangle(&RectInfo);
                    }
                }

                EndWindowDraw(Window);
            }

            if (DrawingMarked != FALSE) {
                SAFE_USE_VALID_ID((LPWINDOW)Window, KOID_WINDOW) {
                    LockMutex(&(((LPWINDOW)Window)->Mutex), INFINITY);
                    ((LPWINDOW)Window)->Status &= ~WINDOW_STATUS_DRAWING;
                    UnlockMutex(&(((LPWINDOW)Window)->Mutex));
                }
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
            return DefWindowFunc(Window, Message, Param1, Param2);
    }

    return 0;
}
