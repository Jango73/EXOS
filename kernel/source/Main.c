
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
#include "Log.h"
#include "System.h"
#include "vbr-multiboot.h"

/************************************************************************/

extern LINEAR __bss_init_start;
extern LINEAR __bss_init_end;

KERNELSTARTUPINFO KernelStartup = {
    .IRQMask_21_PM = 0x000000FB, .IRQMask_A1_PM = 0x000000FF, .IRQMask_21_RM = 0, .IRQMask_A1_RM = 0};

/************************************************************************/

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

    // Validate Multiboot magic number
    if (MultibootMagic != MULTIBOOT_BOOTLOADER_MAGIC) {
        ConsolePanic(TEXT("Multiboot information not valid"));
    }

    // Map the multiboot info structure to access it
    multiboot_info_t* MultibootInfo = (multiboot_info_t*)(UINT)MultibootInfoLinear;

    // Extract information from Multiboot structure
    // Get kernel address from first module
    if (MultibootInfo->flags & MULTIBOOT_INFO_MODS && MultibootInfo->mods_count > 0) {
        multiboot_module_t* FirstModule = (multiboot_module_t*)(UINT)(LINEAR)MultibootInfo->mods_addr;
        KernelStartup.KernelPhysicalBase = FirstModule->mod_start;
        KernelStartup.KernelSize = (UINT)(FirstModule->mod_end - FirstModule->mod_start);
        // Get the command line
        LPCSTR ModuleCommandLine = (LPCSTR)(UINT)(LINEAR)FirstModule->cmdline;
        StringCopy(KernelStartup.CommandLine, ModuleCommandLine);
    } else {
        // Fallback - should not happen with our bootloader
        KernelStartup.KernelPhysicalBase = 0;
        KernelStartup.KernelSize = 0;
        StringClear(KernelStartup.CommandLine);
    }

    // Process memory map if available
    if (MultibootInfo->flags & MULTIBOOT_INFO_MEM_MAP) {
        PHYSICAL MmapCursor = MultibootInfo->mmap_addr;
        PHYSICAL MmapEnd = MultibootInfo->mmap_addr + MultibootInfo->mmap_length;
        U32 E820Count = 0;

        while (MmapCursor < MmapEnd && E820Count < (N_4KB / sizeof(E820ENTRY))) {
            multiboot_memory_map_t* MmapEntry = (multiboot_memory_map_t*)(UINT)MmapCursor;
            // Fill E820 entry with Multiboot data
            KernelStartup.E820[E820Count].Base = U64_Make(MmapEntry->addr_high, MmapEntry->addr_low);
            KernelStartup.E820[E820Count].Size = U64_Make(MmapEntry->len_high, MmapEntry->len_low);
            KernelStartup.E820[E820Count].Attributes = 0;

            // Map Multiboot types to E820 types
            switch (MmapEntry->type) {
                case MULTIBOOT_MEMORY_AVAILABLE:
                    KernelStartup.E820[E820Count].Type = E820_AVAILABLE;
                    break;
                case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
                    KernelStartup.E820[E820Count].Type = E820_ACPI;
                    break;
                case MULTIBOOT_MEMORY_NVS:
                    KernelStartup.E820[E820Count].Type = E820_NVS;
                    break;
                case MULTIBOOT_MEMORY_BADRAM:
                    KernelStartup.E820[E820Count].Type = E820_UNUSABLE;
                    break;
                default:
                    KernelStartup.E820[E820Count].Type = E820_RESERVED;
                    break;
            }
            E820Count++;

            // Move to next entry (size field is at the beginning and doesn't include itself)
            MmapCursor += MmapEntry->size + sizeof(MmapEntry->size);
        }

        KernelStartup.E820_Count = E820Count;
    }

    UpdateKernelMemoryMetricsFromE820();

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

    InitializeKernel();
}
