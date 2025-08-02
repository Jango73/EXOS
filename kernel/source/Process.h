
// Process.h

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\***************************************************************************/

#ifndef PROCESS_H_INCLUDED
#define PROCESS_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Driver.h"
#include "I386.h"
#include "ID.h"
#include "List.h"
#include "System.h"
#include "User.h"

/***************************************************************************/

typedef struct tag_PROCESS PROCESS, *LPPROCESS;
typedef struct tag_TASK TASK, *LPTASK;
typedef struct tag_MESSAGE MESSAGE, *LPMESSAGE;
typedef struct tag_WINDOW WINDOW, *LPWINDOW;
typedef struct tag_DESKTOP DESKTOP, *LPDESKTOP;
typedef struct tag_MUTEX MUTEX, *LPMUTEX;

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

#define EMPTY_SECURITY \
    { ID_SECURITY, 1, NULL, NULL, 0, 0, PERMISSION_NONE }

/***************************************************************************/
// The mutex structure

struct tag_MUTEX {
    LISTNODE_FIELDS         // Standard EXOS object fields
        LPPROCESS Process;  // Process that has locked this sem.
    LPTASK Task;            // Task that has locked this sem.
    U32 Lock;               // Lock count of this sem.
};

// Macro to initialize a mutex

#define EMPTY_MUTEX \
    { ID_MUTEX, 1, NULL, NULL, NULL, NULL, 0 }

/***************************************************************************\

  The process structure

  For kernel process, the heap base should be somewhere above 0xC0000000.
  For a user process, it should be between 0x00400000 and 0x40000000.

\***************************************************************************/

struct tag_PROCESS {
    LISTNODE_FIELDS           // Standard EXOS object fields
        MUTEX Mutex;  // This structure's mutex
    MUTEX HeapMutex;  // This structure's mutex for heap allocation
    SECURITY Security;        // This process' security attributes
    LPDESKTOP Desktop;        // This process' desktop
    LPPROCESS Parent;         // Parent process of this process
    U32 Privilege;            // This process' privilege level
    PHYSICAL PageDirectory;   // This process' page directory
    LINEAR HeapBase;
    U32 HeapSize;
    STR FileName[MAX_PATH_NAME];
    STR CommandLine[MAX_PATH_NAME];
    LPLIST Objects;           // Objects owned by this process
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

// The Task structure

struct tag_TASK {
    LISTNODE_FIELDS           // Standard EXOS object fields
        MUTEX Mutex;  // This structure's mutex
    LPPROCESS Process;        // Process that owns this task
    U32 Status;               // Current status of this task
    U32 Priority;             // Current priority of this task
    TASKFUNC Function;        // Start address of this task
    LPVOID Parameter;         // Parameter passed to the function
    U32 ReturnValue;
    U32 Table;         // Index in the TSS tables
    U32 Selector;      // GDT selector for this task
    LINEAR StackBase;  // This task's stack in the heap
    U32 StackSize;     // This task's stack size
    LINEAR SysStackBase;
    U32 SysStackSize;
    U32 Time;  // Time allocated to this task
    U32 WakeUpTime;
    MUTEX MessageMutex;  // Mutex to access message queue
    LPLIST Message;              // This task's message queue
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

/***************************************************************************/

// The window structure

struct tag_WINDOW {
    LISTNODE_FIELDS           // Standard EXOS object fields
        MUTEX Mutex;  // This window's mutex
    LPTASK Task;              // The task that created this window
    WINDOWFUNC Function;      // The function that manages this window
    LPWINDOW Parent;          // The parent of this window
    LPLIST Children;          // The children of this window
    LPLIST Properties;        // The user-defined properties of this window
    RECT Rect;                // The rectangle of this window
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
    LISTNODE_FIELDS           // Standard EXOS object fields
        MUTEX Mutex;  // This structure's mutex
    LPTASK Task;              // The task that created this desktop
    LPDRIVER Graphics;        // This desktop's graphics driver
    LPWINDOW Window;          // Window of the desktop
    LPWINDOW Capture;         // Window that captured mouse
    LPWINDOW Focus;           // Window that has focus
    I32 Order;
};

/***************************************************************************/

#define MUTEX_KERNEL (&KernelMutex)
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
extern TASK KernelTask;
extern DESKTOP MainDesktop;
extern MUTEX KernelMutex;
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

void InitializeKernelHeap();
LINEAR GetProcessHeap(LPPROCESS);
void DumpProcess(LPPROCESS);
void InitSecurity(LPSECURITY);
BOOL CreateProcess(LPPROCESSINFO);

/***************************************************************************/
// Functions in Task.c

BOOL InitKernelTask();
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
LPMUTEX CreateMutex();
BOOL DeleteMutex(LPMUTEX);
U32 LockMutex(LPMUTEX, U32);
BOOL UnlockMutex(LPMUTEX);

/***************************************************************************/
// Functions in Desktop.c

LPDESKTOP CreateDesktop();
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
// Functions in Schedule.c

void UpdateScheduler();
BOOL AddTaskToQueue(LPTASK);
BOOL RemoveTaskFromQueue(LPTASK);
void Scheduler();
LPTASK GetCurrentTask();
LPPROCESS GetCurrentProcess();
BOOL FreezeScheduler();
BOOL UnfreezeScheduler();

/***************************************************************************/
// EXOS Executable chunk identifiers

#define EXOS_SIGNATURE (*((const U32*)"EXOS"))

#define EXOS_CHUNK_NONE (*((const U32*)"xxxx"))
#define EXOS_CHUNK_INIT (*((const U32*)"INIT"))
#define EXOS_CHUNK_FIXUP (*((const U32*)"FXUP"))
#define EXOS_CHUNK_CODE (*((const U32*)"CODE"))
#define EXOS_CHUNK_DATA (*((const U32*)"DATA"))
#define EXOS_CHUNK_STACK (*((const U32*)"STAK"))
#define EXOS_CHUNK_EXPORT (*((const U32*)"EXPT"))
#define EXOS_CHUNK_IMPORT (*((const U32*)"IMPT"))
#define EXOS_CHUNK_TIMESTAMP (*((const U32*)"TIME"))
#define EXOS_CHUNK_SECURITY (*((const U32*)"SECU"))
#define EXOS_CHUNK_COMMENT (*((const U32*)"NOTE"))
#define EXOS_CHUNK_RESOURCE (*((const U32*)"RSRC"))
#define EXOS_CHUNK_VERSION (*((const U32*)"VERS"))
#define EXOS_CHUNK_MENU (*((const U32*)"MENU"))
#define EXOS_CHUNK_DIALOG (*((const U32*)"DLOG"))
#define EXOS_CHUNK_ICON (*((const U32*)"ICON"))
#define EXOS_CHUNK_BITMAP (*((const U32*)"BTMP"))
#define EXOS_CHUNK_WAVE (*((const U32*)"WAVE"))
#define EXOS_CHUNK_DEBUG (*((const U32*)"DBUG"))
#define EXOS_CHUNK_USER (*((const U32*)"USER"))

/***************************************************************************/

#define EXOS_TYPE_NONE 0x00000000
#define EXOS_TYPE_EXECUTABLE 0x00000001
#define EXOS_TYPE_LIBRARY 0x00000002

#define EXOS_BYTEORDER_LITTLE_ENDIAN 0x00000000
#define EXOS_BYTEORDER_BIG_ENDIAN 0xFFFFFFFF

#define EXOS_FIXUP_SOURCE_CODE 0x00000001
#define EXOS_FIXUP_SOURCE_DATA 0x00000002
#define EXOS_FIXUP_SOURCE_STACK 0x00000004

#define EXOS_FIXUP_DEST_CODE 0x00000010
#define EXOS_FIXUP_DEST_DATA 0x00000020
#define EXOS_FIXUP_DEST_STACK 0x00000040

/***************************************************************************/

typedef struct tag_EXOSHEADER {
    U32 Signature;
    U32 Type;          // Executable, library
    U32 VersionMajor;  // High version number
    U32 VersionMinor;  // Low version number
    U32 ByteOrder;     // Byte ordering
    U32 Machine;       // Target processor
    U32 Reserved1;
    U32 Reserved2;
    U32 Reserved3;
    U32 Reserved4;
} EXOSHEADER, *LPEXOSHEADER;

/***************************************************************************/

typedef struct tag_EXOSCHUNK {
    U32 ID;
    U32 Size;
} EXOSCHUNK, *LPEXOSCHUNK;

/***************************************************************************/

typedef struct tag_EXOSCHUNK_INIT {
    U32 EntryPoint;
    U32 CodeBase;
    U32 CodeSize;
    U32 DataBase;
    U32 DataSize;
    U32 StackMinimum;
    U32 StackRequested;
    U32 HeapMinimum;
    U32 HeapRequested;
    U32 Reserved1;
    U32 Reserved2;
    U32 Reserved3;
    U32 Reserved4;
    U32 Reserved5;
} EXOSCHUNK_INIT, *LPEXOSCHUNK_INIT;

/***************************************************************************/

typedef struct tag_EXOSCHUNK_FIXUP {
    U32 Section;
    U32 Address;
} EXOSCHUNK_FIXUP, *LPEXOSCHUNK_FIXUP;

/***************************************************************************/

typedef struct tag_EXECUTABLEINFO {
    U32 EntryPoint;
    U32 CodeBase;
    U32 CodeSize;
    U32 DataBase;
    U32 DataSize;
    U32 StackMinimum;
    U32 StackRequested;
    U32 HeapMinimum;
    U32 HeapRequested;
} EXECUTABLEINFO, *LPEXECUTABLEINFO;

/***************************************************************************/

#endif
