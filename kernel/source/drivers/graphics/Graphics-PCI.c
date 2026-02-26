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


    Graphics PCI helpers

\************************************************************************/

#include "drivers/graphics/Graphics-PCI.h"

#include "Kernel.h"

/************************************************************************/

static UINT GraphicsPCIDisplayAttachProbe(UINT Function, UINT Parameter);
static LPPCI_DEVICE GraphicsPCIDisplayAttach(LPPCI_DEVICE PciDevice);

static const DRIVER_MATCH GraphicsPCIDisplayAttachMatches[] = {
    {PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_DISPLAY, PCI_ANY_CLASS, PCI_ANY_CLASS}
};

static PCI_DRIVER DATA_SECTION GraphicsPCIDisplayAttachDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_INIT,
    .VersionMajor = 1,
    .VersionMinor = 0,
    .Designer = "Jango73",
    .Manufacturer = "EXOS",
    .Product = "PCI Display Attach",
    .Alias = "pci_display_attach",
    .Flags = 0,
    .Command = GraphicsPCIDisplayAttachProbe,
    .Matches = GraphicsPCIDisplayAttachMatches,
    .MatchCount = sizeof(GraphicsPCIDisplayAttachMatches) / sizeof(GraphicsPCIDisplayAttachMatches[0]),
    .Attach = GraphicsPCIDisplayAttach
};

/************************************************************************/

/**
 * @brief Return the graphics PCI fallback driver for display-class controllers.
 * @return PCI attach driver used to surface generic display devices.
 */
LPPCI_DRIVER GraphicsPCIGetDisplayAttachDriver(void) {
    return &GraphicsPCIDisplayAttachDriver;
}

/************************************************************************/

/**
 * @brief Probe callback used to attach generic PCI display devices.
 * @param Function Driver callback function identifier.
 * @param Parameter Optional probe parameter.
 * @return DF_RETURN_SUCCESS for display-class devices, DF_RETURN_NOT_IMPLEMENTED otherwise.
 */
static UINT GraphicsPCIDisplayAttachProbe(UINT Function, UINT Parameter) {
    LPPCI_INFO PciInfo = NULL;

    if (Function != DF_PROBE) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    PciInfo = (LPPCI_INFO)(LPVOID)Parameter;
    SAFE_USE(PciInfo) {
        if (PciInfo->BaseClass == PCI_CLASS_DISPLAY) {
            return DF_RETURN_SUCCESS;
        }
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Attach callback for generic PCI display devices.
 *
 * Keeps display controllers visible in the kernel PCI device list so
 * graphics backends can discover them.
 *
 * @param PciDevice Stack-built PCI device descriptor from bus scan.
 * @return Heap-allocated PCI device descriptor or NULL on allocation failure.
 */
static LPPCI_DEVICE GraphicsPCIDisplayAttach(LPPCI_DEVICE PciDevice) {
    LPPCI_DEVICE Device = NULL;

    if (PciDevice == NULL) {
        return NULL;
    }

    Device = (LPPCI_DEVICE)KernelHeapAlloc(sizeof(PCI_DEVICE));
    if (Device == NULL) {
        return NULL;
    }

    MemoryCopy(Device, PciDevice, sizeof(PCI_DEVICE));
    Device->TypeID = KOID_PCIDEVICE;
    Device->References = 1;
    Device->Next = NULL;
    Device->Prev = NULL;

    return Device;
}
