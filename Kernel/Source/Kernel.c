
// Kernel.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#include "Kernel.h"
#include "System.h"
#include "Driver.h"
#include "Keyboard.h"
#include "Console.h"
#include "Mouse.h"
#include "HD.h"

/***************************************************************************/

STR Text_OSTitle [] =
"EXOS - Exelsius Operating System - Version 1.00\n"
"Copyright (c) 1999-2010 Exelsius.\n"
"All rights reserved.\n";

/***************************************************************************/

KERNELSTARTUPINFO   KernelStartup = { 0 };
LPGATEDESCRIPTOR    IDT = (LPGATEDESCRIPTOR)    LA_IDT;
LPSEGMENTDESCRIPTOR GDT = (LPSEGMENTDESCRIPTOR) LA_GDT;
LPTASKTSSDESCRIPTOR TTD = (LPTASKTSSDESCRIPTOR) LA_GDT_TASK;
LPTASKSTATESEGMENT  TSS = (LPTASKSTATESEGMENT)  LA_TSS;
LPPAGEBITMAP        PPB = (LPPAGEBITMAP)        LA_PPB;

/***************************************************************************/

static LIST DesktopList =
{
  NULL, NULL, NULL,
  0,
  KernelMemAlloc,
  KernelMemFree,
  NULL
};

/***************************************************************************/

static LIST ProcessList =
{
  (LPLISTNODE) &KernelProcess,
  (LPLISTNODE) &KernelProcess,
  (LPLISTNODE) &KernelProcess,
  1,
  KernelMemAlloc,
  KernelMemFree,
  NULL
};

/***************************************************************************/

static LIST TaskList =
{
  (LPLISTNODE) &KernelTask,
  (LPLISTNODE) &KernelTask,
  (LPLISTNODE) &KernelTask,
  1,
  KernelMemAlloc,
  KernelMemFree,
  NULL
};

/***************************************************************************/

static LIST SemaphoreList =
{
  (LPLISTNODE) &KernelSemaphore,
  (LPLISTNODE) &ConsoleSemaphore,
  (LPLISTNODE) &KernelSemaphore,
  9,
  KernelMemAlloc,
  KernelMemFree,
  NULL
};

/***************************************************************************/

static LIST DiskList =
{
  NULL, NULL, NULL, 0, KernelMemAlloc, KernelMemFree, NULL
};

static LIST FileSystemList =
{
  NULL, NULL, NULL, 0, KernelMemAlloc, KernelMemFree, NULL
};

static LIST FileList =
{
  NULL, NULL, NULL, 0, KernelMemAlloc, KernelMemFree, NULL
};

/***************************************************************************/

KERNELDATA Kernel =
{
  &DesktopList,
  &ProcessList,
  &TaskList,
  &SemaphoreList,
  &DiskList,
  &FileSystemList,
  &FileList,
};

/***************************************************************************/

VOIDFUNC InterruptTable [] =
{
  Interrupt_DivideError,               // 0
  Interrupt_DebugException,            // 1
  Interrupt_NMI,                       // 2
  Interrupt_BreakPoint,                // 3
  Interrupt_Overflow,                  // 4
  Interrupt_BoundRange,                // 5
  Interrupt_InvalidOpcode,             // 6
  Interrupt_DeviceNotAvail,            // 7
  Interrupt_DoubleFault,               // 8
  Interrupt_MathOverflow,              // 9
  Interrupt_InvalidTSS,                // 10
  Interrupt_SegmentFault,              // 11
  Interrupt_StackFault,                // 12
  Interrupt_GeneralProtection,         // 13
  Interrupt_PageFault,                 // 14
  Interrupt_Default,                   // 15
  Interrupt_Default,                   // 16
  Interrupt_AlignmentCheck,            // 17
  Interrupt_Default,                   // 18
  Interrupt_Default,                   // 19
  Interrupt_Default,                   // 20
  Interrupt_Default,                   // 21
  Interrupt_Default,                   // 22
  Interrupt_Default,                   // 23
  Interrupt_Default,                   // 24
  Interrupt_Default,                   // 25
  Interrupt_Default,                   // 26
  Interrupt_Default,                   // 27
  Interrupt_Default,                   // 28
  Interrupt_Default,                   // 29
  Interrupt_Default,                   // 30
  Interrupt_Default,                   // 31
  Interrupt_Clock,                     // 32
  Interrupt_Keyboard,                  // 33  0x01
  Interrupt_Default,                   // 34  0x02
  Interrupt_Default,                   // 35  0x03
  Interrupt_Mouse,                     // 36  0x04
  Interrupt_Default,                   // 37  0x05
  Interrupt_Default,                   // 38  0x06
  Interrupt_Default,                   // 39  0x07
  Interrupt_Default,                   // 40  0x08
  Interrupt_Default,                   // 41  0x09
  Interrupt_Default,                   // 42  0x0A
  Interrupt_Default,                   // 43  0x0B
  Interrupt_Default,                   // 44  0x0C
  Interrupt_Default,                   // 45  0x0D
  Interrupt_HardDrive,                 // 46  0x0E
  Interrupt_Default,                   // 47  0x0F
};

/***************************************************************************/

PHYSICAL StubAddress = 0;

/***************************************************************************/

LPVOID KernelMemAlloc (U32 Size)
{
  // return HeapAlloc_P(&KernelProcess, Size);
  return HeapAlloc_HBHS
  (
    KernelProcess.HeapBase,
    KernelProcess.HeapSize,
    Size
  );
}

/***************************************************************************/

void KernelMemFree (LPVOID Pointer)
{
  // HeapFree_P(&KernelProcess, Pointer);
  return HeapFree_HBHS
  (
    KernelProcess.HeapBase,
    KernelProcess.HeapSize,
    Pointer
  );
}

/***************************************************************************/

void SetGateDescriptorOffset
(
  LPGATEDESCRIPTOR This,
  U32 Offset
)
{
  This->Offset_00_15 = (Offset & (U32) 0x0000FFFF) >> 0x00;
  This->Offset_16_31 = (Offset & (U32) 0xFFFF0000) >> 0x10;
}

/***************************************************************************/

static void InitializeInterrupts ()
{
  U32 Index = 0;

  //-------------------------------------
  // Set all used interrupts

  for (Index = 0; Index < NUM_INTERRUPTS; Index++)
  {
    IDT[Index].Selector  = SELECTOR_KERNEL_CODE;
    IDT[Index].Reserved  = 0;
    IDT[Index].Type      = GATE_TYPE_386_INT;
    IDT[Index].Privilege = PRIVILEGE_KERNEL;
    IDT[Index].Present   = 1;

    SetGateDescriptorOffset(IDT + Index, (U32) InterruptTable[Index]);
  }

  //-------------------------------------
  // Set system call interrupt

  Index = EXOS_USER_CALL;

  IDT[Index].Selector  = SELECTOR_KERNEL_CODE;
  IDT[Index].Reserved  = 0;
  IDT[Index].Type      = GATE_TYPE_386_TRAP;
  IDT[Index].Privilege = PRIVILEGE_KERNEL;
  IDT[Index].Present   = 1;

  SetGateDescriptorOffset(IDT + Index, (U32) Interrupt_SystemCall);

  //-------------------------------------
  // Set driver call interrupt

  Index = EXOS_DRIVER_CALL;

  IDT[Index].Selector  = SELECTOR_KERNEL_CODE;
  IDT[Index].Reserved  = 0;
  IDT[Index].Type      = GATE_TYPE_386_TRAP;
  IDT[Index].Privilege = PRIVILEGE_KERNEL;
  IDT[Index].Present   = 1;

  SetGateDescriptorOffset(IDT + Index, (U32) Interrupt_DriverCall);
}

/***************************************************************************/

BOOL GetSegmentInfo
(
  LPSEGMENTDESCRIPTOR This,
  LPSEGMENTINFO Info
)
{
  if (Info)
  {
    Info->Base         = SEGMENTBASE(This);
    Info->Limit        = SEGMENTLIMIT(This);
    Info->Type         = This->Type;
    Info->Privilege    = This->Privilege;
    Info->Granularity  = SEGMENTGRANULAR(This);
    Info->CanWrite     = This->CanWrite;
    Info->OperandSize  = This->OperandSize ? 32 : 16;
    Info->Conforming   = This->ConformExpand;
    Info->Present      = This->Present;

    return TRUE;
  }

  return FALSE;
}

/***************************************************************************/

U32 SegmentInfoToString (LPSEGMENTINFO This, LPSTR Text)
{
  if (This && Text)
  {
    STR Temp [64];

    Text[0] = STR_NULL;

    StringConcat(Text, "Segment");
    StringConcat(Text, Text_NewLine);

    StringConcat(Text, "Base           : ");
    U32ToHexString(This->Base, Temp);
    StringConcat(Text, Temp);
    StringConcat(Text, Text_NewLine);

    StringConcat(Text, "Limit          : ");
    U32ToHexString(This->Limit, Temp);
    StringConcat(Text, Temp);
    StringConcat(Text, Text_NewLine);

    StringConcat(Text, "Type           : ");
    StringConcat(Text, This->Type ? "Code" : "Data");
    StringConcat(Text, Text_NewLine);

    StringConcat(Text, "Privilege      : ");
    U32ToHexString(This->Privilege, Temp);
    StringConcat(Text, Temp);
    StringConcat(Text, Text_NewLine);

    StringConcat(Text, "Granularity    : ");
    U32ToHexString(This->Granularity, Temp);
    StringConcat(Text, Temp);
    StringConcat(Text, Text_NewLine);

    StringConcat(Text, "Can write      : ");
    StringConcat(Text, This->CanWrite ? "True" : "False");
    StringConcat(Text, Text_NewLine);

/*
    StringConcat(Text, "Operand Size   : ");
    StringConcat(Text, This->OperandSize ? "32-bit" : "16-bit");
    StringConcat(Text, Text_NewLine);

    StringConcat(Text, "Conforming     : ");
    StringConcat(Text, This->Conforming ? "True" : "False");
    StringConcat(Text, Text_NewLine);

    StringConcat(Text, "Present        : ");
    StringConcat(Text, This->Present ? "True" : "False");
    StringConcat(Text, Text_NewLine);
*/
  }

  return 1;
}

/***************************************************************************/

BOOL DumpGlobalDescriptorTable
(
  LPSEGMENTDESCRIPTOR Table,
  U32 Size
)
{
  U32 Index = 0;

  if (Table)
  {
    SEGMENTINFO Info;
    STR Text [256];

    for (Index = 0; Index < Size; Index++)
    {
      GetSegmentInfo(Table + Index, &Info);
      SegmentInfoToString(&Info, Text);
      KernelPrint(Text);
    }
  }

  return TRUE;
}

/***************************************************************************/

void DumpRegisters (LPINTEL386REGISTERS Regs)
{
  STR Temp [32];

  KernelPrint("EAX : ");
  U32ToHexString(Regs->EAX, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("EBX : ");
  U32ToHexString(Regs->EBX, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("ECX : ");
  U32ToHexString(Regs->ECX, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("EDX : ");
  U32ToHexString(Regs->EDX, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_NewLine);

  //-------------------------------------

  KernelPrint("ESI : ");
  U32ToHexString(Regs->ESI, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("EDI : ");
  U32ToHexString(Regs->EDI, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("ESP : ");
  U32ToHexString(Regs->ESP, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("EBP : ");
  U32ToHexString(Regs->EBP, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_NewLine);

  //-------------------------------------

  KernelPrint("CS : ");
  U32ToHexString(Regs->CS, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("DS : ");
  U32ToHexString(Regs->DS, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("SS : ");
  U32ToHexString(Regs->SS, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_NewLine);

  //-------------------------------------

  KernelPrint("ES : ");
  U32ToHexString(Regs->ES, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("FS : ");
  U32ToHexString(Regs->FS, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("GS : ");
  U32ToHexString(Regs->GS, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_NewLine);

  //-------------------------------------

  KernelPrint("E-flags : ");
  U32ToHexString(Regs->EFlags, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("EIP : ");
  U32ToHexString(Regs->EIP, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_NewLine);

  //-------------------------------------

  KernelPrint("CR0 : ");
  U32ToHexString(Regs->CR0, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("CR2 : ");
  U32ToHexString(Regs->CR2, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("CR3 : ");
  U32ToHexString(Regs->CR3, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("CR4 : ");
  U32ToHexString(Regs->CR4, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_NewLine);

  //-------------------------------------

  KernelPrint("DR0 : ");
  U32ToHexString(Regs->DR0, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("DR1 : ");
  U32ToHexString(Regs->DR1, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("DR2 : ");
  U32ToHexString(Regs->DR2, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_Space);

  KernelPrint("DR3 : ");
  U32ToHexString(Regs->DR3, Temp);
  KernelPrint(Temp);
  KernelPrint(Text_NewLine);
}

/***************************************************************************/

typedef struct tag_CPUIDREGISTERS
{
  U32 reg_EAX;
  U32 reg_EBX;
  U32 reg_ECX;
  U32 reg_EDX;
} CPUIDREGISTERS, *LPCPUIDREGISTERS;

/***************************************************************************/

BOOL GetCPUInformation (LPCPUINFORMATION Info)
{
  CPUIDREGISTERS Regs [4];
  U32 Max = 0;

  MemorySet(Info, 0, sizeof (CPUINFORMATION));

  GetCPUID(Regs);

  Max = Regs[0].reg_EAX;

  //-------------------------------------
  // Fill name with register contents

  *((U32*) (Info->Name + 0)) = Regs[0].reg_EBX;
  *((U32*) (Info->Name + 4)) = Regs[0].reg_EDX;
  *((U32*) (Info->Name + 8)) = Regs[0].reg_ECX;
  Info->Name[12] = '\0';

  //-------------------------------------
  // Get model information if available

  Info->Type     = (Regs[1].reg_EAX & INTEL_CPU_MASK_TYPE    ) >> INTEL_CPU_SHFT_TYPE;
  Info->Family   = (Regs[1].reg_EAX & INTEL_CPU_MASK_FAMILY  ) >> INTEL_CPU_SHFT_FAMILY;
  Info->Model    = (Regs[1].reg_EAX & INTEL_CPU_MASK_MODEL   ) >> INTEL_CPU_SHFT_MODEL;
  Info->Stepping = (Regs[1].reg_EAX & INTEL_CPU_MASK_STEPPING) >> INTEL_CPU_SHFT_STEPPING;
  Info->Features = Regs[1].reg_EDX;

  return TRUE;
}

/***************************************************************************/

U32 ClockTask (LPVOID Param)
{
  STR Text [64];
  STR Num [16];
  U32 X       = ((U32) Param & 0xFFFF0000) >> 16;
  U32 Y       = ((U32) Param & 0x0000FFFF) >> 0;
  U32 OldX    = 0;
  U32 OldY    = 0;
  I32 MouseX  = 0;
  I32 MouseY  = 0;
  U32 Buttons = 0;

  U32 Time    = 0;
  U32 OldTime = 0;

  while (1)
  {
    Time = DoSystemCall(SYSCALL_GetSystemTime, 0);
    if (Time - OldTime >= 1000)
    {
      OldTime = Time;
      MilliSecondsToHMS(Time, Text);
      LockSemaphore(SEMAPHORE_CONSOLE, 0);
      OldX = Console.CursorX;
      OldY = Console.CursorY;

      Console.CursorX = X;
      Console.CursorY = Y;
      KernelPrint(Text);

      MouseX  = SerialMouseDriver.Command(DF_MOUSE_GETDELTAX, 0);
      MouseY  = SerialMouseDriver.Command(DF_MOUSE_GETDELTAY, 0);
      Buttons = SerialMouseDriver.Command(DF_MOUSE_GETBUTTONS, 0);
      Console.CursorX = 0;
      Console.CursorY = 0;
      KernelPrint("%d %d %d", MouseX, MouseY, Buttons);

      Console.CursorX = OldX;
      Console.CursorY = OldY;
      UnlockSemaphore(SEMAPHORE_CONSOLE);
    }

    // DoSystemCall(SYSCALL_Sleep, 200);
    DoSystemCall(SYSCALL_Sleep, 40);
  }

/*
  SYSTEMTIME Time;
  SYSTEMTIME OldTime;

  while (1)
  {
    DoSystemCall(SYSCALL_GetLocalTime, (U32) &Time);

    LockSemaphore(SEMAPHORE_CONSOLE, INFINITY);
    // DoSystemCall(SYSCALL_LockSemaphore, (U32) SEMAPHORE_CONSOLE);

    OldX = Console.CursorX;
    OldY = Console.CursorY;
    Console.CursorX = X;
    Console.CursorY = Y;
    KernelPrint("%02d:%02d:%02d", Time.Hour, Time.Minute, Time.Second);
    Console.CursorX = OldX;
    Console.CursorY = OldY;

    UnlockSemaphore(SEMAPHORE_CONSOLE);
    // DoSystemCall(SYSCALL_LockSemaphore, (U32) SEMAPHORE_CONSOLE);

    DoSystemCall(SYSCALL_Sleep, 3000);
  }
*/

  return 0;
}

/***************************************************************************/

void DumpSystemInformation ()
{
  static STR Num [16] = { 0 };

  KernelPrint(Text_NewLine);

  //-------------------------------------
  // Print information on computer

  KernelPrint("Computer ID : ");
  KernelPrint(Kernel.CPU.Name);
  KernelPrint(Text_NewLine);

  //-------------------------------------
  // Print information on memory

  KernelPrint("Physical memory : ");
  U32ToString(Memory / 1024, Num);
  KernelPrint(Num);
  KernelPrint(Text_Space);
  KernelPrint(Text_KB);
  KernelPrint(Text_NewLine);

}

/***************************************************************************/

void InitializePhysicalPageBitmap ()
{
  U32 NumPagesUsed = 0;
  U32 Index        = 0;
  U32 Byte         = 0;
  U32 Value        = 0;

  NumPagesUsed = (PA_KERNEL + N_2MB) >> PAGE_SIZE_MUL;

  for (Index = 0; Index < NumPagesUsed; Index++)
  {
    Byte  = Index >> MUL_8;
    Value = (U32) 0x01 << (Index & 0x07);
    PPB[Byte] |= (U8) Value;
  }
}

/***************************************************************************/

void InitializeFileSystems ()
{
  LPLISTNODE Node;

  KernelPrint("Initializing file system...\n");

  MountSystemFS();

  for (Node = Kernel.Disk->First; Node; Node = Node->Next)
  {
    MountDiskPartitions((LPPHYSICALDISK) Node, NULL, 0);
  }
}

/***************************************************************************/

U32 GetPhysicalMemoryUsed ()
{
  U32 NumPages = 0;
  U32 Index    = 0;
  U32 Byte     = 0;
  U32 Mask     = 0;

  LockSemaphore(SEMAPHORE_MEMORY, INFINITY);

  for (Index = 0; Index < Pages; Index++)
  {
    Byte = Index >> MUL_8;
    Mask = (U32) 0x01 << (Index & 0x07);
    if (PPB[Byte] & Mask) NumPages++;
  }

  UnlockSemaphore(SEMAPHORE_MEMORY);

  return (NumPages << PAGE_SIZE_MUL);
}

/***************************************************************************/

void InitializeKernel ()
{
  PROCESSINFO ProcessInfo;
  TASKINFO TaskInfo;
  LPTASK   Task;
  STR FileName [MAX_PATH_NAME];

  //-------------------------------------
  // No more interrupts

  DisableInterrupts();

  //-------------------------------------
  // Get system information gathered by the stub

  MemoryCopy
  (
    &KernelStartup,
    (LPVOID) (StubAddress + 16),
    sizeof(KERNELSTARTUPINFO)
  );

  IRQMask_21_RM = KernelStartup.IRQMask_21_RM;
  IRQMask_A1_RM = KernelStartup.IRQMask_A1_RM;

  //-------------------------------------
  // Initialize kernel data

  Memory = KernelStartup.MemorySize;
  Pages  = Memory >> PAGE_SIZE_MUL;

  MemorySet((LPVOID) KernelProcess.HeapBase, 0, 16384);

  *((U32*)KernelProcess.HeapBase) = ID_HEAP;

  //-------------------------------------
  // Initialize the physical page bitmap

  InitializePhysicalPageBitmap();

  //-------------------------------------
  // Initialize the keyboard

  StdKeyboardDriver.Command(DF_LOAD, 0);

  //-------------------------------------
  // Initialize the console

  ConsoleInitialize();

  //-------------------------------------
  // Print the EXOS banner

  ConsolePrint(Text_OSTitle);

  //-------------------------------------
  // Initialize interrupts

  InitializeInterrupts();
  LoadInterruptDescriptorTable(LA_IDT, IDT_SIZE - 1);

  KernelLogText(LOG_VERBOSE, "Interrupts initialized...");

  //-------------------------------------
  // Setup the kernel's main task

  InitKernelTask();
  LoadInitialTaskRegister(KernelTask.Selector);

  KernelLogText(LOG_VERBOSE, "Kernel task setup...");

//-------------------------------------
  // Initialize the clock

  InitializeClock();

  KernelLogText(LOG_VERBOSE, "Clock initialized...");

  //-------------------------------------
  // Enable interrupts

  EnableInterrupts();

  KernelLogText(LOG_VERBOSE, "Interrupts enabled...");

  //-------------------------------------
  // Get information on CPU

  GetCPUInformation(&(Kernel.CPU));

  KernelLogText(LOG_VERBOSE, "Got CPU information...");

  //-------------------------------------
  // Initialize RAM drives

  RAMDiskDriver.Command(DF_LOAD, 0);

  KernelLogText(LOG_VERBOSE, "RAM drive initialized...");

  //-------------------------------------
  // Initialize physical drives

  StdHardDiskDriver.Command(DF_LOAD, 0);

  KernelLogText(LOG_VERBOSE, "Physical drives initialized...");

  //-------------------------------------
  // Initialize the file systems

  InitializeFileSystems();

  KernelLogText(LOG_VERBOSE, "File systems initialized...");

  //-------------------------------------
  // Initialize the graphics card

  VESADriver.Command(DF_LOAD, 0);

  KernelLogText(LOG_VERBOSE, "VESA driver initialized...");

  //-------------------------------------
  // Initialize the mouse

  SerialMouseDriver.Command(DF_LOAD, 0);

  KernelLogText(LOG_VERBOSE, "Mouse initialized...");

  //-------------------------------------
  // Print system infomation

  DumpSystemInformation();

  //-------------------------------------
  // Test tasks

/*
  TaskInfo.Func      = ClockTask;
  TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
  TaskInfo.Priority  = TASK_PRIORITY_LOWEST;
  TaskInfo.Flags     = 0;

  TaskInfo.Parameter = (LPVOID) (((U32) 70 << 16) | 0);
  Task = CreateTask(&KernelProcess, &TaskInfo);
*/

  //-------------------------------------
  // Shell task

  TaskInfo.Func      = Shell;
  TaskInfo.Parameter = NULL;
  TaskInfo.StackSize = TASK_MINIMUM_STACK_SIZE;
  TaskInfo.Priority  = TASK_PRIORITY_MEDIUM;
  TaskInfo.Flags     = 0;

  Task = CreateTask(&KernelProcess, &TaskInfo);

  //-------------------------------------
  // Launch the explorer

/*
  StringCopy(FileName, "C:/EXOS/SYSTEM/EXPLORER.PRG");

  ProcessInfo.Size        = sizeof ProcessInfo;
  ProcessInfo.Flags       = 0;
  ProcessInfo.FileName    = FileName;
  ProcessInfo.CommandLine = NULL;
  ProcessInfo.StdOut      = NULL;
  ProcessInfo.StdIn       = NULL;
  ProcessInfo.StdErr      = NULL;

  CreateProcess(&ProcessInfo);
*/
}

/***************************************************************************/
