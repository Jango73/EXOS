
/************************************************************************\

    EXOS Bootloader
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


    Shared declarations between the architecture-specific VBR payloads

\************************************************************************/

#ifndef VBR_PAYLOAD_SHARED_H_INCLUDED
#define VBR_PAYLOAD_SHARED_H_INCLUDED

#include "CoreString.h"
#include "boot-multiboot.h"
#include "vbr-realmode-utils.h"

/************************************************************************/
// Payload memory layout configuration

#ifndef MEMORY_BASE
#define MEMORY_BASE 0x10000u
#endif

#ifndef MEMORY_SIZE
#define MEMORY_SIZE 0x80000u
#endif

#define MEMORY_PAGE_SIZE 0x1000u

/************************************************************************/
// Low memory pages reserved by VBR

#define LOW_MEMORY_PAGE_1 (MEMORY_BASE)
#define LOW_MEMORY_PAGE_2 (MEMORY_BASE + MEMORY_PAGE_SIZE)
#define LOW_MEMORY_PAGE_3 (MEMORY_BASE + (2 * MEMORY_PAGE_SIZE))
#define LOW_MEMORY_PAGE_4 (MEMORY_BASE + (3 * MEMORY_PAGE_SIZE))
#define LOW_MEMORY_PAGE_5 (MEMORY_BASE + (4 * MEMORY_PAGE_SIZE))
#define LOW_MEMORY_PAGE_6 (MEMORY_BASE + (5 * MEMORY_PAGE_SIZE))
#define LOW_MEMORY_PAGE_7 (MEMORY_BASE + (6 * MEMORY_PAGE_SIZE))
#define LOW_MEMORY_PAGE_8 (MEMORY_BASE + (7 * MEMORY_PAGE_SIZE))

#if ((8u * MEMORY_PAGE_SIZE) > MEMORY_SIZE)
#error "MEMORY_SIZE is too small for the reserved payload structures"
#endif

/************************************************************************/
// Common helpers exposed to the architecture specific units

void BootDebugPrint(LPCSTR Format, ...);
void BootVerbosePrint(LPCSTR Format, ...);
void BootErrorPrint(LPCSTR Format, ...);

#endif  // VBR_PAYLOAD_SHARED_H_INCLUDED
