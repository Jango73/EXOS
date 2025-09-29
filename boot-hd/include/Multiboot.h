
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


    Mouse

\************************************************************************/

#ifndef MULTIBOOT_H_INCLUDED
#define MULTIBOOT_H_INCLUDED

// Type definitions
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;

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

// Multiboot memory map entry
typedef struct {
    uint32_t size;
    uint32_t addr_low;
    uint32_t addr_high;
    uint32_t len_low;
    uint32_t len_high;
    uint32_t type;
} __attribute__((packed)) multiboot_memory_map_t;

// Multiboot memory map entry types
#define MULTIBOOT_MEMORY_AVAILABLE    1
#define MULTIBOOT_MEMORY_RESERVED     2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS          4
#define MULTIBOOT_MEMORY_BADRAM       5

// Multiboot module structure
typedef struct {
    uint32_t mod_start;     // Physical start address of module in RAM
    uint32_t mod_end;       // Physical end address of module
    uint32_t cmdline;       // Physical address of ASCII string (module arguments, null-terminated)
    uint32_t reserved;      // Always 0 (padding for alignment)
} __attribute__((packed)) multiboot_module_t;

// Main Multiboot information structure
typedef struct {
    uint32_t flags;           // +0   : Field presence flags
    uint32_t mem_lower;       // +4   : Lower memory in KB (if flags[0])
    uint32_t mem_upper;       // +8   : Upper memory in KB (if flags[0])
    uint32_t boot_device;     // +12  : Boot device (if flags[1])
    uint32_t cmdline;         // +16  : Command line (if flags[2])
    uint32_t mods_count;      // +20  : Number of modules (if flags[3])
    uint32_t mods_addr;       // +24  : Modules address (if flags[3])
    uint32_t syms[4];         // +28-40 : Symbols (if flags[4] or flags[5])
    uint32_t mmap_length;     // +44  : Memory map length (if flags[6])
    uint32_t mmap_addr;       // +48  : Memory map address (if flags[6])
    uint32_t drives_length;   // +52  : Drive info length (if flags[7])
    uint32_t drives_addr;     // +56  : Drive info address (if flags[7])
    uint32_t config_table;    // +60  : ROM config table (if flags[8])
    uint32_t boot_loader_name;// +64  : Bootloader name (if flags[9])
    uint32_t apm_table;       // +68  : APM table (if flags[10])

    // VBE info (if flags[11])
    uint32_t vbe_control_info; // +72
    uint32_t vbe_mode_info;    // +76
    uint16_t vbe_mode;         // +80
    uint16_t vbe_interface_seg;// +82
    uint16_t vbe_interface_off;// +84
    uint16_t vbe_interface_len;// +86

    // Framebuffer info (if flags[12])
    uint32_t framebuffer_addr_low;  // +88
    uint32_t framebuffer_addr_high; // +92
    uint32_t framebuffer_pitch;     // +96
    uint32_t framebuffer_width;     // +100
    uint32_t framebuffer_height;    // +104
    uint8_t  framebuffer_bpp;       // +108
    uint8_t  framebuffer_type;      // +109
    uint8_t  color_info[6];         // +110-115
} __attribute__((packed)) multiboot_info_t;

// E820 memory types
#define E820_AVAILABLE    1
#define E820_RESERVED     2
#define E820_ACPI         3
#define E820_NVS          4
#define E820_UNUSABLE     5

#endif // MULTIBOOT_H_INCLUDED
