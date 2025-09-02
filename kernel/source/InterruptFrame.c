
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


    Interrupt Frame Management

\************************************************************************/

#include "../include/Base.h"
#include "../include/I386.h"
#include "../include/Log.h"
#include "../include/System.h"

/************************************************************************/

LPINTERRUPTFRAME BuildInterruptFrame(U32 intNo, U32 hasErrorCode)
{
    U32 esp, ebp;
    LPINTERRUPTFRAME frame;
    U32 *stack, *hwFrame;
    U32 userMode;

#if CRITICAL_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[BuildInterruptFrame] Enter (%d, %d)"), intNo, hasErrorCode);
#endif

    __asm__ volatile("mov %%esp, %0" : "=r"(esp));
    __asm__ volatile("mov %%ebp, %0" : "=r"(ebp));

    esp -= sizeof(INTERRUPTFRAME);
    frame = (LPINTERRUPTFRAME)esp;

    stack = (U32*)ebp;

    if (hasErrorCode) {
        hwFrame = stack + 14;
    } else {
        hwFrame = stack + 13;
    }

    userMode = (hwFrame[1] & 3) != 0;

    frame->Registers.EFlags = hwFrame[2];
    frame->Registers.EAX = stack[13];
    frame->Registers.EBX = stack[10];
    frame->Registers.ECX = stack[12];
    frame->Registers.EDX = stack[11];
    frame->Registers.ESI = stack[7];
    frame->Registers.EDI = stack[6];
    frame->Registers.EBP = stack[8];
    frame->Registers.EIP = hwFrame[0];

    frame->Registers.CS = (U16)(hwFrame[1] & 0xFFFF);
    frame->Registers.DS = (U16)(stack[5] & 0xFFFF);
    frame->Registers.ES = (U16)(stack[4] & 0xFFFF);
    frame->Registers.FS = (U16)(stack[3] & 0xFFFF);
    frame->Registers.GS = (U16)(stack[2] & 0xFFFF);

    if (userMode) {
        frame->Registers.ESP = hwFrame[3];
        frame->Registers.SS = (U16)(hwFrame[4] & 0xFFFF);
    } else {
        frame->Registers.ESP = stack[9];
        
        U32 savedSS;
        __asm__ volatile("mov -4(%%ebp), %0" : "=r"(savedSS));
        frame->Registers.SS = (U16)(savedSS & 0xFFFF);
    }

    __asm__ volatile("mov %%cr0, %0" : "=r"(frame->Registers.CR0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(frame->Registers.CR2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(frame->Registers.CR3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(frame->Registers.CR4));

    __asm__ volatile("mov %%dr0, %0" : "=r"(frame->Registers.DR0));
    __asm__ volatile("mov %%dr1, %0" : "=r"(frame->Registers.DR1));
    __asm__ volatile("mov %%dr2, %0" : "=r"(frame->Registers.DR2));
    __asm__ volatile("mov %%dr3, %0" : "=r"(frame->Registers.DR3));
    frame->Registers.DR4 = 0;
    frame->Registers.DR5 = 0;
    __asm__ volatile("mov %%dr6, %0" : "=r"(frame->Registers.DR6));
    __asm__ volatile("mov %%dr7, %0" : "=r"(frame->Registers.DR7));

    frame->IntNo = intNo;
    
    if (hasErrorCode) {
        frame->ErrCode = hwFrame[-1];
    } else {
        frame->ErrCode = 0;
    }

#if CRITICAL_DEBUG_OUTPUT == 1
    KernelLogText(LOG_DEBUG, TEXT("[BuildInterruptFrame] Exit"));
#endif

    __asm__ volatile("mov %0, %%esp" : : "r"(esp));

    return frame;
}

/************************************************************************/

void RestoreFromInterruptFrame(LPINTERRUPTFRAME nextFrame)
{
    U32 targetESP, stackSize;
    U32 *newStack;
    U32 userMode;

    if (nextFrame == NULL) return;

    userMode = (nextFrame->Registers.CS & 3) != 0;

    stackSize = 16 + 32 + 12;
    if (userMode) {
        stackSize += 8;
    }

    targetESP = nextFrame->Registers.ESP - stackSize;
    newStack = (U32*)targetESP;

    newStack[0] = (U32)nextFrame->Registers.DS;
    newStack[1] = (U32)nextFrame->Registers.ES;
    newStack[2] = (U32)nextFrame->Registers.FS;
    newStack[3] = (U32)nextFrame->Registers.GS;

    newStack[4] = nextFrame->Registers.EDI;
    newStack[5] = nextFrame->Registers.ESI;
    newStack[6] = nextFrame->Registers.EBP;
    newStack[7] = nextFrame->Registers.ESP;
    newStack[8] = nextFrame->Registers.EBX;
    newStack[9] = nextFrame->Registers.EDX;
    newStack[10] = nextFrame->Registers.ECX;
    newStack[11] = nextFrame->Registers.EAX;

    newStack[12] = nextFrame->Registers.EIP;
    newStack[13] = (U32)nextFrame->Registers.CS;
    newStack[14] = nextFrame->Registers.EFlags;

    if (userMode) {
        newStack[15] = nextFrame->Registers.ESP;
        newStack[16] = (U32)nextFrame->Registers.SS;
    }

    __asm__ volatile("mov %0, %%esp" : : "r"(targetESP));
}
