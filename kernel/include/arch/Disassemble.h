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


    Architecture disassembly helpers

\************************************************************************/
#ifndef ARCH_DISASSEMBLE_H_INCLUDED
#define ARCH_DISASSEMBLE_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/

void Disassemble(LPSTR Buffer, LINEAR InstructionPointer, U32 NumInstructions, U32 NumBits);

/***************************************************************************/

#endif  // ARCH_DISASSEMBLE_H_INCLUDED
