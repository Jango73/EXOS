
/***************************************************************************\

    EXOS Portal
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../../runtime/include/exos.h"

/***************************************************************************/

HANDLE MainWindow = NULL;
HANDLE RedPen = NULL;
HANDLE RedBrush = NULL;
HANDLE GreenPen = NULL;
HANDLE GreenBrush = NULL;

U32 EXOSMain(U32, LPSTR[]);

static STR Prop_Over[] = "OVER";
static STR Prop_Down[] = "DOWN";

// static void* TestFuncPtr = (void*)EXOSMain;

/***************************************************************************/

/*
U32 __start__ ()
{
  unsigned char* argv [2];

  argv[0] = NULL;
  argv[1] = NULL;

  return EXMain(0, argv);
}
*/

/***************************************************************************/

void DrawFrame3D(HANDLE GC, LPRECT Rect, BOOL Invert, BOOL Fill) {
    if (Fill == TRUE) {
        SelectPen(GC, NULL);
        SelectBrush(GC, GetSystemBrush(SM_COLOR_NORMAL));
        Rectangle(GC, Rect->X1, Rect->Y1, Rect->X2, Rect->Y2);
    }
    if (Invert == FALSE) {
        SelectPen(GC, GetSystemPen(SM_COLOR_HIGHLIGHT));
        Line(GC, Rect->X1, Rect->Y2, Rect->X1, Rect->Y1);
        Line(GC, Rect->X1, Rect->Y1, Rect->X2, Rect->Y1);
        SelectPen(GC, GetSystemPen(SM_COLOR_DARK_SHADOW));
        Line(GC, Rect->X2, Rect->Y1, Rect->X2, Rect->Y2);
        Line(GC, Rect->X2, Rect->Y2, Rect->X1, Rect->Y2);
        SelectPen(GC, GetSystemPen(SM_COLOR_LIGHT_SHADOW));
        Line(GC, Rect->X2 - 1, Rect->Y1 + 1, Rect->X2 - 1, Rect->Y2 - 1);
        Line(GC, Rect->X2 - 1, Rect->Y2 - 1, Rect->X1 + 1, Rect->Y2 - 1);
    } else {
        SelectPen(GC, GetSystemPen(SM_COLOR_DARK_SHADOW));
        Line(GC, Rect->X1, Rect->Y2, Rect->X1, Rect->Y1);
        Line(GC, Rect->X1, Rect->Y1, Rect->X2, Rect->Y1);
        SelectPen(GC, GetSystemPen(SM_COLOR_HIGHLIGHT));
        Line(GC, Rect->X2, Rect->Y1, Rect->X2, Rect->Y2);
        Line(GC, Rect->X2, Rect->Y2, Rect->X1, Rect->Y2);
    }
}

/***************************************************************************/

U32 OnButtonCreate(HANDLE Window, U32 Param1, U32 Param2) {
    UNUSED(Param1);
    UNUSED(Param2);

    SetWindowProp(Window, Prop_Down, 0);
    SetWindowProp(Window, Prop_Over, 0);

    return 0;
}

/***************************************************************************/

U32 OnButtonLeftButtonDown(HANDLE Window, U32 Param1, U32 Param2) {
    UNUSED(Param1);
    UNUSED(Param2);

    SetWindowProp(Window, Prop_Down, 1);
    InvalidateWindowRect(Window, NULL);

    return 0;
}

/***************************************************************************/

U32 OnButtonLeftButtonUp(HANDLE Window, U32 Param1, U32 Param2) {
    UNUSED(Param1);
    UNUSED(Param2);

    InvalidateWindowRect(Window, NULL);
    SetWindowProp(Window, Prop_Down, 0);

    /*
      if (GetProp(Window, Prop_Over))
      {
    U32 ID = GetWindowID(Window);
    PostMessage(GetWindowParent(Window), EWM_COMMAND, MAKEU32(ID,
      BN_CLICKED), (U32) Window);
      }
    */

    SetWindowProp(Window, Prop_Over, 0);
    ReleaseMouse();

    return 0;
}

/***************************************************************************/

U32 OnButtonMouseMove(HANDLE Window, U32 Param1, U32 Param2) {
    RECT Rect;
    POINT Size;
    POINT Mouse;

    GetWindowRect(Window, &Rect);

    Size.X = (Rect.X2 - Rect.X1) + 1;
    Size.Y = (Rect.Y2 - Rect.Y1) + 1;
    Mouse.X = SIGNED(Param1);
    Mouse.Y = SIGNED(Param2);

    if (Mouse.X >= 0 && Mouse.Y >= 0 && Mouse.X <= Size.X && Mouse.Y <= Size.Y) {
        if (!GetWindowProp(Window, Prop_Over)) {
            InvalidateWindowRect(Window, NULL);
            SetWindowProp(Window, Prop_Over, 1);
            CaptureMouse(Window);
        }
    } else {
        if (GetWindowProp(Window, Prop_Over)) {
            InvalidateWindowRect(Window, NULL);
            SetWindowProp(Window, Prop_Over, 0);
            if (!GetWindowProp(Window, Prop_Down)) ReleaseMouse();
        }
    }

    return 0;
}

/***************************************************************************/

U32 OnButtonDraw(HANDLE Window, U32 Param1, U32 Param2) {
    UNUSED(Param1);
    UNUSED(Param2);

    RECT Rect;

    HANDLE GC = GetWindowGC(Window);

    // if (GC = BeginWindowDraw(Window))
    if (GC != NULL) {
        GetWindowRect(Window, &Rect);

        if (GetWindowProp(Window, Prop_Down)) {
            DrawFrame3D(GC, &Rect, 1, TRUE);
        } else {
            DrawFrame3D(GC, &Rect, 0, TRUE);
        }

        ReleaseWindowGC(Window);
        // EndWindowDraw(Window);
    }

    return 0;
}

/***************************************************************************/

U32 ButtonFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    switch (Message) {
        case EWM_CREATE:
            return OnButtonCreate(Window, Param1, Param2);
        case EWM_DRAW:
            return OnButtonDraw(Window, Param1, Param2);

        case EWM_MOUSEDOWN: {
            switch (Param1) {
                case MB_LEFT:
                    return OnButtonLeftButtonDown(Window, Param1, Param2);
            }
        } break;

        case EWM_MOUSEUP: {
            switch (Param1) {
                case MB_LEFT:
                    return OnButtonLeftButtonUp(Window, Param1, Param2);
            }
        } break;

        default:
            return DefWindowFunc(Window, Message, Param1, Param2);
    }

    return 0;
}

/***************************************************************************/

U32 MainWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    switch (Message) {
        case EWM_CREATE: {
        } break;

        case EWM_DELETE: {
        } break;

        case EWM_DRAW: {
            RECT Rect;
            HANDLE GC = GetWindowGC(Window);

            if (GC != NULL) {
                GetWindowRect(Window, &Rect);

                DrawFrame3D(GC, &Rect, 0, FALSE);

                Rect.X1++;
                Rect.Y1++;
                Rect.X2--;
                Rect.Y2--;

                SelectPen(GC, NULL);

                SelectBrush(GC, GetSystemBrush(SM_COLOR_TITLE_BAR));
                Rectangle(GC, Rect.X1, Rect.Y1, Rect.X2, Rect.Y1 + 19);

                SelectBrush(GC, GetSystemBrush(SM_COLOR_NORMAL));
                Rectangle(GC, Rect.X1, Rect.Y1 + 20, Rect.X2, Rect.Y2);

                ReleaseWindowGC(GC);
            }
        } break;

        default:
            return DefWindowFunc(Window, Message, Param1, Param2);
    }

    return 0;
}

/***************************************************************************/

U32 DesktopTask(LPVOID Param) {
    UNUSED(Param);

    HANDLE Desktop;
    HANDLE Window;
    POINT MousePos;
    POINT NewMousePos;
    U32 MouseButtons;
    U32 NewMouseButtons;

    Desktop = CreateDesktop();
    if (Desktop == NULL) return MAX_U32;

    Window = GetDesktopWindow(Desktop);

    ShowDesktop(Desktop);

    MousePos.X = 0;
    MousePos.Y = 0;
    MouseButtons = 0;

    while (1) {
        GetMousePos(&NewMousePos);

        if (NewMousePos.X != MousePos.X || NewMousePos.Y != MousePos.Y) {
            MousePos.X = NewMousePos.X;
            MousePos.Y = NewMousePos.Y;

            SendMessage(Window, EWM_MOUSEMOVE, UNSIGNED(MousePos.X), UNSIGNED(MousePos.Y));
        }

        NewMouseButtons = GetMouseButtons();

        if (NewMouseButtons != MouseButtons) {
            U32 DownButtons = 0;
            U32 UpButtons = 0;

            if ((MouseButtons & MB_LEFT) != (NewMouseButtons & MB_LEFT)) {
                if (NewMouseButtons & MB_LEFT)
                    DownButtons |= MB_LEFT;
                else
                    UpButtons |= MB_LEFT;
            }

            if ((MouseButtons & MB_RIGHT) != (NewMouseButtons & MB_RIGHT)) {
                if (NewMouseButtons & MB_RIGHT)
                    DownButtons |= MB_RIGHT;
                else
                    UpButtons |= MB_RIGHT;
            }

            if ((MouseButtons & MB_MIDDLE) != (NewMouseButtons & MB_MIDDLE)) {
                if (NewMouseButtons & MB_MIDDLE)
                    DownButtons |= MB_MIDDLE;
                else
                    UpButtons |= MB_MIDDLE;
            }

            MouseButtons = NewMouseButtons;

            // if (DownButtons) PostMessage(Window, EWM_MOUSEDOWN, DownButtons,
            // 0); if (UpButtons)   PostMessage(Window, EWM_MOUSEUP, UpButtons,
            // 0);

            if (DownButtons) SendMessage(Window, EWM_MOUSEDOWN, DownButtons, 0);
            if (UpButtons) SendMessage(Window, EWM_MOUSEUP, UpButtons, 0);
        }

        /*
            if (GC = GetWindowGC(Window))
            {
              Sequence = 1 - Sequence;
              SelectBrush(GC, Sequence ? RedBrush : GreenBrush);
              Rectangle(GC, 20, 20, 40, 40);
              ReleaseWindowGC(GC);
            }
        */
    }

    // DeleteObject(Desktop);
}

/***************************************************************************/

BOOL InitApplication(void) {
    TASKINFO TaskInfo;

    TaskInfo.Header.Size = sizeof(TASKINFO);
    TaskInfo.Header.Version = EXOS_ABI_VERSION;
    TaskInfo.Header.Flags = 0;
    TaskInfo.Func = DesktopTask;
    TaskInfo.Parameter = NULL;
    TaskInfo.StackSize = 65536;
    TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
    TaskInfo.Flags = 0;

    if (CreateTask(&TaskInfo) == NULL) return FALSE;

    Sleep(1000);

    RedPen = CreatePen(MAKERGB(255, 0, 0), 0xFFFFFFFF);
    RedBrush = CreateBrush(MAKERGB(255, 0, 0), 0xFFFFFFFF);

    GreenPen = CreatePen(MAKERGB(0, 255, 0), 0xFFFFFFFF);
    GreenBrush = CreateBrush(MAKERGB(0, 255, 0), 0xFFFFFFFF);

    MainWindow = CreateWindow(NULL, MainWindowFunc, 0, 0, 100, 100, 400, 300);

    if (MainWindow == NULL) return FALSE;

    CreateWindow(MainWindow, ButtonFunc, EWS_VISIBLE, 0, 400 - 90, 300 - 60, 80, 20);
    CreateWindow(MainWindow, ButtonFunc, EWS_VISIBLE, 0, 400 - 90, 300 - 30, 80, 20);

    ShowWindow(MainWindow);

    return TRUE;
}

/***************************************************************************/

U32 EXOSMain(U32 NumArguments, LPSTR Arguments[]) {
    UNUSED(NumArguments);
    UNUSED(Arguments);

    MESSAGE Message;

    if (InitApplication() == FALSE) return MAX_U32;

    while (GetMessage(NULL, &Message, 0, 0)) {
        DispatchMessage(&Message);
    }

    return 0;
}

/***************************************************************************/
