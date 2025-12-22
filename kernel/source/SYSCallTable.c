
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

#include "SYSCall.h"

/************************************************************************/

SYSCALLENTRY SysCallTable[SYSCALL_Last];

/************************************************************************/

void InitializeSystemCallTable(void) {
    U32 Index;

    for (Index = 0; Index < SYSCALL_Last; Index++) {
        SysCallTable[Index].Function = NULL;
        SysCallTable[Index].Privilege = EXOS_PRIVILEGE_USER;
    }

    // Base Services
    SysCallTable[SYSCALL_GetVersion] = (SYSCALLENTRY){SysCall_GetVersion, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetSystemInfo] = (SYSCALLENTRY){SysCall_GetSystemInfo, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetLastError] = (SYSCALLENTRY){SysCall_GetLastError, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetLastError] = (SYSCALLENTRY){SysCall_SetLastError, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Debug] = (SYSCALLENTRY){SysCall_Debug, EXOS_PRIVILEGE_USER};

    // Socket syscalls
    SysCallTable[SYSCALL_SocketCreate] = (SYSCALLENTRY){SysCall_SocketCreate, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketShutdown] = (SYSCALLENTRY){SysCall_SocketShutdown, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketBind] = (SYSCALLENTRY){SysCall_SocketBind, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketListen] = (SYSCALLENTRY){SysCall_SocketListen, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketAccept] = (SYSCALLENTRY){SysCall_SocketAccept, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketConnect] = (SYSCALLENTRY){SysCall_SocketConnect, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketSend] = (SYSCALLENTRY){SysCall_SocketSend, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketReceive] = (SYSCALLENTRY){SysCall_SocketReceive, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketSendTo] = (SYSCALLENTRY){SysCall_SocketSendTo, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketReceiveFrom] = (SYSCALLENTRY){SysCall_SocketReceiveFrom, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketClose] = (SYSCALLENTRY){SysCall_SocketClose, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketGetOption] = (SYSCALLENTRY){SysCall_SocketGetOption, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketSetOption] = (SYSCALLENTRY){SysCall_SocketSetOption, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketGetPeerName] = (SYSCALLENTRY){SysCall_SocketGetPeerName, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SocketGetSocketName] = (SYSCALLENTRY){SysCall_SocketGetSocketName, EXOS_PRIVILEGE_USER};

    // Time Services
    SysCallTable[SYSCALL_GetSystemTime] = (SYSCALLENTRY){SysCall_GetSystemTime, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetLocalTime] = (SYSCALLENTRY){SysCall_GetLocalTime, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetLocalTime] = (SYSCALLENTRY){SysCall_SetLocalTime, EXOS_PRIVILEGE_USER};

    // Process Services
    SysCallTable[SYSCALL_DeleteObject] = (SYSCALLENTRY){SysCall_DeleteObject, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CreateProcess] = (SYSCALLENTRY){SysCall_CreateProcess, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_KillProcess] = (SYSCALLENTRY){SysCall_KillProcess, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetProcessInfo] = (SYSCALLENTRY){SysCall_GetProcessInfo, EXOS_PRIVILEGE_USER};

    // Threading Services
    SysCallTable[SYSCALL_CreateTask] = (SYSCALLENTRY){SysCall_CreateTask, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_KillTask] = (SYSCALLENTRY){SysCall_KillTask, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Exit] = (SYSCALLENTRY){SysCall_Exit, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SuspendTask] = (SYSCALLENTRY){SysCall_SuspendTask, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ResumeTask] = (SYSCALLENTRY){SysCall_ResumeTask, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Sleep] = (SYSCALLENTRY){SysCall_Sleep, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Wait] = (SYSCALLENTRY){SysCall_Wait, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_PostMessage] = (SYSCALLENTRY){SysCall_PostMessage, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SendMessage] = (SYSCALLENTRY){SysCall_SendMessage, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_PeekMessage] = (SYSCALLENTRY){SysCall_PeekMessage, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetMessage] = (SYSCALLENTRY){SysCall_GetMessage, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_DispatchMessage] = (SYSCALLENTRY){SysCall_DispatchMessage, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CreateMutex] = (SYSCALLENTRY){SysCall_CreateMutex, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_LockMutex] = (SYSCALLENTRY){SysCall_LockMutex, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_UnlockMutex] = (SYSCALLENTRY){SysCall_UnlockMutex, EXOS_PRIVILEGE_USER};

    // Memory Services
    SysCallTable[SYSCALL_AllocRegion] = (SYSCALLENTRY){SysCall_AllocRegion, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_FreeRegion] = (SYSCALLENTRY){SysCall_FreeRegion, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_IsMemoryValid] = (SYSCALLENTRY){SysCall_IsMemoryValid, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetProcessHeap] = (SYSCALLENTRY){SysCall_GetProcessHeap, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_HeapAlloc] = (SYSCALLENTRY){SysCall_HeapAlloc, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_HeapFree] = (SYSCALLENTRY){SysCall_HeapFree, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_HeapRealloc] = (SYSCALLENTRY){SysCall_HeapRealloc, EXOS_PRIVILEGE_USER};

    // File Services
    SysCallTable[SYSCALL_EnumVolumes] = (SYSCALLENTRY){SysCall_EnumVolumes, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetVolumeInfo] = (SYSCALLENTRY){SysCall_GetVolumeInfo, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_OpenFile] = (SYSCALLENTRY){SysCall_OpenFile, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ReadFile] = (SYSCALLENTRY){SysCall_ReadFile, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_WriteFile] = (SYSCALLENTRY){SysCall_WriteFile, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetFileSize] = (SYSCALLENTRY){SysCall_GetFileSize, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetFilePointer] = (SYSCALLENTRY){SysCall_GetFilePosition, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetFilePointer] = (SYSCALLENTRY){SysCall_SetFilePosition, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_FindFirstFile] = (SYSCALLENTRY){SysCall_FindFirstFile, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_FindNextFile] = (SYSCALLENTRY){SysCall_FindNextFile, EXOS_PRIVILEGE_USER};

    // Console Services
    SysCallTable[SYSCALL_ConsolePeekKey] = (SYSCALLENTRY){SysCall_ConsolePeekKey, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleGetKey] = (SYSCALLENTRY){SysCall_ConsoleGetKey, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleGetKeyModifiers] = (SYSCALLENTRY){SysCall_ConsoleGetKeyModifiers, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsolePrint] = (SYSCALLENTRY){SysCall_ConsolePrint, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleGetString] = (SYSCALLENTRY){SysCall_ConsoleGetString, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleGotoXY] = (SYSCALLENTRY){SysCall_ConsoleGotoXY, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleClear] = (SYSCALLENTRY){SysCall_ConsoleClear, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ConsoleBlitBuffer] = (SYSCALLENTRY){SysCall_ConsoleBlitBuffer, EXOS_PRIVILEGE_USER};

    // Authentication Services
    SysCallTable[SYSCALL_Login] = (SYSCALLENTRY){SysCall_Login, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Logout] = (SYSCALLENTRY){SysCall_Logout, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetCurrentUser] = (SYSCALLENTRY){SysCall_GetCurrentUser, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ChangePassword] = (SYSCALLENTRY){SysCall_ChangePassword, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CreateUser] = (SYSCALLENTRY){SysCall_CreateUser, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_DeleteUser] = (SYSCALLENTRY){SysCall_DeleteUser, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ListUsers] = (SYSCALLENTRY){SysCall_ListUsers, EXOS_PRIVILEGE_USER};

    // Mouse Services
    SysCallTable[SYSCALL_GetMousePos] = (SYSCALLENTRY){SysCall_GetMousePos, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetMousePos] = (SYSCALLENTRY){SysCall_SetMousePos, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetMouseButtons] = (SYSCALLENTRY){SysCall_GetMouseButtons, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ShowMouse] = (SYSCALLENTRY){SysCall_ShowMouse, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_HideMouse] = (SYSCALLENTRY){SysCall_HideMouse, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ClipMouse] = (SYSCALLENTRY){SysCall_ClipMouse, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CaptureMouse] = (SYSCALLENTRY){SysCall_CaptureMouse, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ReleaseMouse] = (SYSCALLENTRY){SysCall_ReleaseMouse, EXOS_PRIVILEGE_USER};

    // Windowing Services
    SysCallTable[SYSCALL_CreateDesktop] = (SYSCALLENTRY){SysCall_CreateDesktop, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ShowDesktop] = (SYSCALLENTRY){SysCall_ShowDesktop, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetDesktopWindow] = (SYSCALLENTRY){SysCall_GetDesktopWindow, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetCurrentDesktop] = (SYSCALLENTRY){SysCall_GetCurrentDesktop, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CreateWindow] = (SYSCALLENTRY){SysCall_CreateWindow, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ShowWindow] = (SYSCALLENTRY){SysCall_ShowWindow, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_HideWindow] = (SYSCALLENTRY){SysCall_HideWindow, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_MoveWindow] = (SYSCALLENTRY){SysCall_MoveWindow, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SizeWindow] = (SYSCALLENTRY){SysCall_SizeWindow, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetWindowFunc] = (SYSCALLENTRY){SysCall_SetWindowFunc, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowFunc] = (SYSCALLENTRY){SysCall_GetWindowFunc, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetWindowStyle] = (SYSCALLENTRY){SysCall_SetWindowStyle, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowStyle] = (SYSCALLENTRY){SysCall_GetWindowStyle, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetWindowProp] = (SYSCALLENTRY){SysCall_SetWindowProp, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowProp] = (SYSCALLENTRY){SysCall_GetWindowProp, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowRect] = (SYSCALLENTRY){SysCall_GetWindowRect, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_InvalidateWindowRect] = (SYSCALLENTRY){SysCall_InvalidateWindowRect, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetWindowGC] = (SYSCALLENTRY){SysCall_GetWindowGC, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_ReleaseWindowGC] = (SYSCALLENTRY){SysCall_ReleaseWindowGC, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_EnumWindows] = (SYSCALLENTRY){SysCall_EnumWindows, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_DefWindowFunc] = (SYSCALLENTRY){SysCall_DefWindowFunc, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetSystemBrush] = (SYSCALLENTRY){SysCall_GetSystemBrush, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetSystemPen] = (SYSCALLENTRY){SysCall_GetSystemPen, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CreateBrush] = (SYSCALLENTRY){SysCall_CreateBrush, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_CreatePen] = (SYSCALLENTRY){SysCall_CreatePen, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SelectBrush] = (SYSCALLENTRY){SysCall_SelectBrush, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SelectPen] = (SYSCALLENTRY){SysCall_SelectPen, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_SetPixel] = (SYSCALLENTRY){SysCall_SetPixel, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_GetPixel] = (SYSCALLENTRY){SysCall_GetPixel, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Line] = (SYSCALLENTRY){SysCall_Line, EXOS_PRIVILEGE_USER};
    SysCallTable[SYSCALL_Rectangle] = (SYSCALLENTRY){SysCall_Rectangle, EXOS_PRIVILEGE_USER};
}
