
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/System.h"

/***************************************************************************/

static void KernelIdle(void) {
    // For now, kernel task sleeps
    while(1) {
        IdleCPU();
    }
}

/***************************************************************************/

// The entry point in protected mode

void KernelMain(void) {
    InitKernelLog();

    //--------------------------------------
    // Main intialization routine

    InitializeKernel();

    //--------------------------------------
    // Enter the idle task

    KernelIdle();
}

/***************************************************************************/
