
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef I386_H_INCLUDED
#define I386_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

#pragma pack(1)

/***************************************************************************/

typedef struct tag_INTEL386REGISTERS {
    U32 EFlags;
    U32 EAX, EBX, ECX, EDX;
    U32 ESI, EDI, ESP, EBP;
    U32 EIP;
    U16 CS, DS, SS;
    U16 ES, FS, GS;
    U32 CR0, CR2, CR3, CR4;
    U32 DR0, DR1, DR2, DR3;
    U32 DR4, DR5, DR6, DR7;
} INTEL386REGISTERS, *LPINTEL386REGISTERS;

/***************************************************************************/

typedef union tag_X86REGS {
    struct {
        U16 DS;
        U16 ES;
        U16 FS;
        U16 GS;
        U8 AL;
        U8 AH;
        U16 F1;
        U8 BL;
        U8 BH;
        U16 F2;
        U8 CL;
        U8 CH;
        U16 F3;
        U8 DL;
        U8 DH;
        U16 F4;
    } H;
    struct {
        U16 DS;
        U16 ES;
        U16 FS;
        U16 GS;
        U16 AX;
        U16 F1;
        U16 BX;
        U16 F2;
        U16 CX;
        U16 F3;
        U16 DX;
        U16 F4;
        U16 SI;
        U16 F5;
        U16 DI;
        U16 F6;
        U16 FL;
        U16 F9;
    } X;
    struct {
        U16 DS;
        U16 ES;
        U16 FS;
        U16 GS;
        U32 EAX;
        U32 EBX;
        U32 ECX;
        U32 EDX;
        U32 ESI;
        U32 EDI;
        U32 EFL;
    } E;
} X86REGS, *LPX86REGS;

/***************************************************************************/
// The page directory entry
// Size : 4 bytes

typedef struct tag_PAGEDIRECTORY {
    U32 Present : 1;    // Is page present in RAM ?
    U32 ReadWrite : 1;  // Read-write access rights
    U32 Privilege : 1;  // Privilege level
    U32 WriteThrough : 1;
    U32 CacheDisabled : 1;
    U32 Accessed : 1;  // Has page been accessed ?
    U32 Reserved : 1;
    U32 PageSize : 1;  // 0 = 4KB
    U32 Global : 1;    // Ignored
    U32 User : 2;      // Available to OS
    U32 Fixed : 1;     // EXOS : Can page be swapped ?
    U32 Address : 20;  // Physical address
} PAGEDIRECTORY, *LPPAGEDIRECTORY;

/***************************************************************************/
// The page table entry
// Size : 4 bytes

typedef struct tag_PAGETABLE {
    U32 Present : 1;    // Is page present in RAM ?
    U32 ReadWrite : 1;  // Read-write access rights
    U32 Privilege : 1;  // Privilege level
    U32 WriteThrough : 1;
    U32 CacheDisabled : 1;
    U32 Accessed : 1;  // Has page been accessed ?
    U32 Dirty : 1;     // Has been written to ?
    U32 Reserved : 1;  // Reserved by Intel
    U32 Global : 1;
    U32 User : 2;      // Available to OS
    U32 Fixed : 1;     // EXOS : Can page be swapped ?
    U32 Address : 20;  // Physical address
} PAGETABLE, *LPPAGETABLE;

/***************************************************************************/
// The segment descriptor
// Size : 8 bytes

typedef struct tag_SEGMENTDESCRIPTOR {
    U32 Limit_00_15 : 16;   // Bits 0-15 of segment limit
    U32 Base_00_15 : 16;    // Bits 0-15 of segment base
    U32 Base_16_23 : 8;     // Bits 16-23 of segment base
    U32 Accessed : 1;       // Segment has been accessed since OS cleared this flag
    U32 CanWrite : 1;       // Read-only for data segments, Exec-Only for code
                            // segments
    U32 ConformExpand : 1;  // Conforming for code segments, expand-down for
                            // data segments
    U32 Type : 1;           // Data = 0, code = 1
    U32 Segment : 1;        //
    U32 Privilege : 2;      // Privilege level, 0-3
    U32 Present : 1;        //
    U32 Limit_16_19 : 4;    // Bits 16-19 of segment limit
    U32 Available : 1;      //
    U32 Unused : 1;         // Reserved
    U32 OperandSize : 1;    // 0 = 16-bit, 1 = 32-bit
    U32 Granularity : 1;    // 0 = 1 byte granular, 1 = 4096 bytes granular
    U32 Base_24_31 : 8;     // Bits 24-31 of segment base
} SEGMENTDESCRIPTOR, *LPSEGMENTDESCRIPTOR;

/***************************************************************************/
// The Gate Descriptor

typedef struct tag_GATEDESCRIPTOR {
    U32 Offset_00_15 : 16;  // Bits 0-15 of entry point offset
    U32 Selector : 16;      // Selector for code segment
    U32 Reserved : 8;       // Reserved
    U32 Type : 5;           // Type
    U32 Privilege : 2;      // Privilege level
    U32 Present : 1;        //
    U32 Offset_16_31 : 16;  // Bits 16-31 of entry point offset
} GATEDESCRIPTOR, *LPGATEDESCRIPTOR;

/***************************************************************************/
// The TSS descriptor

typedef struct tag_TSSDESCRIPTOR {
    U32 Limit_00_15 : 16;  // Bits 0-15 of segment limit
    U32 Base_00_15 : 16;   // Bits 0-15 of segment base
    U32 Base_16_23 : 8;    // Bits 16-23 of segment base
    U32 Type : 5;          // Must be GATE_TYPE_386_TSS_xxxx
    U32 Privilege : 2;     // Privilege level
    U32 Present : 1;       //
    U32 Limit_16_19 : 4;   // Bits 16-19 of segment limit
    U32 Available : 1;     //
    U32 Unused : 2;        // Reserved
    U32 Granularity : 1;   // 0 = 1 byte granular, 1 = 4096 bytes granular
    U32 Base_24_31 : 8;    // Bits 24-31 of segment base
} TSSDESCRIPTOR, *LPTSSDESCRIPTOR;

/***************************************************************************/
// The Task State Segment
// It must be 256 bytes long

typedef struct tag_TASKSTATESEGMENT {
    U16 BackLink;  // TSS backlink
    U16 Res1;      // Reserved
    U32 ESP0;      // Stack 0 pointer (CPL = 0)
    U16 SS0;       // Stack 0 segment
    U16 Res2;      // Reserved
    U32 ESP1;      // Stack 1 pointer (CPL = 1)
    U16 SS1;       // Stack 1 segment
    U16 Res3;      // Reserved
    U32 ESP2;      // Stack 2 pointer (CPL = 2)
    U16 SS2;       // Stack 2 segment
    U16 Res4;      // Reserved
    U32 CR3;       // Control register 3
    U32 EIP;       // Instruction pointer
    U32 EFlags;    // Extended flags
    U32 EAX;       // EAX general purpose register
    U32 ECX;       // ECX general purpose register
    U32 EDX;       // EDX general purpose register
    U32 EBX;       // EBX general purpose register
    U32 ESP;       // Stack pointer
    U32 EBP;       // Alternate stack pointer
    U32 ESI;       // ESI general purpose register
    U32 EDI;       // EDI general purpose register

    U16 ES;        // ES segment register (Extra segment)
    U16 Res5;      // Reserved

    U16 CS;        // CS segment register (Code segment)
    U16 Res6;      // Reserved

    U16 SS;        // SS segment register (Stack segment)
    U16 Res7;      // Reserved

    U16 DS;        // DS segment register (Data segment)
    U16 Res8;      // Reserved

    U16 FS;        // FS segment register (Extra segment)
    U16 Res9;      // Reserved

    U16 GS;        // GS segment register (Extra segment)
    U16 Res10;     // Reserved

    U16 LDT;       // Local descriptor table segment selector
    U16 Res11;     // Reserved

    U8 Trap;
    U8 Res12;

    U16 IOMap;          // I/O Map Base Address
    U8 IOMapBits[152];  // Map 1024 port adresses
} TASKSTATESEGMENT, *LPTASKSTATESEGMENT;

/************************************************************************/
// NOTE: fields not meaningful for a given trap are set to 0 by the stub.

typedef struct tag_INTERRUPTFRAME {
    INTEL386REGISTERS Registers;    // Filled by the stub
    U32 IntNo;                      // Interrupt / exception vector
    U32 ErrCode;                    // CPU error code (0 for #UD)
} INTERRUPTFRAME, *LPINTERRUPTFRAME;

/************************************************************************/
// The GDT register

typedef struct __attribute__((packed)) {
    U16 Limit;
    U32 Base;
} GDTREGISTER;

/************************************************************************/
// Page directory and page table

#define PAGE_SIZE N_4KB
#define PAGE_SIZE_MUL MUL_4KB
#define PAGE_SIZE_MASK (PAGE_SIZE - 1)

#define PAGE_TABLE_SIZE N_4KB
#define PAGE_TABLE_SIZE_MUL MUL_4KB
#define PAGE_TABLE_SIZE_MASK (PAGE_TABLE_SIZE - 1)

#define PAGE_TABLE_ENTRY_SIZE (sizeof(U32))
#define PAGE_TABLE_NUM_ENTRIES (PAGE_TABLE_SIZE / PAGE_TABLE_ENTRY_SIZE)

#define PAGE_TABLE_CAPACITY (PAGE_TABLE_NUM_ENTRIES * PAGE_SIZE)
#define PAGE_TABLE_CAPACITY_MUL MUL_4MB
#define PAGE_TABLE_CAPACITY_MASK (PAGE_TABLE_CAPACITY - 1)

#define PAGE_MASK (~(PAGE_SIZE - 1))

#define PAGE_PRIVILEGE_KERNEL 0
#define PAGE_PRIVILEGE_USER 1

#define PAGE_ALIGN(a) (((a) + PAGE_SIZE - 1) & PAGE_MASK)

/***************************************************************************/
// Segment descriptor attributes

#define GDT_TYPE_DATA 0x00
#define GDT_TYPE_CODE 0x01
#define GDT_PRIVILEGE_KERNEL 0x00
#define GDT_PRIVILEGE_DRIVERS 0x01
#define GDT_PRIVILEGE_ROUTINES 0x02
#define GDT_PRIVILEGE_USER 0x03
#define GDT_OPERANDSIZE_16 0x00
#define GDT_OPERANDSIZE_32 0x01
#define GDT_GRANULAR_1B 0x00
#define GDT_GRANULAR_4KB 0x01

/***************************************************************************/

#define SEGMENTBASE(PSD)                                                                                      \
    (NULL32 | ((((U32)(PSD)->Base_00_15) & 0xFFFF) << 0x00) | ((((U32)(PSD)->Base_16_23) & 0x00FF) << 0x10) | \
     ((((U32)(PSD)->Base_24_31) & 0x00FF) << 0x18))

#define SEGMENTGRANULAR(PSD) (((PSD)->Granularity == 0x00) ? N_1B : N_4KB)

#define SEGMENTLIMIT(PSD) \
    (NULL32 | ((((U32)(PSD)->Limit_00_15) & 0xFFFF) << 0x00) | ((((U32)(PSD)->Limit_16_19) & 0x000F) << 0x10))

/***************************************************************************/
// Gate descriptor and TSS descriptor attributes

#define GATE_TYPE_286_TSS_AVAIL 0x01
#define GATE_TYPE_LDT 0x02
#define GATE_TYPE_286_TSS_BUSY 0x03
#define GATE_TYPE_CALL 0x04
#define GATE_TYPE_TASK 0x05
#define GATE_TYPE_286_INT 0x06
#define GATE_TYPE_286_TRAP 0x07
#define GATE_TYPE_386_TSS_AVAIL 0x09
#define GATE_TYPE_386_TSS_BUSY 0x0B
#define GATE_TYPE_386_CALL 0x0C
#define GATE_TYPE_386_INT 0x0E
#define GATE_TYPE_386_TRAP 0x0F

/***************************************************************************/

// ----- Selector bitfield layout (x86) -----
// [15:3] Index | [2] TI (0=GDT,1=LDT) | [1:0] RPL
// Constants below remove all magic numbers.

// Number of low bits used by RPL (Requested Privilege Level)
#define SELECTOR_RPL_BITS          2u
#define SELECTOR_RPL_MASK          0x0003u
#define SELECTOR_RPL_SHIFT         0u

// Table Indicator (0=GDT, 1=LDT)
#define SELECTOR_TI_MASK           0x0001u
#define SELECTOR_TI_SHIFT          2u
#define SELECTOR_TABLE_GDT         0u
#define SELECTOR_TABLE_LDT         1u

// Index starts at bit 3
#define SELECTOR_INDEX_SHIFT       3u

// ----- Accessors -----

// Extract index from selector (ignores RPL and TI)
#define SELECTOR_INDEX(sel)        ((U16)(sel) >> SELECTOR_INDEX_SHIFT)

// Extract RPL (requested privilege level)
#define SELECTOR_RPL(sel)          ((U16)(sel) & SELECTOR_RPL_MASK)

// Extract TI (table indicator: 0=GDT, 1=LDT)
#define SELECTOR_TI(sel)           ((((U16)(sel)) >> SELECTOR_TI_SHIFT) & SELECTOR_TI_MASK)

// ----- Builders -----

// Make a selector from index, TI (0=GDT/1=LDT) and RPL (0..3)
#define MAKE_SELECTOR(index, ti, rpl) \
    ( (SELECTOR)( (((U16)(index)) << SELECTOR_INDEX_SHIFT) \
                | ((((U16)(ti))  & SELECTOR_TI_MASK)  << SELECTOR_TI_SHIFT) \
                | (((U16)(rpl)) & SELECTOR_RPL_MASK)) )

// Convenience: selector into GDT with given index and RPL
#define MAKE_GDT_SELECTOR(index, rpl) MAKE_SELECTOR((index), SELECTOR_TABLE_GDT, (rpl))

// Convenience: selector into LDT with given index and RPL
#define MAKE_LDT_SELECTOR(index, rpl) MAKE_SELECTOR((index), SELECTOR_TABLE_LDT, (rpl))

/***************************************************************************/

typedef U16 SELECTOR;
typedef U32 OFFSET;

typedef struct tag_FARPOINTER {
    OFFSET Offset;
    SELECTOR Selector;
} FARPOINTER, *LPFARPOINTER;

/***************************************************************************/
// Privilege levels (rings)

#define PRIVILEGE_KERNEL 0x00
#define PRIVILEGE_DRIVERS 0x01
#define PRIVILEGE_ROUTINES 0x02
#define PRIVILEGE_USER 0x03

/***************************************************************************/
// Values related to CPUID

// Processor model masks and shifts

#define INTEL_CPU_MASK_STEPPING 0x0000000F
#define INTEL_CPU_MASK_MODEL 0x000000F0
#define INTEL_CPU_MASK_FAMILY 0x00000F00
#define INTEL_CPU_MASK_TYPE 0x00003000

#define INTEL_CPU_SHFT_STEPPING 0x00
#define INTEL_CPU_SHFT_MODEL 0x04
#define INTEL_CPU_SHFT_FAMILY 0x08
#define INTEL_CPU_SHFT_TYPE 0x0C

// Processor type

#define INTEL_CPU_TYPE_OEM 0x00
#define INTEL_CPU_TYPE_OVERDRIVE 0x01
#define INTEL_CPU_TYPE_DUAL 0x02
#define INTEL_CPU_TYPE_RESERVED 0x03

// Processor features

#define INTEL_CPU_FEAT_FPU 0x00000001   // Floating-Point Unit on Chip
#define INTEL_CPU_FEAT_VME 0x00000002   // Virtual-8086 Mode Enhancements
#define INTEL_CPU_FEAT_DE 0x00000004    // Debugging Extensions
#define INTEL_CPU_FEAT_PSE 0x00000008   // Page Size Extensions
#define INTEL_CPU_FEAT_TSC 0x00000010   // Time Stamp Counter
#define INTEL_CPU_FEAT_MSR 0x00000020   // Model Specific Registers
#define INTEL_CPU_FEAT_PAE 0x00000040   // Physical Address Extension
#define INTEL_CPU_FEAT_MCE 0x00000080   // Machine Check Exception
#define INTEL_CPU_FEAT_CX8 0x00000100   // CMPXCHG8B Instruction
#define INTEL_CPU_FEAT_APIC 0x00000200  // Advanced Programmable Interrupt Controller
#define INTEL_CPU_FEAT_RES1 0x00000400  // Reserved
#define INTEL_CPU_FEAT_RES2 0x00000800  // Reserved
#define INTEL_CPU_FEAT_MTRR 0x00001000  // Memory Type Range Registers
#define INTEL_CPU_FEAT_PGE 0x00002000   //
#define INTEL_CPU_FEAT_MCA 0x00004000   // Machine Check Architecture
#define INTEL_CPU_FEAT_CMOV 0x00008000  // Conditional Move and Compare Instructions
#define INTEL_CPU_FEAT_RES3 0x00010000  // Reserved
#define INTEL_CPU_FEAT_RES4 0x00020000  // Reserved
#define INTEL_CPU_FEAT_RES5 0x00040000  // Reserved
#define INTEL_CPU_FEAT_RES6 0x00080000  // Reserved
#define INTEL_CPU_FEAT_RES7 0x00100000  // Reserved
#define INTEL_CPU_FEAT_RES8 0x00200000  // Reserved
#define INTEL_CPU_FEAT_RESA 0x00400000  // Reserved
#define INTEL_CPU_FEAT_MMX 0x00800000   // MMX Technology
#define INTEL_CPU_FEAT_RESB 0x01000000  // Reserved
#define INTEL_CPU_FEAT_RESC 0x02000000  // Reserved
#define INTEL_CPU_FEAT_RESD 0x04000000  // Reserved
#define INTEL_CPU_FEAT_RESE 0x08000000  // Reserved
#define INTEL_CPU_FEAT_RESF 0x10000000  // Reserved
#define INTEL_CPU_FEAT_RESG 0x20000000  // Reserved
#define INTEL_CPU_FEAT_RESH 0x40000000  // Reserved
#define INTEL_CPU_FEAT_RESI 0x80000000  // Reserved

/***************************************************************************/
// Exception and interrupt numbers

#define INT_DIVIDE 0
#define INT_DEBUG 1
#define INT_NMI 2
#define INT_BREAKPOINT 3
#define INT_OVERFLOW 4
#define INT_BOUNDS 5
#define INT_OPCODE 6
#define INT_MATHGONE 7
#define INT_DOUBLE 8
#define INT_MATHOVER 9
#define INT_TSS 10
#define INT_SEGMENT 11
#define INT_STACK 12
#define INT_GENERAL 13
#define INT_PAGE 14
#define INT_RESERVED15 15
#define INT_MATHERR 16
#define INT_RESERVED17 17
#define INT_RESERVED18 18
#define INT_RESERVED19 19
#define INT_RESERVED20 20
#define INT_RESERVED21 21
#define INT_RESERVED22 22
#define INT_RESERVED23 23
#define INT_RESERVED24 24
#define INT_RESERVED25 25
#define INT_RESERVED26 26
#define INT_RESERVED27 27
#define INT_RESERVED28 28
#define INT_RESERVED29 29
#define INT_RESERVED30 30
#define INT_RESERVED31 31
#define INT_KERNELCLOCK 32
#define INT_KEYBOARD 33
#define INT_UNUSED34 34
#define INT_UNUSED35 35
#define INT_UNUSED36 36
#define INT_UNUSED37 37
#define INT_UNUSED38 38
#define INT_UNUSED39 39
#define INT_UNUSED40 40
#define INT_UNUSED41 41
#define INT_UNUSED42 42
#define INT_UNUSED43 43
#define INT_UNUSED44 44
#define INT_UNUSED45 45
#define INT_UNUSED46 46
#define INT_UNUSED47 47

/***************************************************************************/
// Bit layout of the EFlags register

#define EFLAGS_CF 0x00000001  // Carry flag
#define EFLAGS_A1 0x00000002  // Always 1
#define EFLAGS_PF 0x00000004  // Parity flag
#define EFLAGS_RES1 0x00000008
#define EFLAGS_AF 0x00000010
#define EFLAGS_RES2 0x00000020
#define EFLAGS_ZF 0x00000040  // Zero flag
#define EFLAGS_SF 0x00000080  // Sign flag
#define EFLAGS_TF 0x00000100  // Trap flag
#define EFLAGS_IF 0x00000200  // Interrupt flag
#define EFLAGS_RES3 0x00000400
#define EFLAGS_OF 0x00000800     // Overflow flag
#define EFLAGS_IOPL1 0x00001000  // IO privilege level bit 1
#define EFLAGS_IOPL2 0x00002000  // IO privilege level bit 2
#define EFLAGS_NT 0x00004000     // Nested task
#define EFLAGS_RES4 0x00008000
#define EFLAGS_RF 0x00010000  // Resume flag
#define EFLAGS_VM 0x00020000  // Virtual 8086 mode
#define EFLAGS_RES5 0x00040000
#define EFLAGS_RES6 0x00080000
#define EFLAGS_RES7 0x00100000
#define EFLAGS_RES8 0x00200000
#define EFLAGS_RES9 0x00400000
#define EFLAGS_RES10 0x00800000
#define EFLAGS_RES11 0x01000000
#define EFLAGS_RES12 0x02000000
#define EFLAGS_RES13 0x04000000
#define EFLAGS_RES14 0x08000000
#define EFLAGS_RES15 0x10000000
#define EFLAGS_RES16 0x20000000
#define EFLAGS_RES17 0x40000000
#define EFLAGS_RES18 0x80000000

/***************************************************************************/
// Bit layout of CR0 (Control register 0)

#define CR0_PROTECTED_MODE 0x00000001           // Protected mode on/off
#define CR0_COPROCESSOR 0x00000002              // Math present
#define CR0_MONITOR_COPROCESSOR 0x00000004      // Emulate co-processor
#define CR0_TASKSWITCH 0x00000008               // Set on task switch
#define CR0_80387 0x00000010                    // Type of co-processor
#define CR0_PAGING 0x80000000                   // Paging on/off

// CR2 = Faulty linear address in case of page fault
// CR3 = Physical address of page directory

/***************************************************************************/
// Interrupt command port

#define INTERRUPT_COMMAND 0x0020

#define MAX_IRQ 16

/***************************************************************************/
// CMOS

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
// BIOS

#define BIOS_E820_TYPE_USABLE       1
#define BIOS_E820_TYPE_RESERVED     2
#define BIOS_E820_TYPE_ACPI         3
#define BIOS_E820_TYPE_ACPI_NVS     4
#define BIOS_E820_TYPE_BAD_MEM      5

/***************************************************************************/
// Clock ports
// 8253 chip

#define CLOCK_DATA 0x0040
#define CLOCK_COMMAND 0x0043

/***************************************************************************/
// Keyboard ports
// 8042 chip

#define KEYBOARD_IRQ 0x01

#define KEYBOARD_DATA 0x0060     // Keyboard data port
#define KEYBOARD_COMMAND 0x0064  // Keyboard command port

// Keyboard status register bit values

#define KSR_OUT_FULL 0x01
#define KSR_IN_FULL 0x02
#define KSR_COMMAND 0x08
#define KSR_ACTIVE 0x10
#define KSR_OUT_ERROR 0x20
#define KSR_IN_ERROR 0x40
#define KSR_PARITY_ERROR 0x80

// Keyboard LED bit values

#define KSL_SCROLL 0x01
#define KSL_NUM 0x02
#define KSL_CAPS 0x04

// Keyboard commands

#define KSC_READ_MODE 0x20
#define KSC_WRITE_MODE 0x60
#define KSC_SELF_TEST 0xAA
#define KSC_ENABLE 0xAE
#define KSC_SETLEDSTATUS 0xED

#define KSS_ACK 0xFA

/***************************************************************************/
// Static low memory pages

#define LOW_MEMORY_PAGE_1   0x1000      // Reserved by VBR system structures
#define LOW_MEMORY_PAGE_2   0x2000      // Reserved by VBR system structures
#define LOW_MEMORY_PAGE_3   0x3000      // Reserved by VBR system structures
#define LOW_MEMORY_PAGE_4   0x4000      //
#define LOW_MEMORY_PAGE_5   0x5000      // RMC code base
#define LOW_MEMORY_PAGE_6   0x6000      // RMC buffers
#define LOW_MEMORY_PAGE_7   0x7000
#define LOW_MEMORY_PAGE_8   0x8000

/***************************************************************************/
// Structure to receive information about a segment in a more friendly way

typedef struct tag_SEGMENTINFO {
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

/************************************************************************/

BOOL GetSegmentInfo(LPSEGMENTDESCRIPTOR This, LPSEGMENTINFO Info);
BOOL SegmentInfoToString(LPSEGMENTINFO This, LPSTR Text);

/************************************************************************/

#endif
