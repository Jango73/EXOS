
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef SYSTEM_H_INCLUDED
#define SYSTEM_H_INCLUDED

/***************************************************************************/

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

extern U32 GetGDTR(void);
extern U32 GetLDTR(void);
extern U32 GetESP(void);
extern U32 GetEBP(void);
extern U32 GetDR6(void);
extern U32 GetDR7(void);
extern void SetDR6(U32);
extern void SetDR7(U32);
extern void GetCPUID(LPVOID);
extern U32 DisablePaging(void);
extern U32 EnablePaging(void);
extern void DisableInterrupts(void);
extern void EnableInterrupts(void);
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
extern U32 LoadGlobalDescriptorTable(PHYSICAL Base, U32 Limit);
extern U32 LoadLocalDescriptorTable(PHYSICAL Base, U32 Limit);
extern U32 LoadInterruptDescriptorTable(PHYSICAL Base, U32 Limit);
extern U32 LoadPageDirectory(PHYSICAL Base);
extern U32 LoadInitialTaskRegister(U32 TaskRegister);
extern U32 GetTaskRegister(void);
extern U32 GetPageDirectory(void);
extern void SetPageDirectory(PHYSICAL Base);
extern void InvalidatePage(U32 Address);
extern void FlushTLB(void);
extern U32 SwitchToTask(U32);
extern U32 TaskRunner(void);
extern U32 ClearTaskState(void);
extern U32 PeekConsoleWord(U32);
extern U32 PokeConsoleWord(U32, U32);
extern void SetConsoleCursorPosition(U32, U32);
extern U32 SaveRegisters(LPINTEL386REGISTERS);
extern void MemorySet(LPVOID Destination, U32 What, U32 Size);
extern void MemoryCopy(LPVOID Destination, LPCVOID Source, U32 Size);
extern U32 DoSystemCall(U32, U32);
extern void IdleCPU(void);
extern void DeadCPU(void);
extern void Reboot(void);

/***************************************************************************/

// Functions in RMC.asm

extern void RealModeCall(U32, LPX86REGS);
extern void Exit_EXOS(U32 SS, U32 SP);
extern void RealModeCallTest(void);

/***************************************************************************/

#endif
