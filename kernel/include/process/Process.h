
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
#include "Memory.h"
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
typedef struct tag_WINDOW_CLASS WINDOW_CLASS, *LPWINDOW_CLASS;
typedef struct tag_DESKTOP DESKTOP, *LPDESKTOP;
typedef struct tag_FILESYSTEM FILESYSTEM, *LPFILESYSTEM;

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
#define WINDOW_STATUS_HAS_WORK_RECT 0x0008
#define WINDOW_STATUS_BYPASS_PARENT_WORK_RECT 0x0010
#define WINDOW_STATUS_CONTENT_TRANSPARENT 0x0020

/************************************************************************/
// Other window values

#define WINDOW_DIRTY_REGION_CAPACITY 32
#define WINDOW_DRAW_CONTEXT_ACTIVE 0x00000001
#define WINDOW_DRAW_CONTEXT_CLIENT_COORDINATES 0x00000002
#define WINDOW_CONTENT_TRANSPARENCY_HINT_AUTO 0x00000000
#define WINDOW_CONTENT_TRANSPARENCY_HINT_OPAQUE 0x00000001
#define WINDOW_CONTENT_TRANSPARENCY_HINT_TRANSPARENT 0x00000002

/************************************************************************/

struct tag_PROCESS {
    LISTNODE_FIELDS                                         // Standard EXOS object fields
        MUTEX Mutex;                                        // This structure's mutex
    MUTEX HeapMutex;                                        // This structure's mutex for heap allocation
    SECURITY Security;                                      // Security attributes
    U32 Privilege;                                          // This process' privilege level
    U32 Status;                                             // (alive/dead)
    U32 Flags;                                              // Process creation flags
    U32 ControlFlags;                                       // Process control state (pause/interrupt)
    PHYSICAL PageDirectory;
    LINEAR HeapBase;
    UINT HeapSize;
    UINT MaximumAllocatedMemory;
    UINT ExitCode;                                          // Exit code
    STR FileName[MAX_PATH_NAME];
    STR CommandLine[MAX_PATH_NAME];
    STR WorkFolder[MAX_PATH_NAME];
    UINT TaskCount;                                         // Number of active tasks in this process
    MESSAGEQUEUE MessageQueue;                              // Process-level message queue (input, etc.)
    U64 UserID;                                             // Owner user
    LPDESKTOP Desktop;                                      // This process' desktop
    LPUSER_SESSION Session;                                 // User session
    LPFILESYSTEM PackageFileSystem;                         // Mounted package filesystem tied to this process
    MEMORY_REGION_LIST MemoryRegionList;
    PROCESS_ADDRESS_SPACE AddressSpace;
};

typedef struct tag_PROPERTY {
    LISTNODE_FIELDS
    STR Name[32];
    UINT Value;
} PROPERTY, *LPPROPERTY;

struct tag_WINDOW {
    LISTNODE_FIELDS                                 // Standard EXOS object fields
        MUTEX Mutex;                                // This window's mutex
    LPTASK Task;                                    // The task that created this window
    WINDOWFUNC Function;                            // The function that manages this window
    LPWINDOW ParentWindow;                          // The parent of this window
    LPLIST Children;                                // The children of this window
    LPLIST Properties;                              // The user-defined properties of this window
    LPWINDOW_CLASS Class;                           // Window class metadata
    LPVOID ClassData;                               // Window class private data
    U32 DrawContextFlags;
    U32 WindowID;
    U32 Style;
    U32 Status;
    U32 ContentTransparencyHint;
    U32 Level;
    I32 Order;
    STR Caption[MAX_WINDOW_CAPTION];
    POINT DrawOrigin;
    RECT Rect;                                      // The rectangle of this window
    RECT ScreenRect;
    RECT WorkRect;
    RECT DirtyRects[WINDOW_DIRTY_REGION_CAPACITY];
    RECT_REGION DirtyRegion;
    RECT DrawSurfaceRect;
    RECT DrawClipRect;
};

typedef struct tag_WINDOW_CLASS {
    LISTNODE_FIELDS                 // Standard EXOS object fields
        STR Name[64];               // Unique class name
    LPWINDOW_CLASS BaseClass;       // Base class in inheritance chain
    WINDOWFUNC Function;            // Class window procedure
    U32 ClassID;                    // Class identifier for handle-based lookup
    U32 ClassDataSize;              // Optional class-private data size
} WINDOW_CLASS, *LPWINDOW_CLASS;

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

typedef struct tag_DESKTOP_DISPLAY_SELECTION {
    STR BackendAlias[MAX_NAME];
    GRAPHICS_MODE_INFO ModeInfo;
    BOOL IsAssigned;
} DESKTOP_DISPLAY_SELECTION, *LPDESKTOP_DISPLAY_SELECTION;

typedef struct tag_MOUSE_CURSOR {
    I32 X;                // Cursor X position in screen coordinates
    I32 Y;                // Cursor Y position in screen coordinates
    U32 Width;            // Cursor width in pixels
    U32 Height;           // Cursor height in pixels
    BOOL Visible;         // Cursor visibility state
    RECT ClipRect;        // Cursor clipping rectangle in screen coordinates
    U32 RenderPath;       // Active cursor rendering path identifier
    U32 FallbackReason;   // Last fallback reason for cursor rendering path
    I32 PendingX;         // Pending cursor X target for deferred apply
    I32 PendingY;         // Pending cursor Y target for deferred apply
    BOOL SoftwareDirty;   // Software cursor overlay requires redraw
} MOUSE_CURSOR, *LPMOUSE_CURSOR;

struct tag_DESKTOP {
    LISTNODE_FIELDS                 // Standard EXOS object fields
        MUTEX Mutex;                // This structure's mutex
    LPTASK Task;                    // The task that created this desktop
    LPDRIVER Graphics;              // This desktop's graphics driver
    LPWINDOW Window;                // Window of the desktop
    LPWINDOW Capture;               // Window that captured mouse
    LPWINDOW LastMouseMoveTarget;   // Window that last received mouse move dispatch
    I32 CaptureOffsetX;             // Mouse offset X in captured window on drag start
    I32 CaptureOffsetY;             // Mouse offset Y in captured window on drag start
    MUTEX TimerMutex;               // Protect desktop timers
    LPLIST Timers;                  // Per-desktop timer entries
    LPTASK TimerTask;               // Per-desktop timer worker task
    LPWINDOW Focus;                 // Window that has focus
    U32 Mode;                       // Active desktop display mode
    I32 Order;                      // Desktop ordering key among active desktops
    U32 PendingComponents;          // Pending desktop-owned component injection flags
    MOUSE_CURSOR Cursor;            // Desktop cursor runtime state
    DESKTOP_DISPLAY_SELECTION DisplaySelection;
};

/************************************************************************/
// Global objects

extern PROCESS KernelProcess;

/************************************************************************/
// Functions in Process.c

void InitializeKernelProcess(void);
void DumpProcess(LPPROCESS);
void KillProcess(LPPROCESS);
void DeleteProcessCommit(LPPROCESS);
void InitSecurity(LPSECURITY);
BOOL CreateProcess(LPPROCESS_INFO);
UINT Spawn(LPCSTR, LPCSTR);
void SetProcessStatus(LPPROCESS Process, U32 Status);
LINEAR GetProcessHeap(LPPROCESS);
LPMEMORY_REGION_LIST GetProcessMemoryRegionList(LPPROCESS Process);

/***************************************************************************/

#pragma pack(pop)

#endif  // PROCESS_H_INCLUDED
