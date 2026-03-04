
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


    USB HID Mouse

\************************************************************************/

#include "input/MouseCommon.h"
#include "drivers/usb/XHCI-Internal.h"

/***************************************************************************/

#define USB_MOUSE_VER_MAJOR 1
#define USB_MOUSE_VER_MINOR 0

#define USB_CLASS_HID 0x03
#define USB_HID_SUBCLASS_BOOT 0x01
#define USB_HID_PROTOCOL_MOUSE 0x02

#define USB_HID_REQUEST_SET_PROTOCOL 0x0B
#define USB_HID_REQUEST_SET_IDLE 0x0A

#define USB_HID_PROTOCOL_BOOT 0x00

/***************************************************************************/

typedef struct tag_USB_MOUSE_STATE {
    BOOL Initialized;
    LPXHCI_DEVICE Controller;
    LPXHCI_USB_DEVICE UsbDevice;
    LPXHCI_USB_INTERFACE Interface;
    LPXHCI_USB_ENDPOINT Endpoint;
    U8 InterfaceNumber;
    U16 ReportLength;
    PHYSICAL ReportPhysical;
    LINEAR ReportLinear;
    U64 ReportTrbPhysical;
    BOOL ReportPending;
    BOOL ReferencesHeld;
    U32 RetryDelay;
    U32 PollHandle;
} USB_MOUSE_STATE, *LPUSB_MOUSE_STATE;

typedef struct tag_USB_MOUSE_CUSTOM_DATA {
    MOUSE_COMMON_CONTEXT Common;
    USB_MOUSE_STATE State;
} USB_MOUSE_CUSTOM_DATA, *LPUSB_MOUSE_CUSTOM_DATA;

static void USBMousePoll(LPVOID Context);

/***************************************************************************/

UINT USBMouseCommands(UINT Function, UINT Parameter);

static USB_MOUSE_CUSTOM_DATA DATA_SECTION USBMouseCustomData = {
    .Common = {
        .Initialized = FALSE,
        .Mutex = EMPTY_MUTEX,
        .DeltaX = 0,
        .DeltaY = 0,
        .Buttons = 0,
        .Packet = {.DeltaX = 0, .DeltaY = 0, .Buttons = 0, .Pending = FALSE},
        .DeferredHandle = DEFERRED_WORK_INVALID_HANDLE
    },
    .State = {
        .Initialized = FALSE,
        .Controller = NULL,
        .UsbDevice = NULL,
        .Interface = NULL,
        .Endpoint = NULL,
        .InterfaceNumber = 0,
        .ReportLength = 0,
        .ReportPhysical = 0,
        .ReportLinear = 0,
        .ReportTrbPhysical = U64_0,
        .ReportPending = FALSE,
        .ReferencesHeld = FALSE,
        .RetryDelay = 0,
        .PollHandle = DEFERRED_WORK_INVALID_HANDLE
    }
};

static DRIVER DATA_SECTION USBMouseDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_MOUSE,
    .VersionMajor = USB_MOUSE_VER_MAJOR,
    .VersionMinor = USB_MOUSE_VER_MINOR,
    .Designer = "Jango73",
    .Manufacturer = "USB-IF",
    .Product = "USB HID Mouse",
    .Alias = "usb_mouse",
    .Flags = 0,
    .Command = USBMouseCommands,
    .CustomData = &USBMouseCustomData
};

/***************************************************************************/

/**
 * @brief Retrieve the USB mouse driver descriptor.
 * @return Pointer to the USB mouse driver.
 */
LPDRIVER USBMouseGetDriver(void) {
    return &USBMouseDriver;
}

/***************************************************************************/

/**
 * @brief Check if an interface is a HID boot mouse.
 * @param Interface Interface descriptor.
 * @return TRUE when the interface matches a HID mouse.
 */
static BOOL USBMouseIsHidMouseInterface(LPXHCI_USB_INTERFACE Interface) {
    if (Interface == NULL) {
        return FALSE;
    }

    return (Interface->InterfaceClass == USB_CLASS_HID &&
            Interface->InterfaceSubClass == USB_HID_SUBCLASS_BOOT &&
            Interface->InterfaceProtocol == USB_HID_PROTOCOL_MOUSE);
}

/***************************************************************************/

/**
 * @brief Find interrupt IN endpoint in an interface.
 * @param Interface Interface descriptor.
 * @return Endpoint pointer or NULL.
 */
static LPXHCI_USB_ENDPOINT USBMouseFindInterruptInEndpoint(LPXHCI_USB_INTERFACE Interface) {
    if (Interface == NULL) {
        return NULL;
    }

    return XHCI_FindInterfaceEndpoint(Interface, USB_ENDPOINT_TYPE_INTERRUPT, TRUE);
}

/***************************************************************************/

/**
 * @brief Set HID boot protocol on a mouse interface.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param InterfaceNumber Interface number.
 * @return TRUE on success.
 */
static BOOL USBMouseSetBootProtocol(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U8 InterfaceNumber) {
    USB_SETUP_PACKET Setup;
    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_CLASS | USB_REQUEST_RECIPIENT_INTERFACE;
    Setup.Request = USB_HID_REQUEST_SET_PROTOCOL;
    Setup.Value = USB_HID_PROTOCOL_BOOT;
    Setup.Index = InterfaceNumber;
    Setup.Length = 0;

    return XHCI_ControlTransfer(Device, UsbDevice, &Setup, 0, NULL, 0, FALSE);
}

/***************************************************************************/

/**
 * @brief Set HID idle rate on a mouse interface.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param InterfaceNumber Interface number.
 * @return TRUE on success.
 */
static BOOL USBMouseSetIdle(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U8 InterfaceNumber) {
    USB_SETUP_PACKET Setup;
    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_CLASS | USB_REQUEST_RECIPIENT_INTERFACE;
    Setup.Request = USB_HID_REQUEST_SET_IDLE;
    Setup.Value = 0;
    Setup.Index = InterfaceNumber;
    Setup.Length = 0;

    return XHCI_ControlTransfer(Device, UsbDevice, &Setup, 0, NULL, 0, FALSE);
}

/***************************************************************************/

/**
 * @brief Release resources for the active USB mouse.
 */
static void USBMouseClearState(void) {
    if (USBMouseCustomData.State.ReferencesHeld) {
        XHCI_ReleaseUsbEndpoint(USBMouseCustomData.State.Endpoint);
        XHCI_ReleaseUsbInterface(USBMouseCustomData.State.Interface);
        XHCI_ReleaseUsbDevice(USBMouseCustomData.State.UsbDevice);
        USBMouseCustomData.State.ReferencesHeld = FALSE;
    }

    if (USBMouseCustomData.State.ReportLinear != 0) {
        FreeRegion(USBMouseCustomData.State.ReportLinear, PAGE_SIZE);
        USBMouseCustomData.State.ReportLinear = 0;
    }
    if (USBMouseCustomData.State.ReportPhysical != 0) {
        FreePhysicalPage(USBMouseCustomData.State.ReportPhysical);
        USBMouseCustomData.State.ReportPhysical = 0;
    }

    USBMouseCustomData.State.Controller = NULL;
    USBMouseCustomData.State.UsbDevice = NULL;
    USBMouseCustomData.State.Interface = NULL;
    USBMouseCustomData.State.Endpoint = NULL;
    USBMouseCustomData.State.InterfaceNumber = 0;
    USBMouseCustomData.State.ReportLength = 0;
    USBMouseCustomData.State.ReportTrbPhysical = U64_FromUINT(0);
    USBMouseCustomData.State.ReportPending = FALSE;
    USBMouseCustomData.State.RetryDelay = 0;
}

/***************************************************************************/

/**
 * @brief Ensure the active device is still present.
 * @param Device xHCI device.
 * @param UsbDevice USB device to check.
 * @return TRUE when still present.
 */
static BOOL USBMouseIsDevicePresent(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (Device == NULL || UsbDevice == NULL) {
        return FALSE;
    }

    LPLIST UsbDeviceList = GetUsbDeviceList();
    if (UsbDeviceList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = UsbDeviceList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_DEVICE Curr = (LPXHCI_USB_DEVICE)Node;
        if (Curr->Controller != Device) {
            continue;
        }
        if (Curr == UsbDevice && Curr->Present) {
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Locate a HID mouse device on any xHCI controller.
 * @param DeviceOut Receives controller.
 * @param UsbDeviceOut Receives USB device.
 * @param InterfaceOut Receives interface.
 * @param EndpointOut Receives endpoint.
 * @return TRUE if a mouse device was found.
 */
static BOOL USBMouseFindDevice(LPXHCI_DEVICE* DeviceOut,
                               LPXHCI_USB_DEVICE* UsbDeviceOut,
                               LPXHCI_USB_INTERFACE* InterfaceOut,
                               LPXHCI_USB_ENDPOINT* EndpointOut) {
    if (DeviceOut == NULL || UsbDeviceOut == NULL || InterfaceOut == NULL || EndpointOut == NULL) {
        return FALSE;
    }

    LPLIST PciList = GetPCIDeviceList();
    if (PciList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = PciList->First; Node; Node = Node->Next) {
        LPPCI_DEVICE PciDevice = (LPPCI_DEVICE)Node;
        if (PciDevice->Driver != (LPDRIVER)&XHCIDriver) {
            continue;
        }

        LPXHCI_DEVICE Device = (LPXHCI_DEVICE)PciDevice;
        SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
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
                if (!UsbDevice->Present || UsbDevice->IsHub) {
                    continue;
                }

                LPXHCI_USB_CONFIGURATION Config = XHCI_GetSelectedConfig(UsbDevice);
                if (Config == NULL) {
                    continue;
                }

                LPLIST InterfaceList = GetUsbInterfaceList();
                if (InterfaceList == NULL) {
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
                    if (!USBMouseIsHidMouseInterface(Interface)) {
                        continue;
                    }

                    LPXHCI_USB_ENDPOINT Endpoint = USBMouseFindInterruptInEndpoint(Interface);
                    if (Endpoint == NULL) {
                        continue;
                    }

                    *DeviceOut = Device;
                    *UsbDeviceOut = UsbDevice;
                    *InterfaceOut = Interface;
                    *EndpointOut = Endpoint;
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Submit an interrupt IN transfer for the mouse report.
 * @param Device xHCI device.
 * @return TRUE on success.
 */
static BOOL USBMouseSubmitReport(LPXHCI_DEVICE Device) {
    if (Device == NULL || USBMouseCustomData.State.Endpoint == NULL ||
        USBMouseCustomData.State.ReportLinear == 0 || USBMouseCustomData.State.ReportPhysical == 0) {
        return FALSE;
    }

    XHCI_TRB Trb;
    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(USBMouseCustomData.State.ReportPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(USBMouseCustomData.State.ReportPhysical));
    Trb.Dword2 = USBMouseCustomData.State.ReportLength;
    Trb.Dword3 = (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC | XHCI_TRB_DIR_IN;

    if (!XHCI_RingEnqueue(USBMouseCustomData.State.Endpoint->TransferRingLinear,
                          USBMouseCustomData.State.Endpoint->TransferRingPhysical,
                          &USBMouseCustomData.State.Endpoint->TransferRingEnqueueIndex,
                          &USBMouseCustomData.State.Endpoint->TransferRingCycleState,
                          XHCI_TRANSFER_RING_TRBS,
                          &Trb,
                          &USBMouseCustomData.State.ReportTrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, USBMouseCustomData.State.UsbDevice->SlotId, USBMouseCustomData.State.Endpoint->Dci);
    USBMouseCustomData.State.ReportPending = TRUE;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Parse and dispatch a HID boot mouse report.
 */
static void USBMouseHandleReport(void) {
    const U8* Report = (const U8*)USBMouseCustomData.State.ReportLinear;
    U32 Buttons = 0;
    I32 DeltaX = 0;
    I32 DeltaY = 0;

    if (Report == NULL || USBMouseCustomData.State.ReportLength < 3) {
        return;
    }

    if ((Report[0] & BIT_0) != 0) {
        Buttons |= MB_LEFT;
    }
    if ((Report[0] & BIT_1) != 0) {
        Buttons |= MB_RIGHT;
    }
    if ((Report[0] & BIT_2) != 0) {
        Buttons |= MB_MIDDLE;
    }

    DeltaX = (I32)(I8)Report[1];
    DeltaY = (I32)(I8)Report[2];

    MouseCommonQueuePacket(&USBMouseCustomData.Common, DeltaX, DeltaY, Buttons);
}

/***************************************************************************/

/**
 * @brief Initialize the USB mouse state for a detected device.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Interface HID interface.
 * @param Endpoint Interrupt IN endpoint.
 * @return TRUE on success.
 */
static BOOL USBMouseStartDevice(LPXHCI_DEVICE Device,
                                LPXHCI_USB_DEVICE UsbDevice,
                                LPXHCI_USB_INTERFACE Interface,
                                LPXHCI_USB_ENDPOINT Endpoint) {
    if (Device == NULL || UsbDevice == NULL || Interface == NULL || Endpoint == NULL) {
        return FALSE;
    }

    if (!USBMouseSetBootProtocol(Device, UsbDevice, Interface->Number)) {
        WARNING(TEXT("[USBMouseStartDevice] SET_PROTOCOL failed"));
    }
    if (!USBMouseSetIdle(Device, UsbDevice, Interface->Number)) {
        WARNING(TEXT("[USBMouseStartDevice] SET_IDLE failed"));
    }

    if (!XHCI_AddInterruptEndpoint(Device, UsbDevice, Endpoint)) {
        ERROR(TEXT("[USBMouseStartDevice] Interrupt endpoint setup failed"));
        return FALSE;
    }

    if (Endpoint->MaxPacketSize == 0) {
        ERROR(TEXT("[USBMouseStartDevice] Invalid report size"));
        return FALSE;
    }

    U16 ReportLength = Endpoint->MaxPacketSize;
    if (ReportLength > PAGE_SIZE) {
        ReportLength = PAGE_SIZE;
    }

    if (!XHCI_AllocPage(TEXT("USBMouseReport"),
                        &USBMouseCustomData.State.ReportPhysical,
                        &USBMouseCustomData.State.ReportLinear)) {
        ERROR(TEXT("[USBMouseStartDevice] Report buffer alloc failed"));
        return FALSE;
    }

    USBMouseCustomData.State.Controller = Device;
    USBMouseCustomData.State.UsbDevice = UsbDevice;
    USBMouseCustomData.State.Interface = Interface;
    USBMouseCustomData.State.Endpoint = Endpoint;
    USBMouseCustomData.State.InterfaceNumber = Interface->Number;
    USBMouseCustomData.State.ReportLength = ReportLength;
    USBMouseCustomData.State.ReportTrbPhysical = U64_FromUINT(0);
    USBMouseCustomData.State.ReportPending = FALSE;

    XHCI_ReferenceUsbDevice(UsbDevice);
    XHCI_ReferenceUsbInterface(Interface);
    XHCI_ReferenceUsbEndpoint(Endpoint);
    USBMouseCustomData.State.ReferencesHeld = TRUE;

    DEBUG(TEXT("[USBMouseStartDevice] Mouse addr=%x if=%u ep=%x"),
          UsbDevice->Address,
          (U32)Interface->Number,
          (U32)Endpoint->Address);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Poll USB mouse state and process reports.
 * @param Context Unused.
 */
static void USBMousePoll(LPVOID Context) {
    UNUSED(Context);

    if (USBMouseCustomData.State.Initialized == FALSE) {
        return;
    }

    if (USBMouseCustomData.State.RetryDelay != 0) {
        USBMouseCustomData.State.RetryDelay--;
        return;
    }

    if (USBMouseCustomData.State.Controller != NULL && USBMouseCustomData.State.UsbDevice != NULL) {
        if (!USBMouseIsDevicePresent(USBMouseCustomData.State.Controller, USBMouseCustomData.State.UsbDevice)) {
            DEBUG(TEXT("[USBMousePoll] Mouse disconnected"));
            USBMouseClearState();
            USBMouseCustomData.State.RetryDelay = 50;
        }
    }

    if (USBMouseCustomData.State.Controller == NULL) {
        LPXHCI_DEVICE Device = NULL;
        LPXHCI_USB_DEVICE UsbDevice = NULL;
        LPXHCI_USB_INTERFACE Interface = NULL;
        LPXHCI_USB_ENDPOINT Endpoint = NULL;

        if (USBMouseFindDevice(&Device, &UsbDevice, &Interface, &Endpoint)) {
            if (!USBMouseStartDevice(Device, UsbDevice, Interface, Endpoint)) {
                USBMouseClearState();
                USBMouseCustomData.State.RetryDelay = 50;
            }
        }
    }

    if (USBMouseCustomData.State.Controller == NULL) {
        return;
    }

    if (!USBMouseCustomData.State.ReportPending) {
        (void)USBMouseSubmitReport(USBMouseCustomData.State.Controller);
        return;
    }

    U32 Completion = 0;
    if (!XHCI_CheckTransferCompletion(USBMouseCustomData.State.Controller,
                                       USBMouseCustomData.State.ReportTrbPhysical,
                                       &Completion)) {
        return;
    }

    USBMouseCustomData.State.ReportPending = FALSE;
    if (Completion == XHCI_COMPLETION_SUCCESS || Completion == XHCI_COMPLETION_SHORT_PACKET) {
        USBMouseHandleReport();
    } else {
        WARNING(TEXT("[USBMousePoll] Completion %x"), Completion);
    }
}

/***************************************************************************/

/**
 * @brief Driver command dispatcher for USB mouse.
 * @param Function Driver function code.
 * @param Parameter Function-specific parameter.
 * @return Driver status or data.
 */
UINT USBMouseCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD: {
            if ((USBMouseDriver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            if (!MouseCommonInitialize(&USBMouseCustomData.Common)) {
                return DF_RETURN_UNEXPECTED;
            }

            if (USBMouseCustomData.State.PollHandle == DEFERRED_WORK_INVALID_HANDLE) {
                USBMouseCustomData.State.PollHandle = DeferredWorkRegisterPollOnly(USBMousePoll, NULL, TEXT("USBMouse"));
                if (USBMouseCustomData.State.PollHandle == DEFERRED_WORK_INVALID_HANDLE) {
                    return DF_RETURN_UNEXPECTED;
                }
            }

            USBMouseCustomData.State.Initialized = TRUE;
            USBMouseDriver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;
        }
        case DF_UNLOAD:
            if ((USBMouseDriver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            if (USBMouseCustomData.State.PollHandle != DEFERRED_WORK_INVALID_HANDLE) {
                DeferredWorkUnregister(USBMouseCustomData.State.PollHandle);
                USBMouseCustomData.State.PollHandle = DEFERRED_WORK_INVALID_HANDLE;
            }

            USBMouseClearState();
            USBMouseDriver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;
        case DF_GET_VERSION:
            return MAKE_VERSION(USB_MOUSE_VER_MAJOR, USB_MOUSE_VER_MINOR);
        case DF_MOUSE_RESET:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_MOUSE_GETDELTAX:
            return (UINT)MouseCommonGetDeltaX(&USBMouseCustomData.Common);
        case DF_MOUSE_GETDELTAY:
            return (UINT)MouseCommonGetDeltaY(&USBMouseCustomData.Common);
        case DF_MOUSE_GETBUTTONS:
            return (UINT)MouseCommonGetButtons(&USBMouseCustomData.Common);
        case DF_MOUSE_HAS_DEVICE:
            if (USBMouseCustomData.State.Controller != NULL && USBMouseCustomData.State.UsbDevice != NULL) {
                return USBMouseIsDevicePresent(USBMouseCustomData.State.Controller,
                                               USBMouseCustomData.State.UsbDevice)
                           ? 1U
                           : 0U;
            }
            return 0;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/***************************************************************************/
