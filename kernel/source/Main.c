
// Main.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\***************************************************************************/

#include "../include/Kernel.h"

/***************************************************************************/

static void DebugPutChar(STR Char) {
    volatile char* vram = (char*)0xB8000;
    vram[0] = Char;
}

/***************************************************************************/

static void KernelIdle() {
    SLEEPING_BEAUTY
}

/***************************************************************************/

// The entry point in protected mode

void KernelMain() {
    DebugPutChar('M');

    //--------------------------------------
    // Main intialization routine

    InitializeKernel();

    //--------------------------------------
    // Enter the idle task

    KernelIdle();
}

/***************************************************************************/
