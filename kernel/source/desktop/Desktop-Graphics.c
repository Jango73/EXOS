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
#include "Desktop-NonClient.h"
#include "Desktop-ThemeResolver.h"
#include "Desktop-ThemeTokens.h"
#include "Kernel.h"
#include "Log.h"
#include "Desktop.h"
#include "input/Mouse.h"
#include "process/Task-Messaging.h"

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

static U32 DATA_SECTION BeginWindowDrawLogCount = 0;
static U32 DATA_SECTION DefWindowDrawLogCount = 0;
static U32 DATA_SECTION DesktopRootDrawLogCount = 0;

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

    //-------------------------------------
    // Check validity of parameters

    if (This == NULL) return FALSE;
    if (This->TypeID != KOID_WINDOW) return FALSE;

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
    if (GC == NULL && BeginWindowDrawLogCount < 64) {
        DEBUG(TEXT("[BeginWindowDraw] GetWindowGC returned NULL window=%p id=%x style=%x status=%x"),
            This,
            This->WindowID,
            This->Style,
            This->Status);
        BeginWindowDrawLogCount++;
    }

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

    //-------------------------------------
    // Check validity of parameters

    if (PixelInfo == NULL) return FALSE;

    Context = (LPGRAPHICSCONTEXT)PixelInfo->GC;

    if (Context == NULL) return FALSE;
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    PixelInfo->X = Context->Origin.X + PixelInfo->X;
    PixelInfo->Y = Context->Origin.Y + PixelInfo->Y;

    Context->Driver->Command(DF_GFX_SETPIXEL, (UINT)PixelInfo);

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
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    PixelInfo->X = Context->Origin.X + PixelInfo->X;
    PixelInfo->Y = Context->Origin.Y + PixelInfo->Y;

    Context->Driver->Command(DF_GFX_GETPIXEL, (UINT)PixelInfo);

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
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    LineInfo->X1 = Context->Origin.X + LineInfo->X1;
    LineInfo->Y1 = Context->Origin.Y + LineInfo->Y1;
    LineInfo->X2 = Context->Origin.X + LineInfo->X2;
    LineInfo->Y2 = Context->Origin.Y + LineInfo->Y2;

    Context->Driver->Command(DF_GFX_LINE, (UINT)LineInfo);

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
    if (Context->TypeID != KOID_GRAPHICSCONTEXT) return FALSE;

    RectInfo->X1 = Context->Origin.X + RectInfo->X1;
    RectInfo->Y1 = Context->Origin.Y + RectInfo->Y1;
    RectInfo->X2 = Context->Origin.X + RectInfo->X2;
    RectInfo->Y2 = Context->Origin.Y + RectInfo->Y2;

    Context->Driver->Command(DF_GFX_RECTANGLE, (UINT)RectInfo);

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
            RECT Rect;
            LPWINDOW This = (LPWINDOW)Window;

            if (DefWindowDrawLogCount < 128) {
                DEBUG(TEXT("[DefWindowFunc] EWM_DRAW window=%p id=%x style=%x status=%x"),
                    This,
                    This != NULL ? This->WindowID : 0,
                    This != NULL ? This->Style : 0,
                    This != NULL ? This->Status : 0);
                DefWindowDrawLogCount++;
            }

            GC = BeginWindowDraw(Window);

            if (GC) {
                GetWindowRect(Window, &Rect);

                if (DefWindowDrawLogCount < 128) {
                    DEBUG(TEXT("[DefWindowFunc] Draw rect id=%x local_rect=(%u,%u)-(%u,%u)"),
                        This != NULL ? This->WindowID : 0,
                        UNSIGNED(Rect.X1),
                        UNSIGNED(Rect.Y1),
                        UNSIGNED(Rect.X2),
                        UNSIGNED(Rect.Y2));
                    DefWindowDrawLogCount++;
                }

                if (ShouldDrawWindowNonClient(This)) {
                    DrawWindowNonClient(Window, GC, &Rect);
                }

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
            SetWindowProp(Window, Prop_MouseX, 0);
            SetWindowProp(Window, Prop_MouseY, 0);
        } break;

        case EWM_DRAW: {
            HANDLE GC;
            RECTINFO RectInfo;
            RECT Rect;
            BRUSH Brush;
            COLOR Background;
            BOOL HasBackground;

            GC = BeginWindowDraw(Window);

            if (GC) {
                GetWindowRect(Window, &Rect);

                if (DesktopRootDrawLogCount < 64) {
                    DEBUG(TEXT("[DesktopWindowFunc] EWM_DRAW root id=%x rect=(%u,%u)-(%u,%u)"),
                        ((LPWINDOW)Window)->WindowID,
                        UNSIGNED(Rect.X1),
                        UNSIGNED(Rect.Y1),
                        UNSIGNED(Rect.X2),
                        UNSIGNED(Rect.Y2));
                    DesktopRootDrawLogCount++;
                }

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

                PostMessage
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

            X = GetMouseDriver()->Command(DF_MOUSE_GETDELTAX, 0);
            Y = GetMouseDriver()->Command(DF_MOUSE_GETDELTAY, 0);

            Position.X = SIGNED(X);
            Position.Y = SIGNED(Y);

            Target = (LPWINDOW)WindowHitTest(Window, &Position);

            if (Target) {
                PostMessage((HANDLE)Target, EWM_MOUSEDOWN, Param1, Param2);
            }
        } break;

        default:
            return DefWindowFunc(Window, Message, Param1, Param2);
    }

    return 0;
}
