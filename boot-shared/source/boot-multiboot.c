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

#include "boot-multiboot.h"
#include "CoreString.h"

/************************************************************************/

void BootDebugPrint(LPCSTR Format, ...);

/************************************************************************/

/**
 * @brief Build Multiboot information based on an E820 map.
 *
 * @param MultibootInfo Destination Multiboot info structure.
 * @param MultibootMemMap Destination Multiboot memory map array.
 * @param KernelModule Destination Multiboot module descriptor.
 * @param E820Map Input E820 map array.
 * @param E820EntryCount Number of E820 entries.
 * @param KernelPhysBase Physical base address of the loaded kernel.
 * @param FileSize Size of the loaded kernel in bytes.
 * @param KernelReservedBytes Loader-reserved kernel span size in bytes.
 * @param BootloaderName ASCII bootloader name string.
 * @param KernelCmdLine ASCII command line string for the kernel.
 * @return Physical address of MultibootInfo.
 */
U32 BootBuildMultibootInfo(
    multiboot_info_t* MultibootInfo,
    multiboot_memory_map_t* MultibootMemMap,
    multiboot_module_t* KernelModule,
    const E820ENTRY* E820Map,
    U32 E820EntryCount,
    U32 KernelPhysBase,
    U32 FileSize,
    U32 KernelReservedBytes,
    U32 RsdpPhysical,
    LPCSTR BootloaderName,
    LPCSTR KernelCmdLine,
    const BOOT_FRAMEBUFFER_INFO* FramebufferInfo) {
    // Clear the multiboot info structure
    MemorySet(MultibootInfo, 0, sizeof(multiboot_info_t));
    MemorySet(MultibootMemMap, 0, sizeof(multiboot_memory_map_t) * E820_MAX_ENTRIES);

    // Set up multiboot flags
    MultibootInfo->flags = MULTIBOOT_INFO_MEMORY | MULTIBOOT_INFO_MEM_MAP |
                           MULTIBOOT_INFO_BOOT_LOADER_NAME | MULTIBOOT_INFO_MODS;

    // Convert E820 map to Multiboot memory map format
    U32 MmapLength = 0;
    for (U32 Index = 0; Index < E820EntryCount && Index < E820_MAX_ENTRIES; Index++) {
        MultibootMemMap[Index].size = sizeof(multiboot_memory_map_t) - sizeof(U32);
        MultibootMemMap[Index].addr_low = U64_Low32(E820Map[Index].Base);
        MultibootMemMap[Index].addr_high = U64_High32(E820Map[Index].Base);
        MultibootMemMap[Index].len_low = U64_Low32(E820Map[Index].Size);
        MultibootMemMap[Index].len_high = U64_High32(E820Map[Index].Size);

        // Convert E820 types to Multiboot types
        switch (E820Map[Index].Type) {
            case E820_AVAILABLE:
                MultibootMemMap[Index].type = MULTIBOOT_MEMORY_AVAILABLE;
                break;
            case E820_RESERVED:
                MultibootMemMap[Index].type = MULTIBOOT_MEMORY_RESERVED;
                break;
            case E820_ACPI:
                MultibootMemMap[Index].type = MULTIBOOT_MEMORY_ACPI_RECLAIMABLE;
                break;
            case E820_NVS:
                MultibootMemMap[Index].type = MULTIBOOT_MEMORY_NVS;
                break;
            case E820_UNUSABLE:
                MultibootMemMap[Index].type = MULTIBOOT_MEMORY_BADRAM;
                break;
            default:
                MultibootMemMap[Index].type = MULTIBOOT_MEMORY_RESERVED;
                break;
        }

        MmapLength += sizeof(multiboot_memory_map_t);
    }

    // Set memory map info
    MultibootInfo->mmap_length = MmapLength;
    MultibootInfo->mmap_addr = (U32)(UINT)MultibootMemMap;

    // Compute mem_lower and mem_upper from memory map
    U32 LowerMem = 0;
    U32 UpperMem = 0;

    for (U32 Index = 0; Index < E820EntryCount && Index < E820_MAX_ENTRIES; Index++) {
        if (MultibootMemMap[Index].type == MULTIBOOT_MEMORY_AVAILABLE) {
            U32 StartLow = MultibootMemMap[Index].addr_low;
            U32 StartHigh = MultibootMemMap[Index].addr_high;
            U32 LengthLow = MultibootMemMap[Index].len_low;
            U32 LengthHigh = MultibootMemMap[Index].len_high;

            // Only handle memory regions that fit in 32-bit space
            if (StartHigh == 0 && LengthHigh == 0) {
                U32 End = StartLow + LengthLow;

                if (StartLow < 0x100000u) {
                    U32 LowerEnd = (End > 0x100000u) ? 0x100000u : End;
                    U32 LowerSize = LowerEnd - StartLow;
                    if (StartLow >= 0x1000u) {
                        LowerMem += LowerSize / 1024u;
                    }
                }

                if (End > 0x100000u) {
                    U32 UpperStart = (StartLow < 0x100000u) ? 0x100000u : StartLow;
                    U32 UpperSize = End - UpperStart;
                    UpperMem += UpperSize / 1024u;
                }
            } else if (StartLow >= 0x100000u) {
                U32 SizeToAdd = (LengthHigh == 0) ? LengthLow : (0xFFFFFFFFu - StartLow);
                UpperMem += SizeToAdd / 1024u;
            }
        }
    }

    MultibootInfo->mem_lower = LowerMem;
    MultibootInfo->mem_upper = UpperMem;

    // Set bootloader name
    MultibootInfo->boot_loader_name = (U32)(UINT)BootloaderName;

    if (RsdpPhysical != 0u) {
        MultibootInfo->flags |= MULTIBOOT_INFO_CONFIG_TABLE;
        MultibootInfo->config_table = RsdpPhysical;
    }

    // Set up kernel module
    KernelModule->mod_start = KernelPhysBase;
    KernelModule->mod_end = KernelPhysBase + FileSize;
    KernelModule->cmdline = (U32)(UINT)KernelCmdLine;
    KernelModule->reserved = KernelReservedBytes;

    // Set module information in multiboot info
    MultibootInfo->mods_count = 1;
    MultibootInfo->mods_addr = (U32)(UINT)KernelModule;

    if (FramebufferInfo != NULL && FramebufferInfo->Type != 0u) {
        MultibootInfo->flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;
        MultibootInfo->framebuffer_addr_low = U64_Low32(FramebufferInfo->Address);
        MultibootInfo->framebuffer_addr_high = U64_High32(FramebufferInfo->Address);
        MultibootInfo->framebuffer_pitch = FramebufferInfo->Pitch;
        MultibootInfo->framebuffer_width = FramebufferInfo->Width;
        MultibootInfo->framebuffer_height = FramebufferInfo->Height;
        MultibootInfo->framebuffer_bpp = (U8)FramebufferInfo->BitsPerPixel;
        MultibootInfo->framebuffer_type = (U8)FramebufferInfo->Type;
        MultibootInfo->color_info[0] = (U8)FramebufferInfo->RedPosition;
        MultibootInfo->color_info[1] = (U8)FramebufferInfo->RedMaskSize;
        MultibootInfo->color_info[2] = (U8)FramebufferInfo->GreenPosition;
        MultibootInfo->color_info[3] = (U8)FramebufferInfo->GreenMaskSize;
        MultibootInfo->color_info[4] = (U8)FramebufferInfo->BluePosition;
        MultibootInfo->color_info[5] = (U8)FramebufferInfo->BlueMaskSize;
    }

    BootDebugPrint(
        TEXT("[BootBuildMultibootInfo] Multiboot info at %p\r\n"),
        MultibootInfo);
    BootDebugPrint(
        TEXT("[BootBuildMultibootInfo] mem_lower=%u KB, mem_upper=%u KB\r\n"),
        LowerMem,
        UpperMem);
    if ((MultibootInfo->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) != 0u) {
        BootDebugPrint(
            TEXT("[BootBuildMultibootInfo] framebuffer=%x:%x %ux%u pitch=%u bpp=%u type=%u\r\n"),
            U64_High32(FramebufferInfo->Address),
            U64_Low32(FramebufferInfo->Address),
            FramebufferInfo->Width,
            FramebufferInfo->Height,
            FramebufferInfo->Pitch,
            FramebufferInfo->BitsPerPixel,
            FramebufferInfo->Type);
    }
    if ((MultibootInfo->flags & MULTIBOOT_INFO_CONFIG_TABLE) != 0u) {
        BootDebugPrint(
            TEXT("[BootBuildMultibootInfo] rsdp=%x\r\n"),
            RsdpPhysical);
    }

    return (U32)(UINT)MultibootInfo;
}
