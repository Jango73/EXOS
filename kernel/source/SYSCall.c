
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

#include "Base.h"
#include "Clock.h"
#include "Console.h"
#include "File.h"
#include "Heap.h"
#include "utils/Helpers.h"
#include "ID.h"
#include "Kernel.h"
#include "drivers/Keyboard.h"
#include "Log.h"
#include "Memory.h"
#include "Mouse.h"
#include "process/Process.h"
#include "process/Schedule.h"
#include "User.h"
#include "UserAccount.h"
#include "UserSession.h"
#include "Security.h"
#include "Socket.h"
#include "SYSCall.h"

/***************************************************************************/

UINT SysCall_Debug(UINT Parameter) {
    DEBUG((LPCSTR)Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_GetVersion(UINT Parameter) {
    UNUSED(Parameter);
    return ((UINT)1 << 16);
}

/***************************************************************************/

UINT SysCall_GetSystemInfo(UINT Parameter) {
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

UINT SysCall_GetLastError(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_SetLastError(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_GetSystemTime(UINT Parameter) {
    UNUSED(Parameter);
    return GetSystemTime();
}

/***************************************************************************/

UINT SysCall_GetLocalTime(UINT Parameter) {
    LPDATETIME Time = (LPDATETIME)Parameter;
    if (Time) return GetLocalTime(Time);
    return FALSE;
}

/***************************************************************************/

UINT SysCall_SetLocalTime(UINT Parameter) {
    UNUSED(Parameter);
    // LPDATETIME Time = (LPDATETIME)Parameter;
    // if (Time) return SetLocalTime(Time);
    return FALSE;
}

/***************************************************************************/

UINT SysCall_DeleteObject(UINT Parameter) {
    LPOBJECT Object = (LPOBJECT)Parameter;

    SAFE_USE_VALID(Object) {
        switch (Object->TypeID) {
            case KOID_FILE:
                return (UINT)CloseFile((LPFILE)Object);
            case KOID_DESKTOP:
                return (UINT)DeleteDesktop((LPDESKTOP)Object);
            case KOID_WINDOW:
                return (UINT)DeleteWindow((LPWINDOW)Object);
        }
    }

    return 0;
}

/***************************************************************************/

UINT SysCall_CreateProcess(UINT Parameter) {
    LPPROCESSINFO Info = (LPPROCESSINFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, PROCESSINFO) {
        return (UINT)CreateProcess(Info);
    }

    return 0;
}

/***************************************************************************/

UINT SysCall_KillProcess(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_GetProcessInfo(UINT Parameter) {
    LPPROCESSINFO Info = (LPPROCESSINFO)Parameter;
    LPPROCESS CurrentProcess;

    DEBUG(TEXT("[SysCall_GetProcessInfo] Enter, Parameter=%x"), Parameter);

    SAFE_USE_INPUT_POINTER(Info, PROCESSINFO) {
        if (Info->Process == (HANDLE)0) {
            CurrentProcess = GetCurrentProcess();
        } else {
            CurrentProcess = (LPPROCESS)Info->Process;
        }

        SAFE_USE_VALID_ID(CurrentProcess, KOID_PROCESS) {
            DEBUG(TEXT("[SysCall_GetProcessInfo] Info->CommandLine = %s"), Info->CommandLine);
            DEBUG(TEXT("[SysCall_GetProcessInfo] CurrentProcess=%p"), CurrentProcess);
            DEBUG(TEXT("[SysCall_GetProcessInfo] CurrentProcess->CommandLine = %s"), CurrentProcess->CommandLine);

            // Copy the command line
            StringCopy(Info->CommandLine, CurrentProcess->CommandLine);
            StringCopy(Info->WorkFolder, CurrentProcess->WorkFolder);

            return DF_ERROR_SUCCESS;
        }
    }

    return DF_ERROR_GENERIC;
}

/***************************************************************************/

UINT SysCall_CreateTask(UINT Parameter) {
    LPTASKINFO TaskInfo = (LPTASKINFO)Parameter;

    SAFE_USE_INPUT_POINTER(TaskInfo, TASKINFO) {
        return (UINT)CreateTask(GetCurrentProcess(), TaskInfo);
    }

    return 0;
}

/***************************************************************************/

UINT SysCall_KillTask(UINT Parameter) {
    DEBUG(TEXT("[SysCall_KillTask] Enter, Parameter=%x"), Parameter);

    return (UINT)KillTask((LPTASK)Parameter);
}

/***************************************************************************/

UINT SysCall_Exit(UINT Parameter) {
    DEBUG(TEXT("[SysCall_Exit] Enter, Parameter=%x"), Parameter);

    LPTASK Task = GetCurrentTask();
    SetTaskExitCode(Task, Parameter);

    UINT ReturnValue = KillTask(Task);

    DEBUG(TEXT("[SysCall_Exit] Exit"));

    return ReturnValue;
}

/***************************************************************************/

UINT SysCall_SuspendTask(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_ResumeTask(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_Sleep(UINT Parameter) {
    // LPTASK Task = GetCurrentTask();
    // DEBUG(TEXT("[SysCall_Sleep] Enter Parameter=%x Task=%p (%s)"), Parameter, Task, Task ? Task->Name : "?");

    Sleep(Parameter);
    return TRUE;
}

/***************************************************************************/

UINT SysCall_Wait(UINT Parameter) {
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

UINT SysCall_PostMessage(UINT Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    if (Message == NULL) return 0;

    return (UINT)PostMessage(Message->Target, Message->Message, Message->Param1, Message->Param2);
}

/***************************************************************************/

UINT SysCall_SendMessage(UINT Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    if (Message == NULL) return 0;

    return (UINT)SendMessage(Message->Target, Message->Message, Message->Param1, Message->Param2);
}

/***************************************************************************/

UINT SysCall_PeekMessage(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_GetMessage(UINT Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    if (Message == NULL) return 0;

    return (UINT)GetMessage(Message);
}

/***************************************************************************/

UINT SysCall_DispatchMessage(UINT Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    if (Message == NULL) return 0;

    return (UINT)DispatchMessage(Message);
}

/***************************************************************************/

UINT SysCall_CreateMutex(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_DeleteMutex(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_LockMutex(UINT Parameter) {
    LPMUTEXINFO Info = (LPMUTEXINFO)Parameter;
    if (Info == NULL) return (UINT)MAX_U32;

    return LockMutex((LPMUTEX)Info->Mutex, Info->MilliSeconds);
}

/***************************************************************************/

UINT SysCall_UnlockMutex(UINT Parameter) {
    LPMUTEXINFO Info = (LPMUTEXINFO)Parameter;
    if (Info == NULL) return (UINT)MAX_U32;

    return UnlockMutex((LPMUTEX)Info->Mutex);
}

/***************************************************************************/

UINT SysCall_AllocRegion(UINT Parameter) {
    LPALLOCREGIONINFO Info = (LPALLOCREGIONINFO)Parameter;

    if (Info && Info->Header.Size >= sizeof(ALLOCREGIONINFO)) {
        return AllocRegion(Info->Base, Info->Target, Info->Size, Info->Flags);
    }

    return 0;
}

/***************************************************************************/

UINT SysCall_FreeRegion(UINT Parameter) {
    LPALLOCREGIONINFO Info = (LPALLOCREGIONINFO)Parameter;

    if (Info && Info->Header.Size >= sizeof(ALLOCREGIONINFO)) {
        return FreeRegion(Info->Base, Info->Size);
    }

    return 0;
}

/***************************************************************************/

UINT SysCall_IsMemoryValid(UINT Parameter) {
    return (UINT)IsValidMemory((LINEAR)Parameter);
}

/***************************************************************************/

UINT SysCall_GetProcessHeap(UINT Parameter) { return (UINT)GetProcessHeap((LPPROCESS)Parameter); }

/***************************************************************************/

UINT SysCall_HeapAlloc(UINT Parameter) { return (UINT)HeapAlloc(Parameter); }

/***************************************************************************/

UINT SysCall_HeapFree(UINT Parameter) {
    HeapFree((LPVOID)Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_HeapRealloc(UINT Parameter) {
    LPHEAPREALLOCINFO Info = (LPHEAPREALLOCINFO)Parameter;

    if (Info == NULL) return 0;

    return (UINT)HeapRealloc(Info->Pointer, Info->Size);
}

/***************************************************************************/

UINT SysCall_EnumVolumes(UINT Parameter) {
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

UINT SysCall_GetVolumeInfo(UINT Parameter) {
    LPVOLUMEINFO Info;
    LPFILESYSTEM FileSystem;

    Info = (LPVOLUMEINFO)Parameter;
    if (Info == NULL) return 0;

    FileSystem = (LPFILESYSTEM)Info->Volume;
    if (FileSystem == NULL) return 0;
    if (FileSystem->TypeID != KOID_FILESYSTEM) return 0;

    LockMutex(&(FileSystem->Mutex), INFINITY);

    StringCopy(Info->Name, FileSystem->Name);

    UnlockMutex(&(FileSystem->Mutex));

    return 1;
}

/***************************************************************************/

UINT SysCall_OpenFile(UINT Parameter) { return (UINT)OpenFile((LPFILEOPENINFO)Parameter); }

/***************************************************************************/

UINT SysCall_ReadFile(UINT Parameter) { return ReadFile((LPFILEOPERATION)Parameter); }

/***************************************************************************/

UINT SysCall_WriteFile(UINT Parameter) {
    return WriteFile((LPFILEOPERATION)Parameter);
}

/***************************************************************************/

UINT SysCall_GetFileSize(UINT Parameter) { return GetFileSize((LPFILE)Parameter); }

/***************************************************************************/

UINT SysCall_GetFilePosition(UINT Parameter) { return GetFilePosition((LPFILE)Parameter); }

/***************************************************************************/

UINT SysCall_SetFilePosition(UINT Parameter) { return SetFilePosition((LPFILEOPERATION)Parameter); }

/***************************************************************************/

UINT SysCall_ConsolePeekKey(UINT Parameter) {
    UNUSED(Parameter);
    return (UINT)PeekChar();
}

/***************************************************************************/

UINT SysCall_ConsoleGetKey(UINT Parameter) { return (UINT)GetKeyCode((LPKEYCODE)Parameter); }

/***************************************************************************/

UINT SysCall_ConsoleGetChar(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_ConsolePrint(UINT Parameter) {
    if (Parameter) ConsolePrint((LPCSTR)Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_ConsoleGetString(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_ConsoleGotoXY(UINT Parameter) {
    LPPOINT Point = (LPPOINT)Parameter;
    if (Point) {
        SetConsoleCursorPosition(Point->X, Point->Y);
    }
    return 0;
}

/***************************************************************************/

UINT SysCall_ClearScreen(UINT Parameter) {
    UNUSED(Parameter);
    ClearConsole();
    return 0;
}

/***************************************************************************/

UINT SysCall_CreateDesktop(UINT Parameter) {
    UNUSED(Parameter);
    return (UINT)CreateDesktop();
}

/***************************************************************************/

UINT SysCall_ShowDesktop(UINT Parameter) {
    LPDESKTOP Desktop = (LPDESKTOP)Parameter;

    if (Desktop == NULL) return 0;
    if (Desktop->TypeID != KOID_DESKTOP) return 0;

    return (UINT)ShowDesktop(Desktop);
}

/***************************************************************************/

UINT SysCall_GetDesktopWindow(UINT Parameter) {
    LPDESKTOP Desktop = (LPDESKTOP)Parameter;
    LPWINDOW Window = NULL;

    if (Desktop == NULL) return 0;
    if (Desktop->TypeID != KOID_DESKTOP) return 0;

    LockMutex(&(Desktop->Mutex), INFINITY);

    Window = Desktop->Window;

    UnlockMutex(&(Desktop->Mutex));

    return (UINT)Window;
}

/***************************************************************************/

UINT SysCall_CreateWindow(UINT Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    if (WindowInfo == NULL) return 0;

    return (UINT)CreateWindow(WindowInfo);
}

/***************************************************************************/

UINT SysCall_ShowWindow(UINT Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    if (WindowInfo == NULL) return 0;

    return (UINT)ShowWindow(WindowInfo->Window, TRUE);
}

/***************************************************************************/

UINT SysCall_HideWindow(UINT Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    if (WindowInfo == NULL) return 0;

    return (UINT)ShowWindow(WindowInfo->Window, FALSE);
}

/***************************************************************************/

UINT SysCall_MoveWindow(UINT Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    if (WindowInfo == NULL) return 0;

    return (UINT)MoveWindow(WindowInfo->Window, &(WindowInfo->WindowPosition));
}

/***************************************************************************/

UINT SysCall_SizeWindow(UINT Parameter) {
    LPWINDOWINFO WindowInfo = (LPWINDOWINFO)Parameter;

    if (WindowInfo == NULL) return 0;

    return (UINT)SizeWindow(WindowInfo->Window, &(WindowInfo->WindowSize));
}

/***************************************************************************/

UINT SysCall_SetWindowFunc(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_GetWindowFunc(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_SetWindowStyle(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_GetWindowStyle(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_SetWindowProp(UINT Parameter) {
    LPPROPINFO PropInfo = (LPPROPINFO)Parameter;

    if (PropInfo == NULL) return 0;

    return SetWindowProp(PropInfo->Window, PropInfo->Name, PropInfo->Value);
}

/***************************************************************************/

UINT SysCall_GetWindowProp(UINT Parameter) {
    LPPROPINFO PropInfo = (LPPROPINFO)Parameter;

    if (PropInfo == NULL) return 0;

    return GetWindowProp(PropInfo->Window, PropInfo->Name);
}

/***************************************************************************/

UINT SysCall_GetWindowRect(UINT Parameter) {
    LPWINDOWRECT WindowRect = (LPWINDOWRECT)Parameter;

    if (WindowRect == NULL) return 0;

    return (UINT)GetWindowRect(WindowRect->Window, &(WindowRect->Rect));
}

/***************************************************************************/

UINT SysCall_InvalidateWindowRect(UINT Parameter) {
    LPWINDOWRECT WindowRect = (LPWINDOWRECT)Parameter;

    if (WindowRect == NULL) return 0;

    return (UINT)InvalidateWindowRect(WindowRect->Window, &(WindowRect->Rect));
}

/***************************************************************************/

UINT SysCall_GetWindowGC(UINT Parameter) { return (UINT)GetWindowGC((HANDLE)Parameter); }

/***************************************************************************/

UINT SysCall_ReleaseWindowGC(UINT Parameter) {
    UNUSED(Parameter);
    return 1;
}

/***************************************************************************/

UINT SysCall_EnumWindows(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_DefWindowFunc(UINT Parameter) {
    LPMESSAGEINFO Message = (LPMESSAGEINFO)Parameter;

    if (Message == NULL) return 0;

    return (UINT)DefWindowFunc(Message->Target, Message->Message, Message->Param1, Message->Param2);
}

/***************************************************************************/

UINT SysCall_GetSystemBrush(UINT Parameter) { return (UINT)GetSystemBrush(Parameter); }

/***************************************************************************/

UINT SysCall_GetSystemPen(UINT Parameter) { return (UINT)GetSystemPen(Parameter); }

/***************************************************************************/

UINT SysCall_CreateBrush(UINT Parameter) {
    LPBRUSHINFO Info = (LPBRUSHINFO)Parameter;

    return (UINT)CreateBrush(Info);
}

/***************************************************************************/

UINT SysCall_CreatePen(UINT Parameter) {
    LPPENINFO Info = (LPPENINFO)Parameter;

    return (UINT)CreatePen(Info);
}

/***************************************************************************/

UINT SysCall_SelectBrush(UINT Parameter) {
    LPGCSELECT Sel = (LPGCSELECT)Parameter;
    if (Sel == NULL) return 0;
    return (UINT)SelectBrush(Sel->GC, Sel->Object);
}

/***************************************************************************/

UINT SysCall_SelectPen(UINT Parameter) {
    LPGCSELECT Sel = (LPGCSELECT)Parameter;
    if (Sel == NULL) return 0;
    return (UINT)SelectPen(Sel->GC, Sel->Object);
}

/***************************************************************************/

UINT SysCall_SetPixel(UINT Parameter) {
    LPPIXELINFO PixelInfo = (LPPIXELINFO)Parameter;
    if (PixelInfo == NULL) return 0;
    return (UINT)SetPixel(PixelInfo);
}

/***************************************************************************/

UINT SysCall_GetPixel(UINT Parameter) {
    LPPIXELINFO PixelInfo = (LPPIXELINFO)Parameter;
    if (PixelInfo == NULL) return 0;
    return (UINT)GetPixel(PixelInfo);
}

/***************************************************************************/

UINT SysCall_Line(UINT Parameter) {
    LPLINEINFO LineInfo = (LPLINEINFO)Parameter;
    if (LineInfo == NULL) return 0;
    return (UINT)Line(LineInfo);
}

/***************************************************************************/

UINT SysCall_Rectangle(UINT Parameter) {
    LPRECTINFO RectInfo = (LPRECTINFO)Parameter;
    if (RectInfo == NULL) return 0;
    return (UINT)Rectangle(RectInfo);
}

/***************************************************************************/

UINT SysCall_GetMousePos(UINT Parameter) {
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

UINT SysCall_SetMousePos(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_GetMouseButtons(UINT Parameter) {
    UNUSED(Parameter);
    return SerialMouseDriver.Command(DF_MOUSE_GETBUTTONS, 0);
}

/***************************************************************************/

UINT SysCall_ShowMouse(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_HideMouse(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_ClipMouse(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_CaptureMouse(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_ReleaseMouse(UINT Parameter) {
    UNUSED(Parameter);
    return 0;
}

/***************************************************************************/

UINT SysCall_Login(UINT Parameter) {
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

UINT SysCall_Logout(UINT Parameter) {
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

UINT SysCall_GetCurrentUser(UINT Parameter) {
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
    UserInfo->LoginTime = U64_FromUINT(GetSystemTime());
    UserInfo->SessionID = Session->SessionID;

    return TRUE;
}

/***************************************************************************/

UINT SysCall_ChangePassword(UINT Parameter) {
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

UINT SysCall_CreateUser(UINT Parameter) {
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

UINT SysCall_DeleteUser(UINT Parameter) {
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

UINT SysCall_ListUsers(UINT Parameter) {
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

UINT SysCall_SocketCreate(UINT Parameter) {
    LPSOCKET_CREATE_INFO Info = (LPSOCKET_CREATE_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_CREATE_INFO) {
        return SocketCreate(Info->AddressFamily, Info->SocketType, Info->Protocol);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SysCall_SocketBind(UINT Parameter) {
    LPSOCKET_BIND_INFO Info = (LPSOCKET_BIND_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_BIND_INFO) {
        return SocketBind(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressData, Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SysCall_SocketListen(UINT Parameter) {
    LPSOCKET_LISTEN_INFO Info = (LPSOCKET_LISTEN_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_LISTEN_INFO) {
        return SocketListen(Info->SocketHandle, Info->Backlog);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SysCall_SocketAccept(UINT Parameter) {
    LPSOCKET_ACCEPT_INFO Info = (LPSOCKET_ACCEPT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_ACCEPT_INFO) {
        return SocketAccept(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressBuffer, Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SysCall_SocketConnect(UINT Parameter) {
    LPSOCKET_CONNECT_INFO Info = (LPSOCKET_CONNECT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_CONNECT_INFO) {
        return SocketConnect(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressData, Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SysCall_SocketSend(UINT Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketSend(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SysCall_SocketReceive(UINT Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketReceive(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SysCall_SocketSendTo(UINT Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketSendTo(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags, (LPSOCKET_ADDRESS)Info->AddressData, Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SysCall_SocketReceiveFrom(UINT Parameter) {
    LPSOCKET_DATA_INFO Info = (LPSOCKET_DATA_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_DATA_INFO) {
        return SocketReceiveFrom(Info->SocketHandle, Info->Buffer, Info->Length, Info->Flags, (LPSOCKET_ADDRESS)Info->AddressData, &Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SysCall_SocketClose(UINT Parameter) {
    SOCKET_HANDLE SocketHandle = (SOCKET_HANDLE)Parameter;
    return SocketClose(SocketHandle);
}

/***************************************************************************/

UINT SysCall_SocketShutdown(UINT Parameter) {
    LPSOCKET_SHUTDOWN_INFO Info = (LPSOCKET_SHUTDOWN_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_SHUTDOWN_INFO) {
        return SocketShutdown(Info->SocketHandle, Info->How);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SysCall_SocketGetOption(UINT Parameter) {
    LPSOCKET_OPTION_INFO Info = (LPSOCKET_OPTION_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_OPTION_INFO) {
        return SocketGetOption(Info->SocketHandle, Info->Level, Info->OptionName, Info->OptionValue, &Info->OptionLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SysCall_SocketSetOption(UINT Parameter) {
    LPSOCKET_OPTION_INFO Info = (LPSOCKET_OPTION_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_OPTION_INFO) {
        return SocketSetOption(Info->SocketHandle, Info->Level, Info->OptionName, Info->OptionValue, Info->OptionLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SysCall_SocketGetPeerName(UINT Parameter) {
    LPSOCKET_ACCEPT_INFO Info = (LPSOCKET_ACCEPT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_ACCEPT_INFO) {
        return SocketGetPeerName(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressBuffer, Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SysCall_SocketGetSocketName(UINT Parameter) {
    LPSOCKET_ACCEPT_INFO Info = (LPSOCKET_ACCEPT_INFO)Parameter;

    SAFE_USE_INPUT_POINTER(Info, SOCKET_ACCEPT_INFO) {
        return SocketGetSocketName(Info->SocketHandle, (LPSOCKET_ADDRESS)Info->AddressBuffer, Info->AddressLength);
    }

    return DF_ERROR_BADPARAM;
}

/***************************************************************************/

UINT SystemCallHandler(U32 Function, UINT Parameter) {
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
