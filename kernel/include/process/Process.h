
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

/************************************************************************/

#include "Arch.h"
#include "Base.h"
#include "Driver.h"
#include "ID.h"
#include "List.h"
#include "Mutex.h"
#include "Security.h"
#include "System.h"
#include "User.h"
#include "UserAccount.h"
#include "UserSession.h"
#include "process/Message.h"
#include "process/Process-Arena.h"
#include "process/Schedule.h"
#include "process/Task.h"
#include "utils/RectRegion.h"

/***************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

typedef struct tag_PROCESS PROCESS, *LPPROCESS;
typedef struct tag_WINDOW WINDOW, *LPWINDOW;
typedef struct tag_DESKTOP DESKTOP, *LPDESKTOP;
typedef struct tag_FILESYSTEM FILESYSTEM, *LPFILESYSTEM;
struct tag_MEMORY_REGION_DESCRIPTOR;

/************************************************************************/
// Task status values

#define TASK_STATUS_FREE 0x00
#define TASK_STATUS_READY 0x01
#define TASK_STATUS_RUNNING 0x02
#define TASK_STATUS_WAITING 0x03
#define TASK_STATUS_SLEEPING 0x04
#define TASK_STATUS_WAITMESSAGE 0x05
#define TASK_STATUS_DEAD 0xFF

// Process status values

#define PROCESS_STATUS_ALIVE 0x00
#define PROCESS_STATUS_DEAD 0xFF

// Kernel process heap size
#ifdef __EXOS_32__
#define KERNEL_PROCESS_HEAP_SIZE N_2MB
#else
#define KERNEL_PROCESS_HEAP_SIZE N_4MB
#endif

// Task stack values

#ifdef __EXOS_32__
#define TASK_MINIMUM_TASK_STACK_SIZE_DEFAULT N_64KB
#define TASK_MINIMUM_SYSTEM_STACK_SIZE_DEFAULT N_16KB
#else
#define TASK_MINIMUM_TASK_STACK_SIZE_DEFAULT N_128KB
#define TASK_MINIMUM_SYSTEM_STACK_SIZE_DEFAULT N_32KB
#endif

UINT TaskGetMinimumTaskStackSize(void);
UINT TaskGetMinimumSystemStackSize(void);

#define TASK_MINIMUM_TASK_STACK_SIZE TaskGetMinimumTaskStackSize()
#define TASK_MINIMUM_SYSTEM_STACK_SIZE TaskGetMinimumSystemStackSize()

#define STACK_SAFETY_MARGIN 256

// Task creation flags

#define TASK_CREATE_SUSPENDED 0x00000001
#define TASK_CREATE_MAIN_KERNEL 0x00000002

// Process creation flags

#define PROCESS_CREATE_TERMINATE_CHILD_PROCESSES_ON_DEATH 0x00000001

/************************************************************************/
// Desktop modes

#define DESKTOP_MODE_CONSOLE 0x00000000
#define DESKTOP_MODE_GRAPHICS 0x00000001

/************************************************************************/
// Desktop cursor rendering path

#define DESKTOP_CURSOR_PATH_UNSET 0x00000000
#define DESKTOP_CURSOR_PATH_HARDWARE 0x00000001
#define DESKTOP_CURSOR_PATH_SOFTWARE 0x00000002

/************************************************************************/
// Desktop cursor fallback reason

#define DESKTOP_CURSOR_FALLBACK_NONE 0x00000000
#define DESKTOP_CURSOR_FALLBACK_NOT_GRAPHICS 0x00000001
#define DESKTOP_CURSOR_FALLBACK_NO_CAPABILITIES 0x00000002
#define DESKTOP_CURSOR_FALLBACK_NO_CURSOR_PLANE 0x00000003
#define DESKTOP_CURSOR_FALLBACK_SET_SHAPE_FAILED 0x00000004
#define DESKTOP_CURSOR_FALLBACK_SET_POSITION_FAILED 0x00000005
#define DESKTOP_CURSOR_FALLBACK_SET_VISIBLE_FAILED 0x00000006

/************************************************************************/
// Window status bit values

#define WINDOW_STATUS_VISIBLE 0x0001
#define WINDOW_STATUS_NEED_DRAW 0x0002
#define WINDOW_STATUS_DRAWING 0x0004

/************************************************************************/
// Other window values

#define WINDOW_DIRTY_REGION_CAPACITY 32

/************************************************************************/

struct tag_PROCESS {
    LISTNODE_FIELDS          // Standard EXOS object fields
        MUTEX Mutex;         // This structure's mutex
    MUTEX HeapMutex;         // This structure's mutex for heap allocation
    SECURITY Security;       // Security attributes
    LPDESKTOP Desktop;       // This process' desktop
    U32 Privilege;           // This process' privilege level
    U32 Status;              // (alive/dead)
    U32 Flags;               // Process creation flags
    U32 ControlFlags;        // Process control state (pause/interrupt)
    PHYSICAL PageDirectory;  // This process' page directory
    LINEAR HeapBase;
    UINT HeapSize;
    UINT MaximumAllocatedMemory;
    UINT ExitCode;  // This process' exit code
    STR FileName[MAX_PATH_NAME];
    STR CommandLine[MAX_PATH_NAME];
    STR WorkFolder[MAX_PATH_NAME];
    UINT TaskCount;                  // Number of active tasks in this process
    MESSAGEQUEUE MessageQueue;       // Process-level message queue (input, etc.)
    U64 UserID;                      // Owner user
    LPUSERSESSION Session;           // User session
    LPFILESYSTEM PackageFileSystem;  // Mounted package filesystem tied to this process
    struct tag_MEMORY_REGION_DESCRIPTOR* RegionListHead;
    struct tag_MEMORY_REGION_DESCRIPTOR* RegionListTail;
    UINT RegionCount;
    PROCESS_ADDRESS_SPACE AddressSpace;
};

typedef struct tag_PROPERTY {
    LISTNODE_FIELDS
    STR Name[32];
    U32 Value;
} PROPERTY, *LPPROPERTY;

struct tag_WINDOW {
    LISTNODE_FIELDS         // Standard EXOS object fields
        MUTEX Mutex;        // This window's mutex
    LPTASK Task;            // The task that created this window
    WINDOWFUNC Function;    // The function that manages this window
    LPWINDOW ParentWindow;  // The parent of this window
    LPLIST Children;        // The children of this window
    LPLIST Properties;      // The user-defined properties of this window
    RECT Rect;              // The rectangle of this window
    RECT ScreenRect;
    RECT DirtyRects[WINDOW_DIRTY_REGION_CAPACITY];
    RECT_REGION DirtyRegion;
    U32 WindowID;
    U32 Style;
    U32 Status;
    U32 Level;
    I32 Order;
};

typedef struct tag_DESKTOP_THEME {
    LPVOID Builtin;
    LPVOID Active;
    LPVOID Staged;
    STR ActivePath[MAX_FILE_NAME];
    STR StagedPath[MAX_FILE_NAME];
    U32 LastStatus;
    U32 LastFallbackReason;
    BOOL ActiveFromFile;
} DESKTOP_THEME, *LPDESKTOP_THEME;

typedef struct tag_MOUSE_CURSOR {
    I32 X;
    I32 Y;
    U32 Width;
    U32 Height;
    BOOL Visible;
    RECT ClipRect;
    U32 RenderPath;
    U32 FallbackReason;
    I32 PendingX;
    I32 PendingY;
    BOOL SoftwareDirty;
} MOUSE_CURSOR, *LPMOUSE_CURSOR;

struct tag_DESKTOP {
    LISTNODE_FIELDS            // Standard EXOS object fields
        MUTEX Mutex;           // This structure's mutex
    LPTASK Task;               // The task that created this desktop
    LPDRIVER Graphics;         // This desktop's graphics driver
    LPWINDOW Window;           // Window of the desktop
    LPWINDOW Capture;          // Window that captured mouse
    I32 CaptureOffsetX;        // Mouse offset X in captured window on drag start
    I32 CaptureOffsetY;        // Mouse offset Y in captured window on drag start
    MUTEX TimerMutex;          // Protect desktop timers
    LPLIST Timers;             // Per-desktop timer entries
    LPTASK TimerTask;          // Per-desktop timer worker task
    LPWINDOW Focus;            // Window that has focus
    LPPROCESS FocusedProcess;  // Process with input focus on this desktop
    U32 Mode;
    I32 Order;
    DESKTOP_THEME Theme;
    MOUSE_CURSOR Cursor;
};

/************************************************************************/
// Global objects

extern PROCESS KernelProcess;
extern WINDOW MainDesktopWindow;
extern DESKTOP MainDesktop;

/************************************************************************/
// Functions in Process.c

void InitializeKernelProcess(void);
void DumpProcess(LPPROCESS);
void KillProcess(LPPROCESS);
void DeleteProcessCommit(LPPROCESS);
void InitSecurity(LPSECURITY);
BOOL CreateProcess(LPPROCESSINFO);
UINT Spawn(LPCSTR, LPCSTR);
void SetProcessStatus(LPPROCESS Process, U32 Status);
LINEAR GetProcessHeap(LPPROCESS);

/***************************************************************************/

#pragma pack(pop)

#endif  // PROCESS_H_INCLUDED
