
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


    System call

\************************************************************************/

#include "../include/Base.h"
#include "../include/Clock.h"
#include "../include/Console.h"
#include "../include/File.h"
#include "../include/Heap.h"
#include "../include/Helpers.h"
#include "../include/ID.h"
#include "../include/Kernel.h"
#include "../include/Keyboard.h"
#include "../include/Log.h"
#include "../include/Memory.h"
#include "../include/Mouse.h"
#include "../include/Process.h"
#include "../include/Schedule.h"
#include "../include/User.h"
#include "../include/UserAccount.h"
#include "../include/UserSession.h"
#include "../include/Security.h"
#include "../include/Socket.h"
#include "../include/SYSCall.h"

/***************************************************************************/

U32 SysCall_Debug(U32 Parameter) {
    DEBUG((LPCSTR)Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_GetVersion(U32 Parameter) {
    UNUSED(Parameter);
    return (((U32)1 << 16) | (U32)0);
}

/***************************************************************************/

U32 SysCall_GetSystemInfo(U32 Parameter) {
    LPSYSTEMINFO Info = (LPSYSTEMINFO)Parameter;

    if (Info && Info->Header.Size >= sizeof(SYSTEMINFO)) {
        Info->TotalPhysicalMemory = KernelStartup.MemorySize;
        Info->PhysicalMemoryUsed = GetPhysicalMemoryUsed();
        Info->PhysicalMemoryAvail = KernelStartup.MemorySize - Info->PhysicalMemoryUsed;
        Info->TotalSwapMemory = 0;
        Info->SwapMemoryUsed = 0;
        Info->SwapMemoryAvail = 0;
        Info->TotalMemoryAvail = Info->TotalPhysicalMemory + Info->TotalSwapMemory;
        Info->PageSize = PAGE_SIZE;
        Info->TotalPhysicalPages = KernelStartup.PageCount;
        Info->MinimumLinearAddress = VMA_USER;
        Info->MaximumLinearAddress = VMA_KERNEL - 1;
        Info->NumProcesses = Kernel.Process->NumItems;
        Info->NumTasks = Kernel.Task->NumItems;

        LPUSERACCOUNT User = GetCurrentUser();

        StringCopy(Info->UserName, User != NULL ? User->UserName : TEXT(""));
        StringCopy(Info->KeyboardLayout, Kernel.KeyboardCode);

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
    LPDATETIME Time = (LPDATETIME)Parameter;
    if (Time) return GetLocalTime(Time);
    return FALSE;
}

/***************************************************************************/

U32 SysCall_SetLocalTime(U32 Parameter) {
    UNUSED(Parameter);
    // LPDATETIME Time = (LPDATETIME)Parameter;
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
    LPPROCESSINFO Info = (LPPROCESSINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, PROCESSINFO) {
        return (U32)CreateProcess(Info);
    }

    return 0;
}

/***************************************************************************/

U32 SysCall_KillProcess(U32 Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

U32 SysCall_GetProcessInfo(U32 Parameter) {
    LPPROCESSINFO Info = (LPPROCESSINFO)Parameter;
    LPPROCESS CurrentProcess;

    DEBUG(TEXT("[SysCall_GetProcessInfo] Enter, Parameter=%x"), Parameter);

    SAFE_USE_INPUT_POINTER(Info, PROCESSINFO) {
        if (Info->Process == (HANDLE)0) {
            CurrentProcess = GetCurrentProcess();
        } else {
            CurrentProcess = (LPPROCESS)Info->Process;
        }

        SAFE_USE_VALID_ID(CurrentProcess, ID_PROCESS) {
            DEBUG(TEXT("[SysCall_GetProcessInfo] Info->CommandLine = %s"), Info->CommandLine);
            DEBUG(TEXT("[SysCall_GetProcessInfo] CurrentProcess=%x"), CurrentProcess);
            DEBUG(TEXT("[SysCall_GetProcessInfo] CurrentProcess->CommandLine = %s"), CurrentProcess->CommandLine);

            // Copy the command line
            StringCopy(Info->CommandLine, CurrentProcess->CommandLine);

            return DF_ERROR_SUCCESS;
        }
    }

    return DF_ERROR_GENERIC;
}

/***************************************************************************/

U32 SysCall_CreateTask(U32 Parameter) {
    LPTASKINFO TaskInfo = (LPTASKINFO)Parameter;

    SAFE_USE_INPUT_POINTER(TaskInfo, TASKINFO) {
        return (U32)CreateTask(GetCurrentProcess(), TaskInfo);
    }

    return 0;
}

/***************************************************************************/

U32 SysCall_KillTask(U32 Parameter) { return (U32)KillTask((LPTASK)Parameter); }

/***************************************************************************/

U32 SysCall_KillMe(U32 Parameter) {
    UNUSED(Parameter);
    return (U32)KillTask(GetCurrentTask());
}

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

U32 SysCall_Wait(U32 Parameter) {
    LPWAITINFO WaitInfo = (LPWAITINFO)Parameter;

    if (WaitInfo == NULL || WaitInfo->Header.Size < sizeof(WAITINFO)) {
        return WAIT_INVALID_PARAMETER;
    }

    if (WaitInfo->Count == 0 || WaitInfo->Count > WAITINFO_MAX_OBJECTS) {
        return WAIT_INVALID_PARAMETER;
    }

    return Wait(WaitInfo);
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
    LPMUTEXINFO Info = (LPMUTEXINFO)Parameter;
    if (Info == NULL) return MAX_U32;

    return LockMutex((LPMUTEX)Info->Mutex, Info->MilliSeconds);
}

/***************************************************************************/

U32 SysCall_UnlockMutex(U32 Parameter) {
    LPMUTEXINFO Info = (LPMUTEXINFO)Parameter;
    if (Info == NULL) return MAX_U32;

    return UnlockMutex((LPMUTEX)Info->Mutex);
}

/***************************************************************************/

U32 SysCall_AllocRegion(U32 Parameter) {
    LPALLOCREGIONINFO Info = (LPALLOCREGIONINFO)Parameter;

    if (Info && Info->Header.Size >= sizeof(ALLOCREGIONINFO)) {
        return AllocRegion(Info->Base, Info->Target, Info->Size, Info->Flags);
    }

    return 0;
}

/***************************************************************************/

U32 SysCall_FreeRegion(U32 Parameter) {
    LPALLOCREGIONINFO Info = (LPALLOCREGIONINFO)Parameter;

    if (Info && Info->Header.Size >= sizeof(ALLOCREGIONINFO)) {
        return FreeRegion(Info->Base, Info->Size);
    }

    return 0;
}

/***************************************************************************/

U32 SysCall_IsMemoryValid(U32 Parameter) {
    return (U32)IsValidMemory((LINEAR)Parameter);
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

U32 SysCall_HeapRealloc(U32 Parameter) {
    LPHEAPREALLOCINFO Info = (LPHEAPREALLOCINFO)Parameter;

    if (Info == NULL) return 0;

    return (U32)HeapRealloc(Info->Pointer, Info->Size);
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

U32 SysCall_GetFilePosition(U32 Parameter) { return GetFilePosition((LPFILE)Parameter); }

/***************************************************************************/

U32 SysCall_SetFilePosition(U32 Parameter) { return SetFilePosition((LPFILEOPERATION)Parameter); }

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
    LPPOINT Point = (LPPOINT)Parameter;
    if (Point) {
        SetConsoleCursorPosition(Point->X, Point->Y);
    }
    return 0;
}

/***************************************************************************/

U32 SysCall_ClearScreen(U32 Parameter) {
    UNUSED(Parameter);
    ClearConsole();
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

U32 SysCall_Login(U32 Parameter) {
    LPLOGIN_INFO LoginInfo = (LPLOGIN_INFO)Parameter;
    if (LoginInfo == NULL || LoginInfo->Header.Size != sizeof(LOGIN_INFO)) {
        return FALSE;
    }

    LPUSERACCOUNT Account = FindUserAccount(LoginInfo->UserName);
    if (Account == NULL) {
        return FALSE;
    }

    if (!VerifyPassword(LoginInfo->Password, Account->PasswordHash)) {
        return FALSE;
    }

    LPUSERSESSION Session = CreateUserSession(Account->UserID, (HANDLE)GetCurrentTask());
    if (Session == NULL) {
        return FALSE;
    }

    GetLocalTime(&Account->LastLoginTime);
    SetCurrentSession(Session);
    return TRUE;
}

/***************************************************************************/

U32 SysCall_Logout(U32 Parameter) {
    UNUSED(Parameter);
    LPUSERSESSION Session = GetCurrentSession();
    if (Session == NULL) {
        return FALSE;
    }

    DestroyUserSession(Session);
    SetCurrentSession(NULL);
    return TRUE;
}

/***************************************************************************/

U32 SysCall_GetCurrentUser(U32 Parameter) {
    LPCURRENT_USER_INFO UserInfo = (LPCURRENT_USER_INFO)Parameter;
    if (UserInfo == NULL || UserInfo->Header.Size != sizeof(CURRENT_USER_INFO)) {
        return FALSE;
    }

    LPUSERACCOUNT Account = GetCurrentUser();
    if (Account == NULL) {
        return FALSE;
    }

    LPUSERSESSION Session = GetCurrentSession();
    if (Session == NULL) {
        return FALSE;
    }

    StringCopy(UserInfo->UserName, Account->UserName);
    UserInfo->Privilege = Account->Privilege;
    // Use simple timestamp - set both LO and HI parts
    UserInfo->LoginTime.LO = GetSystemTime();
    UserInfo->LoginTime.HI = 0;
    UserInfo->SessionID = Session->SessionID;

    return TRUE;
}

/***************************************************************************/

U32 SysCall_ChangePassword(U32 Parameter) {
    LPPASSWORD_CHANGE PasswordChange = (LPPASSWORD_CHANGE)Parameter;
    if (PasswordChange == NULL || PasswordChange->Header.Size != sizeof(PASSWORD_CHANGE)) {
        return FALSE;
    }

    LPUSERACCOUNT Account = GetCurrentUser();
    if (Account == NULL) {
        return FALSE;
    }

    return ChangeUserPassword(Account->UserName, PasswordChange->OldPassword, PasswordChange->NewPassword);
}

/***************************************************************************/

U32 SysCall_CreateUser(U32 Parameter) {
    LPUSER_CREATE_INFO CreateInfo = (LPUSER_CREATE_INFO)Parameter;
    if (CreateInfo == NULL || CreateInfo->Header.Size != sizeof(USER_CREATE_INFO)) {
        return FALSE;
    }

    LPUSERACCOUNT CurrentAccount = GetCurrentUser();
    if (CurrentAccount == NULL || CurrentAccount->Privilege != EXOS_PRIVILEGE_ADMIN) {
        return FALSE;
    }

    LPUSERACCOUNT NewAccount = CreateUserAccount(CreateInfo->UserName, CreateInfo->Password, CreateInfo->Privilege);
    return (NewAccount != NULL) ? TRUE : FALSE;
}

/***************************************************************************/

U32 SysCall_DeleteUser(U32 Parameter) {
    LPUSER_DELETE_INFO DeleteInfo = (LPUSER_DELETE_INFO)Parameter;
    if (DeleteInfo == NULL || DeleteInfo->Header.Size != sizeof(USER_DELETE_INFO)) {
        return FALSE;
    }

    LPUSERACCOUNT CurrentAccount = GetCurrentUser();
    if (CurrentAccount == NULL || CurrentAccount->Privilege != EXOS_PRIVILEGE_ADMIN) {
        return FALSE;
    }

    return DeleteUserAccount(DeleteInfo->UserName);
}

/***************************************************************************/

U32 SysCall_ListUsers(U32 Parameter) {
    LPUSER_LIST_INFO ListInfo = (LPUSER_LIST_INFO)Parameter;
    if (ListInfo == NULL || ListInfo->Header.Size < sizeof(USER_LIST_INFO)) {
        return FALSE;
    }

    LPUSERACCOUNT CurrentAccount = GetCurrentUser();
    if (CurrentAccount == NULL || CurrentAccount->Privilege != EXOS_PRIVILEGE_ADMIN) {
        return FALSE;
    }

    ListInfo->UserCount = 0;
    LPUSERACCOUNT Account = (LPUSERACCOUNT)Kernel.UserAccount->First;

    while (Account != NULL && ListInfo->UserCount < ListInfo->MaxUsers) {
        StringCopy(ListInfo->UserNames[ListInfo->UserCount], Account->UserName);
        ListInfo->UserCount++;
        Account = (LPUSERACCOUNT)Account->Next;
    }

    return TRUE;
}

/***************************************************************************/
// Socket syscalls

U32 SysCall_SocketCreate(U32 Parameter) {
    LPSOCKET_CREATE_INFO Info = (LPSOCKET_CREATE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_CREATE_INFO) {
        return SocketCreate(Info->AddressFamily, Info->SocketType, Info->Protocol);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SysCall_SocketBind(U32 Parameter) {
    LPSOCKET_BIND_INFO Info = (LPSOCKET_BIND_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_BIND_INFO) {
        return SocketBind(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressData, Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SysCall_SocketListen(U32 Parameter) {
    LPSOCKET_LISTEN_INFO Info = (LPSOCKET_LISTEN_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_LISTEN_INFO) {
        return SocketListen(Info->SocketHandle, Info->Backlog);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SysCall_SocketAccept(U32 Parameter) {
    LPSOCKET_ACCEPT_INFO Info = (LPSOCKET_ACCEPT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_ACCEPT_INFO) {
        return SocketAccept(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressBuffer, Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SysCall_SocketConnect(U32 Parameter) {
    LPSOCKET_CONNECT_INFO Info = (LPSOCKET_CONNECT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_CONNECT_INFO) {
        return SocketConnect(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressData, Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SysCall_SocketSend(U32 Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketSend(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SysCall_SocketReceive(U32 Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketReceive(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SysCall_SocketSendTo(U32 Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketSendTo(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags, (LPSOCKET_ADDRESS)Info->AddressData, Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SysCall_SocketReceiveFrom(U32 Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketReceiveFrom(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags, (LPSOCKET_ADDRESS)Info->AddressData, &Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SysCall_SocketClose(U32 Parameter) {
    U32 SocketHandle = Parameter;
    return SocketClose(SocketHandle);
}

/***************************************************************************/

U32 SysCall_SocketShutdown(U32 Parameter) {
    LPSOCKET_SHUTDOWN_INFO Info = (LPSOCKET_SHUTDOWN_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_SHUTDOWN_INFO) {
        return SocketShutdown(Info->SocketHandle, Info->How);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SysCall_SocketGetOption(U32 Parameter) {
    LPSOCKET_OPTION_INFO Info = (LPSOCKET_OPTION_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_OPTION_INFO) {
        return SocketGetOption(Info->SocketHandle, Info->Level, Info->OptionName, Info->OptionValue, &Info->OptionLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SysCall_SocketSetOption(U32 Parameter) {
    LPSOCKET_OPTION_INFO Info = (LPSOCKET_OPTION_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_OPTION_INFO) {
        return SocketSetOption(Info->SocketHandle, Info->Level, Info->OptionName, Info->OptionValue, Info->OptionLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SysCall_SocketGetPeerName(U32 Parameter) {
    LPSOCKET_ACCEPT_INFO Info = (LPSOCKET_ACCEPT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_ACCEPT_INFO) {
        return SocketGetPeerName(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressBuffer, Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SysCall_SocketGetSocketName(U32 Parameter) {
    LPSOCKET_ACCEPT_INFO Info = (LPSOCKET_ACCEPT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_ACCEPT_INFO) {
        return SocketGetSocketName(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressBuffer, Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

U32 SystemCallHandler(U32 Function, U32 Parameter) {
    if (Function >= SYSCALL_Last || SysCallTable[Function].Function == NULL) {
        return 0;
    }

    LPUSERACCOUNT CurrentUser = GetCurrentUser();
    U32 RequiredPrivilege = SysCallTable[Function].Privilege;

    if (CurrentUser == NULL) {
        if (RequiredPrivilege != EXOS_PRIVILEGE_USER) {
            return 0;
        }
    } else {
        if (CurrentUser->Privilege > RequiredPrivilege) {
            return 0;
        }
    }

    return SysCallTable[Function].Function(Parameter);
}
