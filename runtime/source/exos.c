
/***************************************************************************\

    EXOS
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/exos.h"

/***************************************************************************/

extern unsigned exoscall(unsigned, unsigned);

/***************************************************************************/

HANDLE CreateTask(LPTASKINFO TaskInfo) { return (HANDLE)exoscall(SYSCALL_CreateTask, (U32)TaskInfo); }

/***************************************************************************/

BOOL KillTask(HANDLE Task) { return (BOOL)exoscall(SYSCALL_KillTask, (U32)Task); }

/***************************************************************************/

void Sleep(U32 MilliSeconds) { exoscall(SYSCALL_Sleep, MilliSeconds); }

/***************************************************************************/

BOOL GetMessage(HANDLE Target, LPMESSAGE Message, U32 First, U32 Last) {
    MESSAGEINFO MessageInfo;
    BOOL Result;

    Result = FALSE;

    MessageInfo.Hdr.Size = sizeof MessageInfo;
    MessageInfo.Hdr.Version = EXOS_ABI_VERSION;
    MessageInfo.Hdr.Flags = 0;
    MessageInfo.Target = Target;
    MessageInfo.First = First;
    MessageInfo.Last = Last;

    Result = (BOOL)exoscall(SYSCALL_GetMessage, (U32)&MessageInfo);

    Message->Time = MessageInfo.Time;
    Message->Target = MessageInfo.Target;
    Message->Message = MessageInfo.Message;
    Message->Param1 = MessageInfo.Param1;
    Message->Param2 = MessageInfo.Param2;

    return Result;
}

/***************************************************************************/

BOOL PeekMessage(HANDLE Target, LPMESSAGE Message, U32 First, U32 Last, U32 Flags) {
    UNUSED(Target);
    UNUSED(Message);
    UNUSED(First);
    UNUSED(Last);
    UNUSED(Flags);

    return FALSE;
}

/***************************************************************************/

BOOL DispatchMessage(LPMESSAGE Message) {
    MESSAGEINFO MessageInfo;

    MessageInfo.Hdr.Size = sizeof MessageInfo;
    MessageInfo.Hdr.Version = EXOS_ABI_VERSION;
    MessageInfo.Hdr.Flags = 0;
    MessageInfo.Time = Message->Time;
    MessageInfo.Target = Message->Target;
    MessageInfo.Message = Message->Message;
    MessageInfo.Param1 = Message->Param1;
    MessageInfo.Param2 = Message->Param2;

    return (BOOL)exoscall(SYSCALL_DispatchMessage, (U32)&MessageInfo);
}

/***************************************************************************/

BOOL PostMessage(HANDLE Target, U32 Message, U32 Param1, U32 Param2) {
    UNUSED(Target);

    MESSAGEINFO MessageInfo;

    MessageInfo.Hdr.Size = sizeof MessageInfo;
    MessageInfo.Hdr.Version = EXOS_ABI_VERSION;
    MessageInfo.Hdr.Flags = 0;
    MessageInfo.Message = Message;
    MessageInfo.Param1 = Param1;
    MessageInfo.Param2 = Param2;

    return (BOOL)exoscall(SYSCALL_PostMessage, (U32)&MessageInfo);
}

/***************************************************************************/

U32 SendMessage(HANDLE Target, U32 Message, U32 Param1, U32 Param2) {
    MESSAGEINFO MessageInfo;

    MessageInfo.Hdr.Size = sizeof MessageInfo;
    MessageInfo.Hdr.Version = EXOS_ABI_VERSION;
    MessageInfo.Hdr.Flags = 0;
    MessageInfo.Target = Target;
    MessageInfo.Message = Message;
    MessageInfo.Param1 = Param1;
    MessageInfo.Param2 = Param2;

    return (U32)exoscall(SYSCALL_SendMessage, (U32)&MessageInfo);
}

/***************************************************************************/

HANDLE CreateDesktop() { return (HANDLE)exoscall(SYSCALL_CreateDesktop, 0); }

/***************************************************************************/

BOOL ShowDesktop(HANDLE Desktop) { return (BOOL)exoscall(SYSCALL_ShowDesktop, (U32)Desktop); }

/***************************************************************************/

HANDLE GetDesktopWindow(HANDLE Desktop) { return (HANDLE)exoscall(SYSCALL_GetDesktopWindow, (U32)Desktop); }

/***************************************************************************/

HANDLE CreateWindow(HANDLE Parent, WINDOWFUNC Func, U32 Style, U32 ID, I32 PosX, I32 PosY, I32 SizeX, I32 SizeY) {
    WINDOWINFO WindowInfo;

    WindowInfo.Hdr.Size = sizeof WindowInfo;
    WindowInfo.Hdr.Version = EXOS_ABI_VERSION;
    WindowInfo.Hdr.Flags = 0;
    WindowInfo.Parent = Parent;
    WindowInfo.Function = Func;
    WindowInfo.Style = Style;
    WindowInfo.ID = ID;
    WindowInfo.WindowPosition.X = PosX;
    WindowInfo.WindowPosition.Y = PosY;
    WindowInfo.WindowSize.X = SizeX;
    WindowInfo.WindowSize.Y = SizeY;

    return (HANDLE)exoscall(SYSCALL_CreateWindow, (U32)&WindowInfo);
}

/***************************************************************************/

BOOL DestroyWindow(HANDLE Window) { return (BOOL)exoscall(SYSCALL_DeleteObject, (U32)Window); }

/***************************************************************************/

BOOL ShowWindow(HANDLE Window) {
    WINDOWINFO WindowInfo;

    WindowInfo.Hdr.Size = sizeof WindowInfo;
    WindowInfo.Hdr.Version = EXOS_ABI_VERSION;
    WindowInfo.Hdr.Flags = 0;
    WindowInfo.Window = Window;

    return (BOOL)exoscall(SYSCALL_ShowWindow, (U32)&WindowInfo);
}

/***************************************************************************/

BOOL HideWindow(HANDLE Window) {
    WINDOWINFO WindowInfo;

    WindowInfo.Hdr.Size = sizeof WindowInfo;
    WindowInfo.Hdr.Version = EXOS_ABI_VERSION;
    WindowInfo.Hdr.Flags = 0;
    WindowInfo.Window = Window;

    return (BOOL)exoscall(SYSCALL_HideWindow, (U32)&WindowInfo);
}

/***************************************************************************/

BOOL InvalidateWindowRect(HANDLE Window, LPRECT Rect) {
    WINDOWRECT WindowRect;

    WindowRect.Size = sizeof WindowRect;
    WindowRect.Window = Window;

    if (Rect != NULL) {
        WindowRect.Rect.X1 = Rect->X1;
        WindowRect.Rect.Y1 = Rect->Y1;
        WindowRect.Rect.X2 = Rect->X2;
        WindowRect.Rect.Y2 = Rect->Y2;
    } else {
        WindowRect.Rect.X1 = 0;
        WindowRect.Rect.Y1 = 0;
        WindowRect.Rect.X2 = 0;
        WindowRect.Rect.Y2 = 0;
    }

    return (BOOL)exoscall(SYSCALL_InvalidateWindowRect, (U32)&WindowRect);
}

/***************************************************************************/

U32 SetWindowProp(HANDLE Window, LPCSTR Name, U32 Value) {
    PROPINFO PropInfo;

    PropInfo.Hdr.Size = sizeof PropInfo;
    PropInfo.Hdr.Version = EXOS_ABI_VERSION;
    PropInfo.Hdr.Flags = 0;
    PropInfo.Window = Window;
    PropInfo.Name = Name;
    PropInfo.Value = Value;

    return exoscall(SYSCALL_SetWindowProp, (U32)&PropInfo);
}

/***************************************************************************/

U32 GetWindowProp(HANDLE Window, LPCSTR Name) {
    PROPINFO PropInfo;

    PropInfo.Hdr.Size = sizeof PropInfo;
    PropInfo.Hdr.Version = EXOS_ABI_VERSION;
    PropInfo.Hdr.Flags = 0;
    PropInfo.Window = Window;
    PropInfo.Name = Name;

    return exoscall(SYSCALL_GetWindowProp, (U32)&PropInfo);
}

/***************************************************************************/

HANDLE GetWindowGC(HANDLE Window) { return (HANDLE)exoscall(SYSCALL_GetWindowGC, (U32)Window); }

/***************************************************************************/

BOOL ReleaseWindowGC(HANDLE GC) { return (BOOL)exoscall(SYSCALL_ReleaseWindowGC, (U32)GC); }

/***************************************************************************/

HANDLE BeginWindowDraw(HANDLE Window) {
    UNUSED(Window);
    // return (HANDLE) exoscall(SYSCALL_BeginWindowDraw, (U32) Window);
    return NULL;
}

/***************************************************************************/

BOOL EndWindowDraw(HANDLE Window) {
    UNUSED(Window);
    // return (BOOL) exoscall(SYSCALL_EndWindowDraw, (U32) Window);
    return NULL;
}

/***************************************************************************/

BOOL GetWindowRect(HANDLE Window, LPRECT Rect) {
    WINDOWRECT WindowRect;

    if (Window == NULL) return FALSE;
    if (Rect == NULL) return FALSE;

    WindowRect.Size = sizeof WindowRect;
    WindowRect.Window = Window;
    WindowRect.Rect.X1 = 0;
    WindowRect.Rect.Y1 = 0;
    WindowRect.Rect.X2 = 0;
    WindowRect.Rect.Y2 = 0;

    exoscall(SYSCALL_GetWindowRect, (U32)&WindowRect);

    Rect->X1 = WindowRect.Rect.X1;
    Rect->Y1 = WindowRect.Rect.Y1;
    Rect->X2 = WindowRect.Rect.X2;
    Rect->Y2 = WindowRect.Rect.Y2;

    return TRUE;
}

/***************************************************************************/

HANDLE GetSystemBrush(U32 Index) { return exoscall(SYSCALL_GetSystemBrush, Index); }

/***************************************************************************/

HANDLE GetSystemPen(U32 Index) { return exoscall(SYSCALL_GetSystemPen, Index); }

/***************************************************************************/

HANDLE CreateBrush(COLOR Color, U32 Pattern) {
    BRUSHINFO BrushInfo;

    BrushInfo.Size = sizeof BrushInfo;
    BrushInfo.Color = Color;
    BrushInfo.Pattern = Pattern;

    return exoscall(SYSCALL_CreateBrush, (U32)&BrushInfo);
}

/***************************************************************************/

HANDLE CreatePen(COLOR Color, U32 Pattern) {
    PENINFO PenInfo;

    PenInfo.Size = sizeof PenInfo;
    PenInfo.Color = Color;
    PenInfo.Pattern = Pattern;

    return exoscall(SYSCALL_CreatePen, (U32)&PenInfo);
}

/***************************************************************************/

HANDLE SelectBrush(HANDLE GC, HANDLE Brush) {
    GCSELECT Select;

    Select.Size = sizeof Select;
    Select.GC = GC;
    Select.Object = Brush;

    return (HANDLE)exoscall(SYSCALL_SelectBrush, (U32)&Select);
}

/***************************************************************************/

HANDLE SelectPen(HANDLE GC, HANDLE Pen) {
    GCSELECT Select;

    Select.Size = sizeof Select;
    Select.GC = GC;
    Select.Object = Pen;

    return (HANDLE)exoscall(SYSCALL_SelectPen, (U32)&Select);
}

/***************************************************************************/

U32 DefWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    MESSAGEINFO MessageInfo;

    MessageInfo.Hdr.Size = sizeof MessageInfo;
    MessageInfo.Hdr.Version = EXOS_ABI_VERSION;
    MessageInfo.Hdr.Flags = 0;
    MessageInfo.Target = Window;
    MessageInfo.Message = Message;
    MessageInfo.Param1 = Param1;
    MessageInfo.Param2 = Param2;

    return (U32)exoscall(SYSCALL_DefWindowFunc, (U32)&MessageInfo);
}

/***************************************************************************/

U32 SetPixel(HANDLE GC, U32 X, U32 Y) {
    PIXELINFO PixelInfo;

    PixelInfo.Size = sizeof PixelInfo;
    PixelInfo.GC = GC;
    PixelInfo.X = X;
    PixelInfo.Y = Y;

    return (U32)exoscall(SYSCALL_SetPixel, (U32)&PixelInfo);
}

/***************************************************************************/

U32 GetPixel(HANDLE GC, U32 X, U32 Y) {
    PIXELINFO PixelInfo;

    PixelInfo.Size = sizeof PixelInfo;
    PixelInfo.GC = GC;
    PixelInfo.X = X;
    PixelInfo.Y = Y;

    return (U32)exoscall(SYSCALL_GetPixel, (U32)&PixelInfo);
}

/***************************************************************************/

void Line(HANDLE GC, U32 X1, U32 Y1, U32 X2, U32 Y2) {
    LINEINFO LineInfo;

    LineInfo.Size = sizeof LineInfo;
    LineInfo.GC = GC;
    LineInfo.X1 = X1;
    LineInfo.Y1 = Y1;
    LineInfo.X2 = X2;
    LineInfo.Y2 = Y2;

    exoscall(SYSCALL_Line, (U32)&LineInfo);
}

/***************************************************************************/

void Rectangle(HANDLE GC, U32 X1, U32 Y1, U32 X2, U32 Y2) {
    RECTINFO RectInfo;

    RectInfo.Size = sizeof RectInfo;
    RectInfo.GC = GC;
    RectInfo.X1 = X1;
    RectInfo.Y1 = Y1;
    RectInfo.X2 = X2;
    RectInfo.Y2 = Y2;

    exoscall(SYSCALL_Rectangle, (U32)&RectInfo);
}

/***************************************************************************/

BOOL GetMousePos(LPPOINT Point) { return (BOOL)exoscall(SYSCALL_GetMousePos, (U32)Point); }

/***************************************************************************/

U32 GetMouseButtons() { return (U32)exoscall(SYSCALL_GetMouseButtons, 0); }

/***************************************************************************/

HANDLE CaptureMouse(HANDLE Window) {
    UNUSED(Window);
    return NULL;
}

/***************************************************************************/

BOOL ReleaseMouse() { return FALSE; }

/***************************************************************************/
