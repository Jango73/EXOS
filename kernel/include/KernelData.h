
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


    Kernel data definitions

\************************************************************************/

#ifndef KERNELDATA_H_INCLUDED
#define KERNELDATA_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "utils/Cache.h"
#include "utils/Database.h"
#include "utils/HandleMap.h"
#include "utils/TOML.h"
#include "FileSystem.h"
#include "Heap.h"
#include "ID.h"
#include "List.h"
#include "Memory.h"
#include "vbr-multiboot.h"
#include "CoreString.h"
#include "Text.h"
#include "User.h"
#include "UserAccount.h"
#include "SystemFS.h"
#include "DisplaySession.h"
#include "process/Process.h"
#include "process/Task.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/
// Global constants

#define OBJECT_TERMINATION_TTL_MS 60000  // 1 minute

#define RESERVED_LOW_MEMORY N_4MB
#define LOW_MEMORY_HALF (RESERVED_LOW_MEMORY / 2)
#define LOW_MEMORY_THREE_QUARTER ((RESERVED_LOW_MEMORY * 3) / 4)

/************************************************************************/
// Structure to receive CPU information

typedef struct tag_CPUINFORMATION {
    STR Name[16];
    U32 Type;
    U32 Family;
    U32 Model;
    U32 Stepping;
    U32 Features;
} CPUINFORMATION, *LPCPUINFORMATION;

/************************************************************************/
// Global Kernel Data

typedef struct tag_MULTIBOOTMEMORYENTRY {
    U64 Base;
    U64 Length;
    U32 Type;
} MULTIBOOTMEMORYENTRY, *LPMULTIBOOTMEMORYENTRY;

typedef struct tag_KERNELSTARTUPINFO {
    // WARNING: This layout is mirrored in assembly:
    // - kernel/source/arch/x86-32/asm/x86-32.inc (KernelStartupInfo)
    // - kernel/source/arch/x86-64/asm/x86-64.inc (KernelStartupInfo)
    // Any field addition/removal/reorder here must be reflected in both ASM structs.
    PHYSICAL KernelPhysicalBase;
    UINT KernelSize;
    UINT KernelReservedBytes;
    PHYSICAL StackTop;
    PHYSICAL PageDirectory;
    U32 IRQMask_21_PM;
    U32 IRQMask_A1_PM;
    U32 IRQMask_21_RM;
    U32 IRQMask_A1_RM;
    UINT MemorySize;  // Total memory size in bytes
    UINT PageCount;   // Total memory size in pages (4K)
    U32 MultibootMemoryEntryCount;
    PHYSICAL RsdpPhysical;
    MULTIBOOTMEMORYENTRY MultibootMemoryEntries[N_4KB / sizeof(MULTIBOOTMEMORYENTRY)];
    STR CommandLine[MAX_COMMAND_LINE];
} KERNELSTARTUPINFO, *LPKERNELSTARTUPINFO;

extern KERNELSTARTUPINFO KernelStartup;

typedef struct tag_FILESYSTEM FILESYSTEM, *LPFILESYSTEM;

typedef struct tag_OBJECT_TERMINATION_STATE {
    LPVOID Object;
    U64 ID;
    UINT ExitCode;
} OBJECT_TERMINATION_STATE, *LPOBJECT_TERMINATION_STATE;

typedef struct tag_KERNELDATA {
    // NOTE: This structure has no global assembly mirror.
    // Assembly code must not rely on hardcoded offsets into this struct.
    LPLIST Desktop;
    LPLIST Process;
    LPLIST Task;
    LPLIST Mutex;
    LPLIST Disk;
    LPLIST USBDevice;
    LPLIST USBInterface;
    LPLIST USBEndpoint;
    LPLIST USBStorage;
    LPLIST PCIDevice;
    LPLIST NetworkDevice;
    LPLIST Event;
    LPLIST FileSystem;
    LPLIST UnusedFileSystem;
    LPLIST File;
    LPLIST TCPConnection;
    LPLIST Socket;
    LPLIST Drivers;                 // Driver list in initialization order
    LPLIST UserSessions;            // List of active user sessions
    LPLIST UserAccount;             // List of user accounts
    DISPLAY_SESSION DisplaySession; // Active display ownership state
    LPDESKTOP FocusedDesktop;       // Desktop with input focus
    CACHE ObjectTerminationCache;   // Cache for terminated object states with TTL
    FILESYSTEM_GLOBAL_INFO FileSystemInfo;
    SYSTEMFSFILESYSTEM SystemFS;
    HANDLE_MAP HandleMap;           // Global handle to pointer mapping
    CPUINFORMATION CPU;
    LPTOML Configuration;
    UINT MinimumQuantum;            // Minimum quantum time in milliseconds (adjusted for emulation)
    UINT MaximumQuantum;            // Maximum quantum time in milliseconds (adjusted for emulation)
    UINT DeferredWorkWaitTimeoutMS; // Wait timeout for deferred work dispatcher in milliseconds
    UINT DeferredWorkPollDelayMS;   // Polling delay for deferred work dispatcher in milliseconds
    BOOL DoLogin;                   // Enable/disable login sequence (TRUE=enable, FALSE=disable)
    STR LanguageCode[8];
    STR KeyboardCode[8];
} KERNELDATA, *LPKERNELDATA;

/************************************************************************/
// Functions in KernelData.c

void SetConfiguration(LPTOML Configuration);
void SetDeferredWorkPollDelay(UINT Delay);
void SetDeferredWorkWaitTimeout(UINT Timeout);
void SetDoLogin(BOOL DoLogin);
void SetFocusedDesktop(LPDESKTOP Desktop);
void SetFocusedProcess(LPPROCESS Process);
void SetKeyboardCode(LPCSTR KeyboardCode);
void SetLanguageCode(LPCSTR LanguageCode);
void SetMaximumQuantum(UINT MaximumQuantum);
void SetMinimumQuantum(UINT MinimumQuantum);
void SetUserAccountList(LPLIST List);
void SetUserSessionList(LPLIST List);

BOOL GetCPUInformation(LPCPUINFORMATION);
LPTOML GetConfiguration(void);
UINT GetDeferredWorkPollDelay(void);
UINT GetDeferredWorkWaitTimeout(void);
LPLIST GetDesktopList(void);
LPLIST GetDiskList(void);
BOOL GetDoLogin(void);
LPDRIVER GetDefaultFileSystemDriver(void);
LPDISPLAY_SESSION GetDisplaySession(void);
LPLIST GetDriverList(void);
LPLIST GetEventList(void);
LPLIST GetFileList(void);
FILESYSTEM_GLOBAL_INFO* GetFileSystemGlobalInfo(void);
LPLIST GetFileSystemList(void);
LPLIST GetUnusedFileSystemList(void);
LPDESKTOP GetFocusedDesktop(void);
LPPROCESS GetFocusedProcess(void);
LPDRIVER GetGraphicsDriver(void);
LPHANDLE_MAP GetHandleMap(void);
LPCPUINFORMATION GetKernelCPUInfo(void);
LPCSTR GetKeyboardCode(void);
LPCSTR GetLanguageCode(void);
UINT GetMaximumQuantum(void);
UINT GetMinimumQuantum(void);
LPDRIVER GetMouseDriver(void);
LPLIST GetMutexList(void);
LPLIST GetNetworkDeviceList(void);
LPCACHE GetObjectTerminationCache(void);
LPLIST GetPCIDeviceList(void);
LPLIST GetProcessList(void);
LPLIST GetSocketList(void);
LPSYSTEMFSFILESYSTEM GetSystemFSData(void);
LPLIST GetTaskList(void);
LPLIST GetTCPConnectionList(void);
LPLIST GetUsbDeviceList(void);
LPLIST GetUsbInterfaceList(void);
LPLIST GetUsbEndpointList(void);
LPLIST GetUsbStorageList(void);
LPLIST GetUserAccountList(void);
LPLIST GetUserSessionList(void);
void InitializeDriverList(void);

/************************************************************************/

#pragma pack(pop)

#endif  // KERNELDATA_H_INCLUDED
