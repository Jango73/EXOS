
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2026 Jango73

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


    Shell commands

\************************************************************************/

#include "shell/Shell-Commands-Private.h"
#include "shell/Shell-EmbeddedScripts.h"
#include "autotest/Autotest.h"
#include "utils/SizeFormat.h"

/************************************************************************/

/**
 * @brief Run the embedded driver detail script for one alias.
 * @param Context Shell context.
 * @param Alias Driver alias.
 * @return TRUE on success.
 */
static BOOL RunEmbeddedDriverDetailsScript(
    LPSHELLCONTEXT Context,
    LPCSTR Alias) {
    STR ScriptText[4096];

    if (Context == NULL || Alias == NULL || StringLength(Alias) == 0) {
        return FALSE;
    }

    StringPrintFormat(
        ScriptText,
        TEXT("target_alias = \"%s\";\n%s"),
        Alias,
        ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_DRIVER_DETAILS));
    return RunEmbeddedScript(Context, ScriptText);
}

/************************************************************************/

/**
 * @brief Print one driver detail view selected by alias.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_driver(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Usage: driver list\n"));
        ConsolePrint(TEXT("       driver Alias\n"));
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Context->Command, TEXT("list")) == 0) {
        if (!RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_DRIVER_LIST))) {
            ConsolePrint(TEXT("Unable to run embedded driver list script\n"));
        }
        return DF_RETURN_SUCCESS;
    }

    if (!RunEmbeddedDriverDetailsScript(Context, Context->Command)) {
        ConsolePrint(TEXT("Unable to run embedded driver detail script\n"));
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief List the tasks visible to the current shell caller.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_task(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 ||
        StringCompareNC(Context->Command, TEXT("list")) != 0) {
        ConsolePrint(TEXT("Usage: task list\n"));
        return DF_RETURN_SUCCESS;
    }

    if (!RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_TASK_LIST))) {
        ConsolePrint(TEXT("Unable to run embedded task list script\n"));
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

U32 CMD_memedit(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);
    MemoryEditor(StringToU32(Context->Command));

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

U32 CMD_memorymap(LPSHELLCONTEXT Context) {
    if (!RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_MEMORY_MAP))) {
        ConsolePrint(TEXT("Unable to run embedded memory-map script\n"));
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

U32 CMD_disasm(LPSHELLCONTEXT Context) {

    U32 Address = 0;
    U32 InstrCount = 0;
    STR Buffer[MAX_STRING_BUFFER];

    ParseNextCommandLineComponent(Context);
    Address = StringToU32(Context->Command);

    ParseNextCommandLineComponent(Context);
    InstrCount = StringToU32(Context->Command);

    if (Address != 0 && InstrCount > 0) {
        MemorySet(Buffer, 0, MAX_STRING_BUFFER);

        U32 NumBits = 32;
#if defined(__EXOS_ARCH_X86_64__)
        NumBits = 64;
#endif

        Disassemble(Buffer, Address, InstrCount, NumBits);
        ConsolePrint(Buffer);
    } else {
        ConsolePrint(TEXT("Missing parameter\n"));
    }


    return DF_RETURN_SUCCESS;
}

/************************************************************************/

U32 CMD_network(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 ||
        StringCompareNC(Context->Command, TEXT("devices")) != 0) {
        ConsolePrint(TEXT("Usage: network devices\n"));
        return DF_RETURN_SUCCESS;
    }

    if (!RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_NETWORK_DEVICES))) {
        ConsolePrint(TEXT("Unable to run embedded network device list script\n"));
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

U32 CMD_pic(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ConsolePrint(TEXT("8259-1 RM mask : %08b\n"), KernelStartup.IRQMask_21_RM);
    ConsolePrint(TEXT("8259-2 RM mask : %08b\n"), KernelStartup.IRQMask_A1_RM);
    ConsolePrint(TEXT("8259-1 PM mask : %08b\n"), KernelStartup.IRQMask_21_PM);
    ConsolePrint(TEXT("8259-2 PM mask : %08b\n"), KernelStartup.IRQMask_A1_PM);

    return DF_RETURN_SUCCESS;
}

U32 CMD_reboot(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ConsolePrint(TEXT("Rebooting system...\n"));

    RebootKernel();

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shutdown command implementation.
 * @param Context Shell context.
 */
U32 CMD_shutdown(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ConsolePrint(TEXT("Shutting down system...\n"));

    ShutdownKernel();

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Print one profiling snapshot entry.
 * @param Entry Snapshot entry to print.
 */
static void PrintProfileEntry(LPPROFILE_ENTRY_INFO Entry) {
    UINT Average = 0;

    if (Entry == NULL) {
        return;
    }

    if (Entry->TimedCallCount > 0) {
        Average = Entry->TotalTicks / Entry->TimedCallCount;
    }

    ConsolePrint(
        TEXT("%-32s calls=%u timed=%u last=%u us avg=%u us max=%u us total=%u us\n"),
        Entry->Name,
        Entry->CallCount,
        Entry->TimedCallCount,
        Entry->LastTicks,
        Average,
        Entry->MaxTicks,
        Entry->TotalTicks);
}

/************************************************************************/

U32 CMD_prof(LPSHELLCONTEXT Context) {
    PROFILE_ENTRY_INFO Entries[PROFILE_MAX_ENTRIES];
    PROFILE_QUERY_INFO Query;
    UINT Result;

    MemorySet(Entries, 0, sizeof(Entries));
    MemorySet(&Query, 0, sizeof(Query));

    ParseNextCommandLineComponent(Context);

    Query.Header.Size = sizeof(Query);
    Query.Header.Version = EXOS_ABI_VERSION;
    Query.Header.Flags = 0;
    Query.Capacity = PROFILE_MAX_ENTRIES;
    Query.Flags = 0;
    Query.Entries = Entries;

    if (StringLength(Context->Command) != 0) {
        if (StringCompareNC(Context->Command, TEXT("reset")) == 0) {
            Query.Flags = PROFILE_QUERY_FLAG_RESET;
        } else {
            ConsolePrint(TEXT("Usage: prof [reset]\n"));
            return DF_RETURN_SUCCESS;
        }
    }

    Result = DoSystemCall(SYSCALL_GetProfileInfo, SYSCALL_PARAM(&Query));
    if (Result != DF_RETURN_SUCCESS) {
        ConsolePrint(TEXT("Profiling snapshot unavailable.\n"));
        return Result;
    }

    if (Query.EntryCount == 0) {
        ConsolePrint(TEXT("No profiling samples available.\n"));
        return DF_RETURN_SUCCESS;
    }

    for (UINT Index = 0; Index < Query.EntryCount; ++Index) {
        PrintProfileEntry(&Entries[Index]);
    }

    ConsolePrint(TEXT("entries=%u total_entries=%u samples=%u dropped=%u%s\n"),
                 Query.EntryCount,
                 Query.TotalEntryCount,
                 Query.SampleCount,
                 Query.DroppedCount,
                 (Query.Flags & PROFILE_QUERY_FLAG_RESET) != 0 ? TEXT(" reset=yes") : TEXT(""));
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Run one on-demand autotest module.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS.
 */
U32 CMD_autotest(LPSHELLCONTEXT Context) {
    BOOL Result = FALSE;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 || StringCompareNC(Context->Command, TEXT("stack")) != 0) {
        ConsolePrint(TEXT("Usage: autotest stack\n"));
        return DF_RETURN_SUCCESS;
    }

    Result = RunSingleTestByName(TEXT("TestCopyStack"));

    if (Result) {
        ConsolePrint(TEXT("autotest stack: passed\n"));
    } else {
        ConsolePrint(TEXT("autotest stack: failed\n"));
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Run the System Data View mode from the shell.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_dataview(LPSHELLCONTEXT Context) {
    UNUSED(Context);
    SystemDataViewMode();
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief USB control command (xHCI port report).
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_usb(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 ||
        (StringCompareNC(Context->Command, TEXT("ports")) != 0 &&
         StringCompareNC(Context->Command, TEXT("devices")) != 0 &&
         StringCompareNC(Context->Command, TEXT("device-tree")) != 0 &&
         StringCompareNC(Context->Command, TEXT("drives")) != 0 &&
         StringCompareNC(Context->Command, TEXT("probe")) != 0)) {
        ConsolePrint(TEXT("Usage: usb ports|devices|device-tree|drives|probe\n"));
        return DF_RETURN_SUCCESS;
    }

    if (StringCompareNC(Context->Command, TEXT("drives")) == 0) {
        if (!RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_USB_DRIVES))) {
            ConsolePrint(TEXT("Unable to run embedded USB drive list script\n"));
        }
        return DF_RETURN_SUCCESS;
    } else if (StringCompareNC(Context->Command, TEXT("probe")) == 0) {
        if (!RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_USB_PROBE))) {
            ConsolePrint(TEXT("Unable to run embedded USB probe script\n"));
        }
        return DF_RETURN_SUCCESS;
    } else if (StringCompareNC(Context->Command, TEXT("devices")) == 0) {
        if (!RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_USB_DEVICES))) {
            ConsolePrint(TEXT("Unable to run embedded USB device list script\n"));
        }
        return DF_RETURN_SUCCESS;
    } else if (StringCompareNC(Context->Command, TEXT("ports")) == 0) {
        if (!RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_USB_PORTS))) {
            ConsolePrint(TEXT("Unable to run embedded USB port list script\n"));
        }
        return DF_RETURN_SUCCESS;
    } else if (StringCompareNC(Context->Command, TEXT("device-tree")) == 0) {
        if (!RunEmbeddedScript(Context, ShellGetEmbeddedScript(SHELL_EMBEDDED_SCRIPT_USB_DEVICE_TREE))) {
            ConsolePrint(TEXT("Unable to run embedded USB device tree script\n"));
        }
        return DF_RETURN_SUCCESS;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief NVMe control command (device list).
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_nvme(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 ||
        StringCompareNC(Context->Command, TEXT("list")) != 0) {
        ConsolePrint(TEXT("Usage: nvme list\n"));
        return DF_RETURN_SUCCESS;
    }

    DRIVER_ENUM_QUERY Query;
    MemorySet(&Query, 0, sizeof(Query));
    Query.Header.Size = sizeof(Query);
    Query.Header.Version = EXOS_ABI_VERSION;
    Query.Domain = ENUM_DOMAIN_PCI_DEVICE;
    Query.Flags = 0;

    UINT ProviderIndex = 0;
    BOOL Found = FALSE;
    BOOL Printed = FALSE;
    DRIVER_ENUM_PROVIDER Provider = NULL;
    UINT Index = 0;

    while (KernelEnumGetProvider(&Query, ProviderIndex, &Provider) == DF_RETURN_SUCCESS) {
        DRIVER_ENUM_ITEM Item;

        Found = TRUE;
        Query.Index = 0;

        MemorySet(&Item, 0, sizeof(Item));
        Item.Header.Size = sizeof(Item);
        Item.Header.Version = EXOS_ABI_VERSION;

        while (KernelEnumNext(Provider, &Query, &Item) == DF_RETURN_SUCCESS) {
            if (Item.DataSize < sizeof(DRIVER_ENUM_PCI_DEVICE)) {
                break;
            }

            const DRIVER_ENUM_PCI_DEVICE* Data = (const DRIVER_ENUM_PCI_DEVICE*)Item.Data;
            if (Data->BaseClass != NVME_PCI_CLASS ||
                Data->SubClass != NVME_PCI_SUBCLASS ||
                Data->ProgIF != NVME_PCI_PROG_IF) {
                continue;
            }

            ConsolePrint(TEXT("nvme%u: bus=%x device=%x function=%x vendor_identifier=%x device_identifier=%x revision=%x\n"),
                         Index,
                         (U32)Data->Bus,
                         (U32)Data->Dev,
                         (U32)Data->Func,
                         (U32)Data->VendorID,
                         (U32)Data->DeviceID,
                         (U32)Data->Revision);
            Index++;
            Printed = TRUE;
        }

        ProviderIndex++;
    }

    if (!Found) {
        ConsolePrint(TEXT("No PCI device provider detected\n"));
        return DF_RETURN_SUCCESS;
    }

    if (!Printed) {
        ConsolePrint(TEXT("No NVMe device detected\n"));
    }

    return DF_RETURN_SUCCESS;
}
