
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
#include "Console.h"
#include "DeferredWork.h"
#include "Kernel.h"
#include "Log.h"

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
        .ReferencesHeld = FALSE
    }
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
 * @brief Release resources for the active USB keyboard.
 */
static void USBKeyboardClearState(void) {
    if (USBKeyboardDriverState.State.ReferencesHeld) {
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
static BOOL USBKeyboardSubmitReport(LPXHCI_DEVICE Device) {
    if (Device == NULL || USBKeyboardDriverState.State.Endpoint == NULL ||
        USBKeyboardDriverState.State.ReportLinear == 0 || USBKeyboardDriverState.State.ReportPhysical == 0) {
        return FALSE;
    }

    XHCI_TRB Trb;
    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(USBKeyboardDriverState.State.ReportPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(USBKeyboardDriverState.State.ReportPhysical));
    Trb.Dword2 = USBKeyboardDriverState.State.ReportLength;
    Trb.Dword3 = (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC | XHCI_TRB_DIR_IN;

    if (!XHCI_RingEnqueue(USBKeyboardDriverState.State.Endpoint->TransferRingLinear,
                          USBKeyboardDriverState.State.Endpoint->TransferRingPhysical,
                          &USBKeyboardDriverState.State.Endpoint->TransferRingEnqueueIndex,
                          &USBKeyboardDriverState.State.Endpoint->TransferRingCycleState,
                          XHCI_TRANSFER_RING_TRBS,
                          &Trb,
                          &USBKeyboardDriverState.State.ReportTrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, USBKeyboardDriverState.State.UsbDevice->SlotId, USBKeyboardDriverState.State.Endpoint->Dci);
    USBKeyboardDriverState.State.ReportPending = TRUE;
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
        GetGraphicsDriver()->Command(DF_UNLOAD, 0);
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

    if (Report == NULL || USBKeyboardDriverState.State.ReportLength < USB_KEYBOARD_BOOT_REPORT_SIZE) {
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
 * @brief Process pending boot keyboard reports.
 */
static void USBKeyboardProcessReports(void) {
    if (USBKeyboardDriverState.State.Controller == NULL) {
        return;
    }

    if (!USBKeyboardDriverState.State.ReportPending) {
        (void)USBKeyboardSubmitReport(USBKeyboardDriverState.State.Controller);
        return;
    }

    U32 Completion = 0;
    if (!XHCI_CheckTransferCompletion(USBKeyboardDriverState.State.Controller,
                                       USBKeyboardDriverState.State.ReportTrbPhysical,
                                       &Completion)) {
        return;
    }

    USBKeyboardDriverState.State.ReportPending = FALSE;
    if (Completion == XHCI_COMPLETION_SUCCESS || Completion == XHCI_COMPLETION_SHORT_PACKET) {
        USBKeyboardHandleReport();
    } else {
        WARNING(TEXT("[USBKeyboardProcessReports] Completion %x"), Completion);
    }

    (void)USBKeyboardSubmitReport(USBKeyboardDriverState.State.Controller);
}

/***************************************************************************/

/**
 * @brief Initialize the USB keyboard state for a detected device.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Interface HID interface.
 * @param Endpoint Interrupt IN endpoint.
 * @return TRUE on success.
 */
static BOOL USBKeyboardStartDevice(LPXHCI_DEVICE Device,
                                   LPXHCI_USB_DEVICE UsbDevice,
                                   LPXHCI_USB_INTERFACE Interface,
                                   LPXHCI_USB_ENDPOINT Endpoint) {
    if (Device == NULL || UsbDevice == NULL || Interface == NULL || Endpoint == NULL) {
        return FALSE;
    }

    if (!USBKeyboardSetBootProtocol(Device, UsbDevice, Interface->Number)) {
        WARNING(TEXT("[USBKeyboardStartDevice] SET_PROTOCOL failed"));
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
    Keyboard.SoftwareRepeat = TRUE;

    XHCI_ReferenceUsbDevice(UsbDevice);
    XHCI_ReferenceUsbInterface(Interface);
    XHCI_ReferenceUsbEndpoint(Endpoint);
    USBKeyboardDriverState.State.ReferencesHeld = TRUE;

    DEBUG(TEXT("[USBKeyboardStartDevice] Keyboard addr=%x if=%u ep=%x"),
          UsbDevice->Address,
          (U32)Interface->Number,
          (U32)Endpoint->Address);

    (void)USBKeyboardSubmitReport(Device);
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

        if (USBKeyboardFindDevice(&Device, &UsbDevice, &Interface, &Endpoint)) {
            if (!USBKeyboardStartDevice(Device, UsbDevice, Interface, Endpoint)) {
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
