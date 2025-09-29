
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


    System calls

\************************************************************************/

#ifndef SYSCALL_H_INCLUDED
#define SYSCALL_H_INCLUDED

/************************************************************************/

#include "Kernel.h"
#include "User.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

extern void InitializeSystemCalls(void);

extern SYSCALLENTRY SysCallTable[SYSCALL_Last];

/************************************************************************/

U32 SysCall_Debug(U32 Parameter);
U32 SysCall_GetVersion(U32 Parameter);
U32 SysCall_GetSystemInfo(U32 Parameter);
U32 SysCall_GetLastError(U32 Parameter);
U32 SysCall_SetLastError(U32 Parameter);
U32 SysCall_GetSystemTime(U32 Parameter);
U32 SysCall_GetLocalTime(U32 Parameter);
U32 SysCall_SetLocalTime(U32 Parameter);
U32 SysCall_DeleteObject(U32 Parameter);
U32 SysCall_CreateProcess(U32 Parameter);
U32 SysCall_KillProcess(U32 Parameter);

U32 SysCall_GetProcessInfo(U32 Parameter);
U32 SysCall_CreateTask(U32 Parameter);
U32 SysCall_KillTask(U32 Parameter);
U32 SysCall_Exit(U32 Parameter);
U32 SysCall_SuspendTask(U32 Parameter);
U32 SysCall_ResumeTask(U32 Parameter);
U32 SysCall_Sleep(U32 Parameter);
U32 SysCall_Wait(U32 Parameter);
U32 SysCall_PostMessage(U32 Parameter);
U32 SysCall_SendMessage(U32 Parameter);
U32 SysCall_PeekMessage(U32 Parameter);
U32 SysCall_GetMessage(U32 Parameter);
U32 SysCall_DispatchMessage(U32 Parameter);
U32 SysCall_CreateMutex(U32 Parameter);
U32 SysCall_DeleteMutex(U32 Parameter);
U32 SysCall_LockMutex(U32 Parameter);
U32 SysCall_UnlockMutex(U32 Parameter);
U32 SysCall_AllocRegion(U32 Parameter);
U32 SysCall_FreeRegion(U32 Parameter);
U32 SysCall_IsMemoryValid(U32 Parameter);
U32 SysCall_GetProcessHeap(U32 Parameter);
U32 SysCall_HeapAlloc(U32 Parameter);
U32 SysCall_HeapFree(U32 Parameter);
U32 SysCall_HeapRealloc(U32 Parameter);
U32 SysCall_EnumVolumes(U32 Parameter);
U32 SysCall_GetVolumeInfo(U32 Parameter);
U32 SysCall_OpenFile(U32 Parameter);
U32 SysCall_ReadFile(U32 Parameter);
U32 SysCall_WriteFile(U32 Parameter);
U32 SysCall_GetFileSize(U32 Parameter);
U32 SysCall_GetFilePosition(U32 Parameter);
U32 SysCall_SetFilePosition(U32 Parameter);
U32 SysCall_ConsolePeekKey(U32 Parameter);
U32 SysCall_ConsoleGetKey(U32 Parameter);
U32 SysCall_ConsoleGetChar(U32 Parameter);
U32 SysCall_ConsolePrint(U32 Parameter);
U32 SysCall_ConsoleGetString(U32 Parameter);
U32 SysCall_ConsoleGotoXY(U32 Parameter);
U32 SysCall_ClearScreen(U32 Parameter);

U32 SysCall_CreateDesktop(U32 Parameter);
U32 SysCall_ShowDesktop(U32 Parameter);
U32 SysCall_GetDesktopWindow(U32 Parameter);
U32 SysCall_CreateWindow(U32 Parameter);
U32 SysCall_ShowWindow(U32 Parameter);
U32 SysCall_HideWindow(U32 Parameter);
U32 SysCall_MoveWindow(U32 Parameter);
U32 SysCall_SizeWindow(U32 Parameter);
U32 SysCall_SetWindowFunc(U32 Parameter);
U32 SysCall_GetWindowFunc(U32 Parameter);
U32 SysCall_SetWindowStyle(U32 Parameter);
U32 SysCall_GetWindowStyle(U32 Parameter);
U32 SysCall_SetWindowProp(U32 Parameter);
U32 SysCall_GetWindowProp(U32 Parameter);
U32 SysCall_GetWindowRect(U32 Parameter);
U32 SysCall_InvalidateWindowRect(U32 Parameter);
U32 SysCall_GetWindowGC(U32 Parameter);
U32 SysCall_ReleaseWindowGC(U32 Parameter);
U32 SysCall_EnumWindows(U32 Parameter);
U32 SysCall_DefWindowFunc(U32 Parameter);
U32 SysCall_GetSystemBrush(U32 Parameter);
U32 SysCall_GetSystemPen(U32 Parameter);
U32 SysCall_CreateBrush(U32 Parameter);
U32 SysCall_CreatePen(U32 Parameter);
U32 SysCall_SelectBrush(U32 Parameter);
U32 SysCall_SelectPen(U32 Parameter);
U32 SysCall_SetPixel(U32 Parameter);
U32 SysCall_GetPixel(U32 Parameter);
U32 SysCall_Line(U32 Parameter);
U32 SysCall_Rectangle(U32 Parameter);
U32 SysCall_GetMousePos(U32 Parameter);
U32 SysCall_SetMousePos(U32 Parameter);
U32 SysCall_GetMouseButtons(U32 Parameter);
U32 SysCall_ShowMouse(U32 Parameter);
U32 SysCall_HideMouse(U32 Parameter);
U32 SysCall_ClipMouse(U32 Parameter);
U32 SysCall_CaptureMouse(U32 Parameter);
U32 SysCall_ReleaseMouse(U32 Parameter);
U32 SysCall_Login(U32 Parameter);
U32 SysCall_Logout(U32 Parameter);
U32 SysCall_GetCurrentUser(U32 Parameter);
U32 SysCall_ChangePassword(U32 Parameter);
U32 SysCall_CreateUser(U32 Parameter);
U32 SysCall_DeleteUser(U32 Parameter);
U32 SysCall_ListUsers(U32 Parameter);

U32 SysCall_SocketCreate(U32 Parameter);
U32 SysCall_SocketBind(U32 Parameter);
U32 SysCall_SocketListen(U32 Parameter);
U32 SysCall_SocketAccept(U32 Parameter);
U32 SysCall_SocketConnect(U32 Parameter);
U32 SysCall_SocketSend(U32 Parameter);
U32 SysCall_SocketReceive(U32 Parameter);
U32 SysCall_SocketSendTo(U32 Parameter);
U32 SysCall_SocketReceiveFrom(U32 Parameter);
U32 SysCall_SocketClose(U32 Parameter);
U32 SysCall_SocketShutdown(U32 Parameter);
U32 SysCall_SocketGetOption(U32 Parameter);
U32 SysCall_SocketSetOption(U32 Parameter);
U32 SysCall_SocketGetPeerName(U32 Parameter);
U32 SysCall_SocketGetSocketName(U32 Parameter);

/************************************************************************/

#pragma pack(pop)

#endif  // SYSCALL_H_INCLUDED
