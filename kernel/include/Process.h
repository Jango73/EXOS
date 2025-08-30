
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


    Process

\************************************************************************/
#ifndef PROCESS_H_INCLUDED
#define PROCESS_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Driver.h"
#include "I386.h"
#include "ID.h"
#include "List.h"
#include "Mutex.h"
#include "Schedule.h"
#include "System.h"
#include "Task.h"
#include "User.h"

/***************************************************************************/

typedef struct tag_PROCESS PROCESS, *LPPROCESS;
typedef struct tag_MESSAGE MESSAGE, *LPMESSAGE;
typedef struct tag_WINDOW WINDOW, *LPWINDOW;
typedef struct tag_DESKTOP DESKTOP, *LPDESKTOP;

/***************************************************************************/
// The security structure

typedef struct tag_SECURITY {
    LISTNODE_FIELDS   // Standard EXOS object fields
        U32 Group;    // Group that owns the item
    U32 User;         // User that owns the item
    U32 Permissions;  // Permissions accorded to the item
} SECURITY, *LPSECURITY;

#define PERMISSION_NONE 0x00000000
#define PERMISSION_EXECUTE 0x00000001
#define PERMISSION_READ 0x00000002
#define PERMISSION_WRITE 0x00000004

// Macro to initialize a security

#define EMPTY_SECURITY {ID_SECURITY, 1, NULL, NULL, 0, 0, PERMISSION_NONE}

/***************************************************************************\

  The process structure

  For kernel process, the heap base should be somewhere above 0xC0000000.
  For a user process, it should be between 0x00400000 and 0x40000000.

\***************************************************************************/

struct tag_PROCESS {
    LISTNODE_FIELDS          // Standard EXOS object fields
        MUTEX Mutex;         // This structure's mutex
    MUTEX HeapMutex;         // This structure's mutex for heap allocation
    SECURITY Security;       // This process' security attributes
    LPDESKTOP Desktop;       // This process' desktop
    LPPROCESS Parent;        // Parent process of this process
    U32 Privilege;           // This process' privilege level
    PHYSICAL PageDirectory;  // This process' page directory
    LINEAR HeapBase;
    U32 HeapSize;
    STR FileName[MAX_PATH_NAME];
    STR CommandLine[MAX_PATH_NAME];
    LPLIST Objects;  // Objects owned by this process
};

/***************************************************************************/

// The Message structure

struct tag_MESSAGE {
    LISTNODE_FIELDS
    HANDLE Target;
    U32 Message;
    SYSTEMTIME Time;
    U32 Param1;
    U32 Param2;
};

/***************************************************************************/

// Task status values

#define TASK_STATUS_FREE 0x00
#define TASK_STATUS_RUNNING 0x01
#define TASK_STATUS_WAITING 0x02
#define TASK_STATUS_SLEEPING 0x03
#define TASK_STATUS_WAITMESSAGE 0x04
#define TASK_STATUS_DEAD 0xFF

// Miscellaneous task values

#define TASK_MINIMUM_STACK_SIZE N_32KB
#define TASK_SYSTEM_STACK_SIZE N_4KB

// Task creation flags

#define TASK_CREATE_SUSPENDED 0x00000001
#define TASK_CREATE_MAIN 0x00000002

/***************************************************************************/

// The window structure

struct tag_WINDOW {
    LISTNODE_FIELDS       // Standard EXOS object fields
        MUTEX Mutex;      // This window's mutex
    LPTASK Task;          // The task that created this window
    WINDOWFUNC Function;  // The function that manages this window
    LPWINDOW Parent;      // The parent of this window
    LPLIST Children;      // The children of this window
    LPLIST Properties;    // The user-defined properties of this window
    RECT Rect;            // The rectangle of this window
    RECT ScreenRect;
    RECT InvalidRect;
    U32 WindowID;
    U32 Style;
    U32 Status;
    U32 Level;
    I32 Order;
};

// Status bit values

#define WINDOW_STATUS_VISIBLE 0x0001
#define WINDOW_STATUS_NEED_DRAW 0x0002

/***************************************************************************/
// The property structure

typedef struct tag_PROPERTY {
    LISTNODE_FIELDS
    STR Name[32];
    U32 Value;
} PROPERTY, *LPPROPERTY;

/***************************************************************************/
// The desktop structure

struct tag_DESKTOP {
    LISTNODE_FIELDS     // Standard EXOS object fields
        MUTEX Mutex;    // This structure's mutex
    LPTASK Task;        // The task that created this desktop
    LPDRIVER Graphics;  // This desktop's graphics driver
    LPWINDOW Window;    // Window of the desktop
    LPWINDOW Capture;   // Window that captured mouse
    LPWINDOW Focus;     // Window that has focus
    I32 Order;
};

/***************************************************************************/

#define MUTEX_KERNEL (&KernelMutex)
#define MUTEX_LOG (&LogMutex)
#define MUTEX_MEMORY (&MemoryMutex)
#define MUTEX_SCHEDULE (&ScheduleMutex)
#define MUTEX_DESKTOP (&DesktopMutex)
#define MUTEX_PROCESS (&ProcessMutex)
#define MUTEX_TASK (&TaskMutex)
#define MUTEX_FILESYSTEM (&FileSystemMutex)
#define MUTEX_FILE (&FileMutex)
#define MUTEX_CONSOLE (&ConsoleMutex)

/***************************************************************************/

extern PROCESS KernelProcess;
extern WINDOW MainDesktopWindow;
extern DESKTOP MainDesktop;
extern MUTEX KernelMutex;
extern MUTEX LogMutex;
extern MUTEX MemoryMutex;
extern MUTEX ScheduleMutex;
extern MUTEX DesktopMutex;
extern MUTEX ProcessMutex;
extern MUTEX TaskMutex;
extern MUTEX FileSystemMutex;
extern MUTEX FileMutex;
extern MUTEX ConsoleMutex;

/***************************************************************************/
// Functions in Process.c

void InitializeKernelProcess(void);
LINEAR GetProcessHeap(LPPROCESS);
void DumpProcess(LPPROCESS);
void InitSecurity(LPSECURITY);
BOOL CreateProcess(LPPROCESSINFO);
BOOL Spawn(LPCSTR, LPCSTR);

/***************************************************************************/
// Functions in Task.c

BOOL InitKernelTask(void);
LPTASK CreateTask(LPPROCESS, LPTASKINFO);
BOOL KillTask(LPTASK);
U32 SetTaskPriority(LPTASK, U32);
void Sleep(U32);
BOOL PostMessage(HANDLE, U32, U32, U32);
U32 SendMessage(HANDLE, U32, U32, U32);
BOOL GetMessage(LPMESSAGEINFO);
BOOL DispatchMessage(LPMESSAGEINFO);
void DumpTask(LPTASK);

/***************************************************************************/
// Functions in Mutex.c

void InitMutex(LPMUTEX);
LPMUTEX CreateMutex(void);
BOOL DeleteMutex(LPMUTEX);
U32 LockMutex(LPMUTEX, U32);
BOOL UnlockMutex(LPMUTEX);

/***************************************************************************/
// Functions in Desktop.c

LPDESKTOP CreateDesktop(void);
void DeleteDesktop(LPDESKTOP);
BOOL ShowDesktop(LPDESKTOP);
LPWINDOW CreateWindow(LPWINDOWINFO);
void DeleteWindow(LPWINDOW);
LPWINDOW FindWindow(LPWINDOW, LPWINDOW);
LPDESKTOP GetWindowDesktop(LPWINDOW);
BOOL InvalidateWindowRect(HANDLE, LPRECT);
BOOL ShowWindow(HANDLE, BOOL);
BOOL GetWindowRect(HANDLE, LPRECT);
BOOL MoveWindow(HANDLE, LPPOINT);
BOOL SizeWindow(HANDLE, LPPOINT);
HANDLE GetWindowParent(HANDLE);
U32 SetWindowProp(HANDLE, LPCSTR, U32);
U32 GetWindowProp(HANDLE, LPCSTR);
HANDLE GetWindowGC(HANDLE);
HANDLE BeginWindowDraw(HANDLE);
BOOL EndWindowDraw(HANDLE);
HANDLE GetSystemBrush(U32);
HANDLE GetSystemPen(U32);
HANDLE SelectBrush(HANDLE, HANDLE);
HANDLE SelectPen(HANDLE, HANDLE);
HANDLE CreateBrush(LPBRUSHINFO);
HANDLE CreatePen(LPPENINFO);
BOOL SetPixel(LPPIXELINFO);
BOOL GetPixel(LPPIXELINFO);
BOOL Line(LPLINEINFO);
BOOL Rectangle(LPRECTINFO);
U32 DefWindowFunc(HANDLE, U32, U32, U32);

/***************************************************************************/

#endif
