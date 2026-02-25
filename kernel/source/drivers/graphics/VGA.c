
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.


    VGA

\************************************************************************/

#include "drivers/graphics/VGA.h"

#include "System.h"

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

/**
 * @brief Busy-wait IO delay used between VGA port writes.
 */
void VGAIODelay(void) {
    U32 Index, Data;
    for (Index = 0; Index < 10; Index++) Data = Index;
    UNUSED(Data);
}

/***************************************************************************/

/**
 * @brief Reset the attribute controller flip-flop.
 */
static void VGAResetAttributeFlipFlop(void) {
    InPortByte(VGA_STAT);
    VGAIODelay();
}

/***************************************************************************/

/**
 * @brief Program VGA registers for a display mode.
 *
 * @param Regs Pointer to register array describing the mode.
 * @return 0 on completion.
 */
static U32 SendModeRegs(U8* Regs) {
    U32 Index;

    OutPortByte(VGA_MISC, Regs[REGOFS_MISC]);

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
        VGAResetAttributeFlipFlop();
        OutPortByte(VGA_ATTR, Index);
        VGAIODelay();
        OutPortByte(VGA_ATTR, Regs[REGOFS_ATTR + Index]);
        VGAIODelay();
    }

    VGAResetAttributeFlipFlop();
    OutPortByte(VGA_ATTR, 0x20);

    return 0;
}

/***************************************************************************/

/**
 * @brief Compute text mode metadata from VGA registers.
 * @param Regs Pointer to register array describing the mode.
 * @param Info Output metadata structure.
 * @return TRUE on success, FALSE on invalid parameters.
 */
static BOOL ComputeTextModeInfo(U8* Regs, LPVGAMODEINFO Info) {
    U32 Overflow;
    U32 VerticalDisplayEnd;
    U32 CharHeight;

    if (Regs == NULL || Info == NULL) return FALSE;

    Info->Columns = (U32)Regs[REGOFS_CRTC + 1] + 1;

    Overflow = (U32)Regs[REGOFS_CRTC + 7];
    VerticalDisplayEnd = (U32)Regs[REGOFS_CRTC + 0x12];
    VerticalDisplayEnd |= (Overflow & 0x02) << 7;
    VerticalDisplayEnd |= (Overflow & 0x40) << 3;

    CharHeight = (U32)(Regs[REGOFS_CRTC + 0x09] & 0x1F) + 1;
    if (CharHeight == 0) return FALSE;

    Info->CharHeight = CharHeight;
    Info->Rows = (VerticalDisplayEnd + 1) / CharHeight;

    return Info->Columns > 0 && Info->Rows > 0;
}

/***************************************************************************/

/**
 * @brief Return the number of VGA modes in the table.
 * @return Count of VGA modes.
 */
U32 VGAGetModeCount(void) { return VGAModeRegsCount; }

/***************************************************************************/

/**
 * @brief Retrieve text mode information for a VGA mode index.
 * @param ModeIndex Index in the VGA mode table.
 * @param Info Output mode information.
 * @return TRUE on success, FALSE on invalid parameters.
 */
BOOL VGAGetModeInfo(U32 ModeIndex, LPVGAMODEINFO Info) {
    if (ModeIndex >= VGAModeRegsCount) return FALSE;
    return ComputeTextModeInfo(VGAModeRegs[ModeIndex].Regs, Info);
}

/***************************************************************************/

/**
 * @brief Find a VGA text mode by columns and rows.
 * @param Columns Desired text columns.
 * @param Rows Desired text rows.
 * @param ModeIndex Output index on success.
 * @return TRUE if a matching mode was found.
 */
BOOL VGAFindTextMode(U32 Columns, U32 Rows, U32* ModeIndex) {
    U32 Index;
    VGAMODEINFO Info;

    if (ModeIndex == NULL) return FALSE;

    for (Index = 0; Index < VGAModeRegsCount; Index++) {
        if (VGAGetModeInfo(Index, &Info) == FALSE) continue;
        if (Info.Columns == Columns && Info.Rows == Rows) {
            *ModeIndex = Index;
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Program VGA registers for a mode index.
 * @param ModeIndex Index in the VGA mode table.
 * @return TRUE on success, FALSE on invalid parameters.
 */
BOOL VGASetMode(U32 ModeIndex) {
    if (ModeIndex >= VGAModeRegsCount) return FALSE;
    return SendModeRegs(VGAModeRegs[ModeIndex].Regs) == 0;
}

/***************************************************************************/
