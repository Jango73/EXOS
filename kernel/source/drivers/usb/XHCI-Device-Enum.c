
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

\************************************************************************/

#include "drivers/usb/XHCI-Internal.h"
#include "drivers/usb/XHCI-Device-Internal.h"
#include "Clock.h"
#include "utils/ThresholdLatch.h"

/************************************************************************/

#define XHCI_ENUM_FAILURE_LOG_IMMEDIATE_BUDGET 1
#define XHCI_ENUM_FAILURE_LOG_INTERVAL_MS 2000
#define XHCI_ENABLE_SLOT_TIMEOUT_LOG_IMMEDIATE_BUDGET 1
#define XHCI_ENABLE_SLOT_TIMEOUT_LOG_INTERVAL_MS 2000

/************************************************************************/
/**
 * @brief Emit rate-limited root port enumeration diagnostics.
 * @param UsbDevice Root port USB device.
 * @param Step Failing enumeration step.
 * @param PortStatus Current port status register value.
 */
static void XHCI_LogProbeFailure(LPXHCI_USB_DEVICE UsbDevice, LPCSTR Step, U32 PortStatus) {
    U32 Suppressed = 0;
    U32 UsbCommand = 0;
    U32 UsbStatus = 0;
    LPXHCI_DEVICE Device = NULL;

    if (UsbDevice == NULL || !UsbDevice->IsRootPort) {
        return;
    }

    if (!RateLimiterShouldTrigger(&UsbDevice->EnumFailureLogLimiter, GetSystemTime(), &Suppressed)) {
        return;
    }

    Device = UsbDevice->Controller;
    XHCI_LogHseTransitionIfNeeded(Device, TEXT("ProbeFailure"));
    if (Device != NULL && Device->OpBase != 0) {
        UsbCommand = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
        UsbStatus = XHCI_Read32(Device->OpBase, XHCI_OP_USBSTS);
    }

    WARNING(TEXT("[XHCI_LogProbeFailure] Port %u step=%s err=%x completion=%x raw=%x USBCMD=%x USBSTS=%x suppressed=%u"),
            (U32)UsbDevice->PortNumber,
            (Step != NULL) ? Step : TEXT("?"),
            (U32)UsbDevice->LastEnumError,
            (U32)UsbDevice->LastEnumCompletion,
            PortStatus,
            UsbCommand,
            UsbStatus,
            Suppressed);
}

/************************************************************************/

static U32 XHCI_ReadRootPortStatusSafe(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (Device == NULL || UsbDevice == NULL) {
        return 0;
    }
    if (!UsbDevice->IsRootPort || UsbDevice->PortNumber == 0) {
        return 0;
    }
    return XHCI_ReadPortStatus(Device, (U32)UsbDevice->PortNumber - 1);
}

/**
 * @brief Detect whether a USB device is a hub.
 * @param UsbDevice USB device state.
 * @return TRUE when the device exposes the hub class.
 */
static BOOL XHCI_IsHubDevice(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
        return FALSE;
    }

    if (UsbDevice->DeviceDescriptor.DeviceClass == USB_CLASS_HUB) {
        return TRUE;
    }

    LPXHCI_USB_CONFIGURATION Config = XHCI_GetSelectedConfig(UsbDevice);
    if (Config == NULL) {
        return FALSE;
    }

    LPLIST InterfaceList = GetUsbInterfaceList();
    if (InterfaceList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = InterfaceList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)Node;
        if (Interface->Parent != (LPLISTNODE)UsbDevice) {
            continue;
        }
        if (Interface->ConfigurationValue != Config->ConfigurationValue) {
            continue;
        }
        if (Interface->InterfaceClass == USB_CLASS_HUB) {
            return TRUE;
        }
    }

    return FALSE;
}
/************************************************************************/

typedef BOOL (*XHCI_DESC_CALLBACK)(const U8* Descriptor, U8 Length, void* Context);

/**
 * @brief Walk all descriptors in a configuration buffer.
 * @param Buffer Descriptor buffer.
 * @param Length Buffer length.
 * @param Callback Callback invoked per descriptor.
 * @param Context Callback context.
 * @return TRUE on success.
 */
static BOOL XHCI_ForEachDescriptor(const U8* Buffer, U16 Length, XHCI_DESC_CALLBACK Callback, void* Context) {
    U16 Offset = 0;

    if (Buffer == NULL || Callback == NULL) {
        return FALSE;
    }

    while ((Offset + 2) <= Length) {
        U8 DescLength = Buffer[Offset];
        U8 DescType = Buffer[Offset + 1];
        const U8* Desc = &Buffer[Offset];

        if (DescLength < 2 || (Offset + DescLength) > Length) {
            return FALSE;
        }

        if (!Callback(Desc, DescLength, Context)) {
            return FALSE;
        }

        Offset = (U16)(Offset + DescLength);
    }

    return TRUE;
}

/************************************************************************/

typedef struct tag_XHCI_DESC_COUNT_CONFIG {
    UINT ConfigCount;
} XHCI_DESC_COUNT_CONFIG, *LPXHCI_DESC_COUNT_CONFIG;

static BOOL XHCI_CountConfigCallback(const U8* Descriptor, U8 Length, void* Context) {
    LPXHCI_DESC_COUNT_CONFIG Ctx = (LPXHCI_DESC_COUNT_CONFIG)Context;
    UNUSED(Length);

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_CONFIGURATION) {
        Ctx->ConfigCount++;
    }

    return TRUE;
}

/************************************************************************/

typedef struct tag_XHCI_DESC_FILL_CONTEXT {
    LPXHCI_USB_DEVICE UsbDevice;
    LPXHCI_USB_CONFIGURATION Configs;
    UINT ConfigCount;
    UINT ConfigIndex;
    LPXHCI_USB_CONFIGURATION CurrentConfig;
    LPXHCI_USB_INTERFACE CurrentInterface;
} XHCI_DESC_FILL_CONTEXT, *LPXHCI_DESC_FILL_CONTEXT;

static BOOL XHCI_FillDescriptorCallback(const U8* Descriptor, U8 Length, void* Context) {
    LPXHCI_DESC_FILL_CONTEXT Ctx = (LPXHCI_DESC_FILL_CONTEXT)Context;

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_CONFIGURATION) {
        if (Length < sizeof(USB_CONFIGURATION_DESCRIPTOR) || Ctx->ConfigIndex >= Ctx->ConfigCount) {
            return TRUE;
        }

        const USB_CONFIGURATION_DESCRIPTOR* ConfigDesc = (const USB_CONFIGURATION_DESCRIPTOR*)Descriptor;
        LPXHCI_USB_CONFIGURATION Config = &Ctx->Configs[Ctx->ConfigIndex];

        Config->ConfigurationValue = ConfigDesc->ConfigurationValue;
        Config->ConfigurationIndex = ConfigDesc->ConfigurationIndex;
        Config->Attributes = ConfigDesc->Attributes;
        Config->MaxPower = ConfigDesc->MaxPower;
        Config->NumInterfaces = ConfigDesc->NumInterfaces;
        Config->TotalLength = ConfigDesc->TotalLength;
        Config->InterfaceCount = 0;

        Ctx->CurrentConfig = Config;
        Ctx->CurrentInterface = NULL;
        Ctx->ConfigIndex++;
        return TRUE;
    }

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_INTERFACE) {
        if (Length < sizeof(USB_INTERFACE_DESCRIPTOR) || Ctx->CurrentConfig == NULL) {
            return TRUE;
        }

        const USB_INTERFACE_DESCRIPTOR* IfDesc = (const USB_INTERFACE_DESCRIPTOR*)Descriptor;
        LPXHCI_USB_INTERFACE Interface =
            (LPXHCI_USB_INTERFACE)CreateKernelObject(sizeof(XHCI_USB_INTERFACE), KOID_USBINTERFACE);
        if (Interface == NULL) {
            ERROR(TEXT("[XHCI_FillDescriptorCallback] Interface allocation failed"));
            return FALSE;
        }
        MemorySet((U8*)Interface + sizeof(LISTNODE), 0, sizeof(XHCI_USB_INTERFACE) - sizeof(LISTNODE));

        Interface->ConfigurationValue = Ctx->CurrentConfig->ConfigurationValue;
        Interface->Number = IfDesc->InterfaceNumber;
        Interface->AlternateSetting = IfDesc->AlternateSetting;
        Interface->NumEndpoints = IfDesc->NumEndpoints;
        Interface->InterfaceClass = IfDesc->InterfaceClass;
        Interface->InterfaceSubClass = IfDesc->InterfaceSubClass;
        Interface->InterfaceProtocol = IfDesc->InterfaceProtocol;
        Interface->InterfaceIndex = IfDesc->InterfaceIndex;
        Interface->EndpointCount = 0;

        LPLIST InterfaceList = GetUsbInterfaceList();
        if (InterfaceList == NULL || !ListAddItemWithParent(InterfaceList, Interface, (LPLISTNODE)Ctx->UsbDevice)) {
            ReleaseKernelObject(Interface);
            return FALSE;
        }

        Ctx->CurrentInterface = Interface;
        Ctx->CurrentConfig->InterfaceCount++;
        return TRUE;
    }

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_ENDPOINT) {
        if (Length < sizeof(USB_ENDPOINT_DESCRIPTOR) || Ctx->CurrentInterface == NULL) {
            return TRUE;
        }

        const USB_ENDPOINT_DESCRIPTOR* EpDesc = (const USB_ENDPOINT_DESCRIPTOR*)Descriptor;
        LPXHCI_USB_ENDPOINT Endpoint =
            (LPXHCI_USB_ENDPOINT)CreateKernelObject(sizeof(XHCI_USB_ENDPOINT), KOID_USBENDPOINT);
        if (Endpoint == NULL) {
            ERROR(TEXT("[XHCI_FillDescriptorCallback] Endpoint allocation failed"));
            return FALSE;
        }
        MemorySet((U8*)Endpoint + sizeof(LISTNODE), 0, sizeof(XHCI_USB_ENDPOINT) - sizeof(LISTNODE));

        Endpoint->Address = EpDesc->EndpointAddress;
        Endpoint->Attributes = EpDesc->Attributes;
        Endpoint->MaxPacketSize = EpDesc->MaxPacketSize;
        Endpoint->Interval = EpDesc->Interval;

        LPLIST EndpointList = GetUsbEndpointList();
        if (EndpointList == NULL || !ListAddItemWithParent(EndpointList, Endpoint, (LPLISTNODE)Ctx->CurrentInterface)) {
            ReleaseKernelObject(Endpoint);
            return FALSE;
        }

        Ctx->CurrentInterface->EndpointCount++;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Parse configuration descriptor and build the USB tree.
 * @param UsbDevice USB device state.
 * @param Buffer Descriptor buffer.
 * @param Length Buffer length.
 * @return TRUE on success.
 */
static BOOL XHCI_ParseConfigDescriptor(LPXHCI_USB_DEVICE UsbDevice, const U8* Buffer, U16 Length) {
    XHCI_DESC_COUNT_CONFIG ConfigCountContext;
    XHCI_DESC_FILL_CONTEXT FillContext;

    if (UsbDevice == NULL || Buffer == NULL || Length == 0) {
        return FALSE;
    }

    XHCI_FreeUsbTree(UsbDevice);

    MemorySet(&ConfigCountContext, 0, sizeof(ConfigCountContext));
    if (!XHCI_ForEachDescriptor(Buffer, Length, XHCI_CountConfigCallback, &ConfigCountContext)) {
        return FALSE;
    }

    if (ConfigCountContext.ConfigCount == 0) {
        return FALSE;
    }

    UsbDevice->Configs =
        (LPXHCI_USB_CONFIGURATION)KernelHeapAlloc(sizeof(XHCI_USB_CONFIGURATION) * ConfigCountContext.ConfigCount);
    if (UsbDevice->Configs == NULL) {
        return FALSE;
    }
    MemorySet(UsbDevice->Configs, 0, sizeof(XHCI_USB_CONFIGURATION) * ConfigCountContext.ConfigCount);
    UsbDevice->ConfigCount = ConfigCountContext.ConfigCount;

    MemorySet(&FillContext, 0, sizeof(FillContext));
    FillContext.UsbDevice = UsbDevice;
    FillContext.Configs = UsbDevice->Configs;
    FillContext.ConfigCount = UsbDevice->ConfigCount;

    if (!XHCI_ForEachDescriptor(Buffer, Length, XHCI_FillDescriptorCallback, &FillContext)) {
        XHCI_FreeUsbTree(UsbDevice);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Get default EP0 max packet size for a speed.
 * @param SpeedId xHCI speed ID.
 * @return Max packet size.
 */
static U16 XHCI_GetDefaultMaxPacketSize0(U8 SpeedId) {
    switch (SpeedId) {
        case 1:
        case 2:
            return 8;
        case 3:
            return 64;
        case 4:
        case 5:
            return 512;
        default:
            return 8;
    }
}

/************************************************************************/

/**
 * @brief Compute EP0 max packet size from descriptor data.
 * @param SpeedId xHCI speed ID.
 * @param DescriptorValue bMaxPacketSize0 value.
 * @return Max packet size.
 */
static U16 XHCI_ComputeMaxPacketSize0(U8 SpeedId, U8 DescriptorValue) {
    if (SpeedId == 4 || SpeedId == 5) {
        if (DescriptorValue < 5 || DescriptorValue > 10) {
            return 512;
        }
        return (U16)(1U << DescriptorValue);
    }
    return DescriptorValue;
}

/************************************************************************/

/**
 * @brief Reset a port and wait for completion.
 * @param Device xHCI device.
 * @param PortIndex Port index (0-based).
 * @return TRUE on success.
 */
static BOOL XHCI_ResetPort(LPXHCI_DEVICE Device, U32 PortIndex) {
    U32 Offset = XHCI_PORTSC_BASE + (PortIndex * XHCI_PORTSC_STRIDE);
    U32 PortStatus = XHCI_Read32(Device->OpBase, Offset);

    if ((PortStatus & XHCI_PORTSC_CCS) == 0) {
        return FALSE;
    }

    PortStatus |= XHCI_PORTSC_PR;
    PortStatus &= ~XHCI_PORTSC_W1C_MASK;
    XHCI_Write32(Device->OpBase, Offset, PortStatus);

    if (!XHCI_WaitForRegister(Device->OpBase,
                              Offset,
                              XHCI_PORTSC_PR,
                              0,
                              XHCI_PORT_RESET_TIMEOUT,
                              TEXT("Port reset"))) {
        ERROR(TEXT("[XHCI_ResetPort] Port %u reset timeout"), PortIndex + 1);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

static BOOL XHCI_InitTransferRing(LPXHCI_USB_DEVICE UsbDevice) {
    return XHCI_InitTransferRingCore(TEXT("XHCI_TransferRing"),
                                     &UsbDevice->TransferRingPhysical,
                                     &UsbDevice->TransferRingLinear,
                                     &UsbDevice->TransferRingCycleState,
                                     &UsbDevice->TransferRingEnqueueIndex);
}

/************************************************************************/

/**
 * @brief Initialize USB device state for a port.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_InitUsbDeviceState(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (XHCI_UsbTreeHasReferences(UsbDevice)) {
        WARNING(TEXT("[XHCI_InitUsbDeviceState] Device still referenced, skipping reset"));
        return FALSE;
    }

    XHCI_FreeUsbTree(UsbDevice);
    if (UsbDevice->InputContextLinear) {
        FreeRegion(UsbDevice->InputContextLinear, PAGE_SIZE);
        UsbDevice->InputContextLinear = 0;
    }
    if (UsbDevice->InputContextPhysical) {
        FreePhysicalPage(UsbDevice->InputContextPhysical);
        UsbDevice->InputContextPhysical = 0;
    }
    if (UsbDevice->DeviceContextLinear) {
        FreeRegion(UsbDevice->DeviceContextLinear, PAGE_SIZE);
        UsbDevice->DeviceContextLinear = 0;
    }
    if (UsbDevice->DeviceContextPhysical) {
        FreePhysicalPage(UsbDevice->DeviceContextPhysical);
        UsbDevice->DeviceContextPhysical = 0;
    }
    if (UsbDevice->TransferRingLinear) {
        FreeRegion(UsbDevice->TransferRingLinear, PAGE_SIZE);
        UsbDevice->TransferRingLinear = 0;
    }
    if (UsbDevice->TransferRingPhysical) {
        FreePhysicalPage(UsbDevice->TransferRingPhysical);
        UsbDevice->TransferRingPhysical = 0;
    }
    if (UsbDevice->HubStatusLinear) {
        FreeRegion(UsbDevice->HubStatusLinear, PAGE_SIZE);
        UsbDevice->HubStatusLinear = 0;
    }
    if (UsbDevice->HubStatusPhysical) {
        FreePhysicalPage(UsbDevice->HubStatusPhysical);
        UsbDevice->HubStatusPhysical = 0;
    }
    if (UsbDevice->HubChildren != NULL) {
        KernelHeapFree(UsbDevice->HubChildren);
        UsbDevice->HubChildren = NULL;
    }
    if (UsbDevice->HubPortStatus != NULL) {
        KernelHeapFree(UsbDevice->HubPortStatus);
        UsbDevice->HubPortStatus = NULL;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_InputContext"), &UsbDevice->InputContextPhysical, &UsbDevice->InputContextLinear)) {
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_DeviceContext"), &UsbDevice->DeviceContextPhysical, &UsbDevice->DeviceContextLinear)) {
        FreeRegion(UsbDevice->InputContextLinear, PAGE_SIZE);
        FreePhysicalPage(UsbDevice->InputContextPhysical);
        UsbDevice->InputContextLinear = 0;
        UsbDevice->InputContextPhysical = 0;
        return FALSE;
    }

    if (!XHCI_InitTransferRing(UsbDevice)) {
        FreeRegion(UsbDevice->DeviceContextLinear, PAGE_SIZE);
        FreePhysicalPage(UsbDevice->DeviceContextPhysical);
        FreeRegion(UsbDevice->InputContextLinear, PAGE_SIZE);
        FreePhysicalPage(UsbDevice->InputContextPhysical);
        UsbDevice->DeviceContextLinear = 0;
        UsbDevice->DeviceContextPhysical = 0;
        UsbDevice->InputContextLinear = 0;
        UsbDevice->InputContextPhysical = 0;
        return FALSE;
    }

    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);
    MemorySet((LPVOID)UsbDevice->DeviceContextLinear, 0, PAGE_SIZE);
    UsbDevice->Present = FALSE;
    UsbDevice->SlotId = 0;
    UsbDevice->Address = 0;
    UsbDevice->SelectedConfigValue = 0;
    UsbDevice->StringManufacturer = 0;
    UsbDevice->StringProduct = 0;
    UsbDevice->StringSerial = 0;
    UsbDevice->IsHub = FALSE;
    UsbDevice->HubPortCount = 0;
    UsbDevice->HubInterruptEndpoint = NULL;
    UsbDevice->HubInterruptLength = 0;
    UsbDevice->HubStatusTrbPhysical = U64_FromUINT(0);
    UsbDevice->HubStatusPending = FALSE;
    UsbDevice->DestroyPending = FALSE;
    UsbDevice->Controller = Device;

    return TRUE;
}
/************************************************************************/

/**
 * @brief Read the full configuration descriptor.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param PhysicalOut Receives physical address.
 * @param LinearOut Receives linear address.
 * @param LengthOut Receives total length.
 * @return TRUE on success.
 */
static BOOL XHCI_ReadConfigDescriptor(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice,
                                      PHYSICAL* PhysicalOut, LINEAR* LinearOut, U16* LengthOut) {
    USB_SETUP_PACKET Setup;
    PHYSICAL Physical = 0;
    LINEAR Linear = 0;
    USB_CONFIGURATION_DESCRIPTOR Config;
    U16 TotalLength = 0;

    if (PhysicalOut == NULL || LinearOut == NULL || LengthOut == NULL) {
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_CfgDesc"), &Physical, &Linear)) {
        return FALSE;
    }

    MemorySet((LPVOID)Linear, 0, USB_DESCRIPTOR_LENGTH_CONFIGURATION);

    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_IN | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_DEVICE;
    Setup.Request = USB_REQUEST_GET_DESCRIPTOR;
    Setup.Value = (USB_DESCRIPTOR_TYPE_CONFIGURATION << 8);
    Setup.Index = 0;
    Setup.Length = USB_DESCRIPTOR_LENGTH_CONFIGURATION;

    if (!XHCI_ControlTransfer(Device, UsbDevice, &Setup, Physical, (LPVOID)Linear,
                              USB_DESCRIPTOR_LENGTH_CONFIGURATION, TRUE)) {
        FreeRegion(Linear, PAGE_SIZE);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    MemoryCopy(&Config, (LPVOID)Linear, sizeof(Config));
    TotalLength = Config.TotalLength;
    if (TotalLength == 0) {
        FreeRegion(Linear, PAGE_SIZE);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    if (TotalLength > PAGE_SIZE) {
        TotalLength = PAGE_SIZE;
    }

    MemorySet((LPVOID)Linear, 0, TotalLength);
    Setup.Length = TotalLength;
    if (!XHCI_ControlTransfer(Device, UsbDevice, &Setup, Physical, (LPVOID)Linear, TotalLength, TRUE)) {
        FreeRegion(Linear, PAGE_SIZE);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    *PhysicalOut = Physical;
    *LinearOut = Linear;
    *LengthOut = TotalLength;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Get the USB device descriptor.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_GetDeviceDescriptor(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    USB_SETUP_PACKET Setup;
    LPVOID Buffer = NULL;
    PHYSICAL Physical = 0;
    LINEAR Linear = 0;

    if (!XHCI_AllocPage(TEXT("XHCI_DevDesc"), &Physical, &Linear)) {
        return FALSE;
    }

    Buffer = (LPVOID)Linear;
    MemorySet(Buffer, 0, USB_DESCRIPTOR_LENGTH_DEVICE);

    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_IN | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_DEVICE;
    Setup.Request = USB_REQUEST_GET_DESCRIPTOR;
    Setup.Value = (USB_DESCRIPTOR_TYPE_DEVICE << 8);
    Setup.Index = 0;
    Setup.Length = USB_DESCRIPTOR_LENGTH_DEVICE;

    if (!XHCI_ControlTransfer(Device, UsbDevice, &Setup, Physical, Buffer, USB_DESCRIPTOR_LENGTH_DEVICE, TRUE)) {
        FreeRegion(Linear, PAGE_SIZE);
        FreePhysicalPage(Physical);
        return FALSE;
    }

    MemoryCopy(&UsbDevice->DeviceDescriptor, Buffer, sizeof(USB_DEVICE_DESCRIPTOR));

    FreeRegion(Linear, PAGE_SIZE);
    FreePhysicalPage(Physical);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Enumerate a USB device already reset on a given port.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
BOOL XHCI_EnumerateDevice(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    PHYSICAL ConfigPhysical = 0;
    LINEAR ConfigLinear = 0;
    U16 ConfigLength = 0;
    U32 Completion = 0;

    UsbDevice->LastEnumError = XHCI_ENUM_ERROR_NONE;
    UsbDevice->LastEnumCompletion = 0;
    RateLimiterReset(&UsbDevice->EnumFailureLogLimiter);
    UsbDevice->MaxPacketSize0 = XHCI_GetDefaultMaxPacketSize0(UsbDevice->SpeedId);

    if (!XHCI_InitUsbDeviceState(Device, UsbDevice)) {
        UsbDevice->LastEnumError = XHCI_ENUM_ERROR_INIT_STATE;
        XHCI_LogProbeFailure(UsbDevice, TEXT("InitializeDeviceState"), XHCI_ReadRootPortStatusSafe(Device, UsbDevice));
        return FALSE;
    }

    if (!XHCI_EnableSlot(Device, &UsbDevice->SlotId, &Completion)) {
        UsbDevice->LastEnumError = XHCI_ENUM_ERROR_ENABLE_SLOT;
        UsbDevice->LastEnumCompletion = (U16)Completion;
        XHCI_LogProbeFailure(UsbDevice, TEXT("EnableSlot"), XHCI_ReadRootPortStatusSafe(Device, UsbDevice));
        return FALSE;
    }

    ((U64 *)Device->DcbaaLinear)[UsbDevice->SlotId] = U64_FromUINT(UsbDevice->DeviceContextPhysical);

    XHCI_BuildInputContextForAddress(Device, UsbDevice);
    if (!XHCI_AddressDevice(Device, UsbDevice)) {
        UsbDevice->LastEnumError = XHCI_ENUM_ERROR_ADDRESS_DEVICE;
        XHCI_LogProbeFailure(UsbDevice, TEXT("AddressDevice"), XHCI_ReadRootPortStatusSafe(Device, UsbDevice));
        return FALSE;
    }

    UsbDevice->Address = UsbDevice->SlotId;

    if (!XHCI_GetDeviceDescriptor(Device, UsbDevice)) {
        UsbDevice->LastEnumError = XHCI_ENUM_ERROR_DEVICE_DESC;
        XHCI_LogProbeFailure(UsbDevice, TEXT("GetDeviceDescriptor"), XHCI_ReadRootPortStatusSafe(Device, UsbDevice));
        return FALSE;
    }

    UsbDevice->StringManufacturer = UsbDevice->DeviceDescriptor.ManufacturerIndex;
    UsbDevice->StringProduct = UsbDevice->DeviceDescriptor.ProductIndex;
    UsbDevice->StringSerial = UsbDevice->DeviceDescriptor.SerialNumberIndex;

    UsbDevice->MaxPacketSize0 = XHCI_ComputeMaxPacketSize0(
        UsbDevice->SpeedId, UsbDevice->DeviceDescriptor.MaxPacketSize0);

    XHCI_BuildInputContextForEp0(Device, UsbDevice);
    (void)XHCI_EvaluateContext(Device, UsbDevice);

    if (!XHCI_ReadConfigDescriptor(Device, UsbDevice, &ConfigPhysical, &ConfigLinear, &ConfigLength)) {
        UsbDevice->LastEnumError = XHCI_ENUM_ERROR_CONFIG_DESC;
        XHCI_LogProbeFailure(UsbDevice, TEXT("ReadConfigDescriptor"), XHCI_ReadRootPortStatusSafe(Device, UsbDevice));
        return FALSE;
    }

    if (!XHCI_ParseConfigDescriptor(UsbDevice, (const U8*)ConfigLinear, ConfigLength)) {
        FreeRegion(ConfigLinear, PAGE_SIZE);
        FreePhysicalPage(ConfigPhysical);
        UsbDevice->LastEnumError = XHCI_ENUM_ERROR_CONFIG_PARSE;
        XHCI_LogProbeFailure(UsbDevice, TEXT("ParseConfigDescriptor"), XHCI_ReadRootPortStatusSafe(Device, UsbDevice));
        return FALSE;
    }

    if (ConfigLinear) {
        FreeRegion(ConfigLinear, PAGE_SIZE);
        FreePhysicalPage(ConfigPhysical);
    }

    if (UsbDevice->ConfigCount > 0) {
        USB_SETUP_PACKET Setup;
        MemorySet(&Setup, 0, sizeof(Setup));
        Setup.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_DEVICE;
        Setup.Request = USB_REQUEST_SET_CONFIGURATION;
        Setup.Value = UsbDevice->Configs[0].ConfigurationValue;
        Setup.Index = 0;
        Setup.Length = 0;

        if (!XHCI_ControlTransfer(Device, UsbDevice, &Setup, 0, NULL, 0, FALSE)) {
            UsbDevice->LastEnumError = XHCI_ENUM_ERROR_SET_CONFIG;
            XHCI_LogProbeFailure(UsbDevice, TEXT("SetConfiguration"), XHCI_ReadRootPortStatusSafe(Device, UsbDevice));
            return FALSE;
        }

        UsbDevice->SelectedConfigValue = UsbDevice->Configs[0].ConfigurationValue;
    }

    UsbDevice->IsHub = XHCI_IsHubDevice(UsbDevice);
    UsbDevice->Present = TRUE;
    RateLimiterReset(&UsbDevice->EnumFailureLogLimiter);
    XHCI_AddDeviceToList(Device, UsbDevice);
    return TRUE;
}

/************************************************************************/
/**
 * @brief Probe a port and fetch descriptors.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param PortIndex Port index (0-based).
 * @return TRUE on success.
 */
static BOOL XHCI_ProbePort(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U32 PortIndex) {
    U32 PortStatus = XHCI_ReadPortStatus(Device, PortIndex);
    U32 SpeedId = (PortStatus & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;

    if ((PortStatus & XHCI_PORTSC_CCS) == 0) {
        UsbDevice->Present = FALSE;
        UsbDevice->LastEnumError = XHCI_ENUM_ERROR_NONE;
        UsbDevice->LastEnumCompletion = 0;
        RateLimiterReset(&UsbDevice->EnumFailureLogLimiter);
        return FALSE;
    }

    if (UsbDevice->DestroyPending && XHCI_UsbTreeHasReferences(UsbDevice)) {
        WARNING(TEXT("[XHCI_ProbePort] Port %u still referenced, delaying re-enumeration"),
                PortIndex + 1);
        UsbDevice->LastEnumError = XHCI_ENUM_ERROR_BUSY;
        XHCI_LogProbeFailure(UsbDevice, TEXT("DestroyPending"), PortStatus);
        return FALSE;
    }

    UsbDevice->PortNumber = (U8)(PortIndex + 1);
    UsbDevice->RootPortNumber = UsbDevice->PortNumber;
    UsbDevice->Depth = 0;
    UsbDevice->RouteString = 0;
    UsbDevice->Parent = NULL;
    UsbDevice->ParentPort = 0;
    UsbDevice->IsRootPort = TRUE;
    UsbDevice->Controller = Device;
    UsbDevice->SpeedId = (U8)SpeedId;
    UsbDevice->DestroyPending = FALSE;

    if (UsbDevice->Present) {
        return TRUE;
    }

    BOOL PortEnabled = (PortStatus & XHCI_PORTSC_PED) != 0;
    if (!PortEnabled) {
        if (!XHCI_ResetPort(Device, PortIndex)) {
            U32 RetryStatus = XHCI_ReadPortStatus(Device, PortIndex);
            BOOL RetryConnected = (RetryStatus & XHCI_PORTSC_CCS) != 0;
            BOOL RetryEnabled = (RetryStatus & XHCI_PORTSC_PED) != 0;
            U32 RetrySpeed = (RetryStatus & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
            if (!(RetryConnected && RetryEnabled && RetrySpeed != 0u)) {
                UsbDevice->LastEnumError = XHCI_ENUM_ERROR_RESET_TIMEOUT;
                XHCI_LogProbeFailure(UsbDevice, TEXT("ResetPort"), RetryStatus);
                return FALSE;
            }
            PortStatus = RetryStatus;
            SpeedId = RetrySpeed;
            UsbDevice->SpeedId = (U8)SpeedId;
        }
    }

    PortStatus = XHCI_ReadPortStatus(Device, PortIndex);
    SpeedId = (PortStatus & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
    UsbDevice->SpeedId = (U8)SpeedId;

    if (UsbDevice->SpeedId == 0) {
        WARNING(TEXT("[XHCI_ProbePort] Port %u invalid speed after reset"), PortIndex + 1);
        UsbDevice->LastEnumError = XHCI_ENUM_ERROR_INVALID_SPEED;
        XHCI_LogProbeFailure(UsbDevice, TEXT("ReadSpeed"), PortStatus);
        return FALSE;
    }

    if (!XHCI_EnumerateDevice(Device, UsbDevice)) {
        WARNING(TEXT("[XHCI_ProbePort] Port %u enumerate failed"), PortIndex + 1);
        XHCI_LogProbeFailure(UsbDevice, TEXT("EnumerateDevice"), PortStatus);
        return FALSE;
    }

    if (UsbDevice->IsHub) {
        if (!XHCI_InitHub(Device, UsbDevice)) {
            WARNING(TEXT("[XHCI_ProbePort] Port %u hub init failed"), PortIndex + 1);
            UsbDevice->LastEnumError = XHCI_ENUM_ERROR_HUB_INIT;
        }
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Enumerate connected devices on all ports.
 * @param Device xHCI device.
 */
void XHCI_EnsureUsbDevices(LPXHCI_DEVICE Device) {
    if (Device == NULL || Device->UsbDevices == NULL) {
        return;
    }

    for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
        LPXHCI_USB_DEVICE UsbDevice = Device->UsbDevices[PortIndex];
        if (UsbDevice == NULL) {
            continue;
        }
        U32 PortStatus = XHCI_ReadPortStatus(Device, PortIndex);
        BOOL Connected = (PortStatus & XHCI_PORTSC_CCS) != 0;
        if (!Connected) {
            if (UsbDevice->Present) {
                XHCI_DestroyUsbDevice(Device, UsbDevice, FALSE);
            }
            continue;
        }

        if (!UsbDevice->Present) {
            (void)XHCI_ProbePort(Device, UsbDevice, PortIndex);
        }
    }
}
