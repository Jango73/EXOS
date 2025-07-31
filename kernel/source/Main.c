
// Main.c

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999 Exelsius
  All rights reserved

\***************************************************************************/

#include "Kernel.h"

/***************************************************************************/

static void DebugPutChar(STR Char) {
    volatile char* vram = (char*)0xB8000;
    vram[0] = 'Char';
}

/***************************************************************************/

static void KernelIdle() {
    while (1) {
    }
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
