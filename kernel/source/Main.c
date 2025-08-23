
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

    //-------------------------------------
    // No more interrupts

    DisableInterrupts();

    //-------------------------------------
    // Gather startup information

    KernelStartup.StubAddress = 0x20000;
    KernelStartup.PageDirectory = GetPageDirectory();
    KernelStartup.IRQMask_21_RM = 0;
    KernelStartup.IRQMask_A1_RM = 0;
    KernelStartup.MemorySize = N_128MB;
    KernelStartup.PageCount = KernelStartup.MemorySize >> MUL_4KB;
    KernelStartup.E820_Count = 0;

    //-------------------------------------
    // Init the kernel logger

    InitKernelLog();
    KernelLogText(LOG_VERBOSE, TEXT("[KernelMain] Kernel logger initialized"));

    //-------------------------------------
    // Initialize the memory manager

    InitializeMemoryManager();
    KernelLogText(LOG_VERBOSE, TEXT("[KernelMain] Memory manager initialized"));

    //--------------------------------------
    // Main intialization routine

    InitializeKernel();

    //--------------------------------------
    // Enter the idle task

    while(1) {
        IdleCPU();
    }
}

/***************************************************************************/
