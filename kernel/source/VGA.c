
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "../include/VGA.h"

#include "../include/System.h"

/***************************************************************************/

#define VGA_ATTR 0x03C0
#define VGA_MISC 0x03C2
#define VGA_ENAB 0x03C3
#define VGA_SEQ 0x03C4
#define VGA_GFX 0x03CE
#define VGA_CRTC 0x03D4
#define VGA_STAT 0x03DA

/***************************************************************************/

#define REGOFS_SEQ 0
#define REGOFS_MISC 5
#define REGOFS_CRTC 6
#define REGOFS_ATTR 31
#define REGOFS_GFX 51

/***************************************************************************/

void VGAIODelay() {
    U32 Index, Data;

    for (Index = 0; Index < 10; Index++) Data = Index;
}

/***************************************************************************/

static U32 SendModeRegs(U8* Regs) {
    U32 Index;

    OutPortByte(VGA_MISC, Regs[REGOFS_MISC]);

    OutPortByte(VGA_STAT, 0);

    //-------------------------------------
    // Send SEQ regs

    for (Index = 0; Index < 5; Index++) {
        OutPortByte(VGA_SEQ, Index);
        VGAIODelay();
        OutPortByte(VGA_SEQ + 1, Regs[REGOFS_SEQ + Index]);
        VGAIODelay();
    }

    //-------------------------------------
    // Clear protection bits

    OutPortWord(VGA_CRTC, (((0x0E & 0x7F) << 8) | 0x11));
    VGAIODelay();

    //-------------------------------------
    // Send CRTC regs

    for (Index = 0; Index < 25; Index++) {
        OutPortByte(VGA_CRTC, Index);
        VGAIODelay();
        OutPortByte(VGA_CRTC + 1, Regs[REGOFS_CRTC + Index]);
        VGAIODelay();
    }

    //-------------------------------------
    // Send GFX regs

    for (Index = 0; Index < 9; Index++) {
        OutPortByte(VGA_GFX, Index);
        VGAIODelay();
        OutPortByte(VGA_GFX + 1, Regs[REGOFS_GFX + Index]);
        VGAIODelay();
    }

    //-------------------------------------
    // Send ATTR regs

    for (Index = 0; Index < 20; Index++) {
        InPortWord(VGA_ATTR);
        VGAIODelay();
        OutPortByte(VGA_ATTR, Index);
        VGAIODelay();
        OutPortByte(VGA_ATTR, Regs[REGOFS_ATTR + Index]);
        VGAIODelay();
    }

    return 0;
}

/***************************************************************************/

void TestVGA() { SendModeRegs(VGAModeRegs[0].Regs); }

/***************************************************************************/
