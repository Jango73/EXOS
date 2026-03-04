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


    Early boot console

\************************************************************************/

#ifndef EARLY_BOOT_CONSOLE_H_INCLUDED
#define EARLY_BOOT_CONSOLE_H_INCLUDED

#include "Base.h"

/************************************************************************/

void EarlyBootConsoleInitialize(
    PHYSICAL FramebufferPhysical,
    U32 Width,
    U32 Height,
    U32 Pitch,
    U32 BitsPerPixel,
    U32 Type,
    U32 RedPosition,
    U32 RedMaskSize,
    U32 GreenPosition,
    U32 GreenMaskSize,
    U32 BluePosition,
    U32 BlueMaskSize);
void EarlyBootConsoleWrite(LPCSTR Text);
void EarlyBootConsoleWriteLine(LPCSTR Text);
BOOL EarlyBootConsoleIsInitialized(void);

/************************************************************************/

#endif
