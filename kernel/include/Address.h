
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef ADDRESS_H_INCLUDED
#define ADDRESS_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "I386.h"

/***************************************************************************/

// Static physical and linear addresses
// All processes have the following address space layout

#define LA_RAM 0x00000000        // Reserved for kernel
#define LA_VIDEO 0x000A0000      // Reserved for kernel
#define LA_CONSOLE 0x000B8000    // Reserved for kernel
#define LA_USER 0x00400000       // Start of user address space
#define LA_LIBRARY 0xA0000000    // Dynamic Libraries
#define LA_KERNEL 0xC0000000     // Kernel
#define LA_RAMDISK 0xF8000000    // RAM disk memory
#define LA_SYSTEM 0xFF400000     // IDT, GDT, etc...
#define LA_DIRECTORY 0xFF800000  // Page Directory of current process
#define LA_SYSTABLE 0xFF801000   // Page that maps FF800000+ addresses
#define LA_PAGETABLE 0xFF802000  // First page table of current process
#define LA_TEMP 0xFFBFF000       // Temporary page used by Memory.c

/***************************************************************************\

    Physical memory layout

    address         Size            Description

    00000000        00001000        BIOS Ram
    000A0000        00010000        VGA memory
    000B0000        00010000        Text memory
    000C0000        00010000        BIOS Extension
    000D0000        00010000        BIOS Extension
    000E0000        00010000        BIOS Extension
    000F0000        00010000        BIOS ROM
    00100000        00001000        Interrupt Descriptor Table
    00101000        00002000        Global Descriptor Table
    00103000        00001000        Kernel Page Directory
    00104000        00001000        Kernel System Page Table
    00105000        00001000        Kernel Page Table
    00106000        00001000        Low Memory Page Table
    00107000        00001000        High Memory Page Table
    00108000        00008000        Task State Segment Area
    00110000        00020000        Physical Page Bitmap
    00140000        ?               Kernel Code and Data

\***************************************************************************/

#define STK_SIZE N_32KB           // Kernel Stack Size

// This structure resides at startup in Stub.asm, and is later copied to kernel area.

typedef struct __attribute__((packed)) tag_E820ENTRY {
    U64 Base;
    U64 Size;
    U32 Type;
    U32 Attributes;
} E820ENTRY, *LPE820ENTRY;

typedef struct __attribute__((packed)) tag_KERNELSTARTUPINFO {
    U32 Loader_SS;
    U32 Loader_SP;
    U32 IRQMask_21_RM;
    U32 IRQMask_A1_RM;
    U32 ConsoleWidth;
    U32 ConsoleHeight;
    U32 ConsoleCursorX;
    U32 ConsoleCursorY;
    U32 MemorySize;         // Total memory size in bytes
    U32 PageCount;          // Total memory size in pages (4K)
    U32 StubSize;           // Size in bytes of the stub
    U32 SI_Size_LOW;        // Low Memory Area Size
    U32 SI_Size_HMA;        // High Memory Area Size
    U32 SI_Size_IDT;        // Interrupt Descriptor Table Size
    U32 SI_Size_GDT;        // Kernel Global Descriptor Table Size
    U32 SI_Size_PGD;        // Kernel Page Directory Size
    U32 SI_Size_PGS;        // System Page Table Size
    U32 SI_Size_PGK;        // Kernel Page Table Size
    U32 SI_Size_PGL;        // Low Memory Page Table size
    U32 SI_Size_PGH;        // High Memory Page Table size
    U32 SI_Size_TSS;        // Task State Segment Size
    U32 SI_Size_PPB;        // Physical Page Bitmap Size
    U32 SI_Size_KER;        // Kernel image size (padded)
    U32 SI_Size_BSS;        // Kernel BSS Size
    U32 SI_Size_STK;        // Kernel Stack Size
    U32 SI_Size_SYS;        // Total system size (IDT -> STK)
    U32 SI_Phys_LOW;        // Memory start
    U32 SI_Phys_HMA;        // Physical address of High Memory Area
    U32 SI_Phys_IDT;        // Physical address of Interrupt Descriptor Table
    U32 SI_Phys_GDT;        // Physical address of Kernel Global Descriptor Table
    U32 SI_Phys_PGD;        // Physical address of Kernel Page Directory
    U32 SI_Phys_PGS;        // Physical address of System Page Table
    U32 SI_Phys_PGK;        // Physical address of Kernel Page Table
    U32 SI_Phys_PGL;        // Physical address of Low Memory Page Table
    U32 SI_Phys_PGH;        // Physical address of High Memory Page Table
    U32 SI_Phys_TSS;        // Physical address of Task State Segment
    U32 SI_Phys_PPB;        // Physical address of Physical Page Bitmap
    U32 SI_Phys_KER;        // Physical address of Kernel
    U32 SI_Phys_BSS;        // Kernel BSS Size
    U32 SI_Phys_STK;        // Kernel Stack Size
    U32 SI_Phys_SYS;        // Physical address of system (IDT)
    U32 E820_Count;         // BIOS E820 function entries
    E820ENTRY E820 [N_4KB / sizeof(E820ENTRY)];
} KERNELSTARTUPINFO, *LPKERNELSTARTUPINFO;

extern KERNELSTARTUPINFO KernelStartup;

#define KERNEL_STARTUP_INFO_OFFSET 32

/***************************************************************************/
// Virtual addresses

#define IDT_SIZE N_4KB
#define GDT_SIZE N_8KB

#define LA_IDT LA_SYSTEM
#define LA_GDT (LA_IDT + KernelStartup.SI_Size_IDT)
#define LA_PGD (LA_GDT + KernelStartup.SI_Size_GDT)
#define LA_PGS (LA_PGD + KernelStartup.SI_Size_PGD)
#define LA_PGK (LA_PGS + KernelStartup.SI_Size_PGS)
#define LA_PGL (LA_PGK + KernelStartup.SI_Size_PGK)
#define LA_PGH (LA_PGL + KernelStartup.SI_Size_PGL)
#define LA_TSS (LA_PGH + KernelStartup.SI_Size_PGH)
#define LA_PPB (LA_TSS + KernelStartup.SI_Size_TSS)

#define LA_KERNEL_STACK (LA_KERNEL + (KernelStartup.SI_Phys_STK - KernelStartup.SI_Phys_KER))

/***************************************************************************/

#endif
