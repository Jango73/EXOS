
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef KERNEL_H_INCLUDED
#define KERNEL_H_INCLUDED

#define __DEBUG__

/***************************************************************************/

#include "I386.h"
#include "Base.h"
#include "Heap.h"
#include "ID.h"
#include "List.h"
#include "Memory.h"
#include "Process.h"
#include "String.h"
#include "Text.h"
#include "User.h"

/***************************************************************************/

#pragma pack(1)

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

/***************************************************************************/

#define DESCRIPTOR_SIZE 8
#define GDT_NUM_DESCRIPTORS (GDT_SIZE / DESCRIPTOR_SIZE)
#define GDT_NUM_BASE_DESCRIPTORS 8
#define GDT_NUM_DESCRIPTORS_PER_TASK 2

#define GDT_NUM_TASKS ((GDT_NUM_DESCRIPTORS - GDT_NUM_BASE_DESCRIPTORS) / GDT_NUM_DESCRIPTORS_PER_TASK)
#define GDT_TASK_DESCRIPTORS_SIZE (GDT_NUM_DESCRIPTORS_PER_TASK * DESCRIPTOR_SIZE)
#define NUM_TASKS GDT_NUM_TASKS

#define IDT_SIZE N_4KB
#define GDT_SIZE N_8KB

/***************************************************************************/
// Static linear addresses (VMA)
// All processes have the following address space layout

#define LA_RAM 0x00000000        // Reserved for kernel
#define LA_VIDEO 0x000A0000      // Reserved for kernel
#define LA_CONSOLE 0x000B8000    // Reserved for kernel
#define LA_USER 0x00400000       // Start of user address space
#define LA_LIBRARY 0xA0000000    // Dynamic Libraries
#define LA_KERNEL 0xC0000000     // Kernel

/***************************************************************************/

#define NUM_INTERRUPTS 48

/***************************************************************************/
// The EXOS interrupt for user functions

#define EXOS_USER_CALL 0x80

/***************************************************************************/
// The EXOS interrupt for driver functions

#define EXOS_DRIVER_CALL 0x81

/***************************************************************************/

// Global Kernel Data

#define KERNEL_PHYSICAL_ORIGIN 0x20000
#define RESERVED_LOW_MEMORY N_4MB
#define LOW_MEMORY_HALF (RESERVED_LOW_MEMORY / 2)

typedef struct tag_E820ENTRY {
    U64 Base;
    U64 Size;
    U32 Type;
    U32 Attributes;
} E820ENTRY, *LPE820ENTRY;

typedef struct tag_KERNELSTARTUPINFO {
    PHYSICAL StubAddress;
    PHYSICAL PageDirectory;
    U32 IRQMask_21_PM;
    U32 IRQMask_A1_PM;
    U32 IRQMask_21_RM;
    U32 IRQMask_A1_RM;
    U32 ConsoleX;
    U32 ConsoleY;
    U32 MemorySize;         // Total memory size in bytes
    U32 PageCount;          // Total memory size in pages (4K)
    U32 E820_Count;         // BIOS E820 function entries
    E820ENTRY E820 [N_4KB / sizeof(E820ENTRY)];
} KERNELSTARTUPINFO, *LPKERNELSTARTUPINFO;

extern KERNELSTARTUPINFO KernelStartup;

// These structures are allocated in Memory.c

typedef struct tag_KERNELDATA_I386 {
    LPGATEDESCRIPTOR IDT;
    LPSEGMENTDESCRIPTOR GDT;
    LPTASKTSSDESCRIPTOR TTD;
    LPTASKSTATESEGMENT TSS;
    LPPAGEBITMAP PPB;
} KERNELDATA_I386, *LPKERNELDATA_I386;

extern KERNELDATA_I386 Kernel_i386;

typedef struct tag_KERNELDATA {
    LPLIST Desktop;
    LPLIST Process;
    LPLIST Task;
    LPLIST Mutex;
    LPLIST Disk;
    LPLIST PCIDevice;
    LPLIST FileSystem;
    LPLIST File;
    CPUINFORMATION CPU;
} KERNELDATA, *LPKERNELDATA;

extern KERNELDATA Kernel;

/***************************************************************************/

// Functions in Kernel.c

LPVOID KernelMemAlloc(U32);
void KernelMemFree(LPVOID);
BOOL GetSegmentInfo(LPSEGMENTDESCRIPTOR, LPSEGMENTINFO);
BOOL GetCPUInformation(LPCPUINFORMATION);
U32 ClockTask(LPVOID);
U32 GetPhysicalMemoryUsed(void);
void TestProcess(void);
void InitializeKernel(U32 ImageAddress, U8 CursorX, U8 CursorY);

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

// Functions in MemEdit.c

void PrintMemory(U32, U32);
void MemEdit(U32);

/***************************************************************************/

// Functions in Edit.c

U32 Edit(U32, LPCSTR*);

/***************************************************************************/

#endif
