
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
#include "DisplaySession.h"
#include "DriverGetters.h"
#include "GFX.h"
#include "utils/SizeFormat.h"

/***************************************************************************/

/**
 * @brief Restore text console after graphics smoke rendering.
 */
static void RestoreConsoleAfterGraphicsSmoke(void) {
    if (DisplaySwitchToConsole() != FALSE) {
        return;
    }

    ERROR(TEXT("[RestoreConsoleAfterGraphicsSmoke] Console restore failed"));
}

/***************************************************************************/

/**
 * @brief Window procedure for gfx_smoke window rendering.
 * @param Window Target window handle.
 * @param Message Window message identifier.
 * @param Param1 First message parameter.
 * @param Param2 Second message parameter.
 * @return Message-specific result.
 */
static U32 GfxSmokeWindowFunc(HANDLE Window, U32 Message, U32 Param1, U32 Param2) {
    static const I32 GfxSmokeWindowWidth = 560;
    static const I32 GfxSmokeWindowHeight = 320;

    switch (Message) {
        case EWM_DRAW: {
            HANDLE GraphicsContext = NULL;
            RECTINFO RectangleInfo;
            LINEINFO LineInfo;

            GraphicsContext = GetWindowGC(Window);
            if (GraphicsContext == NULL) {
                return 0;
            }

            RectangleInfo.Header.Size = sizeof(RectangleInfo);
            RectangleInfo.Header.Version = EXOS_ABI_VERSION;
            RectangleInfo.Header.Flags = 0;
            RectangleInfo.GC = GraphicsContext;

            LineInfo.Header.Size = sizeof(LineInfo);
            LineInfo.Header.Version = EXOS_ABI_VERSION;
            LineInfo.Header.Flags = 0;
            LineInfo.GC = GraphicsContext;

            (void)SelectPen(GraphicsContext, GetSystemPen(SM_COLOR_HIGHLIGHT));
            (void)SelectBrush(GraphicsContext, GetSystemBrush(SM_COLOR_TITLE_BAR));
            RectangleInfo.X1 = 0;
            RectangleInfo.Y1 = 0;
            RectangleInfo.X2 = GfxSmokeWindowWidth - 1;
            RectangleInfo.Y2 = 32;
            (void)Rectangle(&RectangleInfo);

            (void)SelectPen(GraphicsContext, GetSystemPen(SM_COLOR_DARK_SHADOW));
            (void)SelectBrush(GraphicsContext, GetSystemBrush(SM_COLOR_CLIENT));
            RectangleInfo.X1 = 0;
            RectangleInfo.Y1 = 33;
            RectangleInfo.X2 = GfxSmokeWindowWidth - 1;
            RectangleInfo.Y2 = GfxSmokeWindowHeight - 1;
            (void)Rectangle(&RectangleInfo);

            (void)SelectPen(GraphicsContext, GetSystemPen(SM_COLOR_SELECTION));
            LineInfo.X1 = 12;
            LineInfo.Y1 = 48;
            LineInfo.X2 = GfxSmokeWindowWidth - 20;
            LineInfo.Y2 = GfxSmokeWindowHeight - 19;
            (void)Line(&LineInfo);

            LineInfo.X1 = GfxSmokeWindowWidth - 20;
            LineInfo.Y1 = 48;
            LineInfo.X2 = 12;
            LineInfo.Y2 = GfxSmokeWindowHeight - 19;
            (void)Line(&LineInfo);

            (void)EndWindowDraw(Window);
            return 0;
        }

        default:
            return DefWindowFunc(Window, Message, Param1, Param2);
    }
}

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

/************************************************************************/

/**
 * @brief Parse one unsigned decimal component from a mode token.
 * @param Text Mode token text.
 * @param InOutIndex Parse cursor.
 * @param ValueOut Parsed value.
 * @return TRUE on success.
 */
static BOOL ParseGraphicsModeComponent(LPCSTR Text, UINT* InOutIndex, U32* ValueOut) {
    UINT Index = 0;
    U32 Value = 0;
    BOOL HasDigit = FALSE;

    if (Text == NULL || InOutIndex == NULL || ValueOut == NULL) {
        return FALSE;
    }

    Index = *InOutIndex;
    while (Text[Index] >= '0' && Text[Index] <= '9') {
        HasDigit = TRUE;
        Value = (Value * 10) + (U32)(Text[Index] - '0');
        Index++;
    }

    if (!HasDigit) {
        return FALSE;
    }

    *InOutIndex = Index;
    *ValueOut = Value;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Parse one graphics mode token formatted as WidthxHeightxBitsPerPixel.
 * @param Token Mode token string.
 * @param InfoOut Parsed mode info.
 * @return TRUE on success.
 */
static BOOL ParseGraphicsModeToken(LPCSTR Token, LPGRAPHICSMODEINFO InfoOut) {
    UINT Index = 0;
    U32 Width = 0;
    U32 Height = 0;
    U32 BitsPerPixel = 0;

    if (Token == NULL || InfoOut == NULL || StringLength(Token) == 0) {
        return FALSE;
    }

    if (!ParseGraphicsModeComponent(Token, &Index, &Width)) {
        return FALSE;
    }

    if (Token[Index] != 'x' && Token[Index] != 'X') {
        return FALSE;
    }
    Index++;

    if (!ParseGraphicsModeComponent(Token, &Index, &Height)) {
        return FALSE;
    }

    if (Token[Index] != 'x' && Token[Index] != 'X') {
        return FALSE;
    }
    Index++;

    if (!ParseGraphicsModeComponent(Token, &Index, &BitsPerPixel)) {
        return FALSE;
    }

    if (Token[Index] != STR_NULL) {
        return FALSE;
    }

    if (Width == 0 || Height == 0 || BitsPerPixel == 0) {
        return FALSE;
    }

    InfoOut->Header.Size = sizeof(GRAPHICSMODEINFO);
    InfoOut->Header.Version = EXOS_ABI_VERSION;
    InfoOut->Header.Flags = 0;
    InfoOut->Width = Width;
    InfoOut->Height = Height;
    InfoOut->BitsPerPixel = BitsPerPixel;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Force one graphics backend and apply one graphics mode.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_gfx(LPSHELLCONTEXT Context) {
    STR DriverName[64];
    GRAPHICSMODEINFO ModeInfo;
    LPDRIVER GraphicsDriver = NULL;
    UINT ModeSetResult = 0;
    LPDESKTOP ActiveDesktop = NULL;
    LPCSTR ActiveBackendName = NULL;

    ParseNextCommandLineComponent(Context);
    StringCopy(DriverName, Context->Command);
    ParseNextCommandLineComponent(Context);

    if (StringLength(DriverName) == 0 || StringLength(Context->Command) == 0) {
        ConsolePrint(TEXT("Usage: gfx driver WidthxHeightxBitsPerPixel\n"));
        return DF_RETURN_SUCCESS;
    }

    if (!ParseGraphicsModeToken(Context->Command, &ModeInfo)) {
        ConsolePrint(TEXT("Usage: gfx driver WidthxHeightxBitsPerPixel\n"));
        return DF_RETURN_SUCCESS;
    }

    if (!GraphicsSelectorForceBackendByName(DriverName)) {
        ConsolePrint(TEXT("gfx: backend '%s' unavailable (supported: igpu|intel|gop|vesa)\n"), DriverName);
        return DF_RETURN_SUCCESS;
    }

    GraphicsDriver = GetGraphicsDriver();
    if (GraphicsDriver == NULL || GraphicsDriver->Command == NULL) {
        ConsolePrint(TEXT("gfx: no graphics driver available\n"));
        return DF_RETURN_SUCCESS;
    }

    ModeSetResult = GraphicsDriver->Command(DF_GFX_SETMODE, (UINT)(LPVOID)&ModeInfo);
    if (ModeSetResult != DF_RETURN_SUCCESS) {
        ConsolePrint(TEXT("gfx: mode set failed (%u)\n"), ModeSetResult);
        return DF_RETURN_SUCCESS;
    }

    ActiveDesktop = DisplaySessionGetActiveDesktop();
    if (ActiveDesktop != NULL) {
        (void)DisplaySessionSetDesktopMode(ActiveDesktop, GraphicsDriver, &ModeInfo);
    }

    ActiveBackendName = GraphicsSelectorGetActiveBackendName();
    if (ActiveBackendName != NULL && StringLength(ActiveBackendName) != 0) {
        ConsolePrint(TEXT("gfx: backend=%s mode=%ux%ux%u\n"),
            ActiveBackendName,
            ModeInfo.Width,
            ModeInfo.Height,
            ModeInfo.BitsPerPixel);
    } else {
        ConsolePrint(TEXT("gfx: mode=%ux%ux%u\n"),
            ModeInfo.Width,
            ModeInfo.Height,
            ModeInfo.BitsPerPixel);
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Draw a temporary desktop/window and return to text console.
 * @param Context Shell context.
 * @return DF_RETURN_SUCCESS on completion.
 */
U32 CMD_gfxsmoke(LPSHELLCONTEXT Context) {
    U32 DurationMilliseconds = 5000;
    LPDESKTOP Desktop = NULL;
    LPWINDOW Window = NULL;
    WINDOWINFO WindowInfo;

    ParseNextCommandLineComponent(Context);
    if (StringLength(Context->Command) != 0) {
        DurationMilliseconds = StringToU32(Context->Command);
        if (DurationMilliseconds == 0) {
            ConsolePrint(TEXT("Usage: gfx_smoke [DurationMilliseconds]\n"));
            return DF_RETURN_SUCCESS;
        }
    }

    Desktop = CreateDesktop();
    if (Desktop == NULL) {
        ConsolePrint(TEXT("gfx_smoke: desktop creation failed\n"));
        return DF_RETURN_SUCCESS;
    }

    if (DisplaySwitchToDesktop(Desktop) == FALSE) {
        ConsolePrint(TEXT("gfx_smoke: desktop show failed\n"));
        DeleteDesktop(Desktop);
        return DF_RETURN_SUCCESS;
    }

    WindowInfo.Header.Size = sizeof(WindowInfo);
    WindowInfo.Header.Version = EXOS_ABI_VERSION;
    WindowInfo.Header.Flags = 0;
    WindowInfo.Window = NULL;
    WindowInfo.Parent = (HANDLE)Desktop->Window;
    WindowInfo.Function = GfxSmokeWindowFunc;
    WindowInfo.Style = EWS_VISIBLE;
    WindowInfo.ID = 0;
    WindowInfo.WindowPosition.X = 120;
    WindowInfo.WindowPosition.Y = 80;
    WindowInfo.WindowSize.X = 560;
    WindowInfo.WindowSize.Y = 320;
    WindowInfo.ShowHide = TRUE;

    Window = CreateWindow(&WindowInfo);
    if (Window == NULL) {
        ConsolePrint(TEXT("gfx_smoke: window creation failed\n"));
        RestoreConsoleAfterGraphicsSmoke();
        DeleteDesktop(Desktop);
        return DF_RETURN_SUCCESS;
    }

    (void)SendMessage((HANDLE)Window, EWM_DRAW, 0, 0);

    Sleep(DurationMilliseconds);

    RestoreConsoleAfterGraphicsSmoke();
    DeleteDesktop(Desktop);
    ConsolePrint(TEXT("gfx_smoke: done\n"));

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

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

/***************************************************************************/

/**
 * @brief Skip spaces inside one command line text.
 * @param Text Source command line.
 * @param InOutIndex Index cursor to advance.
 */
