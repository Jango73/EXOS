
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


    Main

\************************************************************************/

#include "Console.h"
#include "Kernel.h"
#include "Arch.h"
#include "BootStageMarker.h"
#include "Log.h"
#include "System.h"
#include "vbr-multiboot.h"

/************************************************************************/

extern LINEAR __bss_init_start;
extern LINEAR __bss_init_end;

KERNELSTARTUPINFO KernelStartup = {
    .IRQMask_21_PM = 0x000000FB, .IRQMask_A1_PM = 0x000000FF, .IRQMask_21_RM = 0, .IRQMask_A1_RM = 0};

/**
 * @brief Main entry point for the EXOS kernel in paged protected mode.
 *
 * This function initializes the kernel subsystems, processes memory maps,
 * sets up hardware components, and starts the system. It is called after
 * the bootloader has set up protected mode and paging.
 *
 * The function retrieves startup parameters from Multiboot information structure,
 * initializes all kernel subsystems in proper order, and never returns.
 */
void KernelMain(void) {
    U32 MultibootMagic;
    LINEAR MultibootInfoLinear;

    // No more interrupts
    DisableInterrupts();

    // Retrieve Multiboot parameters from registers
#if defined(__EXOS_ARCH_X86_64__)
    register U64 StartupRax __asm__("rax");
    register U64 StartupRbx __asm__("rbx");

    MultibootMagic = (U32)StartupRax;
    MultibootInfoLinear = (LINEAR)StartupRbx;
#else
    __asm__ __volatile__("movl %%eax, %0" : "=m"(MultibootMagic));
    __asm__ __volatile__("movl %%ebx, %0" : "=m"(MultibootInfoLinear));
#endif

#if defined(__EXOS_ARCH_X86_64__) && BOOT_STAGE_MARKERS == 1
    BootStageMarkerFromMultiboot((const multiboot_info_t*)(UINT)MultibootInfoLinear, 29, 255, 128, 0);
#endif

    // Validate Multiboot magic number
    if (MultibootMagic != MULTIBOOT_BOOTLOADER_MAGIC) {
        ConsolePanic(TEXT("Multiboot information not valid"));
    }

#if defined(__EXOS_ARCH_X86_64__) && BOOT_STAGE_MARKERS == 1
    BootStageMarkerFromMultiboot((const multiboot_info_t*)(UINT)MultibootInfoLinear, 28, 255, 255, 0);
#endif

    // Map the multiboot info structure to access it
    multiboot_info_t* MultibootInfo = (multiboot_info_t*)(UINT)MultibootInfoLinear;

    if ((MultibootInfo->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO) != 0u) {
        PHYSICAL FramebufferPhysical = 0;
#if defined(__EXOS_ARCH_X86_64__)
        U64 FramebufferAddress = U64_Make(
            MultibootInfo->framebuffer_addr_high,
            MultibootInfo->framebuffer_addr_low);
        FramebufferPhysical = (PHYSICAL)FramebufferAddress;
#else
        FramebufferPhysical = (PHYSICAL)MultibootInfo->framebuffer_addr_low;
        if (MultibootInfo->framebuffer_addr_high != 0u) {
            WARNING(TEXT("[KernelMain] Framebuffer above 4GB not supported"));
        }
#endif
        ConsoleSetFramebufferInfo(
            FramebufferPhysical,
            MultibootInfo->framebuffer_width,
            MultibootInfo->framebuffer_height,
            MultibootInfo->framebuffer_pitch,
            (U32)MultibootInfo->framebuffer_bpp,
            (U32)MultibootInfo->framebuffer_type,
            (U32)MultibootInfo->color_info[0],
            (U32)MultibootInfo->color_info[1],
            (U32)MultibootInfo->color_info[2],
            (U32)MultibootInfo->color_info[3],
            (U32)MultibootInfo->color_info[4],
            (U32)MultibootInfo->color_info[5]);
    }

#if defined(__EXOS_ARCH_X86_64__) && BOOT_STAGE_MARKERS == 1
    BootStageMarkerFromMultiboot(MultibootInfo, 30, 0, 255, 255);
#endif

    if ((MultibootInfo->flags & MULTIBOOT_INFO_CONFIG_TABLE) != 0u) {
        KernelStartup.RsdpPhysical = (PHYSICAL)MultibootInfo->config_table;
    } else {
        KernelStartup.RsdpPhysical = 0;
    }

    // Extract information from Multiboot structure
    // Get kernel address from first module
    if (MultibootInfo->flags & MULTIBOOT_INFO_MODS && MultibootInfo->mods_count > 0) {
        multiboot_module_t* FirstModule = (multiboot_module_t*)(UINT)(LINEAR)MultibootInfo->mods_addr;
        KernelStartup.KernelPhysicalBase = FirstModule->mod_start;
        KernelStartup.KernelSize = (UINT)(FirstModule->mod_end - FirstModule->mod_start);
        KernelStartup.KernelReservedBytes = (UINT)FirstModule->reserved;
        if (KernelStartup.KernelReservedBytes < KernelStartup.KernelSize) {
            ERROR(TEXT("[KernelMain] Invalid kernel reserved span (reserved=%u size=%u)"),
                KernelStartup.KernelReservedBytes,
                KernelStartup.KernelSize);
            ConsolePanic(TEXT("Invalid boot kernel reserved span"));
        }
        // Get the command line
        LPCSTR ModuleCommandLine = (LPCSTR)(UINT)(LINEAR)FirstModule->cmdline;
        StringCopy(KernelStartup.CommandLine, ModuleCommandLine);
    } else {
        // Fallback - should not happen with our bootloader
        KernelStartup.KernelPhysicalBase = 0;
        KernelStartup.KernelSize = 0;
        KernelStartup.KernelReservedBytes = 0;
        StringClear(KernelStartup.CommandLine);
    }

    // Process memory map if available
    if (MultibootInfo->flags & MULTIBOOT_INFO_MEM_MAP) {
        PHYSICAL MmapCursor = MultibootInfo->mmap_addr;
        PHYSICAL MmapEnd = MultibootInfo->mmap_addr + MultibootInfo->mmap_length;
        U32 EntryCount = 0;

        while (MmapCursor < MmapEnd && EntryCount < (N_4KB / sizeof(MULTIBOOTMEMORYENTRY))) {
            multiboot_memory_map_t* MmapEntry = (multiboot_memory_map_t*)(UINT)MmapCursor;
            // Duplicate Multiboot entry information
            KernelStartup.MultibootMemoryEntries[EntryCount].Base = U64_Make(MmapEntry->addr_high, MmapEntry->addr_low);
            KernelStartup.MultibootMemoryEntries[EntryCount].Length = U64_Make(MmapEntry->len_high, MmapEntry->len_low);
            KernelStartup.MultibootMemoryEntries[EntryCount].Type = MmapEntry->type;
            EntryCount++;

            // Move to next entry (size field is at the beginning and doesn't include itself)
            MmapCursor += MmapEntry->size + sizeof(MmapEntry->size);
        }

        KernelStartup.MultibootMemoryEntryCount = EntryCount;
    }

    UpdateKernelMemoryMetricsFromMultibootMap();

    if (KernelStartup.KernelPhysicalBase == 0) {
        ConsolePanic(TEXT("No physical address specified for the kernel"));
    }

    // Clear the BSS

    LINEAR BSSStart = (LINEAR)(&__bss_init_start);
    LINEAR BSSEnd = (LINEAR)(&__bss_init_end);
    U32 BSSSize = BSSEnd - BSSStart;
    MemorySet((LPVOID)BSSStart, 0, BSSSize);

    //--------------------------------------
    // Main initialization routine

#if defined(__EXOS_ARCH_X86_64__) && BOOT_STAGE_MARKERS == 1
    BootStageMarkerFromMultiboot(MultibootInfo, 31, 255, 255, 255);
#endif

    InitializeKernel();
}
