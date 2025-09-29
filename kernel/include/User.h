
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


    User

\************************************************************************/

#ifndef USER_H_INCLUDED
#define USER_H_INCLUDED

#include "Base.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/************************************************************************\

    EXOS version

    MAJOR (1): Incremented for incompatible changes (breaking changes, e.g.,
        API mods that break existing code). Reset MINOR and PATCH to 0.
    MINOR (5): Incremented for compatible additions (new features or
        backward-compatible APIs). Reset PATCH to 0, MAJOR unchanged.
    PATCH (147): Incremented for bug fixes or minor opts (no API impact).
        MAJOR and MINOR unchanged.

\************************************************************************/

#define EXOS_VERSION_MAJOR 0
#define EXOS_VERSION_MINOR 0
#define EXOS_VERSION_PATCH 100

#define EXOS_VERSION MAKE_VERSION(EXOS_VERSION_MAJOR, EXOS_VERSION_MINOR)

/************************************************************************/
// EXOS ABI

// Global ABI version for user/kernel boundary
#define EXOS_ABI_VERSION 0x0001

/* Common header prefix for syscall payload structures.
   - Size: sizeof(struct) at compile-time of the caller
   - Version: per-struct or global EXOS_ABI_VERSION
   - Flags: reserved for extensions
*/
typedef struct tag_ABI_HEADER {
    U32 Size;
    U16 Version;
    U16 Flags;
} ABI_HEADER;

// C11 static assert helper
#define ABI_STATIC_ASSERT(cond, name) typedef char static_assert_##name[(cond) ? 1 : -1]

/************************************************************************/
// EXOS Base Services - Syscall IDs

#define SYSCALL_GetVersion 0x00000000
#define SYSCALL_GetSystemInfo 0x00000001
#define SYSCALL_GetLastError 0x00000002
#define SYSCALL_SetLastError 0x00000003
#define SYSCALL_Debug 0x00000066

/************************************************************************/
// Time Services

#define SYSCALL_GetSystemTime 0x00000004
#define SYSCALL_GetLocalTime 0x00000005
#define SYSCALL_SetLocalTime 0x00000006

/************************************************************************/
// Process services

#define SYSCALL_DeleteObject 0x00000007
#define SYSCALL_CreateProcess 0x00000008
#define SYSCALL_KillProcess 0x00000009
#define SYSCALL_GetProcessInfo 0x0000000A

/************************************************************************/
// Threading Services

#define SYSCALL_CreateTask 0x0000000B
#define SYSCALL_KillTask 0x0000000C
#define SYSCALL_Exit 0x00000033
#define SYSCALL_SuspendTask 0x0000000D
#define SYSCALL_ResumeTask 0x0000000E
#define SYSCALL_Sleep 0x0000000F
#define SYSCALL_Wait 0x00000010
#define SYSCALL_PostMessage 0x00000011
#define SYSCALL_SendMessage 0x00000012
#define SYSCALL_PeekMessage 0x00000013
#define SYSCALL_GetMessage 0x00000014
#define SYSCALL_DispatchMessage 0x00000015
#define SYSCALL_CreateMutex 0x00000016
#define SYSCALL_LockMutex 0x00000017
#define SYSCALL_UnlockMutex 0x00000018

/************************************************************************/
// Memory Services

#define SYSCALL_AllocRegion 0x00000019
#define SYSCALL_FreeRegion 0x0000001A
#define SYSCALL_IsMemoryValid 0x0000001B
#define SYSCALL_GetProcessHeap 0x0000001C
#define SYSCALL_HeapAlloc 0x0000001D
#define SYSCALL_HeapFree 0x0000001E
#define SYSCALL_HeapRealloc 0x0000001F

/************************************************************************/
// File Services

#define SYSCALL_EnumVolumes 0x00000020
#define SYSCALL_GetVolumeInfo 0x00000021
#define SYSCALL_OpenFile 0x00000022
#define SYSCALL_ReadFile 0x00000023
#define SYSCALL_WriteFile 0x00000024
#define SYSCALL_GetFileSize 0x00000025
#define SYSCALL_GetFilePointer 0x00000026
#define SYSCALL_SetFilePointer 0x00000027
#define SYSCALL_FindFirstFile 0x00000028
#define SYSCALL_FindNextFile 0x00000029
#define SYSCALL_CreateFileMapping 0x0000002A
#define SYSCALL_OpenFileMapping 0x0000002B
#define SYSCALL_MapViewOfFile 0x0000002C
#define SYSCALL_UnmapViewOfFile 0x0000002D

/************************************************************************/
// Console Services

#define SYSCALL_ConsolePeekKey 0x0000002E
#define SYSCALL_ConsoleGetKey 0x0000002F
#define SYSCALL_ConsolePrint 0x00000030
#define SYSCALL_ConsoleGetString 0x00000031
#define SYSCALL_ConsoleGotoXY 0x00000032
#define SYSCALL_ClearScreen 0x00000034

/************************************************************************/
// Authentication Services

#define SYSCALL_Login 0x00000035
#define SYSCALL_Logout 0x00000036
#define SYSCALL_GetCurrentUser 0x00000037
#define SYSCALL_ChangePassword 0x00000038
#define SYSCALL_CreateUser 0x00000039
#define SYSCALL_DeleteUser 0x0000003A
#define SYSCALL_ListUsers 0x0000003B

/************************************************************************/
// Mouse Services

#define SYSCALL_GetMousePos 0x0000003C
#define SYSCALL_SetMousePos 0x0000003D
#define SYSCALL_GetMouseButtons 0x0000003E
#define SYSCALL_ShowMouse 0x0000003F
#define SYSCALL_HideMouse 0x00000040
#define SYSCALL_ClipMouse 0x00000041
#define SYSCALL_CaptureMouse 0x00000042
#define SYSCALL_ReleaseMouse 0x00000043

/************************************************************************/
// Windowing Services

#define SYSCALL_CreateDesktop 0x00000044
#define SYSCALL_ShowDesktop 0x00000045
#define SYSCALL_GetDesktopWindow 0x00000046
#define SYSCALL_CreateWindow 0x00000047
#define SYSCALL_ShowWindow 0x00000048
#define SYSCALL_HideWindow 0x00000049
#define SYSCALL_MoveWindow 0x0000004A
#define SYSCALL_SizeWindow 0x0000004B
#define SYSCALL_SetWindowFunc 0x0000004C
#define SYSCALL_GetWindowFunc 0x0000004D
#define SYSCALL_SetWindowStyle 0x0000004E
#define SYSCALL_GetWindowStyle 0x0000004F
#define SYSCALL_SetWindowProp 0x00000050
#define SYSCALL_GetWindowProp 0x00000051
#define SYSCALL_GetWindowRect 0x00000052
#define SYSCALL_InvalidateWindowRect 0x00000053
#define SYSCALL_GetWindowGC 0x00000054
#define SYSCALL_ReleaseWindowGC 0x00000055
#define SYSCALL_EnumWindows 0x00000056
#define SYSCALL_DefWindowFunc 0x00000057
#define SYSCALL_GetSystemBrush 0x00000058
#define SYSCALL_GetSystemPen 0x00000059
#define SYSCALL_CreateBrush 0x0000005A
#define SYSCALL_CreatePen 0x0000005B
#define SYSCALL_SelectBrush 0x0000005C
#define SYSCALL_SelectPen 0x0000005D
#define SYSCALL_SetPixel 0x0000005E
#define SYSCALL_GetPixel 0x0000005F
#define SYSCALL_Line 0x00000060
#define SYSCALL_Rectangle 0x00000061
#define SYSCALL_CreateRectRegion 0x00000062
#define SYSCALL_CreatePolyRegion 0x00000063
#define SYSCALL_MoveRegion 0x00000064
#define SYSCALL_CombineRegion 0x00000065

/************************************************************************/
// Network Socket Services

#define SYSCALL_SocketCreate      0x00000068
#define SYSCALL_SocketShutdown    0x00000069
#define SYSCALL_SocketBind        0x0000006A
#define SYSCALL_SocketListen      0x0000006B
#define SYSCALL_SocketAccept      0x0000006C
#define SYSCALL_SocketConnect     0x0000006D
#define SYSCALL_SocketSend        0x0000006E
#define SYSCALL_SocketReceive     0x0000006F
#define SYSCALL_SocketSendTo      0x00000070
#define SYSCALL_SocketReceiveFrom 0x00000071
#define SYSCALL_SocketClose       0x00000072
#define SYSCALL_SocketGetOption   0x00000073
#define SYSCALL_SocketSetOption   0x00000074
#define SYSCALL_SocketGetPeerName 0x00000075
#define SYSCALL_SocketGetSocketName 0x00000076

/************************************************************************/

#define SYSCALL_Last 0x00000077

/************************************************************************/
// Structure limits

#define WAITINFO_MAX_OBJECTS 32

/************************************************************************/
// ABI Data Structures

// A function for a thread entry
typedef U32 (*TASKFUNC)(LPVOID);

// A function for window messaging
typedef U32 (*WINDOWFUNC)(HANDLE, U32, U32, U32);

// A function for volume enumeration
typedef BOOL (*ENUMVOLUMESFUNC)(HANDLE, LPVOID);

typedef struct tag_SYSTEMINFO {
    ABI_HEADER Header;
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
    STR KeyboardLayout[MAX_USER_NAME];
} SYSTEMINFO, *LPSYSTEMINFO;

typedef struct tag_SECURITYATTRIBUTES {
    U32 Nothing;
} SECURITYATTRIBUTES, *LPSECURITYATTRIBUTES;

typedef struct tag_PROCESSINFO {
    ABI_HEADER Header;
    U32 Flags;
    STR CommandLine[MAX_PATH_NAME];
    HANDLE StdOut;
    HANDLE StdIn;
    HANDLE StdErr;
    HANDLE Process;
    HANDLE Task;
    SECURITYATTRIBUTES Security;
} PROCESSINFO, *LPPROCESSINFO;

typedef struct tag_TASKINFO {
    ABI_HEADER Header;
    TASKFUNC Func;
    LPVOID Parameter;
    U32 StackSize;
    U32 Priority;
    U32 Flags;
    SECURITYATTRIBUTES Security;
    STR Name[MAX_USER_NAME];
} TASKINFO, *LPTASKINFO;

typedef struct tag_MESSAGEINFO {
    ABI_HEADER Header;
    DATETIME Time;
    U32 First;
    U32 Last;
    HANDLE Target;
    U32 Message;
    U32 Param1;
    U32 Param2;
} MESSAGEINFO, *LPMESSAGEINFO;

// Describes a mutex and some delay
typedef struct tag_MUTEXINFO {
    ABI_HEADER Header;
    HANDLE Mutex;
    U32 MilliSeconds;
} MUTEXINFO, *LPMUTEXINFO;

typedef struct tag_WAITINFO {
    ABI_HEADER Header;
    U32 Count;
    U32 MilliSeconds;
    U32 Flags;
    HANDLE Objects[WAITINFO_MAX_OBJECTS];
    U32 ExitCodes[WAITINFO_MAX_OBJECTS];
} WAITINFO, *LPWAITINFO;

typedef struct tag_ALLOCREGIONINFO {
    ABI_HEADER Header;
    U32 Base;    // The base virtual address (0 = don't care)
    U32 Target;  // The physical address to map to (0 = don't care)
    U32 Size;    // The size in bytes to allocate
    U32 Flags;   // See ALLOC_PAGES_xxx
} ALLOCREGIONINFO, *LPALLOCREGIONINFO;

typedef struct tag_HEAPREALLOCINFO {
    ABI_HEADER Header;
    LPVOID Pointer;  // Pointer to existing memory block, or NULL
    U32 Size;        // New size of memory block in bytes
} HEAPREALLOCINFO, *LPHEAPREALLOCINFO;

typedef struct tag_ENUMVOLUMESINFO {
    ABI_HEADER Header;
    ENUMVOLUMESFUNC Func;  // The callback for enumeration
    LPVOID Parameter;      //
} ENUMVOLUMESINFO, *LPENUMVOLUMESINFO;

typedef struct tag_VOLUMEINFO {
    U32 Size;
    HANDLE Volume;
    STR Name[MAX_FS_LOGICAL_NAME];
} VOLUMEINFO, *LPVOLUMEINFO;

typedef struct tag_FILEOPENINFO {
    ABI_HEADER Header;
    LPCSTR Name;
    U32 Flags;
} FILEOPENINFO, *LPFILEOPENINFO;

typedef struct tag_GRAPHICSMODEINFO {
    ABI_HEADER Header;
    U32 Width;
    U32 Height;
    U32 BitsPerPixel;
} GRAPHICSMODEINFO, *LPGRAPHICSMODEINFO;

typedef struct tag_FILECOPYINFO {
    ABI_HEADER Header;
    LPCSTR Source;
    LPCSTR Destination;
    U32 Flags;
} FILECOPYINFO, *LPFILECOPYINFO;

typedef struct tag_FILEOPERATION {
    ABI_HEADER Header;
    HANDLE File;
    U32 NumBytes;
    LPVOID Buffer;
} FILEOPERATION, *LPFILEOPERATION;

typedef struct tag_NETWORKINFO {
    ABI_HEADER Header;
    U8 MAC[6];       // MAC address
    U32 LinkUp;      // 1 = link up, 0 = link down
    U32 SpeedMbps;   // Link speed in Mbps
    U32 DuplexFull;  // 1 = full duplex, 0 = half duplex
    U32 MTU;         // Maximum Transmission Unit
} NETWORKINFO, *LPNETWORKINFO;

typedef struct tag_KEYCODE {
    U8 VirtualKey;
    STR ASCIICode;
    USTR Unicode;
} KEYCODE, *LPKEYCODE;

typedef struct tag_POINT {
    I32 X, Y;
} POINT, *LPPOINT;

typedef struct tag_RECT {
    I32 X1, Y1;
    I32 X2, Y2;
} RECT, *LPRECT;

typedef struct tag_WINDOWINFO {
    ABI_HEADER Header;
    HANDLE Window;
    HANDLE Parent;
    WINDOWFUNC Function;
    U32 Style;
    U32 ID;
    POINT WindowPosition;
    POINT WindowSize;
    BOOL ShowHide;
} WINDOWINFO, *LPWINDOWINFO;

typedef struct tag_PROPINFO {
    ABI_HEADER Header;
    HANDLE Window;
    LPCSTR Name;
    U32 Value;
} PROPINFO, *LPPROPINFO;

typedef struct tag_WINDOWRECT {
    ABI_HEADER Header;
    HANDLE Window;
    RECT Rect;
} WINDOWRECT, *LPWINDOWRECT;

typedef struct tag_GCSELECT {
    ABI_HEADER Header;
    HANDLE GC;
    HANDLE Object;
} GCSELECT, *LPGCSELECT;

typedef struct tag_BRUSHINFO {
    ABI_HEADER Header;
    COLOR Color;
    U32 Pattern;
    U32 Flags;
} BRUSHINFO, *LPBRUSHINFO;

typedef struct tag_PENINFO {
    ABI_HEADER Header;
    COLOR Color;
    U32 Pattern;
    U32 Flags;
} PENINFO, *LPPENINFO;

typedef struct tag_PIXELINFO {
    ABI_HEADER Header;
    HANDLE GC;
    I32 X;
    I32 Y;
    COLOR Color;
} PIXELINFO, *LPPIXELINFO;

typedef struct tag_LINEINFO {
    ABI_HEADER Header;
    HANDLE GC;
    I32 X1;
    I32 Y1;
    I32 X2;
    I32 Y2;
} LINEINFO, *LPLINEINFO;

typedef struct tag_RECTINFO {
    ABI_HEADER Header;
    HANDLE GC;
    I32 X1;
    I32 Y1;
    I32 X2;
    I32 Y2;
} RECTINFO, *LPRECTINFO;

typedef struct tag_TRIANGLEINFO {
    ABI_HEADER Header;
    HANDLE GC;
    POINT P1;
    POINT P2;
    POINT P3;
} TRIANGLEINFO, *LPTRIANGLEINFO;

typedef struct tag_LOGIN_INFO {
    ABI_HEADER Header;
    STR UserName[MAX_USER_NAME];
    STR Password[MAX_USER_NAME];
} LOGIN_INFO, *LPLOGIN_INFO;

typedef struct tag_PASSWORD_CHANGE {
    ABI_HEADER Header;
    STR OldPassword[MAX_USER_NAME];
    STR NewPassword[MAX_USER_NAME];
} PASSWORD_CHANGE, *LPPASSWORD_CHANGE;

typedef struct tag_USER_CREATE_INFO {
    ABI_HEADER Header;
    STR UserName[MAX_USER_NAME];
    STR Password[MAX_USER_NAME];
    U32 Privilege;
} USER_CREATE_INFO, *LPUSER_CREATE_INFO;

typedef struct tag_USER_DELETE_INFO {
    ABI_HEADER Header;
    STR UserName[MAX_USER_NAME];
} USER_DELETE_INFO, *LPUSER_DELETE_INFO;

typedef struct tag_USER_LIST_INFO {
    ABI_HEADER Header;
    U32 MaxUsers;
    U32 UserCount;
    STR UserNames[1][MAX_USER_NAME];
} USER_LIST_INFO, *LPUSER_LIST_INFO;

typedef struct tag_CURRENT_USER_INFO {
    ABI_HEADER Header;
    STR UserName[MAX_USER_NAME];
    U32 Privilege;
    U64 LoginTime;
    U64 SessionID;
} CURRENT_USER_INFO, *LPCURRENT_USER_INFO;

/************************************************************************/
// Socket Syscall Structures

typedef struct tag_SOCKET_CREATE_INFO {
    ABI_HEADER Header;
    U16 AddressFamily;    // SOCKET_AF_INET
    U16 SocketType;       // SOCKET_TYPE_STREAM, SOCKET_TYPE_DGRAM
    U16 Protocol;         // SOCKET_PROTOCOL_TCP, SOCKET_PROTOCOL_UDP
} SOCKET_CREATE_INFO, *LPSOCKET_CREATE_INFO;

typedef struct tag_SOCKET_BIND_INFO {
    ABI_HEADER Header;
    U32 SocketHandle;
    U8 AddressData[16];  // Storage for socket address
    U32 AddressLength;
} SOCKET_BIND_INFO, *LPSOCKET_BIND_INFO;

typedef struct tag_SOCKET_LISTEN_INFO {
    ABI_HEADER Header;
    U32 SocketHandle;
    U32 Backlog;
} SOCKET_LISTEN_INFO, *LPSOCKET_LISTEN_INFO;

typedef struct tag_SOCKET_ACCEPT_INFO {
    ABI_HEADER Header;
    U32 SocketHandle;
    LPVOID AddressBuffer;
    U32* AddressLength;
} SOCKET_ACCEPT_INFO, *LPSOCKET_ACCEPT_INFO;

typedef struct tag_SOCKET_CONNECT_INFO {
    ABI_HEADER Header;
    U32 SocketHandle;
    U8 AddressData[16];  // Storage for socket address
    U32 AddressLength;
} SOCKET_CONNECT_INFO, *LPSOCKET_CONNECT_INFO;

typedef struct tag_SOCKET_DATA_INFO {
    ABI_HEADER Header;
    U32 SocketHandle;
    LPVOID Buffer;
    U32 Length;
    U32 Flags;
    U8 AddressData[16];  // For SendTo/ReceiveFrom
    U32 AddressLength;
} SOCKET_DATA_INFO, *LPSOCKET_DATA_INFO;

typedef struct tag_SOCKET_OPTION_INFO {
    ABI_HEADER Header;
    U32 SocketHandle;
    U32 Level;
    U32 OptionName;
    LPVOID OptionValue;
    U32 OptionLength;
} SOCKET_OPTION_INFO, *LPSOCKET_OPTION_INFO;

typedef struct tag_SOCKET_SHUTDOWN_INFO {
    ABI_HEADER Header;
    U32 SocketHandle;
    U32 How;  // SOCKET_SHUTDOWN_READ, SOCKET_SHUTDOWN_WRITE, SOCKET_SHUTDOWN_BOTH
} SOCKET_SHUTDOWN_INFO, *LPSOCKET_SHUTDOWN_INFO;

/************************************************************************/
// Socket Address Structures

typedef struct tag_SOCKET_ADDRESS {
    U16 AddressFamily;
    U8  Data[14];
} SOCKET_ADDRESS, *LPSOCKET_ADDRESS;

typedef struct tag_SOCKET_ADDRESS_INET {
    U16 AddressFamily;      // SOCKET_AF_INET
    U16 Port;               // Port in network byte order
    U32 Address;            // IPv4 address in network byte order
    U8  Zero[8];            // Padding to 16 bytes
} SOCKET_ADDRESS_INET, *LPSOCKET_ADDRESS_INET;

/************************************************************************/
// Flags

#define TASK_PRIORITY_LOWEST 0x00
#define TASK_PRIORITY_LOWER 0x04
#define TASK_PRIORITY_MEDIUM 0x08
#define TASK_PRIORITY_HIGHER 0x0C
#define TASK_PRIORITY_HIGHEST 0x10
#define TASK_PRIORITY_CRITICAL 0xFF

#define WAIT_FLAG_ANY 0x00000000
#define WAIT_FLAG_ALL 0x00000001

#define WAIT_INVALID_PARAMETER 0xFFFFFFFF
#define WAIT_TIMEOUT 0x00000102
#define WAIT_OBJECT_0 0x00000000

#define ALLOC_PAGES_RESERVE 0x00000000
#define ALLOC_PAGES_COMMIT 0x00000001
#define ALLOC_PAGES_READONLY 0x00000000
#define ALLOC_PAGES_READWRITE 0x00000002
#define ALLOC_PAGES_UC 0x00000004          // Uncached (for MMIO/BAR mappings)
#define ALLOC_PAGES_WC 0x00000008          // Write-combining (rare; mostly for framebuffers)
#define ALLOC_PAGES_IO 0x00000010          // Exact PMA mapping for IO (BAR) -> do not touch RAM bitmap
#define ALLOC_PAGES_AT_OR_OVER 0x00000020  // If a linear address is specified, can allocate anywhere above it

#define FILE_OPEN_READ 0x00000001
#define FILE_OPEN_WRITE 0x00000002
#define FILE_OPEN_APPEND 0x00000004
#define FILE_OPEN_EXISTING 0x00000008
#define FILE_OPEN_CREATE_ALWAYS 0x00000010
#define FILE_OPEN_TRUNCATE 0x00000020
#define FILE_OPEN_SEEK_END 0x00000040

/************************************************************************/
// Driver generic functions

#define DF_LOAD 0x0000
#define DF_UNLOAD 0x0001
#define DF_GETVERSION 0x0002
#define DF_GETCAPS 0x0003
#define DF_GETLASTFUNC 0x0004
#define DF_PROBE 0x0005
#define DF_ATTACH 0x0006
#define DF_DETACH 0x0007

#define DF_FIRSTFUNC 0x1000

/************************************************************************/
// Error codes common to all EXOS calls

#define DF_ERROR_SUCCESS 0x00000000
#define DF_ERROR_NOTIMPL 0x00000001
#define DF_ERROR_BADPARAM 0x00000002
#define DF_ERROR_NOMEMORY 0x00000003
#define DF_ERROR_UNEXPECT 0x00000004
#define DF_ERROR_IO 0x00000005
#define DF_ERROR_NOPERM 0x00000006
#define DF_ERROR_FIRST 0x00001000
#define DF_ERROR_GENERIC 0xFFFFFFFF

/************************************************************************/
// Window styles

#define EWS_VISIBLE 0x0001
#define EWS_ALWAYS_IN_FRONT 0x0002

/************************************************************************/
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

/************************************************************************/
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

/************************************************************************/
// Values for mouse buttons

#define MB_LEFT 0x0001
#define MB_RIGHT 0x0002
#define MB_MIDDLE 0x0004

/************************************************************************/
// Socket Constants

// Socket Address Family
#define SOCKET_AF_UNSPEC    0
#define SOCKET_AF_INET      2
#define SOCKET_AF_INET6     10

// Socket Type
#define SOCKET_TYPE_STREAM     1  // TCP
#define SOCKET_TYPE_DGRAM      2  // UDP
#define SOCKET_TYPE_RAW        3  // Raw socket

// Socket Protocol
#define SOCKET_PROTOCOL_IP     0
#define SOCKET_PROTOCOL_TCP    6
#define SOCKET_PROTOCOL_UDP    17

// Socket States
#define SOCKET_STATE_CLOSED       0
#define SOCKET_STATE_CREATED      1
#define SOCKET_STATE_BOUND        2
#define SOCKET_STATE_LISTENING    3
#define SOCKET_STATE_CONNECTING   4
#define SOCKET_STATE_CONNECTED    5
#define SOCKET_STATE_CLOSING      6

// Socket Error Codes
#define SOCKET_ERROR_NONE         0
#define SOCKET_ERROR_INVALID      -1
#define SOCKET_ERROR_NOMEM        -2
#define SOCKET_ERROR_INUSE        -3
#define SOCKET_ERROR_NOTBOUND     -4
#define SOCKET_ERROR_NOTLISTENING -5
#define SOCKET_ERROR_NOTCONNECTED -6
#define SOCKET_ERROR_WOULDBLOCK   -7
#define SOCKET_ERROR_CONNREFUSED  -8
#define SOCKET_ERROR_TIMEOUT      -9
#define SOCKET_ERROR_MSGSIZE      -10

// Socket Shutdown Types
#define SOCKET_SHUTDOWN_READ      0
#define SOCKET_SHUTDOWN_WRITE     1
#define SOCKET_SHUTDOWN_BOTH      2

#ifdef __cplusplus
}
#endif

#pragma pack(pop)

#endif /* USER_H_INCLUDED */
