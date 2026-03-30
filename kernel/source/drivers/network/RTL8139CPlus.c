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


    RTL8139CPlus

\************************************************************************/

#include "drivers/network/RTL8139CPlus.h"

#include "Log.h"

/************************************************************************/

#define RTL8139CPLUS_VERSION_MAJOR 1
#define RTL8139CPLUS_VERSION_MINOR 0

/************************************************************************/

static UINT RTL8139CPlusCommands(UINT Function, UINT Parameter);
static U32 RTL8139CPlusOnProbe(const PCI_INFO* PciInfo);
static LPPCI_DEVICE RTL8139CPlusAttach(LPPCI_DEVICE PciDevice);
static U32 RTL8139CPlusOnReset(const NETWORK_RESET* Reset);
static U32 RTL8139CPlusOnGetInfo(const NETWORK_GET_INFO* GetInfo);
static U32 RTL8139CPlusOnGetVersion(void);

/************************************************************************/

static DRIVER_MATCH RTL8139CPlusMatchTable[] = {
    REALTEK_NETWORK_MATCH_ENTRY(RTL8139CPLUS_DEVICE_8139),
};

/************************************************************************/

PCI_DRIVER DATA_SECTION RTL8139CPlusDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_NETWORK,
    .VersionMajor = RTL8139CPLUS_VERSION_MAJOR,
    .VersionMinor = RTL8139CPLUS_VERSION_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "Realtek",
    .Product = "RTL8139CPlus",
    .Alias = "rtl8139cplus",
    .Command = RTL8139CPlusCommands,
    .Matches = RTL8139CPlusMatchTable,
    .MatchCount = sizeof(RTL8139CPlusMatchTable) / sizeof(RTL8139CPlusMatchTable[0]),
    .Attach = RTL8139CPlusAttach,
};

/************************************************************************/

/**
 * @brief Retrieves the RTL8139CPlus PCI driver descriptor.
 * @return Pointer to the RTL8139CPlus PCI driver.
 */
LPDRIVER RTL8139CPlusGetDriver(void) {
    return (LPDRIVER)&RTL8139CPlusDriver;
}

/************************************************************************/

/**
 * @brief Handles PCI probe requests for the Realtek RTL8139CPlus skeleton.
 * @param PciInfo PCI function information provided by the PCI layer.
 * @return DF_RETURN_SUCCESS when the revision matches the CPlus range.
 */
static U32 RTL8139CPlusOnProbe(const PCI_INFO* PciInfo) {
    if (PciInfo == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (PciInfo->BaseClass != PCI_CLASS_NETWORK) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (PciInfo->SubClass != PCI_SUBCLASS_ETHERNET) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    if (PciInfo->Revision < RTL8139CPLUS_MINIMUM_REVISION) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Attaches a minimal PCI-visible RTL8139CPlus device object.
 * @param PciDevice Supported PCI function.
 * @return Heap-allocated PCI device object or NULL on allocation failure.
 */
static LPPCI_DEVICE RTL8139CPlusAttach(LPPCI_DEVICE PciDevice) {
    LPREALTEK_NETWORK_COMMON_DEVICE Device;

    Device = (LPREALTEK_NETWORK_COMMON_DEVICE)RealtekNetworkAttachCommon(
        sizeof(REALTEK_NETWORK_COMMON_DEVICE),
        PciDevice,
        TEXT("RTL8139CPlusAttach"));
    if (Device == NULL) {
        return NULL;
    }

    Device->ProductName = TEXT("RTL8139CPlus");

    DEBUG(TEXT("[RTL8139CPlusAttach] Attached skeleton controller %x:%x on %x:%x.%x revision=%x"),
        (U32)Device->Info.VendorID,
        (U32)Device->Info.DeviceID,
        (U32)Device->Info.Bus,
        (U32)Device->Info.Dev,
        (U32)Device->Info.Func,
        (U32)Device->Info.Revision);
    return (LPPCI_DEVICE)Device;
}

/************************************************************************/

/**
 * @brief Handles reset requests for the non-operational RTL8139CPlus skeleton.
 * @param Reset Reset request.
 * @return DF_RETURN_SUCCESS when the device handle is valid.
 */
static U32 RTL8139CPlusOnReset(const NETWORK_RESET* Reset) {
    return RealtekNetworkOnReset(Reset);
}

/************************************************************************/

/**
 * @brief Reports placeholder network information for the RTL8139CPlus skeleton.
 * @param GetInfo Information request.
 * @return DF_RETURN_SUCCESS when the request is structurally valid.
 */
static U32 RTL8139CPlusOnGetInfo(const NETWORK_GET_INFO* GetInfo) {
    return RealtekNetworkOnGetInfo(GetInfo, FALSE, 0, FALSE, RTL8139CPLUS_MAXIMUM_MTU);
}

/************************************************************************/

/**
 * @brief Returns the RTL8139CPlus driver version.
 * @return Packed major/minor version.
 */
static U32 RTL8139CPlusOnGetVersion(void) {
    return MAKE_VERSION(RTL8139CPLUS_VERSION_MAJOR, RTL8139CPLUS_VERSION_MINOR);
}

/************************************************************************/

/**
 * @brief Dispatches RTL8139CPlus skeleton driver commands.
 * @param Function Requested driver function.
 * @param Parameter Optional parameter payload.
 * @return Driver-specific DF_RETURN_* code.
 */
static UINT RTL8139CPlusCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            return RealtekNetworkOnLoad();
        case DF_UNLOAD:
            return RealtekNetworkOnUnload();
        case DF_GET_VERSION:
            return RTL8139CPlusOnGetVersion();
        case DF_GET_CAPS:
            return RealtekNetworkOnGetCaps();
        case DF_GET_LAST_FUNCTION:
            return RealtekNetworkOnGetLastFunction();
        case DF_PROBE:
            return RTL8139CPlusOnProbe((const PCI_INFO*)(LPVOID)Parameter);
        case DF_NT_RESET:
            return RTL8139CPlusOnReset((const NETWORK_RESET*)(LPVOID)Parameter);
        case DF_NT_GETINFO:
            return RTL8139CPlusOnGetInfo((const NETWORK_GET_INFO*)(LPVOID)Parameter);
        case DF_NT_SETRXCB:
            return RealtekNetworkOnSetReceiveCallback((const NETWORK_SET_RX_CB*)(LPVOID)Parameter);
        case DF_DEV_ENABLE_INTERRUPT:
            return RealtekNetworkOnEnableInterrupts((DEVICE_INTERRUPT_CONFIG*)(LPVOID)Parameter);
        case DF_DEV_DISABLE_INTERRUPT:
            return RealtekNetworkOnDisableInterrupts((DEVICE_INTERRUPT_CONFIG*)(LPVOID)Parameter);
        case DF_NT_SEND:
            return RealtekNetworkOnSendNotImplemented((const NETWORK_SEND*)(LPVOID)Parameter);
        case DF_NT_POLL:
            return RealtekNetworkOnPollIdle((const NETWORK_POLL*)(LPVOID)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
