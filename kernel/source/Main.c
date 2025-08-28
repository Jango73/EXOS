
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\************************************************************************/

#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/System.h"

/************************************************************************/

extern LINEAR __bss_init_start;
extern LINEAR __bss_init_end;

KERNELSTARTUPINFO KernelStartup = {
    .IRQMask_21_PM = 0x000000FB, .IRQMask_A1_PM = 0x000000FF, .IRQMask_21_RM = 0, .IRQMask_A1_RM = 0};

/************************************************************************/
// The entry point in paged protected mode

void KernelMain(void) {
    U32 ImageAddress;
    U8 CursorX;
    U8 CursorY;
    U32 E820Ptr;
    U32 E820Entries;

    __asm__ __volatile__("movl %%eax, %0" : "=m"(ImageAddress));
    __asm__ __volatile__("movb %%bl, %0" : "=m"(CursorX));
    __asm__ __volatile__("movb %%bh, %0" : "=m"(CursorY));
    __asm__ __volatile__("movl %%esi, %0" : "=m"(E820Ptr));
    __asm__ __volatile__("movl %%ecx, %0" : "=m"(E820Entries));

    if (E820Entries > 0 && E820Entries < (N_4KB / sizeof(E820ENTRY))) {
        MemoryCopy(KernelStartup.E820, (LPE820ENTRY)E820Ptr, E820Entries * sizeof(E820ENTRY));
        KernelStartup.E820_Count = E820Entries;
    }

    //-------------------------------------
    // Clear the BSS

    LINEAR BSSStart = (LINEAR)(&__bss_init_start);
    LINEAR BSSEnd = (LINEAR)(&__bss_init_end);
    U32 BSSSize = BSSEnd - BSSStart;
    MemorySet((LPVOID)BSSStart, 0, BSSSize);

    //--------------------------------------
    // Main initialization routine

    InitializeKernel(ImageAddress, CursorX, CursorY);

    //--------------------------------------
    // Enter the idle task

    while (1) {
        IdleCPU();
    }
}
