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

/************************************************************************/

typedef struct tag_RTL8169_DEVICE RTL8169_DEVICE, *LPRTL8169_DEVICE;

/************************************************************************/

#define RTL8169_VERSION_MAJOR 1
#define RTL8169_VERSION_MINOR 0
#define RTL8169_LINK_SPEED_UNKNOWN 0

/************************************************************************/

struct tag_RTL8169_DEVICE {
    REALTEK_NETWORK_COMMON_DEVICE_FIELDS
    const RTL8169_DEVICE_INFO *DeviceInfo;
    U32 HardwareRevision;
};

/************************************************************************/

static UINT RTL8169Commands(UINT Function, UINT Parameter);
static LPPCI_DEVICE RTL8169Attach(LPPCI_DEVICE PciDevice);
static const RTL8169_DEVICE_INFO *RTL8169FindDeviceInfo(U16 VendorID, U16 DeviceID);
static void RTL8169InitializeHardwareDescription(LPRTL8169_DEVICE Device);
static U32 RTL8169InitializeRegisterAccess(LPRTL8169_DEVICE Device);
static U32 RTL8169OnGetVersion(void);

/************************************************************************/

static DRIVER_MATCH RTL8169MatchTable[] = {
    REALTEK_NETWORK_MATCH_ENTRY(RTL8169_DEVICE_8161),
    REALTEK_NETWORK_MATCH_ENTRY(RTL8169_DEVICE_8168),
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
    U32 Result;

    Device = (LPRTL8169_DEVICE)RealtekNetworkAttachCommon(
        sizeof(RTL8169_DEVICE),
        PciDevice,
        TEXT("RTL8169Attach"));
    if (Device == NULL) {
        return NULL;
    }

    RTL8169InitializeHardwareDescription(Device);

    if (Device->DeviceInfo == NULL) {
        ERROR(TEXT("[RTL8169Attach] Missing hardware description for %x:%x"),
              (UINT)Device->Info.VendorID,
              (UINT)Device->Info.DeviceID);
        ReleaseKernelObject(Device);
        return NULL;
    }

    Result = RTL8169InitializeRegisterAccess(Device);
    if (Result != DF_RETURN_SUCCESS) {
        ReleaseKernelObject(Device);
        return NULL;
    }

    Device->ProductName = Device->DeviceInfo->ProductName;
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
 * @brief Initialize the active register BAR and cache the hardware revision.
 * @param Device Target RTL8169 device context.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
static U32 RTL8169InitializeRegisterAccess(LPRTL8169_DEVICE Device) {
    U32 Result;

    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Result = RealtekNetworkInitializeRegisterWindow(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        REALTEK_REGISTER_ACCESS_MODE_MMIO,
        REALTEK_REGISTER_ACCESS_MODE_NONE,
        RTL8169_REG_TXCONFIG,
        TEXT("RTL8169InitializeRegisterAccess"));
    if (Result != DF_RETURN_SUCCESS) {
        return Result;
    }

    Device->HardwareRevision = RealtekNetworkReadRegister32(
        (LPREALTEK_NETWORK_COMMON_DEVICE)Device,
        RTL8169_REG_TXCONFIG);
    DEBUG(TEXT("[RTL8169InitializeRegisterAccess] Revision=%x access=%u bar=%u"),
          Device->HardwareRevision,
          (UINT)Device->RegisterAccessMode,
          (UINT)Device->RegisterBarIndex);
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
 * @brief Dispatches RTL8169 driver commands.
 * @param Function Requested driver function.
 * @param Parameter Optional parameter payload.
 * @return Driver-specific DF_RETURN_* code.
 */
static UINT RTL8169Commands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return RealtekNetworkOnLoad();
        case DF_UNLOAD:
            return RealtekNetworkOnUnload();
        case DF_GET_VERSION:
            return RTL8169OnGetVersion();
        case DF_GET_CAPS:
            return RealtekNetworkOnGetCaps();
        case DF_GET_LAST_FUNCTION:
            return RealtekNetworkOnGetLastFunction();
        case DF_PROBE:
            return RTL8169OnProbe((const PCI_INFO *)(LPVOID)Parameter);
        case DF_NT_RESET:
            return RealtekNetworkOnReset((const NETWORK_RESET *)(LPVOID)Parameter);
        case DF_NT_GETINFO:
            return RealtekNetworkOnGetInfo(
                (const NETWORK_GET_INFO *)(LPVOID)Parameter,
                FALSE,
                RTL8169_LINK_SPEED_UNKNOWN,
                FALSE,
                RTL8169_MAXIMUM_MTU);
        case DF_NT_SETRXCB:
            return RealtekNetworkOnSetReceiveCallback((const NETWORK_SET_RX_CB *)(LPVOID)Parameter);
        case DF_DEV_ENABLE_INTERRUPT:
            return RealtekNetworkOnEnableInterruptsPollingOnly((DEVICE_INTERRUPT_CONFIG *)(LPVOID)Parameter);
        case DF_DEV_DISABLE_INTERRUPT:
            return RealtekNetworkOnDisableInterrupts((DEVICE_INTERRUPT_CONFIG *)(LPVOID)Parameter);
        case DF_NT_SEND:
            return RealtekNetworkOnSendNotImplemented((const NETWORK_SEND *)(LPVOID)Parameter);
        case DF_NT_POLL:
            return RealtekNetworkOnPollIdle((const NETWORK_POLL *)(LPVOID)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
