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

#include "../../kernel/include/String.h"
#include "Multiboot.h"
#include "SegOfs.h"

/* Low memory pages reserved by VBR */
#define LOW_MEMORY_PAGE_1 0x1000
#define LOW_MEMORY_PAGE_2 0x2000
#define LOW_MEMORY_PAGE_3 0x3000
#define LOW_MEMORY_PAGE_4 0x4000
#define LOW_MEMORY_PAGE_5 0x5000
#define LOW_MEMORY_PAGE_6 0x6000
#define LOW_MEMORY_PAGE_7 0x7000
#define LOW_MEMORY_PAGE_8 0x8000

/************************************************************************/
// E820 memory map
/************************************************************************/
typedef struct __attribute__((packed)) tag_E820ENTRY {
    U64 Base;
    U64 Size;
    U32 Type;
    U32 Attributes;
} E820ENTRY;

/************************************************************************/
// Constants shared with the architecture specific code
/************************************************************************/
#define E820_MAX_ENTRIES 32
#define E820_SIZE (E820_MAX_ENTRIES * (UINT)sizeof(E820ENTRY))

/************************************************************************/
// Globals provided by the common payload implementation
/************************************************************************/
extern U32 E820_EntryCount;
extern E820ENTRY E820_Map[E820_MAX_ENTRIES];
extern multiboot_info_t MultibootInfo;
extern multiboot_memory_map_t MultibootMemMap[E820_MAX_ENTRIES];
extern multiboot_module_t KernelModule;
extern const char BootloaderName[];
extern const char KernelCmdLine[];
extern STR TempString[128];

/************************************************************************/
// Common helpers exposed to the architecture specific units
/************************************************************************/
struct tag_SEGMENT_DESCRIPTOR;

U32 BuildMultibootInfo(U32 KernelPhysBase, U32 FileSize);

void BootDebugPrint(LPCSTR Str);
void BootVerbosePrint(LPCSTR Str);
void BootErrorPrint(LPCSTR Str);

void VbrSetSegmentDescriptor(
    struct tag_SEGMENT_DESCRIPTOR* Descriptor,
    U32 Base,
    U32 Limit,
    U32 Type,
    U32 CanWrite,
    U32 Privilege,
    U32 Operand32,
    U32 Gran4K,
    U32 LongMode);

#endif // VBR_PAYLOAD_SHARED_H_INCLUDED
