
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

#include "Base.h"
#include "Database.h"
#include "Heap.h"
#include "I386.h"
#include "ID.h"
#include "List.h"
#include "Memory.h"
#include "Multiboot.h"
#include "Process.h"
#include "String.h"
#include "TOML.h"
#include "Cache.h"
#include "Text.h"
#include "User.h"
#include "UserAccount.h"
#include "SystemFS.h"

/***************************************************************************/

#pragma pack(push, 1)

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
// Selectors

#define SELECTOR_GLOBAL 0x00
#define SELECTOR_LOCAL 0x04

#define SELECTOR_NULL 0x00
#define SELECTOR_KERNEL_CODE (0x08 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_KERNEL_DATA (0x10 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_USER_CODE (0x18 | SELECTOR_GLOBAL | PRIVILEGE_USER)
#define SELECTOR_USER_DATA (0x20 | SELECTOR_GLOBAL | PRIVILEGE_USER)
#define SELECTOR_REAL_CODE (0x28 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_REAL_DATA (0x30 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)

#define PAGE_PRIVILEGE(adr) ((adr >= VMA_USER && adr < VMA_KERNEL) ? PAGE_PRIVILEGE_USER : PAGE_PRIVILEGE_KERNEL)

/***************************************************************************/

#define DESCRIPTOR_SIZE 10
#define GDT_NUM_DESCRIPTORS (GDT_SIZE / DESCRIPTOR_SIZE)
#define GDT_NUM_BASE_DESCRIPTORS 8
#define GDT_TSS_INDEX GDT_NUM_BASE_DESCRIPTORS
#define SELECTOR_TSS MAKE_GDT_SELECTOR(GDT_TSS_INDEX, 0)

#define GDT_NUM_TASKS (GDT_NUM_DESCRIPTORS - GDT_NUM_BASE_DESCRIPTORS)
#define NUM_TASKS GDT_NUM_TASKS

#define IDT_SIZE N_4KB
#define GDT_SIZE N_8KB

/***************************************************************************/

#define NUM_INTERRUPTS 48

/***************************************************************************/
// EXOS system calls

#define EXOS_USER_CALL 0x70
#define EXOS_DRIVER_CALL 0x71

typedef U32 (*SYSCALLFUNC)(U32);

typedef struct tag_SYSCALLENTRY {
    SYSCALLFUNC Function;
    U32 Privilege;
} SYSCALLENTRY, *LPSYSCALLENTRY;

/***************************************************************************/
// Global Kernel Data

#define OBJECT_TERMINATION_TTL_MS 60000  // 1 minute

#define RESERVED_LOW_MEMORY N_4MB
#define LOW_MEMORY_HALF (RESERVED_LOW_MEMORY / 2)

#define PATH_USERS_DATABASE TEXT("/system/data/users.database")

typedef struct tag_E820ENTRY {
    U64 Base;
    U64 Size;
    U32 Type;
    U32 Attributes;
} E820ENTRY, *LPE820ENTRY;

typedef struct tag_KERNELSTARTUPINFO {
    PHYSICAL StubAddress;
    PHYSICAL StackTop;
    PHYSICAL PageDirectory;
    U32 IRQMask_21_PM;
    U32 IRQMask_A1_PM;
    U32 IRQMask_21_RM;
    U32 IRQMask_A1_RM;
    U32 MemorySize;  // Total memory size in bytes
    U32 PageCount;   // Total memory size in pages (4K)
    U32 E820_Count;  // BIOS E820 function entries
    E820ENTRY E820[N_4KB / sizeof(E820ENTRY)];
    struct multiboot_info* MultibootInfo;  // Pointer to Multiboot information structure
} KERNELSTARTUPINFO, *LPKERNELSTARTUPINFO;

extern KERNELSTARTUPINFO KernelStartup;

typedef struct tag_KERNELDATA_I386 {
    LPGATEDESCRIPTOR IDT;
    LPSEGMENTDESCRIPTOR GDT;
    LPTASKSTATESEGMENT TSS;
    LPPAGEBITMAP PPB;
} KERNELDATA_I386, *LPKERNELDATA_I386;

extern KERNELDATA_I386 Kernel_i386;

typedef struct tag_FILESYSTEM FILESYSTEM, *LPFILESYSTEM;

typedef struct {
    LPVOID Object;
    U32 ExitCode;
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
    LPTOML Configuration;
    STR LanguageCode[8];
    STR KeyboardCode[8];
    CPUINFORMATION CPU;
    U32 MinimumQuantum;          // Minimum quantum time in milliseconds (adjusted for emulation)
    U32 MaximumQuantum;          // Maximum quantum time in milliseconds (adjusted for emulation)
    BOOL DoLogin;                // Enable/disable login sequence (TRUE=enable, FALSE=disable)
    LPLIST UserSessions;         // List of active user sessions
    LPLIST UserAccount;          // List of user accounts
    DATABASE* UserDatabase;      // User accounts database
    CACHE ObjectTerminationCache;  // Cache for terminated object states with TTL
} KERNELDATA, *LPKERNELDATA;

extern KERNELDATA Kernel;

/***************************************************************************/
// Functions in Kernel.c

void KernelObjectDestructor(LPVOID);
BOOL GetCPUInformation(LPCPUINFORMATION);
void InitializeQuantumTime(void);
U32 ClockTestTask(LPVOID);
U32 GetPhysicalMemoryUsed(void);
void TestProcess(void);
void InitializeKernel(void);
void StoreObjectTerminationState(LPVOID Object, U32 ExitCode);
BOOL ObjectExists(HANDLE Object);

/***************************************************************************/
// Functions in Segment.c

void InitSegmentDescriptor(LPSEGMENTDESCRIPTOR, U32);
void InitGlobalDescriptorTable(LPSEGMENTDESCRIPTOR Table);
void InitializeTaskSegments(void);
void SetSegmentDescriptorBase(LPSEGMENTDESCRIPTOR Desc, U32 Base);
void SetSegmentDescriptorLimit(LPSEGMENTDESCRIPTOR Desc, U32 Limit);
void SetTSSDescriptorBase(LPTSSDESCRIPTOR Desc, U32 Base);
void SetTSSDescriptorLimit(LPTSSDESCRIPTOR Desc, U32 Limit);

/***************************************************************************/
// Functions in MemoryEditor.c

void PrintMemory(U32, U32);
void MemoryEditor(U32);

/***************************************************************************/
// Functions in Edit.c

U32 Edit(U32, LPCSTR*);

/***************************************************************************/

#pragma pack(pop)

#endif  // KERNEL_H_INCLUDED
