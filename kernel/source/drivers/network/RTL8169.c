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

#include "Log.h"

/************************************************************************/

#define RTL8169_VERSION_MAJOR 1
#define RTL8169_VERSION_MINOR 0

/************************************************************************/

static UINT RTL8169Commands(UINT Function, UINT Parameter);
static LPPCI_DEVICE RTL8169Attach(LPPCI_DEVICE PciDevice);

/************************************************************************/

static DRIVER_MATCH RTL8169MatchTable[] = {
    RTL8169_MATCH_ENTRY(RTL8169_DEVICE_8168),
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
    .Product = "RTL8169 Family",
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
 * @brief Attaches the driver to a supported PCI function.
 *
 * Step 0 only establishes the shared driver family shape and the PCI match
 * table. Hardware initialization is deferred to later implementation steps.
 *
 * @param PciDevice Supported PCI function.
 * @return Always NULL during the skeleton stage.
 */
static LPPCI_DEVICE RTL8169Attach(LPPCI_DEVICE PciDevice) {
    if (PciDevice == NULL) {
        return NULL;
    }

    DEBUG(TEXT("[RTL8169Attach] Step 0 skeleton matched Realtek controller %x:%x on %x:%x.%x"),
          (UINT)PciDevice->Info.VendorID,
          (UINT)PciDevice->Info.DeviceID,
          (UINT)PciDevice->Info.Bus,
          (UINT)PciDevice->Info.Dev,
          (UINT)PciDevice->Info.Func);
    return NULL;
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
 * @return Zero because Step 0 exposes only the family skeleton.
 */
static U32 RTL8169OnGetCaps(void) {
    return 0;
}

/************************************************************************/

/**
 * @brief Returns the highest implemented driver function identifier.
 * @return DF_PROBE for the Step 0 PCI-only skeleton.
 */
static U32 RTL8169OnGetLastFunction(void) {
    return DF_PROBE;
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
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}
