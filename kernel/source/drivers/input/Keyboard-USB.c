
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


    USB HID Keyboard

\************************************************************************/

#include "drivers/input/Keyboard.h"
#include "drivers/usb/XHCI-Internal.h"

#include "Base.h"
#include "Clock.h"
#include "Console.h"
#include "DisplaySession.h"
#include "DeferredWork.h"
#include "Kernel.h"
#include "Log.h"
#include "input/VKey.h"
#include "utils/HIDReport.h"
#include "utils/RateLimiter.h"

/***************************************************************************/

#define USB_KEYBOARD_VER_MAJOR 1
#define USB_KEYBOARD_VER_MINOR 0

#define USB_CLASS_HID 0x03
#define USB_HID_SUBCLASS_BOOT 0x01
#define USB_HID_PROTOCOL_KEYBOARD 0x01

#define USB_HID_REQUEST_SET_PROTOCOL 0x0B
#define USB_HID_REQUEST_SET_IDLE 0x0A

#define USB_HID_PROTOCOL_BOOT 0x00

#define USB_KEYBOARD_BOOT_REPORT_SIZE 8
#define USB_KEYBOARD_BOOT_KEYS 6
#define USB_KEYBOARD_HID_DESCRIPTOR_TYPE 0x21
#define USB_KEYBOARD_HID_REPORT_DESCRIPTOR_TYPE 0x22
#define USB_KEYBOARD_HID_DESCRIPTOR_LENGTH 9
#define USB_KEYBOARD_MAX_REPORT_DESCRIPTOR 256
#define USB_KEYBOARD_USAGE_PAGE_CONSUMER 0x0C
#define USB_KEYBOARD_MEDIA_USAGE_COUNT 18

/***************************************************************************/

typedef struct tag_USB_MEDIA_USAGE_MAP {
    U16 Usage;
    U8 VirtualKey;
} USB_MEDIA_USAGE_MAP, *LPUSB_MEDIA_USAGE_MAP;

/***************************************************************************/

typedef struct tag_USB_KEYBOARD_STATE {
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
    U32 RetryDelay;
    U32 PollHandle;
    U8 PrevModifiers;
    U8 PrevKeys[USB_KEYBOARD_BOOT_KEYS];
    LPXHCI_USB_INTERFACE ConsumerInterface;
    LPXHCI_USB_ENDPOINT ConsumerEndpoint;
    U16 ConsumerReportLength;
    PHYSICAL ConsumerReportPhysical;
    LINEAR ConsumerReportLinear;
    U64 ConsumerReportTrbPhysical;
    BOOL ConsumerReportPending;
    U16 ConsumerReportDescriptorLength;
    U8 ConsumerReportDescriptor[USB_KEYBOARD_MAX_REPORT_DESCRIPTOR];
    HID_REPORT_LAYOUT ConsumerLayout;
    HID_REPORT_FIELD ConsumerFields[HID_REPORT_MAX_FIELDS];
    U8 ConsumerPressed[USB_KEYBOARD_MEDIA_USAGE_COUNT];
    RATE_LIMITER ConsumerUnknownUsageLogLimiter;
    BOOL ReferencesHeld;
} USB_KEYBOARD_STATE, *LPUSB_KEYBOARD_STATE;

typedef struct tag_USB_KEYBOARD_DRIVER {
    DRIVER Driver;
    USB_KEYBOARD_STATE State;
} USB_KEYBOARD_DRIVER, *LPUSB_KEYBOARD_DRIVER;

static void USBKeyboardPoll(LPVOID Context);

/***************************************************************************/

UINT USBKeyboardCommands(UINT Function, UINT Parameter);

static USB_KEYBOARD_DRIVER DATA_SECTION USBKeyboardDriverState = {
    .Driver = {
        .TypeID = KOID_DRIVER,
        .References = 1,
        .Next = NULL,
        .Prev = NULL,
        .Type = DRIVER_TYPE_KEYBOARD,
        .VersionMajor = USB_KEYBOARD_VER_MAJOR,
        .VersionMinor = USB_KEYBOARD_VER_MINOR,
        .Designer = "Jango73",
        .Manufacturer = "USB-IF",
        .Product = "USB HID Keyboard",
        .Flags = 0,
        .Command = USBKeyboardCommands
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
        .RetryDelay = 0,
        .PollHandle = DEFERRED_WORK_INVALID_HANDLE,
        .PrevModifiers = 0,
        .PrevKeys = {0},
        .ConsumerInterface = NULL,
        .ConsumerEndpoint = NULL,
        .ConsumerReportLength = 0,
        .ConsumerReportPhysical = 0,
        .ConsumerReportLinear = 0,
        .ConsumerReportTrbPhysical = U64_0,
        .ConsumerReportPending = FALSE,
        .ConsumerReportDescriptorLength = 0,
        .ConsumerLayout = {.Fields = NULL, .FieldCount = 0, .FieldCapacity = 0},
        .ReferencesHeld = FALSE
    }
};

/***************************************************************************/

static const USB_MEDIA_USAGE_MAP USBKeyboardMediaUsageMap[] = {
    {0x00B0, VK_MEDIA_PLAY},
    {0x00B1, VK_MEDIA_PAUSE},
    {0x00CD, VK_MEDIA_PLAY_PAUSE},
    {0x00B3, VK_MEDIA_NEXT},
    {0x00B4, VK_MEDIA_PREV},
    {0x00B7, VK_MEDIA_STOP},
    {0x00B5, VK_MEDIA_NEXT},
    {0x00B6, VK_MEDIA_PREV},
    {0x00E2, VK_MEDIA_MUTE},
    {0x00E9, VK_MEDIA_VOLUME_UP},
    {0x00EA, VK_MEDIA_VOLUME_DOWN},
    {0x021B, VK_COPY},
    {0x021C, VK_CUT},
    {0x021D, VK_PASTE},
    {0x006F, VK_MEDIA_BRIGHTNESS_UP},
    {0x0070, VK_MEDIA_BRIGHTNESS_DOWN},
    {0x0032, VK_MEDIA_SLEEP},
    {0x00B8, VK_MEDIA_EJECT}
};

/***************************************************************************/

/**
 * @brief Retrieve the USB keyboard driver descriptor.
 * @return Pointer to the USB keyboard driver.
 */
LPDRIVER USBKeyboardGetDriver(void) {
    return &USBKeyboardDriverState.Driver;
}

/***************************************************************************/

/**
 * @brief Check if an interface is a HID boot keyboard.
 * @param Interface Interface descriptor.
 * @return TRUE when the interface matches a HID keyboard.
 */
static BOOL USBKeyboardIsHidKeyboardInterface(LPXHCI_USB_INTERFACE Interface) {
    if (Interface == NULL) {
        return FALSE;
    }

    return (Interface->InterfaceClass == USB_CLASS_HID &&
            Interface->InterfaceSubClass == USB_HID_SUBCLASS_BOOT &&
            Interface->InterfaceProtocol == USB_HID_PROTOCOL_KEYBOARD);
}

/***************************************************************************/

/**
 * @brief Find interrupt IN endpoint in an interface.
 * @param Interface Interface descriptor.
 * @return Endpoint pointer or NULL.
 */
static LPXHCI_USB_ENDPOINT USBKeyboardFindInterruptInEndpoint(LPXHCI_USB_INTERFACE Interface) {
    if (Interface == NULL) {
        return NULL;
    }

    return XHCI_FindInterfaceEndpoint(Interface, USB_ENDPOINT_TYPE_INTERRUPT, TRUE);
}

/***************************************************************************/

/**
 * @brief Set HID boot protocol on a keyboard interface.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param InterfaceNumber Interface number.
 * @return TRUE on success.
 */
static BOOL USBKeyboardSetBootProtocol(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U8 InterfaceNumber) {
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
 * @brief Set HID idle rate on a keyboard interface.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param InterfaceNumber Interface number.
 * @return TRUE on success.
 */
static BOOL USBKeyboardSetIdle(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U8 InterfaceNumber) {
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
 * @brief Retrieve a HID descriptor for an interface.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param InterfaceNumber Interface number.
 * @param Buffer Output buffer.
 * @param BufferLength Output buffer length.
 * @return TRUE on success.
 */
static BOOL USBKeyboardGetHidDescriptor(LPXHCI_DEVICE Device,
                                        LPXHCI_USB_DEVICE UsbDevice,
                                        U8 InterfaceNumber,
                                        U8* Buffer,
                                        U16 BufferLength) {
    USB_SETUP_PACKET Setup;

    if (Device == NULL || UsbDevice == NULL || Buffer == NULL || BufferLength < USB_KEYBOARD_HID_DESCRIPTOR_LENGTH) {
        return FALSE;
    }

    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_IN | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_INTERFACE;
    Setup.Request = USB_REQUEST_GET_DESCRIPTOR;
    Setup.Value = (USB_KEYBOARD_HID_DESCRIPTOR_TYPE << 8);
    Setup.Index = InterfaceNumber;
    Setup.Length = BufferLength;

    MemorySet(Buffer, 0, BufferLength);
    return XHCI_ControlTransfer(Device, UsbDevice, &Setup, 0, Buffer, BufferLength, FALSE);
}

/***************************************************************************/

/**
 * @brief Retrieve a HID report descriptor for an interface.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param InterfaceNumber Interface number.
 * @param Buffer Output buffer.
 * @param BufferLength Output buffer length.
 * @return TRUE on success.
 */
static BOOL USBKeyboardGetReportDescriptor(LPXHCI_DEVICE Device,
                                           LPXHCI_USB_DEVICE UsbDevice,
                                           U8 InterfaceNumber,
                                           U8* Buffer,
                                           U16 BufferLength) {
    USB_SETUP_PACKET Setup;

    if (Device == NULL || UsbDevice == NULL || Buffer == NULL || BufferLength == 0) {
        return FALSE;
    }

    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_IN | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_INTERFACE;
    Setup.Request = USB_REQUEST_GET_DESCRIPTOR;
    Setup.Value = (USB_KEYBOARD_HID_REPORT_DESCRIPTOR_TYPE << 8);
    Setup.Index = InterfaceNumber;
    Setup.Length = BufferLength;

    MemorySet(Buffer, 0, BufferLength);
    return XHCI_ControlTransfer(Device, UsbDevice, &Setup, 0, Buffer, BufferLength, FALSE);
}

/***************************************************************************/

/**
 * @brief Check if an interface can carry consumer control reports.
 * @param Interface Interface descriptor.
 * @return TRUE when the interface matches HID consumer candidates.
 */
static BOOL USBKeyboardIsHidConsumerInterface(LPXHCI_USB_INTERFACE Interface) {
    if (Interface == NULL) {
        return FALSE;
    }

    if (Interface->InterfaceClass != USB_CLASS_HID) {
        return FALSE;
    }

    if (USBKeyboardIsHidKeyboardInterface(Interface)) {
        return FALSE;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Find an optional HID consumer control interface for a USB device.
 * @param UsbDevice USB device state.
 * @param Config Selected configuration.
 * @param InterfaceOut Receives consumer interface when found.
 * @param EndpointOut Receives interrupt IN endpoint when found.
 * @return TRUE when a consumer interface is found.
 */
static BOOL USBKeyboardFindConsumerInterface(LPXHCI_USB_DEVICE UsbDevice,
                                             LPXHCI_USB_CONFIGURATION Config,
                                             LPXHCI_USB_INTERFACE* InterfaceOut,
                                             LPXHCI_USB_ENDPOINT* EndpointOut) {
    LPLIST InterfaceList = NULL;

    if (UsbDevice == NULL || Config == NULL || InterfaceOut == NULL || EndpointOut == NULL) {
        return FALSE;
    }

    *InterfaceOut = NULL;
    *EndpointOut = NULL;

    InterfaceList = GetUsbInterfaceList();
    if (InterfaceList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = InterfaceList->First; Node != NULL; Node = Node->Next) {
        LPXHCI_USB_INTERFACE Interface = (LPXHCI_USB_INTERFACE)Node;
        LPXHCI_USB_ENDPOINT Endpoint = NULL;

        if (Interface->Parent != (LPLISTNODE)UsbDevice) {
            continue;
        }
        if (Interface->ConfigurationValue != Config->ConfigurationValue) {
            continue;
        }
        if (!USBKeyboardIsHidConsumerInterface(Interface)) {
            continue;
        }

        Endpoint = USBKeyboardFindInterruptInEndpoint(Interface);
        if (Endpoint == NULL) {
            continue;
        }

        *InterfaceOut = Interface;
        *EndpointOut = Endpoint;
        return TRUE;
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Release resources for the active USB keyboard.
 */
static void USBKeyboardClearState(void) {
    UINT Index = 0;

    if (USBKeyboardDriverState.State.ReferencesHeld) {
        if (USBKeyboardDriverState.State.ConsumerEndpoint != NULL) {
            XHCI_ReleaseUsbEndpoint(USBKeyboardDriverState.State.ConsumerEndpoint);
        }
        if (USBKeyboardDriverState.State.ConsumerInterface != NULL) {
            XHCI_ReleaseUsbInterface(USBKeyboardDriverState.State.ConsumerInterface);
        }
        XHCI_ReleaseUsbEndpoint(USBKeyboardDriverState.State.Endpoint);
        XHCI_ReleaseUsbInterface(USBKeyboardDriverState.State.Interface);
        XHCI_ReleaseUsbDevice(USBKeyboardDriverState.State.UsbDevice);
        USBKeyboardDriverState.State.ReferencesHeld = FALSE;
    }

    if (USBKeyboardDriverState.State.ReportLinear != 0) {
        FreeRegion(USBKeyboardDriverState.State.ReportLinear, PAGE_SIZE);
        USBKeyboardDriverState.State.ReportLinear = 0;
    }
    if (USBKeyboardDriverState.State.ReportPhysical != 0) {
        FreePhysicalPage(USBKeyboardDriverState.State.ReportPhysical);
        USBKeyboardDriverState.State.ReportPhysical = 0;
    }
    if (USBKeyboardDriverState.State.ConsumerReportLinear != 0) {
        FreeRegion(USBKeyboardDriverState.State.ConsumerReportLinear, PAGE_SIZE);
        USBKeyboardDriverState.State.ConsumerReportLinear = 0;
    }
    if (USBKeyboardDriverState.State.ConsumerReportPhysical != 0) {
        FreePhysicalPage(USBKeyboardDriverState.State.ConsumerReportPhysical);
        USBKeyboardDriverState.State.ConsumerReportPhysical = 0;
    }

    USBKeyboardDriverState.State.Controller = NULL;
    USBKeyboardDriverState.State.UsbDevice = NULL;
    USBKeyboardDriverState.State.Interface = NULL;
    USBKeyboardDriverState.State.Endpoint = NULL;
    USBKeyboardDriverState.State.InterfaceNumber = 0;
    USBKeyboardDriverState.State.ReportLength = 0;
    USBKeyboardDriverState.State.ReportTrbPhysical = U64_FromUINT(0);
    USBKeyboardDriverState.State.ReportPending = FALSE;
    USBKeyboardDriverState.State.RetryDelay = 0;
    USBKeyboardDriverState.State.PrevModifiers = 0;
    MemorySet(USBKeyboardDriverState.State.PrevKeys, 0, sizeof(USBKeyboardDriverState.State.PrevKeys));
    USBKeyboardDriverState.State.ConsumerInterface = NULL;
    USBKeyboardDriverState.State.ConsumerEndpoint = NULL;
    USBKeyboardDriverState.State.ConsumerReportLength = 0;
    USBKeyboardDriverState.State.ConsumerReportTrbPhysical = U64_FromUINT(0);
    USBKeyboardDriverState.State.ConsumerReportPending = FALSE;
    USBKeyboardDriverState.State.ConsumerReportDescriptorLength = 0;
    MemorySet(USBKeyboardDriverState.State.ConsumerReportDescriptor, 0, sizeof(USBKeyboardDriverState.State.ConsumerReportDescriptor));
    MemorySet(USBKeyboardDriverState.State.ConsumerFields, 0, sizeof(USBKeyboardDriverState.State.ConsumerFields));
    USBKeyboardDriverState.State.ConsumerLayout.Fields = NULL;
    USBKeyboardDriverState.State.ConsumerLayout.FieldCount = 0;
    USBKeyboardDriverState.State.ConsumerLayout.FieldCapacity = 0;
    for (Index = 0; Index < (UINT)sizeof(USBKeyboardDriverState.State.ConsumerPressed); Index++) {
        if (USBKeyboardDriverState.State.ConsumerPressed[Index]) {
            HandleKeyboardVirtualKey(USBKeyboardMediaUsageMap[Index].VirtualKey, FALSE);
        }
    }
    MemorySet(USBKeyboardDriverState.State.ConsumerPressed, 0, sizeof(USBKeyboardDriverState.State.ConsumerPressed));
    Keyboard.SoftwareRepeat = FALSE;
}

/***************************************************************************/

/**
 * @brief Ensure the active device is still present.
 * @param Device xHCI device.
 * @param UsbDevice USB device to check.
 * @return TRUE when still present.
 */
static BOOL USBKeyboardIsDevicePresent(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
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
 * @brief Locate a HID keyboard device on any xHCI controller.
 * @param DeviceOut Receives controller.
 * @param UsbDeviceOut Receives USB device.
 * @param InterfaceOut Receives interface.
 * @param EndpointOut Receives endpoint.
 * @return TRUE if a keyboard device was found.
 */
static BOOL USBKeyboardFindDevice(LPXHCI_DEVICE* DeviceOut,
                                  LPXHCI_USB_DEVICE* UsbDeviceOut,
                                  LPXHCI_USB_INTERFACE* InterfaceOut,
                                  LPXHCI_USB_ENDPOINT* EndpointOut,
                                  LPXHCI_USB_INTERFACE* ConsumerInterfaceOut,
                                  LPXHCI_USB_ENDPOINT* ConsumerEndpointOut) {
    if (DeviceOut == NULL || UsbDeviceOut == NULL || InterfaceOut == NULL || EndpointOut == NULL ||
        ConsumerInterfaceOut == NULL || ConsumerEndpointOut == NULL) {
        return FALSE;
    }

    *ConsumerInterfaceOut = NULL;
    *ConsumerEndpointOut = NULL;

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
                    if (!USBKeyboardIsHidKeyboardInterface(Interface)) {
                        continue;
                    }

                    LPXHCI_USB_ENDPOINT Endpoint = USBKeyboardFindInterruptInEndpoint(Interface);
                    if (Endpoint == NULL) {
                        continue;
                    }

                    *DeviceOut = Device;
                    *UsbDeviceOut = UsbDevice;
                    *InterfaceOut = Interface;
                    *EndpointOut = Endpoint;
                    (void)USBKeyboardFindConsumerInterface(UsbDevice, Config, ConsumerInterfaceOut, ConsumerEndpointOut);
                    return TRUE;
                }
            }
        }
    }

    return FALSE;
}

/***************************************************************************/

/**
 * @brief Submit an interrupt IN transfer for the keyboard report.
 * @param Device xHCI device.
 * @return TRUE on success.
 */
static BOOL USBKeyboardSubmitInterruptReport(LPXHCI_DEVICE Device,
                                             LPXHCI_USB_ENDPOINT Endpoint,
                                             U16 ReportLength,
                                             PHYSICAL ReportPhysical,
                                             U64* ReportTrbPhysical,
                                             BOOL* ReportPending) {
    if (Device == NULL || Endpoint == NULL || ReportPhysical == 0 || ReportTrbPhysical == NULL || ReportPending == NULL ||
        ReportLength == 0) {
        return FALSE;
    }

    XHCI_TRB Trb;
    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(ReportPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(ReportPhysical));
    Trb.Dword2 = ReportLength;
    Trb.Dword3 = (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC | XHCI_TRB_DIR_IN;

    if (!XHCI_RingEnqueue(Endpoint->TransferRingLinear,
                          Endpoint->TransferRingPhysical,
                          &Endpoint->TransferRingEnqueueIndex,
                          &Endpoint->TransferRingCycleState,
                          XHCI_TRANSFER_RING_TRBS,
                          &Trb,
                          ReportTrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, USBKeyboardDriverState.State.UsbDevice->SlotId, Endpoint->Dci);
    *ReportPending = TRUE;
    return TRUE;
}

/***************************************************************************/

static BOOL USBKeyboardReportHasUsage(const U8* Keys, U8 Usage) {
    if (Keys == NULL || Usage == 0) {
        return FALSE;
    }

    for (UINT Index = 0; Index < USB_KEYBOARD_BOOT_KEYS; Index++) {
        if (Keys[Index] == Usage) {
            return TRUE;
        }
    }

    return FALSE;
}

/***************************************************************************/

static void USBKeyboardHandleSpecialUsage(U8 Usage) {
    if (Usage != 0x42) {
        return;
    }

    if (Keyboard.UsageStatus[KEY_USAGE_LEFT_CTRL] || Keyboard.UsageStatus[KEY_USAGE_RIGHT_CTRL]) {
        (void)DisplaySwitchToConsole();
    } else {
        TASKINFO TaskInfo;
        TaskInfo.Header.Size = sizeof(TASKINFO);
        TaskInfo.Header.Version = EXOS_ABI_VERSION;
        TaskInfo.Header.Flags = 0;
        TaskInfo.Func = Shell;
        TaskInfo.Parameter = NULL;
        TaskInfo.StackSize = TASK_MINIMUM_TASK_STACK_SIZE;
        TaskInfo.Priority = TASK_PRIORITY_MEDIUM;
        TaskInfo.Flags = 0;
        CreateTask(&KernelProcess, &TaskInfo);
    }
}

/***************************************************************************/

static UINT USBKeyboardFindMediaUsageIndex(U16 Usage) {
    for (UINT Index = 0; Index < (UINT)(sizeof(USBKeyboardMediaUsageMap) / sizeof(USBKeyboardMediaUsageMap[0])); Index++) {
        if (USBKeyboardMediaUsageMap[Index].Usage == Usage) {
            return Index;
        }
    }

    return (UINT)-1;
}

/***************************************************************************/

static void USBKeyboardLogUnknownConsumerUsage(U16 Usage) {
    U32 Suppressed = 0;

    if (!RateLimiterShouldTrigger(&USBKeyboardDriverState.State.ConsumerUnknownUsageLogLimiter, GetSystemTime(), &Suppressed)) {
        return;
    }

    WARNING(TEXT("[USBKeyboardLogUnknownConsumerUsage] Unmapped consumer usage=%x suppressed=%u"),
            (U32)Usage,
            Suppressed);
}

/***************************************************************************/

static void USBKeyboardLogUnknownConsumerUsages(const U8* Report, U16 ReportLength) {
    const HID_REPORT_LAYOUT* Layout = &USBKeyboardDriverState.State.ConsumerLayout;
    U16 UnknownUsages[8];
    UINT UnknownCount = 0;

    if (Report == NULL || Layout == NULL || Layout->Fields == NULL) {
        return;
    }

    MemorySet(UnknownUsages, 0, sizeof(UnknownUsages));

    for (UINT FieldIndex = 0; FieldIndex < Layout->FieldCount; FieldIndex++) {
        const HID_REPORT_FIELD* Field = &Layout->Fields[FieldIndex];
        if (Field->UsagePage != USB_KEYBOARD_USAGE_PAGE_CONSUMER) {
            continue;
        }

        if (!Field->IsArray) {
            U32 Value = 0;
            if (!HidReportReadUnsignedValue(Report, ReportLength, Field->ReportId, Field->BitOffset, Field->BitSize, &Value)) {
                continue;
            }
            if (Value == 0 || Field->Usage == 0 || USBKeyboardFindMediaUsageIndex(Field->Usage) != (UINT)-1) {
                continue;
            }

            BOOL Exists = FALSE;
            for (UINT I = 0; I < UnknownCount; I++) {
                if (UnknownUsages[I] == Field->Usage) {
                    Exists = TRUE;
                    break;
                }
            }
            if (!Exists && UnknownCount < (UINT)(sizeof(UnknownUsages) / sizeof(UnknownUsages[0]))) {
                UnknownUsages[UnknownCount++] = Field->Usage;
            }
            continue;
        }

        for (UINT ElementIndex = 0; ElementIndex < Field->ReportCount; ElementIndex++) {
            U16 ElementOffset = (U16)(Field->BitOffset + (ElementIndex * Field->BitSize));
            U32 Value = 0;
            U16 Usage = 0;
            BOOL Exists = FALSE;
            if (!HidReportReadUnsignedValue(Report, ReportLength, Field->ReportId, ElementOffset, Field->BitSize, &Value)) {
                continue;
            }
            if (Value == 0 || Value > 0xFFFF || USBKeyboardFindMediaUsageIndex((U16)Value) != (UINT)-1) {
                continue;
            }

            Usage = (U16)Value;
            for (UINT I = 0; I < UnknownCount; I++) {
                if (UnknownUsages[I] == Usage) {
                    Exists = TRUE;
                    break;
                }
            }
            if (!Exists && UnknownCount < (UINT)(sizeof(UnknownUsages) / sizeof(UnknownUsages[0]))) {
                UnknownUsages[UnknownCount++] = Usage;
            }
        }
    }

    for (UINT Index = 0; Index < UnknownCount; Index++) {
        USBKeyboardLogUnknownConsumerUsage(UnknownUsages[Index]);
    }
}

/***************************************************************************/

static void USBKeyboardHandleModifiers(U8 NewModifiers) {
    static const struct {
        U8 Mask;
        KEY_USAGE Usage;
    } ModifierMap[] = {
        {BIT_0, KEY_USAGE_LEFT_CTRL},
        {BIT_1, KEY_USAGE_LEFT_SHIFT},
        {BIT_2, KEY_USAGE_LEFT_ALT},
        {BIT_3, KEY_USAGE_LEFT_GUI},
        {BIT_4, KEY_USAGE_RIGHT_CTRL},
        {BIT_5, KEY_USAGE_RIGHT_SHIFT},
        {BIT_6, KEY_USAGE_RIGHT_ALT},
        {BIT_7, KEY_USAGE_RIGHT_GUI}
    };

    U8 OldModifiers = USBKeyboardDriverState.State.PrevModifiers;

    if (NewModifiers == OldModifiers) {
        return;
    }

    for (UINT Index = 0; Index < sizeof(ModifierMap) / sizeof(ModifierMap[0]); Index++) {
        BOOL WasSet = (OldModifiers & ModifierMap[Index].Mask) != 0;
        BOOL IsSet = (NewModifiers & ModifierMap[Index].Mask) != 0;
        if (WasSet == IsSet) {
            continue;
        }

        HandleKeyboardUsage(ModifierMap[Index].Usage, IsSet);
    }

    USBKeyboardDriverState.State.PrevModifiers = NewModifiers;
}

/***************************************************************************/

/**
 * @brief Parse and dispatch a HID boot keyboard report.
 */
static void USBKeyboardHandleReport(void) {
    const U8* Report = (const U8*)USBKeyboardDriverState.State.ReportLinear;
    U8 NewKeys[USB_KEYBOARD_BOOT_KEYS];
    U8 NewModifiers = 0;

    if (Report == NULL) {
        return;
    }

    if (USBKeyboardDriverState.State.ReportLength < USB_KEYBOARD_BOOT_REPORT_SIZE) {
        return;
    }

    NewModifiers = Report[0];
    MemoryCopy(NewKeys, &Report[2], USB_KEYBOARD_BOOT_KEYS);

    USBKeyboardHandleModifiers(NewModifiers);

    for (UINT Index = 0; Index < USB_KEYBOARD_BOOT_KEYS; Index++) {
        U8 Usage = USBKeyboardDriverState.State.PrevKeys[Index];
        if (Usage == 0) {
            continue;
        }
        if (USBKeyboardReportHasUsage(NewKeys, Usage)) {
            continue;
        }
        if (Usage < KEY_USAGE_MIN || Usage > KEY_USAGE_MAX) {
            continue;
        }

        HandleKeyboardUsage(Usage, FALSE);
    }

    for (UINT Index = 0; Index < USB_KEYBOARD_BOOT_KEYS; Index++) {
        U8 Usage = NewKeys[Index];
        if (Usage == 0) {
            continue;
        }
        if (Usage < KEY_USAGE_MIN || Usage > KEY_USAGE_MAX) {
            continue;
        }
        if (USBKeyboardReportHasUsage(USBKeyboardDriverState.State.PrevKeys, Usage)) {
            continue;
        }

        HandleKeyboardUsage(Usage, TRUE);
        USBKeyboardHandleSpecialUsage(Usage);
    }

    MemoryCopy(USBKeyboardDriverState.State.PrevKeys, NewKeys, USB_KEYBOARD_BOOT_KEYS);
}

/***************************************************************************/

/**
 * @brief Parse and dispatch a HID consumer control report.
 */
static void USBKeyboardHandleConsumerReport(void) {
    UINT Index = 0;
    const U8* Report = (const U8*)USBKeyboardDriverState.State.ConsumerReportLinear;

    if (Report == NULL || USBKeyboardDriverState.State.ConsumerLayout.Fields == NULL) {
        return;
    }

    USBKeyboardLogUnknownConsumerUsages(Report, USBKeyboardDriverState.State.ConsumerReportLength);

    for (Index = 0; Index < (UINT)(sizeof(USBKeyboardMediaUsageMap) / sizeof(USBKeyboardMediaUsageMap[0])); Index++) {
        BOOL IsPressed = HidReportIsUsageActive(&USBKeyboardDriverState.State.ConsumerLayout,
                                                Report,
                                                USBKeyboardDriverState.State.ConsumerReportLength,
                                                USB_KEYBOARD_USAGE_PAGE_CONSUMER,
                                                USBKeyboardMediaUsageMap[Index].Usage);
        BOOL WasPressed = USBKeyboardDriverState.State.ConsumerPressed[Index] != 0;
        if (IsPressed == WasPressed) {
            continue;
        }

        USBKeyboardDriverState.State.ConsumerPressed[Index] = IsPressed ? 1 : 0;
        HandleKeyboardVirtualKey(USBKeyboardMediaUsageMap[Index].VirtualKey, IsPressed);
    }
}

/***************************************************************************/

/**
 * @brief Process pending boot keyboard reports.
 */
static void USBKeyboardProcessBootReports(void) {
    U32 Completion = 0;

    if (USBKeyboardDriverState.State.Controller == NULL) {
        return;
    }

    if (!USBKeyboardDriverState.State.ReportPending) {
        (void)USBKeyboardSubmitInterruptReport(USBKeyboardDriverState.State.Controller,
                                               USBKeyboardDriverState.State.Endpoint,
                                               USBKeyboardDriverState.State.ReportLength,
                                               USBKeyboardDriverState.State.ReportPhysical,
                                               &USBKeyboardDriverState.State.ReportTrbPhysical,
                                               &USBKeyboardDriverState.State.ReportPending);
        return;
    }

    if (!XHCI_CheckTransferCompletion(USBKeyboardDriverState.State.Controller,
                                       USBKeyboardDriverState.State.ReportTrbPhysical,
                                       &Completion)) {
        return;
    }

    USBKeyboardDriverState.State.ReportPending = FALSE;
    if (Completion == XHCI_COMPLETION_SUCCESS || Completion == XHCI_COMPLETION_SHORT_PACKET) {
        USBKeyboardHandleReport();
    } else {
        WARNING(TEXT("[USBKeyboardProcessBootReports] Completion %x"), Completion);
    }

    (void)USBKeyboardSubmitInterruptReport(USBKeyboardDriverState.State.Controller,
                                           USBKeyboardDriverState.State.Endpoint,
                                           USBKeyboardDriverState.State.ReportLength,
                                           USBKeyboardDriverState.State.ReportPhysical,
                                           &USBKeyboardDriverState.State.ReportTrbPhysical,
                                           &USBKeyboardDriverState.State.ReportPending);
}

/***************************************************************************/

/**
 * @brief Process pending consumer control reports.
 */
static void USBKeyboardProcessConsumerReports(void) {
    U32 Completion = 0;

    if (USBKeyboardDriverState.State.Controller == NULL || USBKeyboardDriverState.State.ConsumerEndpoint == NULL ||
        USBKeyboardDriverState.State.ConsumerReportLength == 0 || USBKeyboardDriverState.State.ConsumerReportPhysical == 0) {
        return;
    }

    if (!USBKeyboardDriverState.State.ConsumerReportPending) {
        (void)USBKeyboardSubmitInterruptReport(USBKeyboardDriverState.State.Controller,
                                               USBKeyboardDriverState.State.ConsumerEndpoint,
                                               USBKeyboardDriverState.State.ConsumerReportLength,
                                               USBKeyboardDriverState.State.ConsumerReportPhysical,
                                               &USBKeyboardDriverState.State.ConsumerReportTrbPhysical,
                                               &USBKeyboardDriverState.State.ConsumerReportPending);
        return;
    }

    if (!XHCI_CheckTransferCompletion(USBKeyboardDriverState.State.Controller,
                                      USBKeyboardDriverState.State.ConsumerReportTrbPhysical,
                                      &Completion)) {
        return;
    }

    USBKeyboardDriverState.State.ConsumerReportPending = FALSE;
    if (Completion == XHCI_COMPLETION_SUCCESS || Completion == XHCI_COMPLETION_SHORT_PACKET) {
        USBKeyboardHandleConsumerReport();
    } else {
        WARNING(TEXT("[USBKeyboardProcessConsumerReports] Completion %x"), Completion);
    }

    (void)USBKeyboardSubmitInterruptReport(USBKeyboardDriverState.State.Controller,
                                           USBKeyboardDriverState.State.ConsumerEndpoint,
                                           USBKeyboardDriverState.State.ConsumerReportLength,
                                           USBKeyboardDriverState.State.ConsumerReportPhysical,
                                           &USBKeyboardDriverState.State.ConsumerReportTrbPhysical,
                                           &USBKeyboardDriverState.State.ConsumerReportPending);
}

/***************************************************************************/

/**
 * @brief Process pending keyboard reports.
 */
static void USBKeyboardProcessReports(void) {
    USBKeyboardProcessBootReports();
    USBKeyboardProcessConsumerReports();
}

/***************************************************************************/

/**
 * @brief Initialize optional consumer control support for a HID interface.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Interface HID consumer interface.
 * @param Endpoint Interrupt IN endpoint.
 * @return TRUE on success.
 */
static BOOL USBKeyboardInitializeConsumerControl(LPXHCI_DEVICE Device,
                                                 LPXHCI_USB_DEVICE UsbDevice,
                                                 LPXHCI_USB_INTERFACE Interface,
                                                 LPXHCI_USB_ENDPOINT Endpoint) {
    U8 HidDescriptor[USB_KEYBOARD_HID_DESCRIPTOR_LENGTH];
    U16 DescriptorLength = 0;

    if (Device == NULL || UsbDevice == NULL || Interface == NULL || Endpoint == NULL) {
        return FALSE;
    }

    if (!USBKeyboardGetHidDescriptor(Device,
                                     UsbDevice,
                                     Interface->Number,
                                     HidDescriptor,
                                     sizeof(HidDescriptor))) {
        WARNING(TEXT("[USBKeyboardInitializeConsumerControl] HID descriptor fetch failed"));
        return FALSE;
    }

    DescriptorLength = (U16)(HidDescriptor[7] | (HidDescriptor[8] << 8));
    if (DescriptorLength == 0 || DescriptorLength > sizeof(USBKeyboardDriverState.State.ConsumerReportDescriptor)) {
        WARNING(TEXT("[USBKeyboardInitializeConsumerControl] Invalid report descriptor length %u"), DescriptorLength);
        return FALSE;
    }

    if (!USBKeyboardGetReportDescriptor(Device,
                                        UsbDevice,
                                        Interface->Number,
                                        USBKeyboardDriverState.State.ConsumerReportDescriptor,
                                        DescriptorLength)) {
        WARNING(TEXT("[USBKeyboardInitializeConsumerControl] Report descriptor fetch failed"));
        return FALSE;
    }

    USBKeyboardDriverState.State.ConsumerLayout.Fields = USBKeyboardDriverState.State.ConsumerFields;
    USBKeyboardDriverState.State.ConsumerLayout.FieldCapacity =
        sizeof(USBKeyboardDriverState.State.ConsumerFields) / sizeof(USBKeyboardDriverState.State.ConsumerFields[0]);
    USBKeyboardDriverState.State.ConsumerLayout.FieldCount = 0;

    if (!HidReportParseInputLayout(USBKeyboardDriverState.State.ConsumerReportDescriptor,
                                   DescriptorLength,
                                   &USBKeyboardDriverState.State.ConsumerLayout)) {
        WARNING(TEXT("[USBKeyboardInitializeConsumerControl] Report descriptor parse failed"));
        return FALSE;
    }

    if (!HidReportHasUsagePage(&USBKeyboardDriverState.State.ConsumerLayout, USB_KEYBOARD_USAGE_PAGE_CONSUMER)) {
        WARNING(TEXT("[USBKeyboardInitializeConsumerControl] Consumer usage page missing"));
        return FALSE;
    }

    if (!XHCI_AddInterruptEndpoint(Device, UsbDevice, Endpoint)) {
        WARNING(TEXT("[USBKeyboardInitializeConsumerControl] Interrupt endpoint setup failed"));
        return FALSE;
    }

    USBKeyboardDriverState.State.ConsumerInterface = Interface;
    USBKeyboardDriverState.State.ConsumerEndpoint = Endpoint;
    USBKeyboardDriverState.State.ConsumerReportDescriptorLength = DescriptorLength;
    USBKeyboardDriverState.State.ConsumerReportLength = Endpoint->MaxPacketSize;
    if (USBKeyboardDriverState.State.ConsumerReportLength > PAGE_SIZE) {
        USBKeyboardDriverState.State.ConsumerReportLength = PAGE_SIZE;
    }

    if (!XHCI_AllocPage(TEXT("USBKeyboardConsumerReport"),
                        &USBKeyboardDriverState.State.ConsumerReportPhysical,
                        &USBKeyboardDriverState.State.ConsumerReportLinear)) {
        WARNING(TEXT("[USBKeyboardInitializeConsumerControl] Report buffer alloc failed"));
        USBKeyboardDriverState.State.ConsumerInterface = NULL;
        USBKeyboardDriverState.State.ConsumerEndpoint = NULL;
        USBKeyboardDriverState.State.ConsumerReportLength = 0;
        return FALSE;
    }

    USBKeyboardDriverState.State.ConsumerReportTrbPhysical = U64_FromUINT(0);
    USBKeyboardDriverState.State.ConsumerReportPending = FALSE;
    MemorySet(USBKeyboardDriverState.State.ConsumerPressed, 0, sizeof(USBKeyboardDriverState.State.ConsumerPressed));
    (void)RateLimiterInit(&USBKeyboardDriverState.State.ConsumerUnknownUsageLogLimiter, 8, 1000);

    XHCI_ReferenceUsbInterface(Interface);
    XHCI_ReferenceUsbEndpoint(Endpoint);

    DEBUG(TEXT("[USBKeyboardInitializeConsumerControl] Consumer if=%u ep=%x fields=%u"),
          (U32)Interface->Number,
          (U32)Endpoint->Address,
          (U32)USBKeyboardDriverState.State.ConsumerLayout.FieldCount);

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Initialize the USB keyboard state for a detected device.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Interface HID interface.
 * @param Endpoint Interrupt IN endpoint.
 * @param ConsumerInterface Optional HID consumer interface.
 * @param ConsumerEndpoint Optional HID consumer endpoint.
 * @return TRUE on success.
 */
static BOOL USBKeyboardStartDevice(LPXHCI_DEVICE Device,
                                   LPXHCI_USB_DEVICE UsbDevice,
                                   LPXHCI_USB_INTERFACE Interface,
                                   LPXHCI_USB_ENDPOINT Endpoint,
                                   LPXHCI_USB_INTERFACE ConsumerInterface,
                                   LPXHCI_USB_ENDPOINT ConsumerEndpoint) {
    if (Device == NULL || UsbDevice == NULL || Interface == NULL || Endpoint == NULL) {
        return FALSE;
    }

    if (!USBKeyboardSetBootProtocol(Device, UsbDevice, Interface->Number)) {
        WARNING(TEXT("[USBKeyboardStartDevice] SET_PROTOCOL(boot) failed"));
    }
    if (!USBKeyboardSetIdle(Device, UsbDevice, Interface->Number)) {
        WARNING(TEXT("[USBKeyboardStartDevice] SET_IDLE failed"));
    }

    if (!XHCI_AddInterruptEndpoint(Device, UsbDevice, Endpoint)) {
        ERROR(TEXT("[USBKeyboardStartDevice] Interrupt endpoint setup failed"));
        return FALSE;
    }

    if (Endpoint->MaxPacketSize < USB_KEYBOARD_BOOT_REPORT_SIZE) {
        ERROR(TEXT("[USBKeyboardStartDevice] Invalid report size"));
        return FALSE;
    }

    U16 ReportLength = Endpoint->MaxPacketSize;
    if (ReportLength > PAGE_SIZE) {
        ReportLength = PAGE_SIZE;
    }

    if (!XHCI_AllocPage(TEXT("USBKeyboardReport"),
                        &USBKeyboardDriverState.State.ReportPhysical,
                        &USBKeyboardDriverState.State.ReportLinear)) {
        ERROR(TEXT("[USBKeyboardStartDevice] Report buffer alloc failed"));
        return FALSE;
    }

    USBKeyboardDriverState.State.Controller = Device;
    USBKeyboardDriverState.State.UsbDevice = UsbDevice;
    USBKeyboardDriverState.State.Interface = Interface;
    USBKeyboardDriverState.State.Endpoint = Endpoint;
    USBKeyboardDriverState.State.InterfaceNumber = Interface->Number;
    USBKeyboardDriverState.State.ReportLength = ReportLength;
    USBKeyboardDriverState.State.ReportTrbPhysical = U64_FromUINT(0);
    USBKeyboardDriverState.State.ReportPending = FALSE;
    USBKeyboardDriverState.State.PrevModifiers = 0;
    MemorySet(USBKeyboardDriverState.State.PrevKeys, 0, sizeof(USBKeyboardDriverState.State.PrevKeys));
    MemorySet(USBKeyboardDriverState.State.ConsumerPressed, 0, sizeof(USBKeyboardDriverState.State.ConsumerPressed));
    Keyboard.SoftwareRepeat = TRUE;

    XHCI_ReferenceUsbDevice(UsbDevice);
    XHCI_ReferenceUsbInterface(Interface);
    XHCI_ReferenceUsbEndpoint(Endpoint);

    if (ConsumerInterface != NULL && ConsumerEndpoint != NULL) {
        if (!USBKeyboardInitializeConsumerControl(Device, UsbDevice, ConsumerInterface, ConsumerEndpoint)) {
            WARNING(TEXT("[USBKeyboardStartDevice] Consumer control disabled"));
        }
    }

    USBKeyboardDriverState.State.ReferencesHeld = TRUE;

    DEBUG(TEXT("[USBKeyboardStartDevice] Keyboard addr=%x if=%u ep=%x"),
          UsbDevice->Address,
          (U32)Interface->Number,
          (U32)Endpoint->Address);

    (void)USBKeyboardSubmitInterruptReport(Device,
                                           USBKeyboardDriverState.State.Endpoint,
                                           USBKeyboardDriverState.State.ReportLength,
                                           USBKeyboardDriverState.State.ReportPhysical,
                                           &USBKeyboardDriverState.State.ReportTrbPhysical,
                                           &USBKeyboardDriverState.State.ReportPending);
    if (USBKeyboardDriverState.State.ConsumerEndpoint != NULL) {
        (void)USBKeyboardSubmitInterruptReport(Device,
                                               USBKeyboardDriverState.State.ConsumerEndpoint,
                                               USBKeyboardDriverState.State.ConsumerReportLength,
                                               USBKeyboardDriverState.State.ConsumerReportPhysical,
                                               &USBKeyboardDriverState.State.ConsumerReportTrbPhysical,
                                               &USBKeyboardDriverState.State.ConsumerReportPending);
    }
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Poll USB keyboard state and process reports.
 * @param Context Unused.
 */
static void USBKeyboardPoll(LPVOID Context) {
    UNUSED(Context);

    if (USBKeyboardDriverState.State.Initialized == FALSE) {
        return;
    }

    if (USBKeyboardDriverState.State.RetryDelay != 0) {
        USBKeyboardDriverState.State.RetryDelay--;
        return;
    }

    if (USBKeyboardDriverState.State.Controller != NULL && USBKeyboardDriverState.State.UsbDevice != NULL) {
        if (!USBKeyboardIsDevicePresent(USBKeyboardDriverState.State.Controller, USBKeyboardDriverState.State.UsbDevice)) {
            DEBUG(TEXT("[USBKeyboardPoll] Keyboard disconnected"));
            USBKeyboardClearState();
            USBKeyboardDriverState.State.RetryDelay = 50;
        }
    }

    if (USBKeyboardDriverState.State.Controller == NULL) {
        LPXHCI_DEVICE Device = NULL;
        LPXHCI_USB_DEVICE UsbDevice = NULL;
        LPXHCI_USB_INTERFACE Interface = NULL;
        LPXHCI_USB_ENDPOINT Endpoint = NULL;
        LPXHCI_USB_INTERFACE ConsumerInterface = NULL;
        LPXHCI_USB_ENDPOINT ConsumerEndpoint = NULL;

        if (USBKeyboardFindDevice(&Device,
                                  &UsbDevice,
                                  &Interface,
                                  &Endpoint,
                                  &ConsumerInterface,
                                  &ConsumerEndpoint)) {
            if (!USBKeyboardStartDevice(Device, UsbDevice, Interface, Endpoint, ConsumerInterface, ConsumerEndpoint)) {
                USBKeyboardClearState();
                USBKeyboardDriverState.State.RetryDelay = 50;
            }
        }
    }

    if (USBKeyboardDriverState.State.Controller == NULL) {
        return;
    }

    if (DeferredWorkIsPollingMode()) {
        USBKeyboardProcessReports();
    }
}

/***************************************************************************/

/**
 * @brief Process keyboard reports on xHCI interrupts.
 * @param Device xHCI device issuing the interrupt.
 */
void USBKeyboardOnXhciInterrupt(LPXHCI_DEVICE Device) {
    if (USBKeyboardDriverState.State.Initialized == FALSE) {
        return;
    }

    if (USBKeyboardDriverState.State.Controller == NULL || USBKeyboardDriverState.State.Controller != Device) {
        return;
    }

    USBKeyboardProcessReports();
}

/***************************************************************************/

/**
 * @brief Driver command dispatcher for USB keyboard.
 * @param Function Driver function code.
 * @param Parameter Function-specific parameter.
 * @return Driver status or data.
 */
UINT USBKeyboardCommands(UINT Function, UINT Parameter) {
    UNUSED(Parameter);

    switch (Function) {
        case DF_LOAD: {
            if ((USBKeyboardDriverState.Driver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            KeyboardCommonInitialize();

            if (USBKeyboardDriverState.State.PollHandle == DEFERRED_WORK_INVALID_HANDLE) {
                USBKeyboardDriverState.State.PollHandle =
                    DeferredWorkRegisterPollOnly(USBKeyboardPoll, NULL, TEXT("USBKeyboard"));
                if (USBKeyboardDriverState.State.PollHandle == DEFERRED_WORK_INVALID_HANDLE) {
                    return DF_RETURN_UNEXPECTED;
                }
            }

            USBKeyboardDriverState.State.Initialized = TRUE;
            USBKeyboardDriverState.Driver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;
        }
        case DF_UNLOAD:
            if ((USBKeyboardDriverState.Driver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            if (USBKeyboardDriverState.State.PollHandle != DEFERRED_WORK_INVALID_HANDLE) {
                DeferredWorkUnregister(USBKeyboardDriverState.State.PollHandle);
                USBKeyboardDriverState.State.PollHandle = DEFERRED_WORK_INVALID_HANDLE;
            }

            USBKeyboardClearState();
            USBKeyboardDriverState.Driver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;
        case DF_GET_VERSION:
            return MAKE_VERSION(USB_KEYBOARD_VER_MAJOR, USB_KEYBOARD_VER_MINOR);
        case DF_KEY_GETSTATE:
            return DF_RETURN_NOT_IMPLEMENTED;
        case DF_KEY_GETLED:
        case DF_KEY_SETLED:
        case DF_KEY_GETDELAY:
        case DF_KEY_SETDELAY:
        case DF_KEY_GETRATE:
        case DF_KEY_SETRATE:
        case DF_KEY_ISKEY:
        case DF_KEY_GETKEY:
            return DF_RETURN_NOT_IMPLEMENTED;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/***************************************************************************/
