
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


    Multiboot definitions

\************************************************************************/

#ifndef VBR_MULTIBOOT_H_INCLUDED
#define VBR_MULTIBOOT_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

// Multiboot magic number
#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

// Multiboot info flags
#define MULTIBOOT_INFO_MEMORY         0x00000001
#define MULTIBOOT_INFO_BOOTDEV        0x00000002
#define MULTIBOOT_INFO_CMDLINE        0x00000004
#define MULTIBOOT_INFO_MODS           0x00000008
#define MULTIBOOT_INFO_AOUT_SYMS      0x00000010
#define MULTIBOOT_INFO_ELF_SHDR       0x00000020
#define MULTIBOOT_INFO_MEM_MAP        0x00000040
#define MULTIBOOT_INFO_DRIVE_INFO     0x00000080
#define MULTIBOOT_INFO_CONFIG_TABLE   0x00000100
#define MULTIBOOT_INFO_BOOT_LOADER_NAME 0x00000200
#define MULTIBOOT_INFO_APM_TABLE      0x00000400
#define MULTIBOOT_INFO_VBE_INFO       0x00000800
#define MULTIBOOT_INFO_FRAMEBUFFER_INFO 0x00001000

// Multiboot framebuffer types
#define MULTIBOOT_FRAMEBUFFER_RGB 1
#define MULTIBOOT_FRAMEBUFFER_TEXT 2

// Multiboot memory map entry types
#define MULTIBOOT_MEMORY_AVAILABLE    1
#define MULTIBOOT_MEMORY_RESERVED     2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS          4
#define MULTIBOOT_MEMORY_BADRAM       5

// Multiboot memory map structure
typedef struct {
    U32 size;
    U32 addr_low;
    U32 addr_high;
    U32 len_low;
    U32 len_high;
    U32 type;
} PACKED multiboot_memory_map_t;

// Multiboot module structure
typedef struct {
    U32 mod_start;     // Physical start address of module in RAM
    U32 mod_end;       // Physical end address of module
    U32 cmdline;       // Physical address of ASCII string (module arguments, null-terminated)
    U32 reserved;      // EXOS: loader-reserved kernel span in bytes
} PACKED multiboot_module_t;

// Main Multiboot information structure
typedef struct {
    U32 flags;           // +0   : Field presence flags
    U32 mem_lower;       // +4   : Lower memory in KB (if flags[0])
    U32 mem_upper;       // +8   : Upper memory in KB (if flags[0])
    U32 boot_device;     // +12  : Boot device (if flags[1])
    U32 cmdline;         // +16  : Command line (if flags[2])
    U32 mods_count;      // +20  : Number of modules (if flags[3])
    U32 mods_addr;       // +24  : Modules address (if flags[3])
    U32 syms[4];         // +28-40 : Symbols (if flags[4] or flags[5])
    U32 mmap_length;     // +44  : Memory map length (if flags[6])
    U32 mmap_addr;       // +48  : Memory map address (if flags[6])
    U32 drives_length;   // +52  : Drive info length (if flags[7])
    U32 drives_addr;     // +56  : Drive info address (if flags[7])
    U32 config_table;    // +60  : ROM config table (if flags[8])
    U32 boot_loader_name;// +64  : Bootloader name (if flags[9])
    U32 apm_table;       // +68  : APM table (if flags[10])

    // VBE info (if flags[11])
    U32 vbe_control_info; // +72
    U32 vbe_mode_info;    // +76
    U16 vbe_mode;         // +80
    U16 vbe_interface_seg;// +82
    U16 vbe_interface_off;// +84
    U16 vbe_interface_len;// +86

    // Framebuffer info (if flags[12])
    U32 framebuffer_addr_low;  // +88
    U32 framebuffer_addr_high; // +92
    U32 framebuffer_pitch;     // +96
    U32 framebuffer_width;     // +100
    U32 framebuffer_height;    // +104
    U8  framebuffer_bpp;       // +108
    U8  framebuffer_type;      // +109
    U8  color_info[6];         // +110-115
} PACKED multiboot_info_t;


/************************************************************************/

#endif  // VBR_MULTIBOOT_H_INCLUDED
