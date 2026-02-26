
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


    Shell commands

\************************************************************************/

#include "shell/Shell-Commands-Private.h"
#include "Autotest.h"
#include "utils/SizeFormat.h"

/**
 * @brief Print known non-empty driver aliases.
 */
static void PrintKnownDriverAliases(void) {
    UINT PrintedCount = 0;
    LPLIST DriverList = GetDriverList();

    if (DriverList == NULL) {
        ConsolePrint(TEXT("none"));
        return;
    }

    for (LPLISTNODE Node = DriverList->First; Node; Node = Node->Next) {
        LPDRIVER Driver = (LPDRIVER)Node;

        SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
            if (StringLength(Driver->Alias) == 0) {
                continue;
            }

            if (PrintedCount != 0) {
                ConsolePrint(TEXT("|"));
            }

            ConsolePrint(TEXT("%s"), Driver->Alias);
            PrintedCount++;
        }
    }

    if (PrintedCount == 0) {
        ConsolePrint(TEXT("none"));
    }
}

/***************************************************************************/

/**
 * @brief Print detailed information for one driver.
 * @param Driver Driver descriptor.
 */
static void PrintDriverDetails(LPDRIVER Driver) {
    UINT VersionFromCommand = 0;
    UINT CapsFromCommand = 0;
    UINT LastFunctionFromCommand = 0;
    U16 VersionMajorFromCommand = 0;
    U16 VersionMinorFromCommand = 0;
    UINT DomainCount = 0;
    UINT Flags = 0;
    BOOL IsReady = FALSE;
    BOOL IsCritical = FALSE;

    Flags = Driver->Flags;
    IsReady = (Flags & DRIVER_FLAG_READY) != 0;
    IsCritical = (Flags & DRIVER_FLAG_CRITICAL) != 0;

    if (Driver->Command != NULL) {
        VersionFromCommand = Driver->Command(DF_GET_VERSION, 0);
        CapsFromCommand = Driver->Command(DF_GET_CAPS, 0);
        LastFunctionFromCommand = Driver->Command(DF_GET_LAST_FUNCTION, 0);
        VersionMajorFromCommand = (U16)((VersionFromCommand >> 16) & 0xFFFF);
        VersionMinorFromCommand = (U16)(VersionFromCommand & 0xFFFF);
    }

    ConsolePrint(TEXT("Address            : %p\n"), (LPVOID)Driver);
    ConsolePrint(TEXT("Alias              : %s\n"),
                 StringLength(Driver->Alias) != 0 ? Driver->Alias : TEXT("<none>"));
    ConsolePrint(TEXT("Type               : %s (%x)\n"), DriverTypeToText(Driver->Type), Driver->Type);
    ConsolePrint(TEXT("Version fields     : %u.%u\n"), Driver->VersionMajor, Driver->VersionMinor);
    ConsolePrint(TEXT("Version command    : %u.%u (raw=%x)\n"),
                 (U32)VersionMajorFromCommand,
                 (U32)VersionMinorFromCommand,
                 VersionFromCommand);
    ConsolePrint(TEXT("Designer           : %s\n"), Driver->Designer);
    ConsolePrint(TEXT("Manufacturer       : %s\n"), Driver->Manufacturer);
    ConsolePrint(TEXT("Product            : %s\n"), Driver->Product);
    ConsolePrint(TEXT("Flags              : %x\n"), Flags);
    ConsolePrint(TEXT("Ready              : %s\n"), IsReady ? TEXT("yes") : TEXT("no"));
    ConsolePrint(TEXT("Critical           : %s\n"), IsCritical ? TEXT("yes") : TEXT("no"));
    ConsolePrint(TEXT("Command            : %p\n"), (LPVOID)Driver->Command);
    ConsolePrint(TEXT("Caps command       : %x\n"), CapsFromCommand);
    ConsolePrint(TEXT("Last function      : %x\n"), LastFunctionFromCommand);

    DomainCount = Driver->EnumDomainCount;
    if (DomainCount > DRIVER_ENUM_MAX_DOMAINS) {
        DomainCount = DRIVER_ENUM_MAX_DOMAINS;
    }

    ConsolePrint(TEXT("Enum domains       : %u\n"), Driver->EnumDomainCount);
    if (DomainCount == 0) {
        ConsolePrint(TEXT("  <none>\n"));
    } else {
        for (UINT Index = 0; Index < DomainCount; Index++) {
            UINT Domain = Driver->EnumDomains[Index];
            ConsolePrint(TEXT("  %u: %s (%x)\n"), Index, DriverDomainToText(Domain), Domain);
        }
    }
}

/***************************************************************************/

/**
 * @brief Print one driver detail view selected by alias.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_driver(LPSHELLCONTEXT Context) {
    BOOL Found = FALSE;
    LPLIST DriverList = NULL;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Usage: driver Alias\n"));
        return DF_RETURN_SUCCESS;
    }

    DriverList = GetDriverList();
    if (DriverList == NULL || DriverList->First == NULL) {
        ConsolePrint(TEXT("No driver detected\n"));
        return DF_RETURN_SUCCESS;
    }

    for (LPLISTNODE Node = DriverList->First; Node; Node = Node->Next) {
        LPDRIVER Driver = (LPDRIVER)Node;

        SAFE_USE_VALID_ID(Driver, KOID_DRIVER) {
            if (StringLength(Driver->Alias) == 0) {
                continue;
            }

            if (StringCompareNC(Driver->Alias, Context->Command) != 0) {
                continue;
            }

            PrintDriverDetails(Driver);
            Found = TRUE;
            break;
        }
    }

    if (!Found) {
        ConsolePrint(TEXT("driver: alias '%s' not found (known: "), Context->Command);
        PrintKnownDriverAliases();
        ConsolePrint(TEXT(")\n"));
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_killtask(LPSHELLCONTEXT Context) {
    U32 TaskNum = 0;
    LPTASK Task = NULL;
    ParseNextCommandLineComponent(Context);
    TaskNum = StringToU32(Context->Command);
    LPLIST TaskList = GetTaskList();
    Task = (LPTASK)ListGetItem(TaskList, TaskNum);
    if (Task) KillTask(Task);

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_showprocess(LPSHELLCONTEXT Context) {
    LPPROCESS Process;
    ParseNextCommandLineComponent(Context);
    LPLIST ProcessList = GetProcessList();
    Process = ListGetItem(ProcessList, StringToU32(Context->Command));
    if (Process) DumpProcess(Process);

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_showtask(LPSHELLCONTEXT Context) {
    LPTASK Task;
    ParseNextCommandLineComponent(Context);
    LPLIST TaskList = GetTaskList();
    Task = ListGetItem(TaskList, StringToU32(Context->Command));

    if (Task) {
        DumpTask(Task);
    } else {
        STR Text[MAX_FILE_NAME];

        for (LPTASK Task = (LPTASK)TaskList->First; Task != NULL; Task = (LPTASK)Task->Next) {
            StringPrintFormat(Text, TEXT("%x Status %x\n"), Task, Task->Status);
            ConsolePrint(Text);
        }
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_memedit(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);
    MemoryEditor(StringToU32(Context->Command));

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_memorymap(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    LPPROCESS Process = &KernelProcess;
    LPMEMORY_REGION_DESCRIPTOR Descriptor = Process->RegionListHead;
    UINT Index = 0;

    ConsolePrint(TEXT("Kernel regions: %u\n"), Process->RegionCount);

    while (Descriptor != NULL) {
        LPCSTR Tag = (Descriptor->Tag[0] == STR_NULL) ? TEXT("???") : Descriptor->Tag;
        STR SizeText[32];
        SizeFormatBytesText(U64_FromUINT(Descriptor->Size), SizeText);
        if (Descriptor->PhysicalBase == 0) {
            ConsolePrint(TEXT("%u: tag=%s base=%p size=%s phys=???\n"),
                Index,
                Tag,
                (LPVOID)Descriptor->CanonicalBase,
                SizeText);
        } else {
            ConsolePrint(TEXT("%u: tag=%s base=%p size=%s phys=%p\n"),
                Index,
                Tag,
                (LPVOID)Descriptor->CanonicalBase,
                SizeText,
                (LPVOID)Descriptor->PhysicalBase);
        }
        Descriptor = (LPMEMORY_REGION_DESCRIPTOR)Descriptor->Next;
        Index++;
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

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

/***************************************************************************/

U32 CMD_network(LPSHELLCONTEXT Context) {
    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 ||
        StringCompareNC(Context->Command, TEXT("devices")) != 0) {
        ConsolePrint(TEXT("Usage: network devices\n"));
        return DF_RETURN_SUCCESS;
    }

    LPLIST NetworkDeviceList = GetNetworkDeviceList();
    if (NetworkDeviceList == NULL || NetworkDeviceList->First == NULL) {
        ConsolePrint(TEXT("No network device detected\n"));
        return DF_RETURN_SUCCESS;
    }

    SAFE_USE(NetworkDeviceList) {
        for (LPLISTNODE Node = NetworkDeviceList->First; Node; Node = Node->Next) {
            LPNETWORK_DEVICE_CONTEXT NetContext = (LPNETWORK_DEVICE_CONTEXT)Node;

            SAFE_USE_VALID_ID(NetContext, KOID_NETWORKDEVICE) {
                LPPCI_DEVICE Device = NetContext->Device;

                SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
                    SAFE_USE_VALID_ID(Device->Driver, KOID_DRIVER) {
                        NETWORKINFO Info;
                        MemorySet(&Info, 0, sizeof(Info));
                        NETWORKGETINFO GetInfo = {.Device = Device, .Info = &Info};
                        Device->Driver->Command(DF_NT_GETINFO, (UINT)(LPVOID)&GetInfo);

                        U32 IpHost = Ntohl(NetContext->ActiveConfig.LocalIPv4_Be);
                        U8 Ip1 = (IpHost >> 24) & 0xFF;
                        U8 Ip2 = (IpHost >> 16) & 0xFF;
                        U8 Ip3 = (IpHost >> 8) & 0xFF;
                        U8 Ip4 = IpHost & 0xFF;

                        ConsolePrint(TEXT("Name         : %s\n"), Device->Name);
                        ConsolePrint(TEXT("Manufacturer : %s\n"), Device->Driver->Manufacturer);
                        ConsolePrint(TEXT("Product      : %s\n"), Device->Driver->Product);
                        ConsolePrint(TEXT("MAC          : %x:%x:%x:%x:%x:%x\n"),
                                    Info.MAC[0], Info.MAC[1], Info.MAC[2],
                                    Info.MAC[3], Info.MAC[4], Info.MAC[5]);
                        ConsolePrint(TEXT("IP Address   : %u.%u.%u.%u\n"), Ip1, Ip2, Ip3, Ip4);
                        ConsolePrint(TEXT("Link         : %s\n"), Info.LinkUp ? TEXT("UP") : TEXT("DOWN"));
                        ConsolePrint(TEXT("Speed        : %u Mbps\n"), Info.SpeedMbps);
                        ConsolePrint(TEXT("Duplex       : %s\n"), Info.DuplexFull ? TEXT("FULL") : TEXT("HALF"));
                        ConsolePrint(TEXT("MTU          : %u\n"), Info.MTU);
                        ConsolePrint(TEXT("Initialized  : %s\n"), NetContext->IsInitialized ? TEXT("YES") : TEXT("NO"));
                        ConsolePrint(TEXT("\n"));
                    }
                }
            }
        }
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

U32 CMD_pic(LPSHELLCONTEXT Context) {
    UNUSED(Context);

    ConsolePrint(TEXT("8259-1 RM mask : %08b\n"), KernelStartup.IRQMask_21_RM);
    ConsolePrint(TEXT("8259-2 RM mask : %08b\n"), KernelStartup.IRQMask_A1_RM);
    ConsolePrint(TEXT("8259-1 PM mask : %08b\n"), KernelStartup.IRQMask_21_PM);
    ConsolePrint(TEXT("8259-2 PM mask : %08b\n"), KernelStartup.IRQMask_A1_PM);

    return DF_RETURN_SUCCESS;
}

U32 CMD_outp(LPSHELLCONTEXT Context) {
    U32 Port, Data;
    ParseNextCommandLineComponent(Context);
    Port = StringToU32(Context->Command);
    ParseNextCommandLineComponent(Context);
    Data = StringToU32(Context->Command);
    OutPortByte(Port, Data);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

U32 CMD_inp(LPSHELLCONTEXT Context) {
    U32 Port, Data;
    ParseNextCommandLineComponent(Context);
    Port = StringToU32(Context->Command);
    Data = InPortByte(Port);
    ConsolePrint(TEXT("Port %X = %X\n"), Port, Data);

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

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

U32 CMD_prof(LPSHELLCONTEXT Context) {
    UNUSED(Context);
    ProfileDump();
    return 0;
}

/***************************************************************************/

/**
 * @brief Run one on-demand autotest module.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS.
 */
U32 CMD_autotest(LPSHELLCONTEXT Context) {
    BOOL PreviousErrorConsoleEnabled;
    BOOL Result = FALSE;

    ParseNextCommandLineComponent(Context);

    if (StringLength(Context->Command) == 0 || StringCompareNC(Context->Command, TEXT("stack")) != 0) {
        ConsolePrint(TEXT("Usage: autotest stack\n"));
        return DF_RETURN_SUCCESS;
    }

    PreviousErrorConsoleEnabled = KernelLogGetErrorConsoleEnabled();
    KernelLogSetErrorConsoleEnabled(FALSE);
    Result = RunSingleTestByName(TEXT("TestCopyStack"));
    KernelLogSetErrorConsoleEnabled(PreviousErrorConsoleEnabled);

    if (Result) {
        ConsolePrint(TEXT("autotest stack: passed\n"));
    } else {
        ConsolePrint(TEXT("autotest stack: failed\n"));
    }

    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

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

/***************************************************************************/

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
        LPLIST UsbStorageList = GetUsbStorageList();
        if (UsbStorageList == NULL || UsbStorageList->First == NULL) {
            ConsolePrint(TEXT("No USB drive detected\n"));
            return DF_RETURN_SUCCESS;
        }

        UINT Index = 0;
        for (LPLISTNODE Node = UsbStorageList->First; Node; Node = Node->Next) {
            STR BlockSizeText[32];
            LPUSB_STORAGE_ENTRY Entry = (LPUSB_STORAGE_ENTRY)Node;
            if (Entry == NULL) {
                continue;
            }

            SizeFormatBytesText(U64_FromUINT(Entry->BlockSize), BlockSizeText);
            ConsolePrint(TEXT("usb%u: addr=%x vid=%x pid=%x blocks=%u block_size=%s state=%s\n"),
                         Index,
                         (U32)Entry->Address,
                         (U32)Entry->VendorId,
                         (U32)Entry->ProductId,
                         Entry->BlockCount,
                         BlockSizeText,
                         Entry->Present ? TEXT("online") : TEXT("offline"));
            Index++;
        }

        return DF_RETURN_SUCCESS;
    }

    DRIVER_ENUM_QUERY Query;
    MemorySet(&Query, 0, sizeof(Query));
    Query.Header.Size = sizeof(Query);
    Query.Header.Version = EXOS_ABI_VERSION;
    if (StringCompareNC(Context->Command, TEXT("probe")) == 0) {
        DRIVER_ENUM_QUERY PortQuery;
        MemorySet(&PortQuery, 0, sizeof(PortQuery));
        PortQuery.Header.Size = sizeof(PortQuery);
        PortQuery.Header.Version = EXOS_ABI_VERSION;
        PortQuery.Domain = ENUM_DOMAIN_XHCI_PORT;
        PortQuery.Flags = 0;

        UINT ProviderIndexProbe = 0;
        DRIVER_ENUM_PROVIDER ProviderProbe = NULL;
        BOOL FoundProbe = FALSE;

        while (KernelEnumGetProvider(&PortQuery, ProviderIndexProbe, &ProviderProbe) == DF_RETURN_SUCCESS) {
            DRIVER_ENUM_ITEM ItemProbe;
            PortQuery.Index = 0;
            FoundProbe = TRUE;

            MemorySet(&ItemProbe, 0, sizeof(ItemProbe));
            ItemProbe.Header.Size = sizeof(ItemProbe);
            ItemProbe.Header.Version = EXOS_ABI_VERSION;

            while (KernelEnumNext(ProviderProbe, &PortQuery, &ItemProbe) == DF_RETURN_SUCCESS) {
                const DRIVER_ENUM_XHCI_PORT* Data = (const DRIVER_ENUM_XHCI_PORT*)ItemProbe.Data;
                if (ItemProbe.DataSize < sizeof(DRIVER_ENUM_XHCI_PORT)) {
                    break;
                }
                if (Data->Connected) {
                    if (Data->LastEnumError == XHCI_ENUM_ERROR_ENABLE_SLOT) {
                        ConsolePrint(TEXT("P%u Err=%s C=%u\n"),
                                     (U32)Data->PortNumber,
                                     UsbEnumErrorToString(Data->LastEnumError),
                                     (U32)Data->LastEnumCompletion);
                    } else {
                        ConsolePrint(TEXT("P%u Err=%s\n"),
                                     (U32)Data->PortNumber,
                                     UsbEnumErrorToString(Data->LastEnumError));
                    }
                }
            }
            ProviderIndexProbe++;
        }

        if (!FoundProbe) {
            ConsolePrint(TEXT("No xHCI controller detected\n"));
        }
        return DF_RETURN_SUCCESS;
    } else if (StringCompareNC(Context->Command, TEXT("devices")) == 0) {
        Query.Domain = ENUM_DOMAIN_USB_DEVICE;
    } else if (StringCompareNC(Context->Command, TEXT("device-tree")) == 0) {
        Query.Domain = ENUM_DOMAIN_USB_NODE;
    } else {
        Query.Domain = ENUM_DOMAIN_XHCI_PORT;
    }
    Query.Flags = 0;

    UINT ProviderIndex = 0;
    BOOL Found = FALSE;
    BOOL Printed = FALSE;
    DRIVER_ENUM_PROVIDER Provider = NULL;

    while (KernelEnumGetProvider(&Query, ProviderIndex, &Provider) == DF_RETURN_SUCCESS) {
        DRIVER_ENUM_ITEM Item;
        STR Buffer[256];

        Found = TRUE;
        Query.Index = 0;

        MemorySet(&Item, 0, sizeof(Item));
        Item.Header.Size = sizeof(Item);
        Item.Header.Version = EXOS_ABI_VERSION;

        while (KernelEnumNext(Provider, &Query, &Item) == DF_RETURN_SUCCESS) {
            if (KernelEnumPretty(Provider, &Query, &Item, Buffer, sizeof(Buffer)) == DF_RETURN_SUCCESS) {
                ConsolePrint(TEXT("%s\n"), Buffer);
                Printed = TRUE;
            }
        }

        ProviderIndex++;
    }

    if (!Found) {
        ConsolePrint(TEXT("No xHCI controller detected\n"));
        return DF_RETURN_SUCCESS;
    }

    if (!Printed && Query.Domain == ENUM_DOMAIN_USB_DEVICE) {
        ConsolePrint(TEXT("No USB device detected\n"));
    } else if (!Printed && Query.Domain == ENUM_DOMAIN_USB_NODE) {
        ConsolePrint(TEXT("No USB device tree detected\n"));
    }
    return DF_RETURN_SUCCESS;
}

/***************************************************************************/

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
