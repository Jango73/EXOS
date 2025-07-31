
// USER.h

/***************************************************************************\

  EXOS
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#ifndef USER_H_INCLUDED
#define USER_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

#pragma pack(1)

/***************************************************************************/

// EXOS Base Services

#define SYSCALL_GetVersion 0x00000000
#define SYSCALL_GetSystemInfo 0x00000001
#define SYSCALL_GetLastError 0x00000002
#define SYSCALL_SetLastError 0x00000003
#define SYSCALL_GetSystemTime 0x00000004
#define SYSCALL_GetLocalTime 0x00000005
#define SYSCALL_SetLocalTime 0x00000006
#define SYSCALL_DeleteObject 0x00000007
#define SYSCALL_CreateProcess 0x00000008
#define SYSCALL_KillProcess 0x00000009
#define SYSCALL_CreateTask 0x0000000A
#define SYSCALL_KillTask 0x0000000B
#define SYSCALL_SuspendTask 0x0000000C
#define SYSCALL_ResumeTask 0x0000000D
#define SYSCALL_Sleep 0x0000000E
#define SYSCALL_PostMessage 0x0000000F
#define SYSCALL_SendMessage 0x00000010
#define SYSCALL_PeekMessage 0x00000011
#define SYSCALL_GetMessage 0x00000012
#define SYSCALL_DispatchMessage 0x00000013
#define SYSCALL_CreateSemaphore 0x00000014
#define SYSCALL_LockSemaphore 0x00000015
#define SYSCALL_UnlockSemaphore 0x00000016
#define SYSCALL_VirtualAlloc 0x00000017
#define SYSCALL_VirtualFree 0x00000018
#define SYSCALL_GetProcessHeap 0x00000019
#define SYSCALL_HeapAlloc 0x0000001A
#define SYSCALL_HeapFree 0x0000001B
#define SYSCALL_EnumVolumes 0x0000001C
#define SYSCALL_GetVolumeInfo 0x0000001D
#define SYSCALL_OpenFile 0x0000001E
#define SYSCALL_ReadFile 0x0000001F
#define SYSCALL_WriteFile 0x00000020
#define SYSCALL_GetFileSize 0x00000021
#define SYSCALL_GetFilePointer 0x00000022
#define SYSCALL_SetFilePointer 0x00000023
#define SYSCALL_FindFirstFile 0x00000024
#define SYSCALL_FindNextFile 0x00000025
#define SYSCALL_CreateFileMapping 0x00000026
#define SYSCALL_OpenFileMapping 0x00000027
#define SYSCALL_MapViewOfFile 0x00000028
#define SYSCALL_UnmapViewOfFile 0x00000029
#define SYSCALL_ConsolePeekKey 0x0000002A
#define SYSCALL_ConsoleGetKey 0x0000002B
#define SYSCALL_ConsolePrint 0x0000002C
#define SYSCALL_ConsoleGetString 0x0000002D
#define SYSCALL_ConsoleGotoXY 0x0000002E

// EXOS Windowing Services

#define SYSCALL_CreateDesktop 0x00000040
#define SYSCALL_ShowDesktop 0x00000041
#define SYSCALL_GetDesktopWindow 0x00000042
#define SYSCALL_CreateWindow 0x00000043
#define SYSCALL_ShowWindow 0x00000044
#define SYSCALL_HideWindow 0x00000045
#define SYSCALL_MoveWindow 0x00000046
#define SYSCALL_SizeWindow 0x00000047
#define SYSCALL_SetWindowFunc 0x00000048
#define SYSCALL_GetWindowFunc 0x00000049
#define SYSCALL_SetWindowStyle 0x0000004A
#define SYSCALL_GetWindowStyle 0x0000004B
#define SYSCALL_SetWindowProp 0x0000004C
#define SYSCALL_GetWindowProp 0x0000004D
#define SYSCALL_GetWindowRect 0x0000004E
#define SYSCALL_InvalidateWindowRect 0x0000004F
#define SYSCALL_GetWindowGC 0x00000050
#define SYSCALL_ReleaseWindowGC 0x00000051
#define SYSCALL_EnumWindows 0x00000052
#define SYSCALL_DefWindowFunc 0x00000053
#define SYSCALL_GetSystemBrush 0x00000054
#define SYSCALL_GetSystemPen 0x00000055
#define SYSCALL_CreateBrush 0x00000056
#define SYSCALL_CreatePen 0x00000057
#define SYSCALL_SelectBrush 0x00000058
#define SYSCALL_SelectPen 0x00000059
#define SYSCALL_SetPixel 0x0000005A
#define SYSCALL_GetPixel 0x0000005B
#define SYSCALL_Line 0x0000005C
#define SYSCALL_Rectangle 0x0000005D
#define SYSCALL_CreateRectRegion 0x0000005E
#define SYSCALL_CreatePolyRegion 0x0000005F
#define SYSCALL_MoveRegion 0x00000060
#define SYSCALL_CombineRegion 0x00000061
#define SYSCALL_GetMousePos 0x00000062
#define SYSCALL_SetMousePos 0x00000063
#define SYSCALL_GetMouseButtons 0x00000064
#define SYSCALL_ShowMouse 0x00000065
#define SYSCALL_HideMouse 0x00000066
#define SYSCALL_ClipMouse 0x00000067
#define SYSCALL_CaptureMouse 0x00000068
#define SYSCALL_ReleaseMouse 0x00000069

/***************************************************************************/

typedef struct tag_SYSTEMINFO {
    U32 Size;
    U32 TotalPhysicalMemory;
    U32 PhysicalMemoryUsed;
    U32 PhysicalMemoryAvail;
    U32 TotalSwapMemory;
    U32 SwapMemoryUsed;
    U32 SwapMemoryAvail;
    U32 TotalMemoryUsed;
    U32 TotalMemoryAvail;
    U32 PageSize;
    U32 TotalPhysicalPages;
    U32 MinimumLinearAddress;
    U32 MaximumLinearAddress;
    U32 NumProcesses;
    U32 NumTasks;
    STR UserName[MAX_USER_NAME];
    STR CompanyName[MAX_USER_NAME];
} SYSTEMINFO, *LPSYSTEMINFO;

/***************************************************************************/

typedef struct tag_SECURITYATTRIBUTES {
    U32 Nothing;
} SECURITYATTRIBUTES, *LPSECURITYATTRIBUTES;

/***************************************************************************/

typedef struct tag_PROCESSINFO {
    U32 Size;
    U32 Flags;
    LPCSTR FileName;
    LPCSTR CommandLine;
    HANDLE StdOut;
    HANDLE StdIn;
    HANDLE StdErr;
    HANDLE Process;
    HANDLE Task;
    SECURITYATTRIBUTES Security;
} PROCESSINFO, *LPPROCESSINFO;

/***************************************************************************/

typedef struct tag_TASKINFO {
    U32 Size;
    TASKFUNC Func;
    LPVOID Parameter;
    U32 StackSize;
    U32 Priority;
    U32 Flags;
    SECURITYATTRIBUTES Security;
} TASKINFO, *LPTASKINFO;

#define TASK_PRIORITY_LOWEST 0x00
#define TASK_PRIORITY_LOWER 0x04
#define TASK_PRIORITY_MEDIUM 0x08
#define TASK_PRIORITY_HIGHER 0x0C
#define TASK_PRIORITY_HIGHEST 0x10
#define TASK_PRIORITY_CRITICAL 0xFF

/***************************************************************************/

typedef struct tag_MESSAGEINFO {
    U32 Size;
    SYSTEMTIME Time;
    U32 First;
    U32 Last;
    HANDLE Target;
    U32 Message;
    U32 Param1;
    U32 Param2;
} MESSAGEINFO, *LPMESSAGEINFO;

/***************************************************************************/

typedef struct tag_SEMINFO {
    U32 Size;
    HANDLE Semaphore;
    U32 MilliSeconds;
} SEMINFO, *LPSEMINFO;

/***************************************************************************/

typedef struct tag_VIRTUALINFO {
    U32 Base;
    U32 Size;
    U32 Flags;
} VIRTUALINFO, *LPVIRTUALINFO;

#define ALLOC_PAGES_RESERVE 0x00000000
#define ALLOC_PAGES_COMMIT 0x00000001
#define ALLOC_PAGES_READONLY 0x00000000
#define ALLOC_PAGES_READWRITE 0x00000002

/***************************************************************************/

typedef BOOL (*ENUMVOLUMESFUNC)(HANDLE, LPVOID);

typedef struct tag_ENUMVOLUMESINFO {
    U32 Size;
    ENUMVOLUMESFUNC Func;
    LPVOID Parameter;
} ENUMVOLUMESINFO, *LPENUMVOLUMESINFO;

/***************************************************************************/

typedef struct tag_VOLUMEINFO {
    U32 Size;
    HANDLE Volume;
    STR Name[MAX_FS_LOGICAL_NAME];
} VOLUMEINFO, *LPVOLUMEINFO;

/***************************************************************************/

typedef struct tag_FILEOPENINFO {
    U32 Size;
    LPCSTR Name;
    U32 Flags;
} FILEOPENINFO, *LPFILEOPENINFO;

#define FILE_OPEN_READ 0x00000001
#define FILE_OPEN_WRITE 0x00000002
#define FILE_OPEN_APPEND 0x00000004
#define FILE_OPEN_EXISTING 0x00000008
#define FILE_OPEN_CREATE_ALWAYS 0x00000010

/***************************************************************************/

typedef struct tag_FILECOPYINFO {
    U32 Size;
    LPCSTR Source;
    LPCSTR Destination;
    U32 Flags;
} FILECOPYINFO, *LPFILECOPYINFO;

/***************************************************************************/

typedef struct tag_FILEOPERATION {
    U32 Size;
    HANDLE File;
    U32 NumBytes;
    LPVOID Buffer;
} FILEOPERATION, *LPFILEOPERATION;

/***************************************************************************/

typedef struct tag_KEYCODE {
    U8 VirtualKey;
    STR ASCIICode;
    USTR Unicode;
} KEYCODE, *LPKEYCODE;

/***************************************************************************/

typedef struct tag_POINT {
    I32 X, Y;
} POINT, *LPPOINT;

/***************************************************************************/

typedef struct tag_RECT {
    I32 X1, Y1;
    I32 X2, Y2;
} RECT, *LPRECT;

/***************************************************************************/

typedef U32 (*WINDOWFUNC)(HANDLE, U32, U32, U32);

/***************************************************************************/

typedef struct tag_WINDOWINFO {
    U32 Size;
    HANDLE Window;
    HANDLE Parent;
    WINDOWFUNC Function;
    U32 Style;
    U32 ID;
    POINT WindowPosition;
    POINT WindowSize;
    BOOL ShowHide;
} WINDOWINFO, *LPWINDOWINFO;

/***************************************************************************/

typedef struct tag_PROPINFO {
    U32 Size;
    HANDLE Window;
    LPCSTR Name;
    U32 Value;
} PROPINFO, *LPPROPINFO;

/***************************************************************************/

typedef struct tag_WINDOWRECT {
    U32 Size;
    HANDLE Window;
    RECT Rect;
} WINDOWRECT, *LPWINDOWRECT;

/***************************************************************************/

typedef struct tag_GCSELECT {
    U32 Size;
    HANDLE GC;
    HANDLE Object;
} GCSELECT, *LPGCSELECT;

/***************************************************************************/

typedef struct tag_BRUSHINFO {
    U32 Size;
    COLOR Color;
    U32 Pattern;
    U32 Flags;
} BRUSHINFO, *LPBRUSHINFO;

/***************************************************************************/

typedef struct tag_PENINFO {
    U32 Size;
    COLOR Color;
    U32 Pattern;
    U32 Flags;
} PENINFO, *LPPENINFO;

/***************************************************************************/

typedef struct tag_PIXELINFO {
    U32 Size;
    HANDLE GC;
    I32 X;
    I32 Y;
    COLOR Color;
} PIXELINFO, *LPPIXELINFO;

/***************************************************************************/

typedef struct tag_LINEINFO {
    U32 Size;
    HANDLE GC;
    I32 X1;
    I32 Y1;
    I32 X2;
    I32 Y2;
} LINEINFO, *LPLINEINFO;

/***************************************************************************/

typedef struct tag_RECTINFO {
    U32 Size;
    HANDLE GC;
    I32 X1;
    I32 Y1;
    I32 X2;
    I32 Y2;
} RECTINFO, *LPRECTINFO;

/***************************************************************************/

typedef struct tag_TRIANGLEINFO {
    U32 Size;
    HANDLE GC;
    POINT P1;
    POINT P2;
    POINT P3;
} TRIANGLEINFO, *LPTRIANGLEINFO;

/***************************************************************************/

// Window styles

#define EWS_VISIBLE 0x0001
#define EWS_ALWAYS_IN_FRONT 0x0002

/***************************************************************************/

// Task and window messages

#define ETM_NONE 0x00000000
#define ETM_QUIT 0x00000001
#define ETM_CREATE 0x00000002
#define ETM_DELETE 0x00000003
#define ETM_PAUSE 0x00000004
#define ETM_USER 0x20000000

#define EWM_NONE 0x40000000
#define EWM_CREATE 0x40000001
#define EWM_DELETE 0x40000002
#define EWM_SHOW 0x40000003
#define EWM_HIDE 0x40000004
#define EWM_MOVE 0x40000005
#define EWM_MOVING 0x40000006
#define EWM_SIZE 0x40000007
#define EWM_SIZING 0x40000008
#define EWM_DRAW 0x40000009
#define EWM_KEYDOWN 0x4000000A
#define EWM_KEYUP 0x4000000B
#define EWM_MOUSEMOVE 0x4000000C
#define EWM_MOUSEDOWN 0x4000000D
#define EWM_MOUSEUP 0x4000000E
#define EWM_COMMAND 0x4000000F
#define EWM_NOTIFY 0x40000010
#define EWM_GOTFOCUS 0x40000011
#define EWM_LOSTFOCUS 0x40000012
#define EM_USER 0x60000000

/***************************************************************************/

// Value for GetSystemMetrics

#define SM_SCREEN_WIDTH 1
#define SM_SCREEN_HEIGHT 2
#define SM_SCREEN_BITS_PER_PIXEL 3
#define SM_MINIMUM_WINDOW_WIDTH 4
#define SM_MINIMUM_WINDOW_HEIGHT 5
#define SM_MAXIMUM_WINDOW_WIDTH 6
#define SM_MAXIMUM_WINDOW_HEIGHT 7
#define SM_SMALL_ICON_WIDTH 8
#define SM_SMALL_ICON_HEIGHT 9
#define SM_LARGE_ICON_WIDTH 10
#define SM_LARGE_ICON_HEIGHT 11
#define SM_MOUSE_CURSOR_WIDTH 12
#define SM_MOUSE_CURSOR_HEIGHT 13
#define SM_TITLE_BAR_HEIGHT 14

#define SM_COLOR_DESKTOP 100
#define SM_COLOR_HIGHLIGHT 101
#define SM_COLOR_NORMAL 102
#define SM_COLOR_LIGHT_SHADOW 103
#define SM_COLOR_DARK_SHADOW 104
#define SM_COLOR_CLIENT 105
#define SM_COLOR_TEXT_NORMAL 106
#define SM_COLOR_TEXT_SELECTED 107
#define SM_COLOR_SELECTION 108
#define SM_COLOR_TITLE_BAR 109
#define SM_COLOR_TITLE_BAR_2 110
#define SM_COLOR_TITLE_TEXT 111

/***************************************************************************/

// Values for mouse buttons

#define MB_LEFT 0x0001
#define MB_RIGHT 0x0002
#define MB_MIDDLE 0x0004

/***************************************************************************/

#endif
