
// Kernel.h

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#ifndef KERNEL_H_INCLUDED
#define KERNEL_H_INCLUDED

// #define __DEBUG__

/***************************************************************************/

#include "Base.h"
#include "Address.h"
#include "ID.h"
#include "String.h"
#include "List.h"
#include "Text.h"
#include "Process.h"
#include "VMM.h"
#include "Heap.h"
#include "User.h"

/***************************************************************************/

#pragma pack (1)

/***************************************************************************/

// Structure to receive information about a segment in a more friendly way

typedef struct tag_SEGMENTINFO
{
  U32 Base;
  U32 Limit;
  U32 Type;
  U32 Privilege;
  U32 Granularity;
  U32 CanWrite;
  U32 OperandSize;
  U32 Conforming;
  U32 Present;
} SEGMENTINFO, *LPSEGMENTINFO;

/***************************************************************************/

// Structure to receive CPU information

typedef struct tag_CPUINFORMATION
{
  STR Name [16];
  U32 Type;
  U32 Family;
  U32 Model;
  U32 Stepping;
  U32 Features;
} CPUINFORMATION, *LPCPUINFORMATION;

/***************************************************************************/

typedef struct tag_KERNELSTARTUPINFO
{
  U32 Loader_SS;
  U32 Loader_SP;
  U32 IRQMask_21_RM;
  U32 IRQMask_A1_RM;
  U32 ConsoleWidth;
  U32 ConsoleHeight;
  U32 ConsoleCursorX;
  U32 ConsoleCursorY;
  U32 MemorySize;
} KERNELSTARTUPINFO, *LPKERNELSTARTUPINFO;

/***************************************************************************/

// Kernel selectors

#define SELECTOR_GLOBAL 0x00
#define SELECTOR_LOCAL  0x04

#define SELECTOR_NULL        0x00
#define SELECTOR_UNUSED      0x08
#define SELECTOR_KERNEL_CODE (0x10 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_KERNEL_DATA (0x18 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_USER_CODE   (0x20 | SELECTOR_GLOBAL | PRIVILEGE_USER)
#define SELECTOR_USER_DATA   (0x28 | SELECTOR_GLOBAL | PRIVILEGE_USER)
#define SELECTOR_REAL_CODE   (0x30 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_REAL_DATA   (0x38 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_TSS_0       (0x40 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_TSS_1       (0x50 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)

/***************************************************************************/

// Task selectors

#define TASK_SELECTOR_NULL 0x0000
#define TASK_SELECTOR_RAM  0x0008
#define TASK_SELECTOR_CODE 0x0010
#define TASK_SELECTOR_DATA 0x0018
#define TASK_SELECTOR_HEAP 0x0020
#define TASK_SELECTOR_STAK 0x0028

#define TASK_SELINDEX_NULL 0
#define TASK_SELINDEX_RAM  1
#define TASK_SELINDEX_CODE 2
#define TASK_SELINDEX_DATA 3
#define TASK_SELINDEX_HEAP 4
#define TASK_SELINDEX_STAK 5

/***************************************************************************/

#define DESCRIPTOR_SIZE              8
#define IDT_NUM_DESCRIPTORS          (IDT_SIZE / DESCRIPTOR_SIZE)
#define GDT_NUM_DESCRIPTORS          (GDT_SIZE / DESCRIPTOR_SIZE)
#define GDT_NUM_BASE_DESCRIPTORS     8
#define GDT_NUM_DESCRIPTORS_PER_TASK 2
#define LDT_NUM_DESCRIPTORS          6

#define TSK_NUM_TASKS (TSK_SIZE / TSS_SIZE)

#define GDT_NUM_TASKS                                \
(                                                    \
  (GDT_NUM_DESCRIPTORS - GDT_NUM_BASE_DESCRIPTORS) / \
  GDT_NUM_DESCRIPTORS_PER_TASK                       \
)

#define LA_GDT_TASK                            \
(                                              \
  LA_GDT +                                     \
  (GDT_NUM_BASE_DESCRIPTORS * DESCRIPTOR_SIZE) \
)

#define GDT_TASK_DESCRIPTORS_SIZE \
(GDT_NUM_DESCRIPTORS_PER_TASK * DESCRIPTOR_SIZE)

/***************************************************************************/

#define NUM_INTERRUPTS    48

#if 0
#define NUM_TASKS \
(GDT_NUM_TASKS < TSK_NUM_TASKS ? GDT_NUM_TASKS : TSK_NUM_TASKS)
#endif

#define NUM_TASKS      64

/***************************************************************************/

// The EXOS interrupt for user functions

#define EXOS_USER_CALL 0x80

// The EXOS interrupt for driver functions

#define EXOS_DRIVER_CALL 0x81

/***************************************************************************/

// Global Kernel Data

typedef struct tag_KERNELDATA
{
  LPLIST         Desktop;
  LPLIST         Process;
  LPLIST         Task;
  LPLIST         Semaphore;
  LPLIST         Disk;
  LPLIST         FileSystem;
  LPLIST         File;
  CPUINFORMATION CPU;
} KERNELDATA, *LPKERNELDATA;

/***************************************************************************/

// Functions in Kernel.c

LPVOID KernelMemAlloc          (U32);
void   KernelMemFree           (LPVOID);
void   SetGateDescriptorOffset (LPGATEDESCRIPTOR, U32);
BOOL   GetSegmentInfo          (LPSEGMENTDESCRIPTOR, LPSEGMENTINFO);
void   DumpRegisters           (LPINTEL386REGISTERS);
BOOL   GetCPUInformation       (LPCPUINFORMATION);
U32    ClockTask               (LPVOID);
U32    GetPhysicalMemoryUsed   ();
void   TestProcess             ();
void   InitializeKernel        ();

/***************************************************************************/

// Variables in Kernel.c

extern KERNELSTARTUPINFO   KernelStartup;
extern LPGATEDESCRIPTOR    IDT;
extern LPSEGMENTDESCRIPTOR GDT;
extern LPTASKTSSDESCRIPTOR TTD;
extern LPTASKSTATESEGMENT  TSS;
extern LPPAGEBITMAP        PPB;
extern VOIDFUNC            InterruptTable [];
extern KERNELDATA          Kernel;
extern PHYSICAL            StubAddress;

/***************************************************************************/

// Functions in Log.c

#define LOG_DEBUG    0x0001
#define LOG_VERBOSE  0x0002
#define LOG_WARNING  0x0004
#define LOG_ERROR    0x0008

void KernelLogText (U32, LPSTR);

/***************************************************************************/

// Functions in Segment.c

void InitSegmentDescriptor     (LPSEGMENTDESCRIPTOR, U32);
void SetSegmentDescriptorBase  (LPSEGMENTDESCRIPTOR, U32);
void SetSegmentDescriptorLimit (LPSEGMENTDESCRIPTOR, U32);
void SetTSSDescriptorBase      (LPTSSDESCRIPTOR, U32);
void SetTSSDescriptorLimit     (LPTSSDESCRIPTOR, U32);

/***************************************************************************/

// Functions in MemEdit.c

void PrintMemory (U32, U32);
void MemEdit     (U32);

/***************************************************************************/

// Functions in Int.asm

extern void Interrupt_Default               ();
extern void Interrupt_DivideError           ();
extern void Interrupt_DebugException        ();
extern void Interrupt_NMI                   ();
extern void Interrupt_BreakPoint            ();
extern void Interrupt_Overflow              ();
extern void Interrupt_BoundRange            ();
extern void Interrupt_InvalidOpcode         ();
extern void Interrupt_DeviceNotAvail        ();
extern void Interrupt_DoubleFault           ();
extern void Interrupt_MathOverflow          ();
extern void Interrupt_InvalidTSS            ();
extern void Interrupt_SegmentFault          ();
extern void Interrupt_StackFault            ();
extern void Interrupt_GeneralProtection     ();
extern void Interrupt_PageFault             ();
extern void Interrupt_AlignmentCheck        ();
extern void Interrupt_Clock                 ();
extern void Interrupt_Keyboard              ();
extern void Interrupt_Mouse                 ();
extern void Interrupt_HardDrive             ();
extern void Interrupt_SystemCall            ();
extern void Interrupt_DriverCall            ();

/***************************************************************************/

// Functions in Edit.c

U32 Edit (U32, LPCSTR*);

/***************************************************************************/

#endif
