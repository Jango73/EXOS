
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef KERNEL_H_INCLUDED
#define KERNEL_H_INCLUDED

#define __DEBUG__

/***************************************************************************/

#include "Address.h"
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

// Kernel selectors

#define SELECTOR_GLOBAL 0x00
#define SELECTOR_LOCAL 0x04

#define SELECTOR_NULL 0x00
#define SELECTOR_UNUSED 0x08
#define SELECTOR_KERNEL_CODE (0x10 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_KERNEL_DATA (0x18 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_USER_CODE (0x20 | SELECTOR_GLOBAL | PRIVILEGE_USER)
#define SELECTOR_USER_DATA (0x28 | SELECTOR_GLOBAL | PRIVILEGE_USER)
#define SELECTOR_REAL_CODE (0x30 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_REAL_DATA (0x38 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_TSS_0 (0x40 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_TSS_1 (0x50 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)

/***************************************************************************/

// Task selectors

#define TASK_SELECTOR_NULL 0x0000
#define TASK_SELECTOR_RAM 0x0008
#define TASK_SELECTOR_CODE 0x0010
#define TASK_SELECTOR_DATA 0x0018
#define TASK_SELECTOR_HEAP 0x0020
#define TASK_SELECTOR_STAK 0x0028

#define TASK_SELINDEX_NULL 0
#define TASK_SELINDEX_RAM 1
#define TASK_SELINDEX_CODE 2
#define TASK_SELINDEX_DATA 3
#define TASK_SELINDEX_HEAP 4
#define TASK_SELINDEX_STAK 5

/***************************************************************************/

#define DESCRIPTOR_SIZE 8
#define IDT_NUM_DESCRIPTORS (IDT_SIZE / DESCRIPTOR_SIZE)
#define GDT_NUM_DESCRIPTORS (GDT_SIZE / DESCRIPTOR_SIZE)
#define GDT_NUM_BASE_DESCRIPTORS 8
#define GDT_NUM_DESCRIPTORS_PER_TASK 2
#define LDT_NUM_DESCRIPTORS 6

#define TSK_NUM_TASKS (TSK_SIZE / TSS_SIZE)

#define GDT_NUM_TASKS ((GDT_NUM_DESCRIPTORS - GDT_NUM_BASE_DESCRIPTORS) / GDT_NUM_DESCRIPTORS_PER_TASK)

#define LA_GDT_TASK (LA_GDT + (GDT_NUM_BASE_DESCRIPTORS * DESCRIPTOR_SIZE))

#define GDT_TASK_DESCRIPTORS_SIZE (GDT_NUM_DESCRIPTORS_PER_TASK * DESCRIPTOR_SIZE)

/***************************************************************************/

#define NUM_INTERRUPTS 48

#if 0
#define NUM_TASKS (GDT_NUM_TASKS < TSK_NUM_TASKS ? GDT_NUM_TASKS : TSK_NUM_TASKS)
#endif

#define NUM_TASKS 64

/***************************************************************************/

// The EXOS interrupt for user functions

#define EXOS_USER_CALL 0x80

// The EXOS interrupt for driver functions

#define EXOS_DRIVER_CALL 0x81

/***************************************************************************/

// Global Kernel Data

typedef struct tag_KERNELDATA_I386 {
    LPGATEDESCRIPTOR IDT;
    LPSEGMENTDESCRIPTOR GDT;
    LPTASKTSSDESCRIPTOR TTD;
    LPTASKSTATESEGMENT TSS;
    LPPAGEBITMAP PPB;
} KERNELDATA_I386, *LPKERNELDATA_I386;

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

/***************************************************************************/

// Functions in Kernel.c

LPVOID KernelMemAlloc(U32);
void KernelMemFree(LPVOID);
BOOL GetSegmentInfo(LPSEGMENTDESCRIPTOR, LPSEGMENTINFO);
void LogRegisters(LPINTEL386REGISTERS);
BOOL GetCPUInformation(LPCPUINFORMATION);
U32 ClockTask(LPVOID);
U32 GetPhysicalMemoryUsed();
void TestProcess();
void InitializeKernel();

/***************************************************************************/

// Variables in Kernel.c

extern KERNELDATA_I386 Kernel_i386;
extern KERNELDATA Kernel;
extern PHYSICAL StubAddress;

/***************************************************************************/

// Functions in Segment.c

void InitSegmentDescriptor(LPSEGMENTDESCRIPTOR, U32);
void SetSegmentDescriptorBase(LPSEGMENTDESCRIPTOR, U32);
void SetSegmentDescriptorLimit(LPSEGMENTDESCRIPTOR, U32);
void SetTSSDescriptorBase(LPTSSDESCRIPTOR, U32);
void SetTSSDescriptorLimit(LPTSSDESCRIPTOR, U32);

/***************************************************************************/

// Functions in MemEdit.c

void PrintMemory(U32, U32);
void MemEdit(U32);

/***************************************************************************/

// Functions in Edit.c

U32 Edit(U32, LPCSTR*);

/***************************************************************************/

#endif
