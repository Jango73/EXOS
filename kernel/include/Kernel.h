
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


    Kernel definitions

\************************************************************************/

#ifndef KERNEL_H_INCLUDED
#define KERNEL_H_INCLUDED

/***************************************************************************/

// Privilege levels (rings)
#define PRIVILEGE_KERNEL 0x00
#define PRIVILEGE_DRIVERS 0x01
#define PRIVILEGE_ROUTINES 0x02
#define PRIVILEGE_USER 0x03

/***************************************************************************/

#include "Base.h"
#include "utils/Cache.h"
#include "utils/Database.h"
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

/***************************************************************************/

#pragma pack(push, 1)

struct tag_PROCESS;
struct tag_SEGMENT_DESCRIPTOR;
struct tag_TSS_DESCRIPTOR;

/***************************************************************************/
// Structure to receive CPU information

typedef struct tag_CPUINFORMATION {
    STR Name[16];
    U32 Type;
    U32 Family;
    U32 Model;
    U32 Stepping;
    U32 Features;
} CPUINFORMATION, *LPCPUINFORMATION;

/***************************************************************************/
// EXOS system calls

#define EXOS_USER_CALL 0x70
#define EXOS_DRIVER_CALL 0x71

typedef UINT (*SYSCALLFUNC)(UINT);

typedef struct tag_SYSCALLENTRY {
    SYSCALLFUNC Function;
    U32 Privilege;
} SYSCALLENTRY, *LPSYSCALLENTRY;

/***************************************************************************/
// Global Kernel Data

#define OBJECT_TERMINATION_TTL_MS 60000  // 1 minute

#define RESERVED_LOW_MEMORY N_4MB
#define LOW_MEMORY_HALF (RESERVED_LOW_MEMORY / 2)
#define LOW_MEMORY_THREE_QUARTER ((RESERVED_LOW_MEMORY * 3) / 4)

typedef struct tag_E820ENTRY {
    U64 Base;
    U64 Size;
    U32 Type;
    U32 Attributes;
} E820ENTRY, *LPE820ENTRY;

typedef struct tag_KERNELSTARTUPINFO {
    PHYSICAL KernelPhysicalBase;
    UINT KernelSize;
    PHYSICAL StackTop;
    PHYSICAL PageDirectory;
    U32 IRQMask_21_PM;
    U32 IRQMask_A1_PM;
    U32 IRQMask_21_RM;
    U32 IRQMask_A1_RM;
    UINT MemorySize;  // Total memory size in bytes
    UINT PageCount;   // Total memory size in pages (4K)
    U32 E820_Count;  // BIOS E820 function entries
    E820ENTRY E820[N_4KB / sizeof(E820ENTRY)];
    STR CommandLine[MAX_COMMAND_LINE];
} KERNELSTARTUPINFO, *LPKERNELSTARTUPINFO;

extern KERNELSTARTUPINFO KernelStartup;

typedef struct tag_FILESYSTEM FILESYSTEM, *LPFILESYSTEM;

typedef struct {
    LPVOID Object;
    U64 ID;
    UINT ExitCode;
} OBJECT_TERMINATION_STATE, *LPOBJECT_TERMINATION_STATE;

typedef struct tag_KERNELDATA {
    LPLIST Desktop;
    LPLIST Process;
    LPLIST Task;
    LPLIST Mutex;
    LPLIST Disk;
    LPLIST PCIDevice;
    LPLIST NetworkDevice;
    LPLIST FileSystem;
    LPLIST File;
    LPLIST TCPConnection;
    LPLIST Socket;
    SYSTEMFSFILESYSTEM SystemFS;
    FILESYSTEM_GLOBAL_INFO FileSystemInfo;
    LPTOML Configuration;
    STR LanguageCode[8];
    STR KeyboardCode[8];
    CPUINFORMATION CPU;
    U32 MinimumQuantum;          // Minimum quantum time in milliseconds (adjusted for emulation)
    U32 MaximumQuantum;          // Maximum quantum time in milliseconds (adjusted for emulation)
    BOOL DoLogin;                // Enable/disable login sequence (TRUE=enable, FALSE=disable)
    LPLIST UserSessions;         // List of active user sessions
    LPLIST UserAccount;          // List of user accounts
    CACHE ObjectTerminationCache;  // Cache for terminated object states with TTL
    LPPAGEBITMAP PPB;            // Physical page bitmap shared across architectures
} KERNELDATA, *LPKERNELDATA;

extern KERNELDATA Kernel;


/***************************************************************************/
// Functions in Kernel.c

BOOL GetCPUInformation(LPCPUINFORMATION);
void InitializeQuantumTime(void);
U32 ClockTestTask(LPVOID);
U32 GetPhysicalMemoryUsed(void);
void TestProcess(void);
void InitializeKernel(void);
void StoreObjectTerminationState(LPVOID Object, UINT ExitCode);

void KernelObjectDestructor(LPVOID);
LPVOID CreateKernelObject(UINT Size, U32 ObjectTypeID);
void ReleaseKernelObject(LPVOID Object);
void ReleaseProcessKernelObjects(struct tag_PROCESS* Process);

// Functions in MemoryEditor.c

void PrintMemory(U32, U32);
void MemoryEditor(U32);

/***************************************************************************/
// Functions in Edit.c

U32 Edit(U32, LPCSTR*, BOOL);

/***************************************************************************/

#pragma pack(pop)

#endif  // KERNEL_H_INCLUDED
