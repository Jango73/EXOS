
// Fault.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#include "Process.h"
#include "Text.h"

/***************************************************************************/

static void PrintFaultDetails ()
{
  INTEL386REGISTERS Regs;
  LPPROCESS Process;
  LPTASK Task;

  Task = GetCurrentTask();

  if (Task != NULL)
  {
    Process = Task->Process;

    if (Process != NULL)
    {
      KernelPrint(Text_Image);
      KernelPrint(Text_Space);
      KernelPrint(Process->FileName);
      KernelPrint(Text_NewLine);

      KernelPrint(Text_Registers);
      KernelPrint(Text_NewLine);

      SaveRegisters(&Regs);
      DumpRegisters(&Regs);
    }
  }
}

/***************************************************************************/

static void Die ()
{
  LPTASK            Task;
  INTEL386REGISTERS Regs;

  LockSemaphore(SEMAPHORE_KERNEL, INFINITY);
  LockSemaphore(SEMAPHORE_MEMORY, INFINITY);
  LockSemaphore(SEMAPHORE_CONSOLE, INFINITY);

  FreezeScheduler();

  KillTask(GetCurrentTask());

  UnlockSemaphore(SEMAPHORE_KERNEL);
  UnlockSemaphore(SEMAPHORE_MEMORY);
  UnlockSemaphore(SEMAPHORE_CONSOLE);

  UnfreezeScheduler();

  EnableInterrupts();

  while (1) {}
}

/***************************************************************************/

void DefaultHandler ()
{
  KernelPrint("Unknown interrupt\n");
  PrintFaultDetails();
}

/***************************************************************************/

void DivideErrorHandler ()
{
  KernelPrint("Divide error !\n");
  PrintFaultDetails();
  Die();
}

/***************************************************************************/

void DebugExceptionHandler ()
{
  KernelPrint("Debug exception !\n");
  PrintFaultDetails();
}

/***************************************************************************/

void NMIHandler ()
{
  KernelPrint("Non-maskable interrupt !\n");
  PrintFaultDetails();
}

/***************************************************************************/

void BreakPointHandler ()
{
  KernelPrint("Breakpoint !\n");
  PrintFaultDetails();
  Die();
}

/***************************************************************************/

void OverflowHandler ()
{
  KernelPrint("Overflow !\n");
  PrintFaultDetails();
  Die();
}

/***************************************************************************/

void BoundRangeHandler ()
{
  KernelPrint("Bound range fault !\n");
  PrintFaultDetails();
  Die();
}

/***************************************************************************/

void InvalidOpcodeHandler ()
{
  KernelPrint("Invalid opcode !\n");
  PrintFaultDetails();
  Die();
}

/***************************************************************************/

void DeviceNotAvailHandler ()
{
  KernelPrint("Device not available !\n");
  PrintFaultDetails();
  Die();
}

/***************************************************************************/

void DoubleFaultHandler ()
{
  KernelPrint("Double fault !\n");
  PrintFaultDetails();
  Die();
}

/***************************************************************************/

void MathOverflowHandler ()
{
  KernelPrint("Math overflow !\n");
  PrintFaultDetails();
  Die();
}

/***************************************************************************/

void InvalidTSSHandler ()
{
  KernelPrint("Invalid TSS !\n");
  PrintFaultDetails();
  Die();
}

/***************************************************************************/

void SegmentFaultHandler ()
{
  KernelPrint("Segment fault !\n");
  PrintFaultDetails();
  Die();
}

/***************************************************************************/

void StackFaultHandler ()
{
  KernelPrint("Stack fault !\n");
  PrintFaultDetails();
  Die();
}

/***************************************************************************/

void GeneralProtectionHandler (U32 Code)
{
  STR Num [16];

  KernelPrint("General protection fault !\n");
  PrintFaultDetails();
  Die();

/*
  U32ToHexString(Code, Num);
  KernelPrint(Num);
  KernelPrint(Text_NewLine);

  KernelPrint("Killing task...\n");
  KillTask(GetCurrentTask());
  KernelPrint("Done\n");
*/

/*
  INTEL386REGISTERS Regs;

  KernelPrint(Text_NewLine);
  KernelPrint("General protection fault !\n");
  KernelPrint(Text_Registers);
  KernelPrint(Text_NewLine);

  SaveRegisters(&Regs);
  DumpRegisters(&Regs);
*/
}

/***************************************************************************/

void PageFaultHandler (U32 ErrorCode, LINEAR Address)
{
  STR Num [16];

  ConsolePrint("Page fault !\n");

  ConsolePrint("The current task did an unauthorized access\n");
  ConsolePrint("at linear address : ");
  U32ToHexString(Address, Num);
  ConsolePrint(Num);
  ConsolePrint(Text_NewLine);
  ConsolePrint("Since this error is unrecoverable,\n");
  ConsolePrint("the task will be shutdown now.\n");
  ConsolePrint("Shutdown in progress...\n");

  Die();
}

/***************************************************************************/

void AlignmentCheckHandler ()
{
  KernelPrint("Alignment check fault !\n");
  PrintFaultDetails();
  Die();
}

/***************************************************************************/
