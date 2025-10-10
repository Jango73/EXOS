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


    Intel x86-64 Architecture Support

\************************************************************************/

#ifndef ARCH_X86_64_X86_64_H_INCLUDED
#define ARCH_X86_64_X86_64_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "arch/x86/x86-Common.h"
#include "arch/x86-64/x86-64-Memory.h"

/***************************************************************************/

// PIC and IRQ helpers

#define INTERRUPT_COMMAND 0x0020
#define MAX_IRQ 16
#define IRQ_KEYBOARD 0x01
#define IRQ_MOUSE 0x04
#define IRQ_ATA 0x0E

/***************************************************************************/

// CMOS helpers

#define CMOS_COMMAND 0x0070
#define CMOS_DATA 0x0071
#define CMOS_SECOND 0x00
#define CMOS_ALARM_SECOND 0x01
#define CMOS_MINUTE 0x02
#define CMOS_ALARM_MINUTE 0x03
#define CMOS_HOUR 0x04
#define CMOS_ALARM_HOUR 0x05
#define CMOS_DAY_OF_WEEK 0x06
#define CMOS_DAY_OF_MONTH 0x07
#define CMOS_MONTH 0x08
#define CMOS_YEAR 0x09
#define CMOS_CENTURY 0x32

/***************************************************************************/

// PIT clock

#define CLOCK_COMMAND 0x0043
#define CLOCK_DATA 0x0040

/***************************************************************************/

// Keyboard controller

#define KEYBOARD_COMMAND 0x0064
#define KEYBOARD_DATA 0x0060
#define KSR_OUT_FULL 0x01
#define KSR_IN_FULL 0x02
#define KSR_COMMAND 0x08
#define KSR_ACTIVE 0x10
#define KSR_OUT_ERROR 0x20
#define KSR_IN_ERROR 0x40
#define KSR_PARITY_ERROR 0x80
#define KSL_SCROLL 0x01
#define KSL_NUM 0x02
#define KSL_CAPS 0x04
#define KSC_READ_MODE 0x20
#define KSC_WRITE_MODE 0x60
#define KSC_SELF_TEST 0xAA
#define KSC_ENABLE 0xAE
#define KSC_SETLEDSTATUS 0xED
#define KSS_ACK 0xFA

/***************************************************************************/

// Low memory pages reserved by VBR

#define LOW_MEMORY_PAGE_1 0x1000
#define LOW_MEMORY_PAGE_2 0x2000
#define LOW_MEMORY_PAGE_3 0x3000
#define LOW_MEMORY_PAGE_4 0x4000
#define LOW_MEMORY_PAGE_5 0x5000
#define LOW_MEMORY_PAGE_6 0x6000
#define LOW_MEMORY_PAGE_7 0x7000
#define LOW_MEMORY_PAGE_8 0x8000

/***************************************************************************/

// Extended control register helpers

#define READ_CR8(var) __asm__ volatile("mov %%cr8, %0" : "=r"(var))
#define WRITE_CR8(value) __asm__ volatile("mov %0, %%cr8" : : "r"(value) : "memory")

/***************************************************************************/

// Swap GS base in long mode

#define SWAPGS() __asm__ volatile("swapgs" : : : "memory")

/***************************************************************************/

// Read and write RFLAGS using 64-bit instructions

#define READ_RFLAGS64(var) __asm__ volatile("pushfq; pop %0" : "=r"(var))
#define WRITE_RFLAGS64(value) __asm__ volatile("push %0; popfq" : : "r"(value) : "memory", "cc")

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/

// General purpose register snapshot for 64-bit mode

typedef struct tag_INTEL_64_GENERAL_REGISTERS {
    U64 RFlags;
    U64 RAX, RBX, RCX, RDX;
    U64 RSI, RDI, RBP, RSP;
    U64 R8, R9, R10, R11, R12, R13, R14, R15;
    U64 RIP;
    U16 CS, DS, SS;
    U16 ES, FS, GS;
    U64 CR0, CR2, CR3, CR4, CR8;
    U64 DR0, DR1, DR2, DR3, DR6, DR7;
} INTEL_64_GENERAL_REGISTERS, *LPINTEL_64_GENERAL_REGISTERS;

/***************************************************************************/

// IDT entry layout for 64-bit mode (16 bytes)

typedef struct tag_X86_64_IDT_ENTRY {
    U16 Offset_00_15;
    U16 Selector;
    U16 InterruptStackTable : 3;
    U16 Reserved_0 : 5;
    U16 Type : 4;
    U16 Privilege : 2;
    U16 Present : 1;
    U16 Reserved_1 : 1;
    U16 Offset_16_31;
    U32 Offset_32_63;
    U32 Reserved_2;
} X86_64_IDT_ENTRY, *LPX86_64_IDT_ENTRY;

/***************************************************************************/

// System segment descriptor (e.g. TSS/LDT) layout for 64-bit mode (16 bytes)

typedef struct tag_X86_64_SYSTEM_SEGMENT_DESCRIPTOR {
    U16 Limit_00_15;
    U16 Base_00_15;
    U8 Base_16_23;
    U8 Type : 4;
    U8 Zero0 : 1;
    U8 Privilege : 2;
    U8 Present : 1;
    U8 Limit_16_19 : 4;
    U8 Available : 1;
    U8 Zero1 : 2;
    U8 Granularity : 1;
    U8 Base_24_31;
    U32 Base_32_63;
    U32 Reserved;
} X86_64_SYSTEM_SEGMENT_DESCRIPTOR, *LPX86_64_SYSTEM_SEGMENT_DESCRIPTOR;

/***************************************************************************/

// Interrupt context saved on entry

typedef struct tag_INTERRUPT_FRAME {
    INTEL_64_GENERAL_REGISTERS Registers;
    INTEL_FPU_REGISTERS FPURegisters;
    U64 SS0;
    U64 RSP0;
    U32 IntNo;
    U32 ErrCode;
} INTERRUPT_FRAME, *LPINTERRUPT_FRAME;

/***************************************************************************/

// Architecture-specific task data

typedef struct tag_ARCH_TASK_DATA {
    INTERRUPT_FRAME Context;
    U64 StackBase;
    UINT StackSize;
    U64 SysStackBase;
    UINT SysStackSize;
} ARCH_TASK_DATA, *LPARCH_TASK_DATA;

/***************************************************************************/

#pragma pack(pop)

/***************************************************************************/

#endif
