
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/System.h"

/***************************************************************************/

KERNELSTARTUPINFO KernelStartup = {
    .IRQMask_21_PM = 0x000000FB,
    .IRQMask_A1_PM = 0x000000FF,
    .IRQMask_21_RM = 0,
    .IRQMask_A1_RM = 0
};

// The entry point in paged protected mode

void KernelMain(void) {
    U32 ImageAddress;
    U8 CursorX;
    U8 CursorY;

    __asm__ __volatile__("movl %%eax, %0" : "=r"(ImageAddress));
    __asm__ __volatile__("movb %%bl, %0" : "=r"(CursorX));
    __asm__ __volatile__("movb %%bh, %0" : "=r"(CursorY));

    //--------------------------------------
    // Main initialization routine

    InitializeKernel(ImageAddress, CursorX, CursorY);

    //--------------------------------------
    // Enter the idle task

    while(1) {
        IdleCPU();
    }
}

/***************************************************************************/
