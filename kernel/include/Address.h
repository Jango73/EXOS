
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
#define LA_TEMP 0xFFBFF000       // Temporary page used by VMM

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

#define LOW_SIZE N_1MB // Low Memory Area Size
#define HMA_SIZE N_128KB // High Memory Area Size
#define IDT_SIZE N_4KB // Interrupt Descriptor Table Size
#define GDT_SIZE N_8KB // Kernel Global Descriptor Size
#define PGD_SIZE PAGE_TABLE_SIZE // Kernel Page Directory Size
#define PGS_SIZE PAGE_TABLE_SIZE // System Page Table Size
#define PGK_SIZE PAGE_TABLE_SIZE // Kernel Page Table Size
#define PGL_SIZE PAGE_TABLE_SIZE // Low Memory Page Table size
#define PGH_SIZE PAGE_TABLE_SIZE // High Memory Page Table size
#define TSS_SIZE N_32KB // Task State Segment Size
#define PPB_SIZE N_128KB // Physical Page Bitmap Size
#define KER_SIZE N_128KB // Kernel code and data size
#define BSS_SIZE N_4KB // Kernel BSS Size
#define STK_SIZE N_32KB // Kernel Stack Size
#define SYS_SIZE (IDT_SIZE + GDT_SIZE + PGD_SIZE + PGS_SIZE + PGK_SIZE + PGL_SIZE + PGH_SIZE + TSS_SIZE + PPB_SIZE + KER_SIZE)

#define SYS_SIZE_PAGES (SYS_SIZE >> PAGE_SIZE_MUL)

#define PA_LOW 0x00000000
#define PA_HMA (PA_LOW + LOW_SIZE)
#define PA_IDT (PA_HMA + HMA_SIZE)
#define PA_GDT (PA_IDT + IDT_SIZE)
#define PA_PGD (PA_GDT + GDT_SIZE)
#define PA_PGS (PA_PGD + PGD_SIZE)
#define PA_PGK (PA_PGS + PGS_SIZE)
#define PA_PGL (PA_PGK + PGK_SIZE)
#define PA_PGH (PA_PGL + PGL_SIZE)
#define PA_TSS (PA_PGH + PGH_SIZE)
#define PA_PPB (PA_TSS + TSS_SIZE)
#define PA_KER (PA_PPB + PPB_SIZE)
#define PA_BSS (PA_KER + KER_SIZE)
#define PA_STK (PA_BSS + BSS_SIZE)

#define PA_SYS PA_IDT

#define LA_IDT LA_SYSTEM
#define LA_GDT (LA_IDT + IDT_SIZE)
#define LA_PGD (LA_GDT + GDT_SIZE)
#define LA_PGS (LA_PGD + PGD_SIZE)
#define LA_PGK (LA_PGS + PGS_SIZE)
#define LA_PGL (LA_PGK + PGK_SIZE)
#define LA_PGH (LA_PGL + PGL_SIZE)
#define LA_TSS (LA_PGH + PGH_SIZE)
#define LA_PPB (LA_TSS + TSS_SIZE)

#define LA_KERNEL_STACK (LA_KERNEL + (PA_STK - PA_KER))

/***************************************************************************/

#endif
