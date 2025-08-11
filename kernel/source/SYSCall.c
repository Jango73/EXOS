
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Address.h"
#include "../include/Base.h"
#include "../include/Clock.h"
#include "../include/Console.h"
#include "../include/File.h"
#include "../include/Heap.h"
#include "../include/ID.h"
#include "../include/Kernel.h"
#include "../include/Keyboard.h"
#include "../include/Mouse.h"
#include "../include/Process.h"
#include "../include/User.h"
#include "../include/VMM.h"

/***************************************************************************/

typedef U32 (*SYSCALLFUNC)(U32);

/***************************************************************************/

U32 SysCall_GetVersion(U32 Parameter) {
    UNUSED(Parameter);
    return (((U32)1 << 16) | (U32)0);
}

/***************************************************************************/

U32 SysCall_GetSystemInfo(U32 Parameter) {
    LPSYSTEMINFO Info = (LPSYSTEMINFO)Parameter;

    if (Info) {
        Info->TotalPhysicalMemory = Memory;
        Info->PhysicalMemoryUsed = GetPhysicalMemoryUsed();
        Info->PhysicalMemoryAvail = Memory - Info->PhysicalMemoryUsed;
        Info->TotalSwapMemory = 0;
        Info->SwapMemoryUsed = 0;
        Info->SwapMemoryAvail = 0;
        Info->TotalMemoryAvail = Info->TotalPhysicalMemory + Info->TotalSwapMemory;
        Info->PageSize = PAGE_SIZE;
        Info->TotalPhysicalPages = Pages;
        Info->MinimumLinearAddress = LA_USER;
        Info->MaximumLinearAddress = LA_DIRECTORY - 1;
        Info->NumProcesses = Kernel.Process->NumItems;
        Info->NumTasks = Kernel.Task->NumItems;

        StringCopy(Info->UserName, TEXT("Not implemented"));
        StringCopy(Info->CompanyName, TEXT("Not implemented"));

        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

U32 SysCall_GetLastError(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_SetLastError(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_GetSystemTime(U32 Parameter) {
    UNUSED(Parameter);
    return GetSystemTime();
}

/***************************************************************************/

U32 SysCall_GetLocalTime(U32 Parameter) {
    LPSYSTEMTIME Time = (LPSYSTEMTIME)Parameter;
    if (Time) return GetLocalTime(Time);
    return FALSE;
}

/***************************************************************************/

U32 SysCall_SetLocalTime(U32 Parameter) {
    UNUSED(Parameter);
    // LPSYSTEMTIME Time = (LPSYSTEMTIME)Parameter;
    // if (Time) return SetLocalTime(Time);
    return FALSE;
}

/***************************************************************************/

U32 SysCall_DeleteObject(U32 Parameter) {
    LPOBJECT Object = (LPOBJECT)Parameter;

    switch (Object->ID) {
        case ID_FILE:
            CloseFile((LPFILE)Object);
            break;
        case ID_DESKTOP:
            DeleteDesktop((LPDESKTOP)Object);
            break;
        case ID_WINDOW:
            DeleteWindow((LPWINDOW)Object);
            break;
    }

    return 0;
}

/***************************************************************************/

U32 SysCall_CreateProcess(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_KillProcess(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_CreateTask(U32 Parameter) {
    LPTASKINFO TaskInfo = (LPTASKINFO)Parameter;

    if (TaskInfo == NULL) return NULL;

    return (U32)CreateTask(GetCurrentProcess(), TaskInfo);
}

/***************************************************************************/

U32 SysCall_KillTask(U32 Parameter) { return (U32)KillTask((LPTASK)Parameter); }

/***************************************************************************/

U32 SysCall_SuspendTask(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_ResumeTask(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_Sleep(U32 Parameter) {
    Sleep(Parameter);
    return TRUE;
}

/***************************************************************************/

U32 SysCall_PostMessage(U32 Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    if (Message == NULL) return 0;

    return (U32)PostMessage(Message->Target, Message->Message, Message->Param1, Message->Param2);
}

/***************************************************************************/

U32 SysCall_SendMessage(U32 Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    if (Message == NULL) return 0;

    return (U32)SendMessage(Message->Target, Message->Message, Message->Param1, Message->Param2);
}

/***************************************************************************/

U32 SysCall_PeekMessage(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_GetMessage(U32 Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    if (Message == NULL) return 0;

    return (U32)GetMessage(Message);
}

/***************************************************************************/

U32 SysCall_DispatchMessage(U32 Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    if (Message == NULL) return 0;

    return (U32)DispatchMessage(Message);
}

/***************************************************************************/

U32 SysCall_CreateMutex(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_DeleteMutex(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_LockMutex(U32 Parameter) {
    LPSEMINFO Info = (LPSEMINFO)Parameter;
    if (Info == NULL) return MAX_U32;

    return LockMutex((LPMUTEX)Info->Mutex, Info->MilliSeconds);
}

/***************************************************************************/

U32 SysCall_UnlockMutex(U32 Parameter) {
    LPSEMINFO Info = (LPSEMINFO)Parameter;
    if (Info == NULL) return MAX_U32;

    return UnlockMutex((LPMUTEX)Info->Mutex);
}

/***************************************************************************/

U32 SysCall_VirtualAlloc(U32 Parameter) {
    LPVIRTUALINFO Info = (LPVIRTUALINFO)Parameter;

    if (Info) {
        return VirtualAlloc(Info->Base, Info->Size, Info->Flags);
    }

    return 0;
}

/***************************************************************************/

U32 SysCall_VirtualFree(U32 Parameter) {
    LPVIRTUALINFO Info = (LPVIRTUALINFO)Parameter;

    if (Info) {
        return VirtualFree(Info->Base, Info->Size);
    }

    return 0;
}

/***************************************************************************/

U32 SysCall_GetProcessHeap(U32 Parameter) { return (U32)GetProcessHeap((LPPROCESS)Parameter); }

/***************************************************************************/

U32 SysCall_HeapAlloc(U32 Parameter) { return (U32)HeapAlloc(Parameter); }

/***************************************************************************/

U32 SysCall_HeapFree(U32 Parameter) {
    HeapFree((LPVOID)Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_EnumVolumes(U32 Parameter) {
    LPLISTNODE Node;
    LPENUMVOLUMESINFO Info;
    U32 Result;

    Info = (LPENUMVOLUMESINFO)Parameter;

    if (Info == NULL) return 0;
    if (Info->Func == NULL) return 0;

    LockMutex(MUTEX_FILESYSTEM, INFINITY);

    for (Node = Kernel.FileSystem->First; Node; Node = Node->Next) {
        Result = Info->Func((HANDLE)Node, Info->Parameter);
        if (Result == 0) break;
    }

    UnlockMutex(MUTEX_FILESYSTEM);
    return 1;
}

/***************************************************************************/

U32 SysCall_GetVolumeInfo(U32 Parameter) {
    LPVOLUMEINFO Info;
    LPFILESYSTEM FileSystem;

    Info = (LPVOLUMEINFO)Parameter;
    if (Info == NULL) return 0;

    FileSystem = (LPFILESYSTEM)Info->Volume;
    if (FileSystem == NULL) return 0;
    if (FileSystem->ID != ID_FILESYSTEM) return 0;

    LockMutex(&(FileSystem->Mutex), INFINITY);

    StringCopy(Info->Name, FileSystem->Name);

    UnlockMutex(&(FileSystem->Mutex));

    return 1;
}

/***************************************************************************/

U32 SysCall_OpenFile(U32 Parameter) { return (U32)OpenFile((LPFILEOPENINFO)Parameter); }

/***************************************************************************/

U32 SysCall_ReadFile(U32 Parameter) { return ReadFile((LPFILEOPERATION)Parameter); }

/***************************************************************************/

U32 SysCall_WriteFile(U32 Parameter) { return WriteFile((LPFILEOPERATION)Parameter); }

/***************************************************************************/

U32 SysCall_GetFileSize(U32 Parameter) { return GetFileSize((LPFILE)Parameter); }

/***************************************************************************/

U32 SysCall_GetFilePointer(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_SetFilePointer(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_ConsolePeekKey(U32 Parameter) {
    UNUSED(Parameter);
    return (U32)PeekChar();
}

/***************************************************************************/

U32 SysCall_ConsoleGetKey(U32 Parameter) { return (U32)GetKeyCode((LPKEYCODE)Parameter); }

/***************************************************************************/

U32 SysCall_ConsoleGetChar(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_ConsolePrint(U32 Parameter) {
    if (Parameter) ConsolePrint((LPCSTR)Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_ConsoleGetString(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_ConsoleGotoXY(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_CreateDesktop(U32 Parameter) {
    UNUSED(Parameter);
    return (U32)CreateDesktop();
}

/***************************************************************************/

U32 SysCall_ShowDesktop(U32 Parameter) {
    LPDESKTOP Desktop = (LPDESKTOP)Parameter;

    if (Desktop == NULL) return 0;
    if (Desktop->ID != ID_DESKTOP) return 0;

    return (U32)ShowDesktop(Desktop);
}

/***************************************************************************/

U32 SysCall_GetDesktopWindow(U32 Parameter) {
    LPDESKTOP Desktop = (LPDESKTOP)Parameter;
    LPWINDOW Window = NULL;

    if (Desktop == NULL) return 0;
    if (Desktop->ID != ID_DESKTOP) return 0;

    LockMutex(&(Desktop->Mutex), INFINITY);

    Window = Desktop->Window;

    UnlockMutex(&(Desktop->Mutex));

    return (U32)Window;
}

/***************************************************************************/

U32 SysCall_CreateWindow(U32 Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    if (WindowInfo == NULL) return 0;

    return (U32)CreateWindow(WindowInfo);
}

/***************************************************************************/

U32 SysCall_ShowWindow(U32 Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    if (WindowInfo == NULL) return 0;

    return (U32)ShowWindow(WindowInfo->Window, TRUE);
}

/***************************************************************************/

U32 SysCall_HideWindow(U32 Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    if (WindowInfo == NULL) return 0;

    return (U32)ShowWindow(WindowInfo->Window, FALSE);
}

/***************************************************************************/

U32 SysCall_MoveWindow(U32 Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    if (WindowInfo == NULL) return 0;

    return (U32)MoveWindow(WindowInfo->Window, &(WindowInfo->WindowPosition));
}

/***************************************************************************/

U32 SysCall_SizeWindow(U32 Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    if (WindowInfo == NULL) return 0;

    return (U32)SizeWindow(WindowInfo->Window, &(WindowInfo->WindowSize));
}

/***************************************************************************/

U32 SysCall_SetWindowFunc(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_GetWindowFunc(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_SetWindowStyle(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_GetWindowStyle(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_SetWindowProp(U32 Parameter) {
    LPPROPINFO PropInfo = (LPPROPINFO)Parameter;

    if (PropInfo == NULL) return 0;

    return SetWindowProp(PropInfo->Window, PropInfo->Name, PropInfo->Value);
}

/***************************************************************************/

U32 SysCall_GetWindowProp(U32 Parameter) {
    LPPROPINFO PropInfo = (LPPROPINFO)Parameter;

    if (PropInfo == NULL) return 0;

    return GetWindowProp(PropInfo->Window, PropInfo->Name);
}

/***************************************************************************/

U32 SysCall_GetWindowRect(U32 Parameter) {
    LPWINDOWRECT WindowRect = (LPWINDOWRECT)Parameter;

    if (WindowRect == NULL) return 0;

    return (U32)GetWindowRect(WindowRect->Window, &(WindowRect->Rect));
}

/***************************************************************************/

U32 SysCall_InvalidateWindowRect(U32 Parameter) {
    LPWINDOWRECT WindowRect = (LPWINDOWRECT)Parameter;

    if (WindowRect == NULL) return 0;

    return (U32)InvalidateWindowRect(WindowRect->Window, &(WindowRect->Rect));
}

/***************************************************************************/

U32 SysCall_GetWindowGC(U32 Parameter) { return (U32)GetWindowGC((HANDLE)Parameter); }

/***************************************************************************/

U32 SysCall_ReleaseWindowGC(U32 Parameter) {
    UNUSED(Parameter);
    return 1;
}

/***************************************************************************/

U32 SysCall_EnumWindows(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_DefWindowFunc(U32 Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    if (Message == NULL) return 0;

    return (U32)DefWindowFunc(Message->Target, Message->Message, Message->Param1, Message->Param2);
}

/***************************************************************************/

U32 SysCall_GetSystemBrush(U32 Parameter) { return (U32)GetSystemBrush(Parameter); }

/***************************************************************************/

U32 SysCall_GetSystemPen(U32 Parameter) { return (U32)GetSystemPen(Parameter); }

/***************************************************************************/

U32 SysCall_CreateBrush(U32 Parameter) {
    LPBRUSHINFO Info = (LPBRUSHINFO)Parameter;

    return (U32)CreateBrush(Info);
}

/***************************************************************************/

U32 SysCall_CreatePen(U32 Parameter) {
    LPPENINFO Info = (LPPENINFO)Parameter;

    return (U32)CreatePen(Info);
}

/***************************************************************************/

U32 SysCall_SelectBrush(U32 Parameter) {
    LPGCSELECT Sel = (LPGCSELECT)Parameter;
    if (Sel == NULL) return 0;
    return (U32)SelectBrush(Sel->GC, Sel->Object);
}

/***************************************************************************/

U32 SysCall_SelectPen(U32 Parameter) {
    LPGCSELECT Sel = (LPGCSELECT)Parameter;
    if (Sel == NULL) return 0;
    return (U32)SelectPen(Sel->GC, Sel->Object);
}

/***************************************************************************/

U32 SysCall_SetPixel(U32 Parameter) {
    LPPIXELINFO PixelInfo = (LPPIXELINFO)Parameter;
    if (PixelInfo == NULL) return 0;
    return (U32)SetPixel(PixelInfo);
}

/***************************************************************************/

U32 SysCall_GetPixel(U32 Parameter) {
    LPPIXELINFO PixelInfo = (LPPIXELINFO)Parameter;
    if (PixelInfo == NULL) return 0;
    return (U32)GetPixel(PixelInfo);
}

/***************************************************************************/

U32 SysCall_Line(U32 Parameter) {
    LPLINEINFO LineInfo = (LPLINEINFO)Parameter;
    if (LineInfo == NULL) return 0;
    return (U32)Line(LineInfo);
}

/***************************************************************************/

U32 SysCall_Rectangle(U32 Parameter) {
    LPRECTINFO RectInfo = (LPRECTINFO)Parameter;
    if (RectInfo == NULL) return 0;
    return (U32)Rectangle(RectInfo);
}

/***************************************************************************/

U32 SysCall_GetMousePos(U32 Parameter) {
    LPPOINT Point = (LPPOINT)Parameter;
    U32 UX, UY;

    if (Point == NULL) return 0;

    UX = SerialMouseDriver.Command(DF_MOUSE_GETDELTAX, 0);
    UY = SerialMouseDriver.Command(DF_MOUSE_GETDELTAY, 0);

    Point->X = *((I32*)&UX);
    Point->Y = *((I32*)&UY);

    return 1;
}

/***************************************************************************/

U32 SysCall_SetMousePos(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_GetMouseButtons(U32 Parameter) {
    UNUSED(Parameter);
    return SerialMouseDriver.Command(DF_MOUSE_GETBUTTONS, 0);
}

/***************************************************************************/

U32 SysCall_ShowMouse(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_HideMouse(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_ClipMouse(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_CaptureMouse(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_ReleaseMouse(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

#define MAX_SYSCALL 0x00000070

SYSCALLFUNC SysCallTable[MAX_SYSCALL] = {
    SysCall_GetVersion,            // 0x00000000
    SysCall_GetSystemInfo,         // 0x00000001
    SysCall_GetLastError,          // 0x00000002
    SysCall_SetLastError,          // 0x00000003
    SysCall_GetSystemTime,         // 0x00000004
    SysCall_GetLocalTime,          // 0x00000005
    SysCall_SetLocalTime,          // 0x00000006
    SysCall_DeleteObject,          // 0x00000007
    SysCall_CreateProcess,         // 0x00000008
    SysCall_KillProcess,           // 0x00000009
    SysCall_CreateTask,            // 0x0000000A
    SysCall_KillTask,              // 0x0000000B
    SysCall_SuspendTask,           // 0x0000000C
    SysCall_ResumeTask,            // 0x0000000D
    SysCall_Sleep,                 // 0x0000000E
    SysCall_PostMessage,           // 0x0000000F
    SysCall_SendMessage,           // 0x00000010
    SysCall_PeekMessage,           // 0x00000011
    SysCall_GetMessage,            // 0x00000012
    SysCall_DispatchMessage,       // 0x00000013
    SysCall_CreateMutex,           // 0x00000014
    SysCall_LockMutex,             // 0x00000015
    SysCall_UnlockMutex,           // 0x00000016
    SysCall_VirtualAlloc,          // 0x00000017
    SysCall_VirtualFree,           // 0x00000018
    SysCall_GetProcessHeap,        // 0x00000019
    SysCall_HeapAlloc,             // 0x0000001A
    SysCall_HeapFree,              // 0x0000001B
    SysCall_EnumVolumes,           // 0x0000001C
    SysCall_GetVolumeInfo,         // 0x0000001D
    SysCall_OpenFile,              // 0x0000001E
    SysCall_ReadFile,              // 0x0000001F
    SysCall_WriteFile,             // 0x00000020
    SysCall_GetFileSize,           // 0x00000021
    SysCall_GetFilePointer,        // 0x00000022
    SysCall_SetFilePointer,        // 0x00000023
    NULL,                          // 0x00000024
    NULL,                          // 0x00000025
    NULL,                          // 0x00000026
    NULL,                          // 0x00000027
    NULL,                          // 0x00000028
    NULL,                          // 0x00000029
    SysCall_ConsolePeekKey,        // 0x0000002A
    SysCall_ConsoleGetKey,         // 0x0000002B
    SysCall_ConsolePrint,          // 0x0000002C
    SysCall_ConsoleGetString,      // 0x0000002D
    SysCall_ConsoleGotoXY,         // 0x0000002E
    NULL,                          // 0x0000002F
    NULL,                          // 0x00000030
    NULL,                          // 0x00000031
    NULL,                          // 0x00000032
    NULL,                          // 0x00000033
    NULL,                          // 0x00000034
    NULL,                          // 0x00000035
    NULL,                          // 0x00000036
    NULL,                          // 0x00000037
    NULL,                          // 0x00000038
    NULL,                          // 0x00000039
    NULL,                          // 0x0000003A
    NULL,                          // 0x0000003B
    NULL,                          // 0x0000003C
    NULL,                          // 0x0000003D
    NULL,                          // 0x0000003E
    NULL,                          // 0x0000003F
    SysCall_CreateDesktop,         // 0x00000040
    SysCall_ShowDesktop,           // 0x00000041
    SysCall_GetDesktopWindow,      // 0x00000042
    SysCall_CreateWindow,          // 0x00000043
    SysCall_ShowWindow,            // 0x00000044
    SysCall_HideWindow,            // 0x00000045
    SysCall_MoveWindow,            // 0x00000046
    SysCall_SizeWindow,            // 0x00000047
    SysCall_SetWindowFunc,         // 0x00000048
    SysCall_GetWindowFunc,         // 0x00000049
    SysCall_SetWindowStyle,        // 0x0000004A
    SysCall_GetWindowStyle,        // 0x0000004B
    SysCall_SetWindowProp,         // 0x0000004C
    SysCall_GetWindowProp,         // 0x0000004D
    SysCall_GetWindowRect,         // 0x0000004E
    SysCall_InvalidateWindowRect,  // 0x0000004F
    SysCall_GetWindowGC,           // 0x00000050
    SysCall_ReleaseWindowGC,       // 0x00000051
    SysCall_EnumWindows,           // 0x00000052
    SysCall_DefWindowFunc,         // 0x00000053
    SysCall_GetSystemBrush,        // 0x00000054
    SysCall_GetSystemPen,          // 0x00000055
    SysCall_CreateBrush,           // 0x00000056
    SysCall_CreatePen,             // 0x00000057
    SysCall_SelectBrush,           // 0x00000058
    SysCall_SelectPen,             // 0x00000059
    SysCall_SetPixel,              // 0x0000005A
    SysCall_GetPixel,              // 0x0000005B
    SysCall_Line,                  // 0x0000005C
    SysCall_Rectangle,             // 0x0000005D
    NULL,
    NULL,
    NULL,
    NULL,
    SysCall_GetMousePos,      // 0x00000062
    SysCall_SetMousePos,      // 0x00000063
    SysCall_GetMouseButtons,  // 0x00000064
    SysCall_ShowMouse,        // 0x00000065
    SysCall_HideMouse,        // 0x00000066
    SysCall_ClipMouse,        // 0x00000067
    SysCall_CaptureMouse,     // 0x00000068
    SysCall_ReleaseMouse,     // 0x00000069
};

/***************************************************************************/

U32 SystemCallHandler(U32 Function, U32 Parameter) {
    if (Function < MAX_SYSCALL && SysCallTable[Function] != NULL) {
        return SysCallTable[Function](Parameter);
    }

    // return ERROR_INVALID_INDEX;
    return 0;
}

/***************************************************************************/
