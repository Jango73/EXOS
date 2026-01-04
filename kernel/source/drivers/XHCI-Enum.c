
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


    xHCI

\\************************************************************************/

#include "drivers/XHCI-Internal.h"

/************************************************************************/

/**
 * @brief Convert endpoint attributes to a readable type string.
 * @param Attributes Endpoint attributes byte.
 * @return Type string.
 */
static LPCSTR XHCI_EndpointTypeToString(U8 Attributes) {
    switch (Attributes & 0x03) {
        case USB_ENDPOINT_TYPE_CONTROL:
            return TEXT("Control");
        case USB_ENDPOINT_TYPE_ISOCHRONOUS:
            return TEXT("Iso");
        case USB_ENDPOINT_TYPE_BULK:
            return TEXT("Bulk");
        case USB_ENDPOINT_TYPE_INTERRUPT:
            return TEXT("Intr");
        default:
            return TEXT("Unknown");
    }
}

/************************************************************************/

/**
 * @brief Initialize USB node data with common fields.
 * @param Data Output data.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 */
static void XHCI_InitUsbNodeData(DRIVER_ENUM_USB_NODE* Data, LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    MemorySet(Data, 0, sizeof(*Data));
    Data->Bus = Device->Info.Bus;
    Data->Dev = Device->Info.Dev;
    Data->Func = Device->Info.Func;
    Data->PortNumber = UsbDevice->PortNumber;
    Data->Address = UsbDevice->Address;
    Data->SpeedId = UsbDevice->SpeedId;
    Data->VendorID = UsbDevice->DeviceDescriptor.VendorID;
    Data->ProductID = UsbDevice->DeviceDescriptor.ProductID;
    Data->DeviceClass = UsbDevice->DeviceDescriptor.DeviceClass;
    Data->DeviceSubClass = UsbDevice->DeviceDescriptor.DeviceSubClass;
    Data->DeviceProtocol = UsbDevice->DeviceDescriptor.DeviceProtocol;
}

/************************************************************************/

/**
 * @brief Fill an enumeration item with provided data.
 * @param Item Enumeration item.
 * @param Domain Domain identifier.
 * @param Index Item index.
 * @param Data Data buffer.
 * @param DataSize Data size.
 */
static void XHCI_FillEnumItem(LPDRIVER_ENUM_ITEM Item, UINT Domain, UINT Index, const void* Data, UINT DataSize) {
    MemorySet(Item, 0, sizeof(DRIVER_ENUM_ITEM));
    Item->Header.Size = sizeof(DRIVER_ENUM_ITEM);
    Item->Header.Version = EXOS_ABI_VERSION;
    Item->Domain = Domain;
    Item->Index = Index;
    Item->DataSize = DataSize;
    MemoryCopy(Item->Data, Data, DataSize);
}

/************************************************************************/

U32 XHCI_EnumNext(LPDRIVER_ENUM_NEXT Next) {
    if (Next == NULL || Next->Query == NULL || Next->Item == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }
    if (Next->Query->Header.Size < sizeof(DRIVER_ENUM_QUERY) ||
        Next->Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Next->Query->Domain != ENUM_DOMAIN_XHCI_PORT &&
        Next->Query->Domain != ENUM_DOMAIN_USB_DEVICE &&
        Next->Query->Domain != ENUM_DOMAIN_USB_NODE) {
        return DF_RETURN_NOT_IMPLEMENTED;
    }

    LPLIST PciList = GetPCIDeviceList();
    if (PciList == NULL) {
        return DF_RETURN_NO_MORE;
    }

    UINT MatchIndex = 0;
    for (LPLISTNODE Node = PciList->First; Node; Node = Node->Next) {
        LPPCI_DEVICE PciDevice = (LPPCI_DEVICE)Node;
        if (PciDevice->Driver != (LPDRIVER)&XHCIDriver) {
            continue;
        }

        LPXHCI_DEVICE Device = (LPXHCI_DEVICE)PciDevice;
        SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
            if (Next->Query->Domain == ENUM_DOMAIN_XHCI_PORT) {
                for (UINT PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
                    if (MatchIndex == Next->Query->Index) {
                        U32 PortStatus = XHCI_ReadPortStatus(Device, PortIndex);
                        U32 SpeedId = (PortStatus & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
                        UINT Connected = (PortStatus & XHCI_PORTSC_CCS) ? 1U : 0U;
                        UINT Enabled = (PortStatus & XHCI_PORTSC_PED) ? 1U : 0U;

                    DRIVER_ENUM_XHCI_PORT Data;
                    MemorySet(&Data, 0, sizeof(Data));
                    Data.Bus = Device->Info.Bus;
                    Data.Dev = Device->Info.Dev;
                    Data.Func = Device->Info.Func;
                    Data.PortNumber = (U8)(PortIndex + 1);
                    Data.PortStatus = PortStatus;
                    Data.SpeedId = SpeedId;
                    Data.Connected = Connected;
                    Data.Enabled = Enabled;

                    XHCI_FillEnumItem(Next->Item, ENUM_DOMAIN_XHCI_PORT, Next->Query->Index, &Data, sizeof(Data));

                    Next->Query->Index++;
                    return DF_RETURN_SUCCESS;
                }

                    MatchIndex++;
                }
            } else if (Next->Query->Domain == ENUM_DOMAIN_USB_DEVICE) {
                XHCI_EnsureUsbDevices(Device);
                LPLIST UsbDeviceList = GetUsbDeviceList();
                if (UsbDeviceList == NULL) {
                    continue;
                }
                for (LPLISTNODE UsbNode = UsbDeviceList->First; UsbNode != NULL; UsbNode = UsbNode->Next) {
                    LPXHCI_USB_DEVICE UsbDevice = (LPXHCI_USB_DEVICE)UsbNode;
                    if (UsbDevice->Controller != Device) {
                        continue;
                    }
                    if (!UsbDevice->Present) {
                        continue;
                    }

                    if (MatchIndex == Next->Query->Index) {
                        DRIVER_ENUM_USB_DEVICE Data;
                        MemorySet(&Data, 0, sizeof(Data));
                        Data.Bus = Device->Info.Bus;
                        Data.Dev = Device->Info.Dev;
                        Data.Func = Device->Info.Func;
                        Data.PortNumber = UsbDevice->PortNumber;
                        Data.Address = UsbDevice->Address;
                        Data.SpeedId = UsbDevice->SpeedId;
                        Data.VendorID = UsbDevice->DeviceDescriptor.VendorID;
                        Data.ProductID = UsbDevice->DeviceDescriptor.ProductID;

                        XHCI_FillEnumItem(Next->Item, ENUM_DOMAIN_USB_DEVICE, Next->Query->Index, &Data, sizeof(Data));

                        Next->Query->Index++;
                        return DF_RETURN_SUCCESS;
                    }

                    MatchIndex++;
                }
            } else if (Next->Query->Domain == ENUM_DOMAIN_USB_NODE) {
                XHCI_EnsureUsbDevices(Device);
                LPLIST UsbDeviceList = GetUsbDeviceList();
                if (UsbDeviceList == NULL) {
                    continue;
                }
                for (LPLISTNODE UsbNode = UsbDeviceList->First; UsbNode != NULL; UsbNode = UsbNode->Next) {
                    LPXHCI_USB_DEVICE UsbDevice = (LPXHCI_USB_DEVICE)UsbNode;
                    if (UsbDevice->Controller != Device) {
                        continue;
                    }
                    if (!UsbDevice->Present) {
                        continue;
                    }

                    if (MatchIndex == Next->Query->Index) {
                        DRIVER_ENUM_USB_NODE Data;
                        XHCI_InitUsbNodeData(&Data, Device, UsbDevice);
                        Data.NodeType = USB_NODE_DEVICE;

                        XHCI_FillEnumItem(Next->Item, ENUM_DOMAIN_USB_NODE, Next->Query->Index, &Data, sizeof(Data));

                        Next->Query->Index++;
                        return DF_RETURN_SUCCESS;
                    }
                    MatchIndex++;

                    for (UINT ConfigIndex = 0; ConfigIndex < UsbDevice->ConfigCount; ConfigIndex++) {
                        LPXHCI_USB_CONFIGURATION Config = &UsbDevice->Configs[ConfigIndex];

                        if (MatchIndex == Next->Query->Index) {
                            DRIVER_ENUM_USB_NODE Data;
                            XHCI_InitUsbNodeData(&Data, Device, UsbDevice);
                            Data.NodeType = USB_NODE_CONFIG;
                            Data.ConfigValue = Config->ConfigurationValue;
                            Data.ConfigAttributes = Config->Attributes;
                            Data.ConfigMaxPower = Config->MaxPower;

                            XHCI_FillEnumItem(Next->Item, ENUM_DOMAIN_USB_NODE, Next->Query->Index, &Data, sizeof(Data));

                            Next->Query->Index++;
                            return DF_RETURN_SUCCESS;
                        }
                        MatchIndex++;

                        LPLIST InterfaceList = GetUsbInterfaceList();
                        LPLIST EndpointList = GetUsbEndpointList();
                        if (InterfaceList == NULL || EndpointList == NULL) {
                            continue;
                        }

                        for (LPLISTNODE IfNode = InterfaceList->First; IfNode != NULL; IfNode = IfNode->Next) {
                            LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)IfNode;
                            if (Interface->Parent != (LPLISTNODE)UsbDevice) {
                                continue;
                            }
                            if (Interface->ConfigurationValue != Config->ConfigurationValue) {
                                continue;
                            }

                            if (MatchIndex == Next->Query->Index) {
                                DRIVER_ENUM_USB_NODE Data;
                                XHCI_InitUsbNodeData(&Data, Device, UsbDevice);
                                Data.NodeType = USB_NODE_INTERFACE;
                                Data.ConfigValue = Config->ConfigurationValue;
                                Data.InterfaceNumber = Interface->Number;
                                Data.AlternateSetting = Interface->AlternateSetting;
                                Data.InterfaceClass = Interface->InterfaceClass;
                                Data.InterfaceSubClass = Interface->InterfaceSubClass;
                                Data.InterfaceProtocol = Interface->InterfaceProtocol;

                                XHCI_FillEnumItem(Next->Item, ENUM_DOMAIN_USB_NODE, Next->Query->Index, &Data, sizeof(Data));

                                Next->Query->Index++;
                                return DF_RETURN_SUCCESS;
                            }
                            MatchIndex++;

                            for (LPLISTNODE EpNode = EndpointList->First; EpNode != NULL; EpNode = EpNode->Next) {
                                LPXHCI_USB_ENDPOINT Endpoint = (LPXHCI_USB_ENDPOINT)EpNode;
                                if (Endpoint->Parent != (LPLISTNODE)Interface) {
                                    continue;
                                }

                                if (MatchIndex == Next->Query->Index) {
                                    DRIVER_ENUM_USB_NODE Data;
                                    XHCI_InitUsbNodeData(&Data, Device, UsbDevice);
                                    Data.NodeType = USB_NODE_ENDPOINT;
                                    Data.ConfigValue = Config->ConfigurationValue;
                                    Data.InterfaceNumber = Interface->Number;
                                    Data.AlternateSetting = Interface->AlternateSetting;
                                    Data.EndpointAddress = Endpoint->Address;
                                    Data.EndpointAttributes = Endpoint->Attributes;
                                    Data.EndpointMaxPacketSize = Endpoint->MaxPacketSize;
                                    Data.EndpointInterval = Endpoint->Interval;

                                    XHCI_FillEnumItem(Next->Item, ENUM_DOMAIN_USB_NODE, Next->Query->Index, &Data, sizeof(Data));

                                    Next->Query->Index++;
                                    return DF_RETURN_SUCCESS;
                                }
                                MatchIndex++;
                            }
                        }
                    }
                }
            } else {
                return DF_RETURN_NOT_IMPLEMENTED;
            }
        }
    }

    return DF_RETURN_NO_MORE;
}

/************************************************************************/

U32 XHCI_EnumPretty(LPDRIVER_ENUM_PRETTY Pretty) {
    if (Pretty == NULL || Pretty->Item == NULL || Pretty->Buffer == NULL || Pretty->BufferSize == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }
    if (Pretty->Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Pretty->Item->Domain == ENUM_DOMAIN_XHCI_PORT) {
        if (Pretty->Item->DataSize < sizeof(DRIVER_ENUM_XHCI_PORT)) {
            return DF_RETURN_BAD_PARAMETER;
        }

        const DRIVER_ENUM_XHCI_PORT* Data = (const DRIVER_ENUM_XHCI_PORT*)Pretty->Item->Data;
        StringPrintFormat(Pretty->Buffer,
                          TEXT("xHCI %x:%x.%u Port %u CCS=%u PED=%u Speed=%s Raw=%x"),
                          (U32)Data->Bus,
                          (U32)Data->Dev,
                          (U32)Data->Func,
                          (U32)Data->PortNumber,
                          Data->Connected,
                          Data->Enabled,
                          XHCI_SpeedToString(Data->SpeedId),
                          Data->PortStatus);
        return DF_RETURN_SUCCESS;
    }

    if (Pretty->Item->Domain == ENUM_DOMAIN_USB_DEVICE) {
        if (Pretty->Item->DataSize < sizeof(DRIVER_ENUM_USB_DEVICE)) {
            return DF_RETURN_BAD_PARAMETER;
        }

        const DRIVER_ENUM_USB_DEVICE* Data = (const DRIVER_ENUM_USB_DEVICE*)Pretty->Item->Data;
        StringPrintFormat(Pretty->Buffer,
                          TEXT("USB %x:%x.%u Port %u Addr %u VID=%x PID=%x Speed=%s"),
                          (U32)Data->Bus,
                          (U32)Data->Dev,
                          (U32)Data->Func,
                          (U32)Data->PortNumber,
                          (U32)Data->Address,
                          (U32)Data->VendorID,
                          (U32)Data->ProductID,
                          XHCI_SpeedToString(Data->SpeedId));
        return DF_RETURN_SUCCESS;
    }

    if (Pretty->Item->Domain == ENUM_DOMAIN_USB_NODE) {
        if (Pretty->Item->DataSize < sizeof(DRIVER_ENUM_USB_NODE)) {
            return DF_RETURN_BAD_PARAMETER;
        }

        const DRIVER_ENUM_USB_NODE* Data = (const DRIVER_ENUM_USB_NODE*)Pretty->Item->Data;

        switch (Data->NodeType) {
            case USB_NODE_DEVICE:
                StringPrintFormat(Pretty->Buffer,
                                  TEXT("Device Port %u Addr %u VID=%x PID=%x Class=%x/%x/%x Speed=%s"),
                                  (U32)Data->PortNumber,
                                  (U32)Data->Address,
                                  (U32)Data->VendorID,
                                  (U32)Data->ProductID,
                                  (U32)Data->DeviceClass,
                                  (U32)Data->DeviceSubClass,
                                  (U32)Data->DeviceProtocol,
                                  XHCI_SpeedToString(Data->SpeedId));
                return DF_RETURN_SUCCESS;
            case USB_NODE_CONFIG:
                StringPrintFormat(Pretty->Buffer,
                                  TEXT("  Config %u Attr=%x MaxPower=%u"),
                                  (U32)Data->ConfigValue,
                                  (U32)Data->ConfigAttributes,
                                  (U32)Data->ConfigMaxPower);
                return DF_RETURN_SUCCESS;
            case USB_NODE_INTERFACE:
                StringPrintFormat(Pretty->Buffer,
                                  TEXT("    Interface %u Alt=%u Class=%x/%x/%x"),
                                  (U32)Data->InterfaceNumber,
                                  (U32)Data->AlternateSetting,
                                  (U32)Data->InterfaceClass,
                                  (U32)Data->InterfaceSubClass,
                                  (U32)Data->InterfaceProtocol);
                return DF_RETURN_SUCCESS;
            case USB_NODE_ENDPOINT:
                StringPrintFormat(Pretty->Buffer,
                                  TEXT("      Endpoint %x %s %s MaxPacket=%u Interval=%u"),
                                  (U32)Data->EndpointAddress,
                                  (Data->EndpointAddress & 0x80) ? TEXT("IN") : TEXT("OUT"),
                                  XHCI_EndpointTypeToString(Data->EndpointAttributes),
                                  (U32)Data->EndpointMaxPacketSize,
                                  (U32)Data->EndpointInterval);
                return DF_RETURN_SUCCESS;
        }
    }

    return DF_RETURN_BAD_PARAMETER;
}
