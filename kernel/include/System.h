
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


    System

\************************************************************************/

#ifndef SYSTEM_H_INCLUDED
#define SYSTEM_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Arch.h"

/***************************************************************************/

// Variables in System.asm

extern U32 IRQMask_21;
extern U32 IRQMask_A1;
extern U32 IRQMask_21_RM;
extern U32 IRQMask_A1_RM;

/***************************************************************************/

// Functions in System.asm

extern void GetCPUID(LPVOID);
extern U32 DisablePaging(void);
extern U32 EnablePaging(void);
extern void SaveFPU(LPVOID StateBuffer);
extern void RestoreFPU(LPVOID StateBuffer);
extern U32 InPortByte(U32 Port);
extern U32 OutPortByte(U32 Port, U32 Value);
extern U32 InPortWord(U32 Port);
extern U32 OutPortWord(U32 Port, U32 Value);
extern U32 InPortLong(U32 Port);
extern U32 OutPortLong(U32 Port, U32 Value);
extern U32 InPortStringWord(U32 Port, LPVOID Buffer, U32 Count);
extern U32 OutPortStringWord(U32 Port, LPVOID Buffer, U32 Count);
extern U32 MaskIRQ(U32 IRQ);
extern U32 UnmaskIRQ(U32 IRQ);
extern U32 DisableIRQ(U32 IRQ);
extern U32 EnableIRQ(U32 IRQ);
extern U32 LoadGlobalDescriptorTable(PHYSICAL Base, U32 Limit);
extern void ReadGlobalDescriptorTable(LPVOID GdtrPointer);
extern U32 GetTaskRegister(void);
extern U32 GetPageDirectory(void);
extern void InvalidatePage(U32 Address);
extern void FlushTLB(void);
extern U32 TaskRunner(void);
extern void *__task_runner_start;
extern void *__task_runner_end;
extern U32 ClearTaskState(void);
extern U32 PeekConsoleWord(U32);
extern U32 PokeConsoleWord(U32, U32);
extern void SetConsoleCursorPosition(U32 X, U32 Y);
#if defined(__EXOS_ARCH_I386__)
extern U32 SaveRegisters(LPINTEL_386_REGISTERS Registers);
#elif defined(__EXOS_ARCH_X86_64__)
extern U32 SaveRegisters(LPINTEL_64_GENERAL_REGISTERS Registers);
#endif
extern U32 DoSystemCall(U32 Number, U32 Parameter);
extern void IdleCPU(void);
extern void DeadCPU(void);
extern void Reboot(void);

/***************************************************************************/

// Functions in RMC.asm

extern void RealModeCall(U32, LPINTEL_X86_REGISTERS);
extern void RealModeCallTest(void);

/***************************************************************************/

#endif
