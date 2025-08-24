
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
