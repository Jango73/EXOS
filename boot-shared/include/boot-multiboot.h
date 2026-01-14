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


    Shared Multiboot builder helpers

\************************************************************************/

#ifndef BOOT_MULTIBOOT_H_INCLUDED
#define BOOT_MULTIBOOT_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "vbr-multiboot.h"

/************************************************************************/
// E820 memory map layout

#define E820_MAX_ENTRIES 32
#define E820_ENTRY_SIZE 24
#define E820_SIZE (E820_MAX_ENTRIES * E820_ENTRY_SIZE)

typedef struct PACKED tag_E820ENTRY {
    U64 Base;
    U64 Size;
    U32 Type;
    U32 Attributes;
} E820ENTRY;

/************************************************************************/

U32 BootBuildMultibootInfo(
    multiboot_info_t* MultibootInfo,
    multiboot_memory_map_t* MultibootMemMap,
    multiboot_module_t* KernelModule,
    const E820ENTRY* E820Map,
    U32 E820EntryCount,
    U32 KernelPhysBase,
    U32 FileSize,
    LPCSTR BootloaderName,
    LPCSTR KernelCmdLine);

#endif  // BOOT_MULTIBOOT_H_INCLUDED
