
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef SYSTEM_H_INCLUDED
#define SYSTEM_H_INCLUDED

/***************************************************************************/

#include "Address.h"
#include "Base.h"
#include "I386.h"

/***************************************************************************/

// Variables in System.asm

extern U32 IRQMask_21;
extern U32 IRQMask_A1;
extern U32 IRQMask_21_RM;
extern U32 IRQMask_A1_RM;

/***************************************************************************/

// Functions in System.asm

extern void GetCPUID(LPVOID);
extern U32 DisablePaging();
extern U32 EnablePaging();
extern void DisableInterrupts();
extern void EnableInterrupts();
extern void SaveFlags(U32*);
extern void RestoreFlags(U32*);
extern U32 InPortByte(U32);
extern U32 OutPortByte(U32, U32);
extern U32 InPortWord(U32);
extern U32 OutPortWord(U32, U32);
extern U32 InPortLong(U32);
extern U32 OutPortLong(U32, U32);
extern U32 InPortStringWord(U32, LPVOID, U32);
extern U32 OutPortStringWord(U32, LPVOID, U32);
extern U32 MaskIRQ(U32);
extern U32 UnmaskIRQ(U32);
extern U32 DisableIRQ(U32);
extern U32 EnableIRQ(U32);
extern U32 LoadGlobalDescriptorTable(U32, U32);
extern U32 LoadLocalDescriptorTable(U32, U32);
extern U32 LoadInterruptDescriptorTable(U32, U32);
extern U32 LoadPageDirectory(U32);
extern U32 LoadInitialTaskRegister(U32);
extern U32 GetTaskRegister();
extern U32 GetPageDirectory();
extern U32 FlushTLB();
extern U32 SwitchToTask(U32);
extern U32 TaskRunner();
extern U32 ClearTaskState();
extern U32 PeekConsoleWord(U32);
extern U32 PokeConsoleWord(U32, U32);
extern void SetConsoleCursorPosition(U32, U32);
extern U32 SaveRegisters(LPINTEL386REGISTERS);
extern U32 DoSystemCall(U32, U32);
extern void Reboot();

/***************************************************************************/

// Functions in RMC.asm

extern void RealModeCall(U32, LPX86REGS);
extern void Exit_EXOS(U32 SS, U32 SP);
extern void RealModeCallTest();

/***************************************************************************/

#endif
