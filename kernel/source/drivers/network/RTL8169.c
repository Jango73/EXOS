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


    RTL8169

\************************************************************************/

#include "drivers/network/RTL8169.h"

#include "Kernel.h"
#include "Log.h"
#include "Memory.h"

/************************************************************************/

typedef struct tag_RTL8169_DEVICE RTL8169_DEVICE, *LPRTL8169_DEVICE;

/************************************************************************/

#define RTL8169_VERSION_MAJOR 1
#define RTL8169_VERSION_MINOR 0
#define RTL8169_DEFAULT_MTU 1500
#define RTL8169_LINK_SPEED_UNKNOWN 0

/************************************************************************/

struct tag_RTL8169_DEVICE {
    PCI_DEVICE_FIELDS

    const RTL8169_DEVICE_INFO *DeviceInfo;
    U32 HardwareRevision;
    U8 Mac[6];
    NT_RXCB RxCallback;
    LPVOID RxUserData;
};

/************************************************************************/

static UINT RTL8169Commands(UINT Function, UINT Parameter);
static LPPCI_DEVICE RTL8169Attach(LPPCI_DEVICE PciDevice);
static const RTL8169_DEVICE_INFO *RTL8169FindDeviceInfo(U16 VendorID, U16 DeviceID);
static void RTL8169BuildPlaceholderMac(LPRTL8169_DEVICE Device);
static void RTL8169InitializeHardwareDescription(LPRTL8169_DEVICE Device);
static U32 RTL8169OnReset(const NETWORK_RESET *Reset);
static U32 RTL8169OnGetInfo(const NETWORK_GET_INFO *GetInfo);
static U32 RTL8169OnSetReceiveCallback(const NETWORK_SET_RX_CB *Set);
static U32 RTL8169OnSend(const NETWORK_SEND *Send);
static U32 RTL8169OnPoll(const NETWORK_POLL *Poll);
static U32 RTL8169OnEnableInterrupts(DEVICE_INTERRUPT_CONFIG *Config);
static U32 RTL8169OnDisableInterrupts(DEVICE_INTERRUPT_CONFIG *Config);

/************************************************************************/

static DRIVER_MATCH RTL8169MatchTable[] = {
    RTL8169_MATCH_ENTRY(RTL8169_DEVICE_8161),
    RTL8169_MATCH_ENTRY(RTL8169_DEVICE_8168),
};

/************************************************************************/

static const RTL8169_DEVICE_INFO RTL8169DeviceInfoTable[] = {
    {
        .VendorID = RTL8169_VENDOR_REALTEK,
        .DeviceID = RTL8169_DEVICE_8161,
        .Family = RTL8169_DEVICE_FAMILY_8111,
        .QuirkFlags = RTL8169_QUIRK_PCIE_GIGABIT |
                      RTL8169_QUIRK_REVISION_BY_MAC_VERSION |
                      RTL8169_QUIRK_SHARED_8111_8168_REGISTERS,
        .ProductName = TEXT("RTL8111 Family"),
    },
    {
        .VendorID = RTL8169_VENDOR_REALTEK,
        .DeviceID = RTL8169_DEVICE_8168,
        .Family = RTL8169_DEVICE_FAMILY_8168,
        .QuirkFlags = RTL8169_QUIRK_PCIE_GIGABIT |
                      RTL8169_QUIRK_REVISION_BY_MAC_VERSION |
                      RTL8169_QUIRK_SHARED_8111_8168_REGISTERS |
                      RTL8169_QUIRK_SHARED_8411_REGISTERS,
        .ProductName = TEXT("RTL8168/8411 Family"),
    },
};

/************************************************************************/

PCI_DRIVER DATA_SECTION RTL8169Driver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_NETWORK,
    .VersionMajor = RTL8169_VERSION_MAJOR,
    .VersionMinor = RTL8169_VERSION_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Realtek",
    .Product = "RTL8111/8168/8411 Family",
    .Alias = "rtl8169",
    .Command = RTL8169Commands,
    .Matches = RTL8169MatchTable,
    .MatchCount = sizeof(RTL8169MatchTable) / sizeof(RTL8169MatchTable[0]),
    .Attach = RTL8169Attach,
};

/************************************************************************/

/**
 * @brief Retrieves the RTL8169 PCI driver descriptor.
 * @return Pointer to the RTL8169 PCI driver.
 */
LPDRIVER RTL8169GetDriver(void) {
    return (LPDRIVER)&RTL8169Driver;
}

/************************************************************************/

/**
 * @brief Handles PCI probe requests for the Realtek RTL8169 family.
 * @param PciInfo PCI function information provided by the PCI layer.
 * @return DF_RETURN_SUCCESS when the function matches the Ethernet family.
 */
static U32 RTL8169OnProbe(const PCI_INFO *PciInfo) {
    if (PciInfo == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (PciInfo->BaseClass != PCI_CLASS_NETWORK) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (PciInfo->SubClass != PCI_SUBCLASS_ETHERNET) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Looks up the compact hardware description for a PCI identifier.
 * @param VendorID PCI vendor identifier.
 * @param DeviceID PCI device identifier.
 * @return Matching device-description entry or NULL when unsupported.
 */
static const RTL8169_DEVICE_INFO *RTL8169FindDeviceInfo(U16 VendorID, U16 DeviceID) {
    UINT Index;

    for (Index = 0; Index < sizeof(RTL8169DeviceInfoTable) / sizeof(RTL8169DeviceInfoTable[0]); Index++) {
        const RTL8169_DEVICE_INFO *DeviceInfo = &RTL8169DeviceInfoTable[Index];

        if (DeviceInfo->VendorID == VendorID && DeviceInfo->DeviceID == DeviceID) {
            return DeviceInfo;
        }
    }

    return NULL;
}

/************************************************************************/

/**
 * @brief Builds a deterministic placeholder MAC address for Step 2.
 *
 * The permanent hardware address is introduced later with MMIO and EEPROM
 * support. Until then, provide a stable locally administered unicast address
 * so the network stack can initialize without an all-zero MAC.
 *
 * @param Device Target RTL8169 device context.
 */
static void RTL8169BuildPlaceholderMac(LPRTL8169_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    Device->Mac[0] = 0x02;
    Device->Mac[1] = 0x10;
    Device->Mac[2] = 0xEC;
    Device->Mac[3] = Device->Info.Bus;
    Device->Mac[4] = Device->Info.Dev;
    Device->Mac[5] = Device->Info.Func;
}

/************************************************************************/

/**
 * @brief Initializes the revision-independent hardware-description state.
 * @param Device Target RTL8169 device context.
 */
static void RTL8169InitializeHardwareDescription(LPRTL8169_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    Device->DeviceInfo = RTL8169FindDeviceInfo(Device->Info.VendorID, Device->Info.DeviceID);
    Device->HardwareRevision = 0;
}

/************************************************************************/

/**
 * @brief Attaches the driver to a supported PCI function.
 *
 * Step 2 establishes the generic EXOS network-driver shape and returns an
 * attached PCI device object. Hardware MMIO probing and controller setup are
 * deferred to later steps.
 *
 * @param PciDevice Supported PCI function.
 * @return Attached PCI device on success, NULL on failure.
 */
static LPPCI_DEVICE RTL8169Attach(LPPCI_DEVICE PciDevice) {
    LPRTL8169_DEVICE Device;

    if (PciDevice == NULL) {
        return NULL;
    }

    Device = (LPRTL8169_DEVICE)CreateKernelObject(sizeof(RTL8169_DEVICE), KOID_PCIDEVICE);
    if (Device == NULL) {
        ERROR(TEXT("[RTL8169Attach] Failed to allocate device object"));
        return NULL;
    }

    Device->Driver = PciDevice->Driver;
    Device->Info = PciDevice->Info;
    MemoryCopy(Device->BARPhys, PciDevice->BARPhys, sizeof(Device->BARPhys));
    MemoryCopy((LPVOID)Device->BARMapped, (LPVOID)PciDevice->BARMapped, sizeof(Device->BARMapped));
    MemoryCopy(Device->Name, PciDevice->Name, sizeof(Device->Name));
    InitMutex(&(Device->Mutex));
    RTL8169InitializeHardwareDescription(Device);
    RTL8169BuildPlaceholderMac(Device);

    if (Device->DeviceInfo == NULL) {
        ERROR(TEXT("[RTL8169Attach] Missing hardware description for %x:%x"),
              (UINT)Device->Info.VendorID,
              (UINT)Device->Info.DeviceID);
        ReleaseKernelObject(Device);
        return NULL;
    }

    DEBUG(TEXT("[RTL8169Attach] Attached %s controller %x:%x on %x:%x.%x"),
          Device->DeviceInfo->ProductName,
          (UINT)Device->Info.VendorID,
          (UINT)Device->Info.DeviceID,
          (UINT)Device->Info.Bus,
          (UINT)Device->Info.Dev,
          (UINT)Device->Info.Func);
    return (LPPCI_DEVICE)Device;
}

/************************************************************************/

/**
 * @brief Reset callback invoked by the network manager.
 * @param Reset Reset request.
 * @return DF_RETURN_SUCCESS when the attached device handle is valid.
 */
static U32 RTL8169OnReset(const NETWORK_RESET *Reset) {
    if (Reset == NULL || Reset->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Reports the current device information.
 * @param GetInfo Information query and output buffer.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8169OnGetInfo(const NETWORK_GET_INFO *GetInfo) {
    LPRTL8169_DEVICE Device;

    if (GetInfo == NULL || GetInfo->Device == NULL || GetInfo->Info == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPRTL8169_DEVICE)GetInfo->Device;

    GetInfo->Info->MAC[0] = Device->Mac[0];
    GetInfo->Info->MAC[1] = Device->Mac[1];
    GetInfo->Info->MAC[2] = Device->Mac[2];
    GetInfo->Info->MAC[3] = Device->Mac[3];
    GetInfo->Info->MAC[4] = Device->Mac[4];
    GetInfo->Info->MAC[5] = Device->Mac[5];
    GetInfo->Info->LinkUp = FALSE;
    GetInfo->Info->SpeedMbps = RTL8169_LINK_SPEED_UNKNOWN;
    GetInfo->Info->DuplexFull = FALSE;
    GetInfo->Info->MTU = RTL8169_MAXIMUM_MTU;

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Stores the receive callback for later RX-path implementation.
 * @param Set Callback registration request.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8169OnSetReceiveCallback(const NETWORK_SET_RX_CB *Set) {
    LPRTL8169_DEVICE Device;

    if (Set == NULL || Set->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPRTL8169_DEVICE)Set->Device;
    Device->RxCallback = Set->Callback;
    Device->RxUserData = Set->UserData;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Transmit stub used until TX rings are implemented.
 * @param Send Send request.
 * @return DF_RETURN_NOT_IMPLEMENTED for the Step 2 integration skeleton.
 */
static U32 RTL8169OnSend(const NETWORK_SEND *Send) {
    if (Send == NULL || Send->Device == NULL || Send->Data == NULL || Send->Length == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Poll stub used until RX rings are implemented.
 * @param Poll Poll request.
 * @return DF_RETURN_SUCCESS while no receive path exists yet.
 */
static U32 RTL8169OnPoll(const NETWORK_POLL *Poll) {
    if (Poll == NULL || Poll->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Interrupt-enable stub for polling-only early integration.
 * @param Config Interrupt configuration request.
 * @return DF_RETURN_NOT_IMPLEMENTED so the network manager stays in polling mode.
 */
static U32 RTL8169OnEnableInterrupts(DEVICE_INTERRUPT_CONFIG *Config) {
    if (Config == NULL || Config->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Config->VectorSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Config->InterruptEnabled = FALSE;
    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Interrupt-disable stub for polling-only early integration.
 * @param Config Interrupt configuration request.
 * @return DF_RETURN_SUCCESS when the request is structurally valid.
 */
static U32 RTL8169OnDisableInterrupts(DEVICE_INTERRUPT_CONFIG *Config) {
    if (Config == NULL || Config->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Config->VectorSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Config->InterruptEnabled = FALSE;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Driver load callback.
 * @return DF_RETURN_SUCCESS.
 */
static U32 RTL8169OnLoad(void) {
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Driver unload callback.
 * @return DF_RETURN_SUCCESS.
 */
static U32 RTL8169OnUnload(void) {
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Retrieves the encoded driver version.
 * @return Driver version encoded with MAKE_VERSION.
 */
static U32 RTL8169OnGetVersion(void) {
    return MAKE_VERSION(RTL8169_VERSION_MAJOR, RTL8169_VERSION_MINOR);
}

/************************************************************************/

/**
 * @brief Retrieves the driver capabilities bitmask.
 * @return Zero because advanced capabilities are not exposed yet.
 */
static U32 RTL8169OnGetCaps(void) {
    return 0;
}

/************************************************************************/

/**
 * @brief Returns the highest implemented driver function identifier.
 * @return DF_DEV_DISABLE_INTERRUPT for the Step 2 integration skeleton.
 */
static U32 RTL8169OnGetLastFunction(void) {
    return DF_DEV_DISABLE_INTERRUPT;
}

/************************************************************************/

/**
 * @brief Dispatches RTL8169 driver commands.
 * @param Function Requested driver function.
 * @param Parameter Optional parameter payload.
 * @return Driver-specific DF_RETURN_* code.
 */
static UINT RTL8169Commands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return RTL8169OnLoad();
        case DF_UNLOAD:
            return RTL8169OnUnload();
        case DF_GET_VERSION:
            return RTL8169OnGetVersion();
        case DF_GET_CAPS:
            return RTL8169OnGetCaps();
        case DF_GET_LAST_FUNCTION:
            return RTL8169OnGetLastFunction();
        case DF_PROBE:
            return RTL8169OnProbe((const PCI_INFO *)(LPVOID)Parameter);
        case DF_NT_RESET:
            return RTL8169OnReset((const NETWORK_RESET *)(LPVOID)Parameter);
        case DF_NT_GETINFO:
            return RTL8169OnGetInfo((const NETWORK_GET_INFO *)(LPVOID)Parameter);
        case DF_NT_SETRXCB:
            return RTL8169OnSetReceiveCallback((const NETWORK_SET_RX_CB *)(LPVOID)Parameter);
        case DF_DEV_ENABLE_INTERRUPT:
            return RTL8169OnEnableInterrupts((DEVICE_INTERRUPT_CONFIG *)(LPVOID)Parameter);
        case DF_DEV_DISABLE_INTERRUPT:
            return RTL8169OnDisableInterrupts((DEVICE_INTERRUPT_CONFIG *)(LPVOID)Parameter);
        case DF_NT_SEND:
            return RTL8169OnSend((const NETWORK_SEND *)(LPVOID)Parameter);
        case DF_NT_POLL:
            return RTL8169OnPoll((const NETWORK_POLL *)(LPVOID)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
