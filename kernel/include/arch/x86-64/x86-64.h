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
// Descriptor and selector helpers
/***************************************************************************/

#define IDT_SIZE N_4KB
#define GDT_SIZE N_8KB
#define NUM_INTERRUPTS 48
#define NUM_TASKS 128

#define BIOS_E820_TYPE_USABLE 1
#define BIOS_E820_TYPE_RESERVED 2
#define BIOS_E820_TYPE_ACPI 3
#define BIOS_E820_TYPE_ACPI_NVS 4
#define BIOS_E820_TYPE_BAD_MEM 5

#define GATE_TYPE_386_INT 0x0E
#define GATE_TYPE_386_TRAP 0x0F

#define SELECTOR_RPL_BITS 2u
#define SELECTOR_RPL_MASK 0x0003u
#define SELECTOR_RPL_SHIFT 0u

#define SELECTOR_TI_MASK 0x0001u
#define SELECTOR_TI_SHIFT 2u
#define SELECTOR_TABLE_GDT 0u
#define SELECTOR_TABLE_LDT 1u

#define SELECTOR_INDEX_SHIFT 3u

#define SELECTOR_INDEX(sel) ((U16)(sel) >> SELECTOR_INDEX_SHIFT)
#define SELECTOR_RPL(sel) ((U16)(sel) & SELECTOR_RPL_MASK)
#define SELECTOR_TI(sel) ((((U16)(sel)) >> SELECTOR_TI_SHIFT) & SELECTOR_TI_MASK)

#define MAKE_SELECTOR(index, ti, rpl) \
    ((U16)((((U16)(index)) << SELECTOR_INDEX_SHIFT) | ((((U16)(ti)) & SELECTOR_TI_MASK) << SELECTOR_TI_SHIFT) | \
            (((U16)(rpl)) & SELECTOR_RPL_MASK)))
#define MAKE_GDT_SELECTOR(index, rpl) MAKE_SELECTOR((index), SELECTOR_TABLE_GDT, (rpl))
#define MAKE_LDT_SELECTOR(index, rpl) MAKE_SELECTOR((index), SELECTOR_TABLE_LDT, (rpl))

#define SELECTOR_GLOBAL 0x00
#define SELECTOR_LOCAL 0x04

#define SELECTOR_NULL 0x00
#define SELECTOR_KERNEL_CODE (0x08 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_KERNEL_DATA (0x10 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_USER_CODE (0x18 | SELECTOR_GLOBAL | PRIVILEGE_USER)
#define SELECTOR_USER_DATA (0x20 | SELECTOR_GLOBAL | PRIVILEGE_USER)
#define SELECTOR_REAL_CODE (0x28 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)
#define SELECTOR_REAL_DATA (0x30 | SELECTOR_GLOBAL | PRIVILEGE_KERNEL)

#define RFLAGS_ALWAYS_1 ((U64)1 << 1)
#define RFLAGS_IF ((U64)1 << 9)

/***************************************************************************/

#define TRACED_FUNCTION
#define TRACED_EPILOGUE(FunctionName)

#define SwitchToNextTask_2(prev, next) ((void)(prev), (void)(next))
#define ArchPrepareNextTaskSwitch(CurrentTask, NextTask) ((void)(CurrentTask), (void)(NextTask))
#define SetupStackForKernelMode(Task, StackTop) ((void)(Task), (void)(StackTop))
#define JumpToReadyTask(Task, StackTop) ((void)(Task), (void)(StackTop))
#define SetupStackForUserMode(Task, StackTop, UserESP) ((void)(Task), (void)(StackTop), (void)(UserESP))

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

typedef struct tag_SEGMENT_DESCRIPTOR {
    U32 Limit_00_15 : 16;
    U32 Base_00_15 : 16;
    U32 Base_16_23 : 8;
    U32 Accessed : 1;
    U32 CanWrite : 1;
    U32 ConformExpand : 1;
    U32 Type : 1;
    U32 Segment : 1;
    U32 Privilege : 2;
    U32 Present : 1;
    U32 Limit_16_19 : 4;
    U32 Available : 1;
    U32 Unused : 1;
    U32 OperandSize : 1;
    U32 Granularity : 1;
    U32 Base_24_31 : 8;
} SEGMENT_DESCRIPTOR, *LPSEGMENT_DESCRIPTOR;

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

typedef struct tag_GDT_REGISTER {
    U16 Limit;
    U64 Base;
} GDT_REGISTER;

/***************************************************************************/

typedef U16 SELECTOR;
typedef U64 OFFSET;

typedef struct tag_KERNELDATA_X86_64 {
    LPX86_64_IDT_ENTRY IDT;
    LPVOID GDT;
    LPVOID TSS;
    LPPAGEBITMAP PPB;
} KERNELDATA_X86_64, *LPKERNELDATA_X86_64;

/***************************************************************************/

extern KERNELDATA_X86_64 Kernel_i386;

struct tag_TASK;
struct tag_PROCESS;
struct tag_TASKINFO;

BOOL SetupTask(struct tag_TASK* Task, struct tag_PROCESS* Process, struct tag_TASKINFO* Info);

/***************************************************************************/

#endif
