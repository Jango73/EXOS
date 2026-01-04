
/************************************************************************\

    EXOS Runtime
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


    EXOS C API

\************************************************************************/

#ifndef EXOS_H_INCLUDED
#define EXOS_H_INCLUDED

/************************************************************************/

#include "../../kernel/include/User.h"

#ifdef __cplusplus
extern "C" {
#endif

/************************************************************************/

// Helper macro to cast parameters to the architecture-sized integer type.
#ifndef EXOS_PARAM
#define EXOS_PARAM(Value) ((uint_t)(Value))
#endif

static inline I32 imin(I32 A, I32 B) { return (A < B) ? A : B; }
static inline I32 imax(I32 A, I32 B) { return (A > B) ? A : B; }

typedef struct tag_MESSAGE {
    HANDLE Target;
    DATETIME Time;
    U32 Message;
    U32 Param1;
    U32 Param2;
} MESSAGE, *LPMESSAGE;

/************************************************************************/

HANDLE CreateTask(LPTASKINFO);
BOOL KillTask(HANDLE);
void Exit(void);
void Sleep(U32);
U32 Wait(LPWAITINFO);
U32 GetSystemTime(void);
U32 FindFirstFile(FILEFINDINFO* Info);
U32 FindNextFile(FILEFINDINFO* Info);
BOOL GetMessage(HANDLE, LPMESSAGE, U32, U32);
BOOL PeekMessage(HANDLE, LPMESSAGE, U32, U32, U32);
BOOL DispatchMessage(LPMESSAGE);
BOOL PostMessage(HANDLE, U32, U32, U32);
U32 SendMessage(HANDLE, U32, U32, U32);
HANDLE CreateDesktop(void);
BOOL ShowDesktop(HANDLE);
HANDLE GetDesktopWindow(HANDLE);
HANDLE GetCurrentDesktop(void);
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
U32 GetKeyModifiers(void);
U32 ConsoleGetKey(LPKEYCODE);
U32 ConsoleBlitBuffer(LPCONSOLEBLITBUFFER);
void ConsoleGotoXY(LPPOINT);
void ConsoleClear(void);
U32 ConsoleSetMode(U32 Columns, U32 Rows);
BOOL DeleteObject(HANDLE);
void srand(U32);
U32 rand(void);

/************************************************************************/
// Berkeley Socket API for userland

SOCKET_HANDLE SocketCreate(U16 AddressFamily, U16 SocketType, U16 Protocol);
U32 SocketBind(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32 AddressLength);
U32 SocketListen(SOCKET_HANDLE SocketHandle, U32 Backlog);
SOCKET_HANDLE SocketAccept(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength);
U32 SocketConnect(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32 AddressLength);
I32 SocketSend(SOCKET_HANDLE SocketHandle, LPCVOID Buffer, U32 Length, U32 Flags);
I32 SocketReceive(SOCKET_HANDLE SocketHandle, LPVOID Buffer, U32 Length, U32 Flags);
I32 SocketSendTo(SOCKET_HANDLE SocketHandle, LPCVOID Buffer, U32 Length, U32 Flags, LPSOCKET_ADDRESS DestAddress, U32 AddressLength);
I32 SocketReceiveFrom(SOCKET_HANDLE SocketHandle, LPVOID Buffer, U32 Length, U32 Flags, LPSOCKET_ADDRESS SourceAddress, U32* AddressLength);
U32 SocketClose(SOCKET_HANDLE SocketHandle);
U32 SocketShutdown(SOCKET_HANDLE SocketHandle, U32 How);
U32 SocketGetOption(SOCKET_HANDLE SocketHandle, U32 Level, U32 OptionName, LPVOID OptionValue, U32* OptionLength);
U32 SocketSetOption(SOCKET_HANDLE SocketHandle, U32 Level, U32 OptionName, LPCVOID OptionValue, U32 OptionLength);
U32 SocketGetPeerName(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength);
U32 SocketGetSocketName(SOCKET_HANDLE SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength);

// Address utility functions
U32 InternetAddressFromString(LPCSTR IPString);
LPCSTR InternetAddressToString(U32 IPAddress);

// Socket address utility functions
U32 SocketAddressInetToGeneric(LPSOCKET_ADDRESS_INET InetAddress, LPSOCKET_ADDRESS GenericAddress);

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

static inline unsigned short HToNs(unsigned short Value) { return Value; }
static inline unsigned short NToHs(unsigned short Value) { return Value; }
static inline unsigned long HToNl(unsigned long Value) { return Value; }
static inline unsigned long NToHl(unsigned long Value) { return Value; }

#elif defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

static inline unsigned short HToNs(unsigned short Value) { return (unsigned short)((Value << 8) | (Value >> 8)); }
static inline unsigned short NToHs(unsigned short Value) { return HToNs(Value); }
static inline unsigned long HToNl(unsigned long Value) {
    return ((Value & 0x000000FFU) << 24) | ((Value & 0x0000FF00U) << 8) | ((Value & 0x00FF0000U) >> 8) |
           ((Value & 0xFF000000U) >> 24);
}
static inline unsigned long NToHl(unsigned long Value) { return HToNl(Value); }

#else
    #error "Endianness not defined"
#endif

/************************************************************************/

#ifdef __cplusplus
}
#endif

#endif
