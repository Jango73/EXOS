
/***************************************************************************\

    EXOS
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef EXOS_H_INCLUDED
#define EXOS_H_INCLUDED

/***************************************************************************/

#include "../../kernel/include/Base.h"
#include "../../kernel/include/User.h"

/***************************************************************************/

typedef struct tag_MESSAGE {
    HANDLE Target;
    SYSTEMTIME Time;
    U32 Message;
    U32 Param1;
    U32 Param2;
} MESSAGE, *LPMESSAGE;

/***************************************************************************/

HANDLE CreateTask(LPTASKINFO);
BOOL KillTask(HANDLE);
void Sleep(U32);
BOOL GetMessage(HANDLE, LPMESSAGE, U32, U32);
BOOL PeekMessage(HANDLE, LPMESSAGE, U32, U32, U32);
BOOL DispatchMessage(LPMESSAGE);
BOOL PostMessage(HANDLE, U32, U32, U32);
U32 SendMessage(HANDLE, U32, U32, U32);
HANDLE CreateDesktop(void);
BOOL ShowDesktop(HANDLE);
HANDLE GetDesktopWindow(HANDLE);
HANDLE CreateWindow(HANDLE, WINDOWFUNC, U32, U32, I32, I32, I32, I32);
BOOL DestroyWindow(HANDLE);
BOOL ShowWindow(HANDLE);
BOOL HideWindow(HANDLE);
BOOL InvalidateWindowRect(HANDLE, LPRECT);
U32 SetWindowProp(HANDLE, LPCSTR, U32);
U32 GetWindowProp(HANDLE, LPCSTR);
HANDLE GetWindowGC(HANDLE);
BOOL ReleaseWindowGC(HANDLE);
HANDLE BeginWindowDraw(HANDLE);
BOOL EndWindowDraw(HANDLE);
BOOL GetWindowRect(HANDLE, LPRECT);
HANDLE GetSystemBrush(U32);
HANDLE GetSystemPen(U32);
HANDLE CreateBrush(COLOR, U32);
HANDLE CreatePen(COLOR, U32);
HANDLE SelectBrush(HANDLE, HANDLE);
HANDLE SelectPen(HANDLE, HANDLE);
U32 DefWindowFunc(HANDLE, U32, U32, U32);
U32 SetPixel(HANDLE, U32, U32);
U32 GetPixel(HANDLE, U32, U32);
void Line(HANDLE, U32, U32, U32, U32);
void Rectangle(HANDLE, U32, U32, U32, U32);
BOOL GetMousePos(LPPOINT);
U32 GetMouseButtons(void);
HANDLE CaptureMouse(HANDLE);
BOOL ReleaseMouse(void);

/***************************************************************************/

#endif
