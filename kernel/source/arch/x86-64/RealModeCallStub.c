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


    Real mode call stubs for x86-64 builds

\************************************************************************/

#include "System.h"
#include "Log.h"
#include "Text.h"

/***************************************************************************/

void RealModeCall(U32 IntOrAddress, LPINTEL_X86_REGISTERS Registers) {
    UNUSED(IntOrAddress);
    UNUSED(Registers);

    WARNING(TEXT("[RealModeCall] Real mode BIOS calls are not supported on x86-64 builds"));
}

/***************************************************************************/

void RealModeCallTest(void) {
    WARNING(TEXT("[RealModeCallTest] Real mode BIOS calls are not supported on x86-64 builds"));
}

/***************************************************************************/
