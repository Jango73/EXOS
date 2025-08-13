
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/System.h"

/***************************************************************************/

static void KernelIdle() {
    // For now, kernel task sleeps
    while(1) {
        IdleCPU();
    }
}

/***************************************************************************/

// The entry point in protected mode

void KernelMain() {
    InitKernelLog();
    KernelLogText(LOG_DEBUG, TEXT("[KernelMain] Calling InitializeKernel"));

    //--------------------------------------
    // Main intialization routine

    InitializeKernel();

    //--------------------------------------
    // Enter the idle task

    KernelIdle();
}

/***************************************************************************/
