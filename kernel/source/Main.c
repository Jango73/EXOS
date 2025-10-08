
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
#include "arch/i386/I386.h"
#include "Log.h"
#include "System.h"
#include "Multiboot.h"

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
    U32 MultibootInfoPhys;

    // No more interrupts
    DisableInterrupts();

    // Retrieve Multiboot parameters from registers
    __asm__ __volatile__("movl %%eax, %0" : "=m"(MultibootMagic));
    __asm__ __volatile__("movl %%ebx, %0" : "=m"(MultibootInfoPhys));

    // Validate Multiboot magic number
    if (MultibootMagic != MULTIBOOT_BOOTLOADER_MAGIC) {
        ConsolePanic(TEXT("Multiboot information not valid"));
        __builtin_unreachable();
    }

    // Map the multiboot info structure to access it
    multiboot_info_t* MultibootInfo = (multiboot_info_t*)MultibootInfoPhys;

    // Extract information from Multiboot structure
    // Get kernel address from first module
    if (MultibootInfo->flags & MULTIBOOT_INFO_MODS && MultibootInfo->mods_count > 0) {
        multiboot_module_t* FirstModule = (multiboot_module_t*)MultibootInfo->mods_addr;
        KernelStartup.StubAddress = FirstModule->mod_start;
        // Get the command line
        StringCopy(KernelStartup.CommandLine, FirstModule->cmdline);
    } else {
        // Fallback - should not happen with our bootloader
        KernelStartup.StubAddress = 0;
        StringClear(KernelStartup.CommandLine);
    }

    // Process memory map if available
    if (MultibootInfo->flags & MULTIBOOT_INFO_MEM_MAP) {
        multiboot_memory_map_t* MmapEntry = (multiboot_memory_map_t*)MultibootInfo->mmap_addr;
        U32 MmapEnd = MultibootInfo->mmap_addr + MultibootInfo->mmap_length;
        U32 E820Count = 0;

        while ((U32)MmapEntry < MmapEnd && E820Count < (N_4KB / sizeof(E820ENTRY))) {
            // Fill E820 entry with Multiboot data
            KernelStartup.E820[E820Count].Base.LO = MmapEntry->addr_low;
            KernelStartup.E820[E820Count].Base.HI = MmapEntry->addr_high;
            KernelStartup.E820[E820Count].Size.LO = MmapEntry->len_low;
            KernelStartup.E820[E820Count].Size.HI = MmapEntry->len_high;
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
            MmapEntry = (multiboot_memory_map_t*)((U8*)MmapEntry + MmapEntry->size + sizeof(MmapEntry->size));
        }

        KernelStartup.E820_Count = E820Count;
    }

    if (KernelStartup.StubAddress == 0) {
        ConsolePanic(TEXT("No physical address specified for the kernel"));
    }

    //-------------------------------------
    // Read current GDT base address

    GDT_REGISTER gdtr;
    ReadGlobalDescriptorTable(&gdtr);
    Kernel_i386.GDT = (LPSEGMENT_DESCRIPTOR)gdtr.Base;

    //-------------------------------------
    // Clear the BSS

    LINEAR BSSStart = (LINEAR)(&__bss_init_start);
    LINEAR BSSEnd = (LINEAR)(&__bss_init_end);
    U32 BSSSize = BSSEnd - BSSStart;
    MemorySet((LPVOID)BSSStart, 0, BSSSize);

    //--------------------------------------
    // Main initialization routine

    InitializeKernel();
}
