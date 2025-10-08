
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

--------------------------------------------------------------------------

    Trap entry stack for #DB, #DF, #TS, #NP, #SS, #GP, #PF, #AC


    High addresses
         |
         v

 E  +------------------+ <-- ESP before exception
 S  |                  |
 P  |   User Stack     |     (user data before exception)
    |                  |
 G  +------------------+
 O  |        |   SS    | <-- 16 bits - PRESENT only if user->kernel privilege change
 E  +------------------+
 S  |      ESP         | <-- 32 bits - PRESENT only if user->kernel privilege change
    +------------------+
 D  |    EFLAGS        | <-- 32 bits - ALWAYS PRESENT
 O  +------------------+
 W  |        |   CS    | <-- 16 bits - ALWAYS PRESENT
 N  +------------------+
    |      EIP         | <-- 32 bits - ALWAYS PRESENT
 |  +------------------+
 v  |   ERROR_CODE     | <-- 32 bits - ALWAYS PRESENT
    +------------------+ <-- ESP after exception (in handler)
    |                  |
    |  Kernel Stack    |     (exception handler)
    |                  |
         |
         v
    Low addresses

    ERROR_CODE = 0 for #DB, #DF, #AC
               = selector for #TS, #NP, #SS, #GP
               = info for #PF

--------------------------------------------------------------------------

    Trap entry stack for IRQs and #DE, #BR, #UD, #NM, #MF
    Just short of an error code


    High addresses
         |
         v

 E  +------------------+ <-- ESP before exception
 S  |                  |
 P  |   User Stack     |     (user data before exception)
    |                  |
 G  +------------------+
 O  |        |   SS    | <-- 16 bits - PRESENT only if user->kernel privilege change
 E  +------------------+
 S  |      ESP         | <-- 32 bits - PRESENT only if user->kernel privilege change
    +------------------+
 D  |    EFLAGS        | <-- 32 bits - ALWAYS PRESENT
 O  +------------------+
 W  |        |   CS    | <-- 16 bits - ALWAYS PRESENT
 N  +------------------+
    |      EIP         | <-- 32 bits - ALWAYS PRESENT
 |  +------------------+ <-- ESP after exception (in handler)
 v  |                  |
    |  Kernel Stack    |     (exception handler)
    |                  |
         |
         v
    Low addresses

--------------------------------------------------------------------------

    Trap stack after all pushes from stub, without an error code

 E  +------------------+ <-- ESP before exception (may be top of system stack)
 S  |                  |
 P  | Some stack data  |     (user data before exception)
    |                  |
 G  +------------------+ <-- TRAP (may be top of system stack)
 O  |        |   SS    | <-- 16 bits - PRESENT only if user->kernel privilege change
 E  +------------------+
 S  |      ESP         | <-- 32 bits - PRESENT only if user->kernel privilege change
    +------------------+
 D  |    EFLAGS        | <-- 32 bits - pushed by CPU
 O  +------------------+
 W  |        |   CS    | <-- 16 bits - pushed by CPU
 N  +------------------+
    |      EIP         | <-- 32 bits - pushed by CPU
 |  +------------------+ <-- ESP after exception (in handler)
 v  |      EAX         | <-- pushed by pushad
    +------------------+
    |      ECX         | <-- pushed by "pushad"
    +------------------+
    |      EDX         | <-- pushed by "pushad"
    +------------------+
    |      EBX         | <-- pushed by "pushad"
    +------------------+
    |      ESP         | <-- pushed by "pushad"
    +------------------+
    |      EBP         | <-- pushed by "pushad"
    +------------------+
    |      ESI         | <-- pushed by "pushad"
    +------------------+
    |      EDI         | <-- pushed by "pushad"
    +------------------+
    |        |   DS    | <-- pushed by "push ds"
    +------------------+
    |        |   ES    | <-- pushed by "push es"
    +------------------+
    |        |   FS    | <-- pushed by "push fs"
    +------------------+
    |        |   GS    | <-- pushed by "push gs"
    +------------------+
    |      EBP         |
    +------------------+
    |        |   SS    |
    +------------------+
    |      DATA        |
    |       OF         |
    |  INTERRUPT_FRAME  |
    +------------------+ <-- ESP as given to BuildInterruptFrame

\************************************************************************/

#include "InterruptFrame.h"

#include "Base.h"
#include "arch/i386/I386.h"
#include "Log.h"
#include "Memory.h"
#include "String.h"
#include "System.h"

/************************************************************************/

#define INCOMING_SS_INDEX 0
#define INCOMING_C_EBP_INDEX 1
#define INCOMING_GS_INDEX 2
#define INCOMING_FS_INDEX 3
#define INCOMING_ES_INDEX 4
#define INCOMING_DS_INDEX 5
#define INCOMING_EDI_INDEX 6
#define INCOMING_ESI_INDEX 7
#define INCOMING_EBP_INDEX 8
#define INCOMING_ESP_INDEX 9
#define INCOMING_EBX_INDEX 10
#define INCOMING_EDX_INDEX 11
#define INCOMING_ECX_INDEX 12
#define INCOMING_EAX_INDEX 13
#define INCOMING_ERROR_CODE_INDEX 14  // If present, the following indexes will be shifted up by 1 (+ HasErrorCode)
#define INCOMING_EIP_INDEX 14         // Yes, it is the same index as INCOMING_ERROR_CODE_INDEX, don't touch this
#define INCOMING_CS_INDEX 15
#define INCOMING_EFLAGS_INDEX 16
#define INCOMING_R3_ESP_INDEX 17
#define INCOMING_R3_SS_INDEX 18

/************************************************************************/

LPINTERRUPT_FRAME BuildInterruptFrame(U32 intNo, U32 HasErrorCode, U32 ESP) {
    LPINTERRUPT_FRAME Frame;
    U32* Stack;
    U32 UserMode;

    if (HasErrorCode > 1) HasErrorCode = 1;

    Frame = (LPINTERRUPT_FRAME)ESP;
    Stack = (U32*)(ESP + sizeof(INTERRUPT_FRAME));

    if (IsValidMemory((LINEAR)Stack) == FALSE) {
        DEBUG(TEXT("[BuildInterruptFrame] Invalid stack computed : %x"), Stack);
        DO_THE_SLEEPING_BEAUTY;
    }

    UserMode = (Stack[INCOMING_CS_INDEX + HasErrorCode] & SELECTOR_RPL_MASK) != 0;

    MemorySet(Frame, 0, sizeof(INTERRUPT_FRAME));

    Frame->Registers.EFlags = Stack[INCOMING_EFLAGS_INDEX + HasErrorCode];
    Frame->Registers.EIP = Stack[INCOMING_EIP_INDEX + HasErrorCode];
    Frame->Registers.CS = Stack[INCOMING_CS_INDEX + HasErrorCode] & MAX_U16;

#if SCHEDULING_DEBUG_OUTPUT == 1
    KernelLogText(
        LOG_DEBUG, TEXT("[BuildInterruptFrame] FRAME BUILD DEBUG - intNo=%d HasErrorCode=%d UserMode=%d"), intNo,
        HasErrorCode, UserMode);
    DEBUG(TEXT("[BuildInterruptFrame] Stack at %x:"), Stack);
    KernelLogMem(LOG_DEBUG, (U32)Stack, 256);
    KernelLogText(
        LOG_DEBUG, TEXT("[BuildInterruptFrame] Extracted: EIP=%x CS=%x EFlags=%x"), Frame->Registers.EIP,
        Frame->Registers.CS, Frame->Registers.EFlags);
#endif

    Frame->Registers.EAX = Stack[INCOMING_EAX_INDEX];
    Frame->Registers.EBX = Stack[INCOMING_EBX_INDEX];
    Frame->Registers.ECX = Stack[INCOMING_ECX_INDEX];
    Frame->Registers.EDX = Stack[INCOMING_EDX_INDEX];
    Frame->Registers.ESI = Stack[INCOMING_ESI_INDEX];
    Frame->Registers.EDI = Stack[INCOMING_EDI_INDEX];
    Frame->Registers.EBP = Stack[INCOMING_EBP_INDEX];

    Frame->Registers.DS = Stack[INCOMING_DS_INDEX] & MAX_U16;
    Frame->Registers.ES = Stack[INCOMING_ES_INDEX] & MAX_U16;
    Frame->Registers.FS = Stack[INCOMING_FS_INDEX] & MAX_U16;
    Frame->Registers.GS = Stack[INCOMING_GS_INDEX] & MAX_U16;

    if (UserMode) {
        Frame->Registers.ESP = Stack[INCOMING_R3_ESP_INDEX + HasErrorCode];
        Frame->Registers.SS = Stack[INCOMING_R3_SS_INDEX + HasErrorCode] & MAX_U16;
    } else {
        Frame->Registers.ESP = Stack[INCOMING_ESP_INDEX];
        Frame->Registers.SS = Stack[INCOMING_SS_INDEX] & MAX_U16;
    }

    __asm__ volatile("mov %%cr0, %0" : "=r"(Frame->Registers.CR0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(Frame->Registers.CR2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(Frame->Registers.CR3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(Frame->Registers.CR4));

    __asm__ volatile("mov %%dr0, %0" : "=r"(Frame->Registers.DR0));
    __asm__ volatile("mov %%dr1, %0" : "=r"(Frame->Registers.DR1));
    __asm__ volatile("mov %%dr2, %0" : "=r"(Frame->Registers.DR2));
    __asm__ volatile("mov %%dr3, %0" : "=r"(Frame->Registers.DR3));
    __asm__ volatile("mov %%dr6, %0" : "=r"(Frame->Registers.DR6));
    __asm__ volatile("mov %%dr7, %0" : "=r"(Frame->Registers.DR7));

    Frame->Registers.DR4 = 0;
    Frame->Registers.DR5 = 0;
    Frame->IntNo = intNo;

    if (HasErrorCode) {
        Frame->ErrCode = Stack[INCOMING_ERROR_CODE_INDEX];
    } else {
        Frame->ErrCode = 0;
    }

    return Frame;
}

/************************************************************************/

void RestoreFromInterruptFrame(LPINTERRUPT_FRAME NextFrame, U32 ESP) {
    U32* Stack;
    U32 UserMode;
    U32 HasErrorCode = 0;  // Timer interrupts don't have error codes

#if SCHEDULING_DEBUG_OUTPUT == 1
    DEBUG(TEXT("[RestoreFromInterruptFrame] Enter. ESP = %x"), ESP);
#endif

    SAFE_USE_VALID(NextFrame) {
        Stack = (U32*)(ESP + sizeof(INTERRUPT_FRAME));
        // For task switching via timer interrupt (32), HasErrorCode = 0
        UserMode = (NextFrame->Registers.CS & SELECTOR_RPL_MASK) != 0;

        // Restore segment registers
        Stack[INCOMING_DS_INDEX] = NextFrame->Registers.DS;
        Stack[INCOMING_ES_INDEX] = NextFrame->Registers.ES;
        Stack[INCOMING_FS_INDEX] = NextFrame->Registers.FS;
        Stack[INCOMING_GS_INDEX] = NextFrame->Registers.GS;

        // Restore general-purpose registers
        Stack[INCOMING_EAX_INDEX] = NextFrame->Registers.EAX;
        Stack[INCOMING_EBX_INDEX] = NextFrame->Registers.EBX;
        Stack[INCOMING_ECX_INDEX] = NextFrame->Registers.ECX;
        Stack[INCOMING_EDX_INDEX] = NextFrame->Registers.EDX;
        Stack[INCOMING_ESI_INDEX] = NextFrame->Registers.ESI;
        Stack[INCOMING_EDI_INDEX] = NextFrame->Registers.EDI;
        Stack[INCOMING_EBP_INDEX] = NextFrame->Registers.EBP;

        // Restore stack-related registers based on privilege mode
        // For user mode tasks, always use R3 positions for IRET
        Stack[INCOMING_ESP_INDEX] = NextFrame->Registers.ESP;
        Stack[INCOMING_SS_INDEX] = NextFrame->Registers.SS;

        if (UserMode) {
            Stack[INCOMING_R3_SS_INDEX + HasErrorCode] = NextFrame->Registers.SS;
            Stack[INCOMING_R3_ESP_INDEX + HasErrorCode] = NextFrame->Registers.ESP;
        }

        // Restore CS, EIP, and EFLAGS
        Stack[INCOMING_CS_INDEX + HasErrorCode] = NextFrame->Registers.CS;
        Stack[INCOMING_EIP_INDEX + HasErrorCode] = NextFrame->Registers.EIP;
        Stack[INCOMING_EFLAGS_INDEX + HasErrorCode] = NextFrame->Registers.EFlags;

        // Restore error code if present
        if (HasErrorCode) {
            Stack[INCOMING_ERROR_CODE_INDEX] = NextFrame->ErrCode;
        }

#if SCHEDULING_DEBUG_OUTPUT == 1

        DEBUG(TEXT("[RestoreFromInterruptFrame] CRITICAL DEBUG - Before Stack Restore"));
        KernelLogText(
            LOG_DEBUG, TEXT("[RestoreFromInterruptFrame] NextFrame: ESP=%x EIP=%x CS=%x"), NextFrame->Registers.ESP,
            NextFrame->Registers.EIP, NextFrame->Registers.CS);
        KernelLogText(
            LOG_DEBUG, TEXT("[RestoreFromInterruptFrame] NextFrame: CR3=%x (page dir)"), NextFrame->Registers.CR3);
        DEBUG(TEXT("[RestoreFromInterruptFrame] UserMode=%d"), UserMode);
        DEBUG(TEXT("[RestoreFromInterruptFrame] ==== Stack at NextFrame->Registers.ESP:"));
        KernelLogMem(LOG_DEBUG, NextFrame->Registers.ESP, 256);
        DEBUG(TEXT("[RestoreFromInterruptFrame] ==== Current stack (ESP):"));
        KernelLogMem(LOG_DEBUG, GetESP(), 256);

        // Debug the stack data we're about to restore
        DEBUG(TEXT("[RestoreFromInterruptFrame] Stack data restore:"));
        KernelLogText(
            LOG_DEBUG, TEXT("[RestoreFromInterruptFrame] EIP index %d: %x"), INCOMING_EIP_INDEX + HasErrorCode,
            Stack[INCOMING_EIP_INDEX + HasErrorCode]);
        KernelLogText(
            LOG_DEBUG, TEXT("[RestoreFromInterruptFrame] CS index %d: %x"), INCOMING_CS_INDEX + HasErrorCode,
            Stack[INCOMING_CS_INDEX + HasErrorCode]);
        KernelLogText(
            LOG_DEBUG, TEXT("[RestoreFromInterruptFrame] EFLAGS index %d: %x"), INCOMING_EFLAGS_INDEX + HasErrorCode,
            Stack[INCOMING_EFLAGS_INDEX + HasErrorCode]);

        DEBUG(TEXT("\n"));

#endif
    }
    else {
        ERROR(TEXT("[RestoreFromInterruptFrame] Invalid frame %x"), NextFrame);
    }
}
