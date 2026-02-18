/************************************************************************\

    EXOS BIOS Keyboard Payload
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


    BIOS keyboard payload (real mode)

\************************************************************************/

// X86_32 16-bit real mode payload for BIOS keyboard echo

#include "../../kernel/include/arch/x86-32/x86-32.h"
#include "../../kernel/include/CoreString.h"
#include "../include/vbr-realmode-utils.h"

/************************************************************************/

__asm__(".code16gcc");

/************************************************************************/

/**
 * @brief Output a single character to the BIOS TTY.
 * @param Character ASCII character to output.
 */
static void OutputChar(U8 Character) {
    __asm__ __volatile__(
        "mov   $0x0E, %%ah\n\t"
        "mov   %0, %%al\n\t"
        "int   $0x10\n\t"
        :
        : "r"(Character)
        : "ah", "al");
}

/************************************************************************/

/**
 * @brief Entry point for the boot payload.
 * @param BootDrive Boot drive number.
 * @param PartitionLba Partition LBA.
 */
void BootMain(U32 BootDrive, U32 PartitionLba) {
    UNUSED(BootDrive);
    UNUSED(PartitionLba);

    while (TRUE) {
        U16 Key = BootReadKeyBlocking();
        U8 Character = (U8)(Key & 0xFF);
        Character = (U8)(Character + 1);
        OutputChar(Character);
    }

    Hang();
}
