
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


    USB Mass Storage (BOT, read-only)

\************************************************************************/

#include "drivers/USBMassStorage.h"

#include "DeferredWork.h"
#include "CoreString.h"
#include "Disk.h"
#include "Endianness.h"
#include "FileSystem.h"
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "Console.h"
#include "User.h"
#include "drivers/XHCI-Internal.h"
#include "process/TaskMessaging.h"
#include "utils/Helpers.h"

/************************************************************************/

#define USB_MASS_STORAGE_VER_MAJOR 1
#define USB_MASS_STORAGE_VER_MINOR 0

#define USB_MASS_STORAGE_SUBCLASS_SCSI 0x06
#define USB_MASS_STORAGE_PROTOCOL_BOT 0x50

#define USB_MASS_STORAGE_COMMAND_BLOCK_SIGNATURE 0x43425355
#define USB_MASS_STORAGE_COMMAND_STATUS_SIGNATURE 0x53425355
#define USB_MASS_STORAGE_COMMAND_BLOCK_LENGTH 31
#define USB_MASS_STORAGE_COMMAND_STATUS_LENGTH 13

#define USB_SCSI_INQUIRY 0x12
#define USB_SCSI_READ_CAPACITY_10 0x25
#define USB_SCSI_READ_10 0x28

#define USB_MASS_STORAGE_BULK_TIMEOUT_MILLISECONDS 1000
#define USB_MASS_STORAGE_BULK_RETRIES 3

#define DF_RETURN_HARDWARE 0x00001001
#define DF_RETURN_TIMEOUT 0x00001002
#define DF_RETURN_NODEVICE 0x00001004

/************************************************************************/

#pragma pack(push, 1)

typedef struct tag_USB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER {
    U32 Signature;
    U32 Tag;
    U32 DataTransferLength;
    U8 Flags;
    U8 LogicalUnitNumber;
    U8 CommandBlockLength;
    U8 CommandBlock[16];
} USB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER, *LPUSB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER;

typedef struct tag_USB_MASS_STORAGE_COMMAND_STATUS_WRAPPER {
    U32 Signature;
    U32 Tag;
    U32 DataResidue;
    U8 Status;
} USB_MASS_STORAGE_COMMAND_STATUS_WRAPPER, *LPUSB_MASS_STORAGE_COMMAND_STATUS_WRAPPER;

#pragma pack(pop)

/************************************************************************/

typedef struct tag_USB_MASS_STORAGE_DEVICE {
    STORAGE_UNIT Disk;
    U32 Access;
    LPXHCI_DEVICE Controller;
    LPXHCI_USB_DEVICE UsbDevice;
    LPXHCI_USB_INTERFACE Interface;
    LPXHCI_USB_ENDPOINT BulkInEndpoint;
    LPXHCI_USB_ENDPOINT BulkOutEndpoint;
    U8 InterfaceNumber;
    U32 Tag;
    UINT BlockCount;
    UINT BlockSize;
    PHYSICAL InputOutputBufferPhysical;
    LINEAR InputOutputBufferLinear;
    BOOL Ready;
    BOOL ReferencesHeld;
    LPUSB_STORAGE_ENTRY ListEntry;
} USB_MASS_STORAGE_DEVICE, *LPUSB_MASS_STORAGE_DEVICE;

typedef struct tag_USB_MASS_STORAGE_STATE {
    BOOL Initialized;
    U32 PollHandle;
    UINT RetryDelay;
} USB_MASS_STORAGE_STATE, *LPUSB_MASS_STORAGE_STATE;

typedef struct tag_USB_MASS_STORAGE_DRIVER {
    DRIVER Driver;
    USB_MASS_STORAGE_STATE State;
} USB_MASS_STORAGE_DRIVER, *LPUSB_MASS_STORAGE_DRIVER;

/************************************************************************/

UINT USBMassStorageCommands(UINT Function, UINT Parameter);

static USB_MASS_STORAGE_DRIVER DATA_SECTION USBMassStorageDriverState = {
    .Driver = {
        .TypeID = KOID_DRIVER,
        .References = 1,
        .Next = NULL,
        .Prev = NULL,
        .Type = DRIVER_TYPE_USB_STORAGE,
        .VersionMajor = USB_MASS_STORAGE_VER_MAJOR,
        .VersionMinor = USB_MASS_STORAGE_VER_MINOR,
        .Designer = "Jango73",
        .Manufacturer = "USB-IF",
        .Product = "USB Mass Storage",
        .Flags = 0,
        .Command = USBMassStorageCommands
    },
    .State = {
        .Initialized = FALSE,
        .PollHandle = DEFERRED_WORK_INVALID_HANDLE,
        .RetryDelay = 0
    }
};

/************************************************************************/

static UINT USBMassStorageReportMounts(LPUSB_MASS_STORAGE_DEVICE Device, LPLISTNODE PreviousLast) {
    LPLIST FileSystemList = GetFileSystemList();
    UINT MountedCount = 0;
    LPLISTNODE Node = NULL;

    if (Device == NULL || FileSystemList == NULL) {
        return 0;
    }

    if (PreviousLast != NULL) {
        Node = PreviousLast->Next;
    } else {
        Node = FileSystemList->First;
    }

    for (; Node; Node = Node->Next) {
        LPFILESYSTEM FileSystem = (LPFILESYSTEM)Node;
        if (FileSystemGetStorageUnit(FileSystem) != (LPSTORAGE_UNIT)Device) {
            continue;
        }

        MountedCount++;
    }

    return MountedCount;
}

/************************************************************************/

/**
 * @brief Unmount and release filesystems associated with a USB disk.
 * @param Disk USB disk to detach.
 */
static void USBMassStorageDetachFileSystems(LPSTORAGE_UNIT Disk, U32 UsbAddress) {
    LPLIST FileSystemList = GetFileSystemList();
    LPLIST UnusedFileSystemList = GetUnusedFileSystemList();
    FILESYSTEM_GLOBAL_INFO* GlobalInfo = GetFileSystemGlobalInfo();
    UINT UnmountedCount = 0;
    UINT UnusedCount = 0;

    if (Disk == NULL || FileSystemList == NULL || UnusedFileSystemList == NULL || GlobalInfo == NULL) {
        return;
    }

    for (LPLISTNODE Node = FileSystemList->First; Node;) {
        LPLISTNODE Next = Node->Next;
        LPFILESYSTEM FileSystem = (LPFILESYSTEM)Node;
        LPSTORAGE_UNIT FileSystemDisk = FileSystemGetStorageUnit(FileSystem);

        if (FileSystemDisk == Disk) {
            SystemFSUnmountFileSystem(FileSystem);
            if (StringCompare(GlobalInfo->ActivePartitionName, FileSystem->Name) == 0) {
                StringClear(GlobalInfo->ActivePartitionName);
            }
            ReleaseKernelObject(FileSystem);
            UnmountedCount++;
        }

        Node = Next;
    }

    for (LPLISTNODE Node = UnusedFileSystemList->First; Node;) {
        LPLISTNODE Next = Node->Next;
        LPFILESYSTEM FileSystem = (LPFILESYSTEM)Node;
        LPSTORAGE_UNIT FileSystemDisk = FileSystemGetStorageUnit(FileSystem);

        if (FileSystemDisk == Disk) {
            ReleaseKernelObject(FileSystem);
            UnusedCount++;
        }

        Node = Next;
    }

    if (UnmountedCount > 0 || UnusedCount > 0) {
        BroadcastProcessMessage(ETM_USB_MASS_STORAGE_UNMOUNTED, UsbAddress, 0);
    }
}

/************************************************************************/

/**
 * @brief Detach a USB mass storage device and release its resources.
 * @param Device Device to detach.
 */
static void USBMassStorageDetachDevice(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    Device->Ready = FALSE;

    if (Device->ListEntry != NULL) {
        USBMassStorageDetachFileSystems((LPSTORAGE_UNIT)Device, (U32)Device->ListEntry->Address);
    } else {
        USBMassStorageDetachFileSystems((LPSTORAGE_UNIT)Device, 0);
    }

    if (Device->InputOutputBufferLinear != 0) {
        FreeRegion(Device->InputOutputBufferLinear, PAGE_SIZE);
        Device->InputOutputBufferLinear = 0;
    }
    if (Device->InputOutputBufferPhysical != 0) {
        FreePhysicalPage(Device->InputOutputBufferPhysical);
        Device->InputOutputBufferPhysical = 0;
    }

    if (Device->ListEntry != NULL) {
        Device->ListEntry->Present = FALSE;
        Device->ListEntry->Device = NULL;
        ReleaseKernelObject(Device->ListEntry);
        Device->ListEntry = NULL;
    }

    ReleaseKernelObject(Device);
}

/************************************************************************/

/**
 * @brief Retrieve the USB mass storage driver descriptor.
 * @return Pointer to the USB mass storage driver.
 */
LPDRIVER USBMassStorageGetDriver(void) {
    return &USBMassStorageDriverState.Driver;
}

/************************************************************************/

/**
 * @brief Convert a USB enumeration error code to a short text label.
 * @param Code Enumeration error code.
 * @return Constant label for the code.
 */
LPCSTR UsbEnumErrorToString(U8 Code) {
    switch (Code) {
        case XHCI_ENUM_ERROR_NONE:
            return TEXT("OK");
        case XHCI_ENUM_ERROR_BUSY:
            return TEXT("BUSY");
        case XHCI_ENUM_ERROR_RESET_TIMEOUT:
            return TEXT("RESET");
        case XHCI_ENUM_ERROR_INVALID_SPEED:
            return TEXT("SPEED");
        case XHCI_ENUM_ERROR_INIT_STATE:
            return TEXT("STATE");
        case XHCI_ENUM_ERROR_ENABLE_SLOT:
            return TEXT("SLOT");
        case XHCI_ENUM_ERROR_ADDRESS_DEVICE:
            return TEXT("ADDRESS");
        case XHCI_ENUM_ERROR_DEVICE_DESC:
            return TEXT("DEVICE");
        case XHCI_ENUM_ERROR_CONFIG_DESC:
            return TEXT("CONFIG");
        case XHCI_ENUM_ERROR_CONFIG_PARSE:
            return TEXT("PARSE");
        case XHCI_ENUM_ERROR_SET_CONFIG:
            return TEXT("SETCONFIG");
        case XHCI_ENUM_ERROR_HUB_INIT:
            return TEXT("HUB");
        default:
            return TEXT("UNKNOWN");
    }
}

/************************************************************************/

/**
 * @brief Check whether an interface matches USB mass storage BOT.
 * @param Interface USB interface descriptor.
 * @return TRUE when the interface matches BOT class/subclass/protocol.
 */
static BOOL USBMassStorageIsMassStorageInterface(LPXHCI_USB_INTERFACE Interface) {
    if (Interface == NULL) {
        return FALSE;
    }

    return (Interface->InterfaceClass == USB_CLASS_MASS_STORAGE &&
            Interface->InterfaceSubClass == USB_MASS_STORAGE_SUBCLASS_SCSI &&
            Interface->InterfaceProtocol == USB_MASS_STORAGE_PROTOCOL_BOT);
}

/************************************************************************/

/**
 * @brief Locate bulk IN/OUT endpoints for an interface.
 * @param Interface USB interface descriptor.
 * @param BulkInOut Receives bulk IN endpoint pointer.
 * @param BulkOutOut Receives bulk OUT endpoint pointer.
 * @return TRUE when both endpoints are found.
 */
static BOOL USBMassStorageFindBulkEndpoints(LPXHCI_USB_INTERFACE Interface,
                                            LPXHCI_USB_ENDPOINT* BulkInOut,
                                            LPXHCI_USB_ENDPOINT* BulkOutOut) {
    if (Interface == NULL || BulkInOut == NULL || BulkOutOut == NULL) {
        return FALSE;
    }

    *BulkInOut = XHCI_FindInterfaceEndpoint(Interface, USB_ENDPOINT_TYPE_BULK, TRUE);
    *BulkOutOut = XHCI_FindInterfaceEndpoint(Interface, USB_ENDPOINT_TYPE_BULK, FALSE);

    return (*BulkInOut != NULL && *BulkOutOut != NULL);
}

/************************************************************************/

/**
 * @brief Verify a USB device is still present on a controller.
 * @param Device xHCI controller.
 * @param UsbDevice USB device to validate.
 * @return TRUE when still present.
 */
static BOOL USBMassStorageIsDevicePresent(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    if (Device == NULL || UsbDevice == NULL) {
        return FALSE;
    }

    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
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
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Check whether a USB device is already tracked.
 * @param UsbDevice USB device to check.
 * @return TRUE when already registered.
 */
static BOOL USBMassStorageIsTracked(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
        return FALSE;
    }

    LPLIST UsbStorageList = GetUsbStorageList();
    if (UsbStorageList == NULL) {
        return FALSE;
    }

    for (LPLISTNODE Node = UsbStorageList->First; Node; Node = Node->Next) {
        LPUSB_STORAGE_ENTRY Entry = (LPUSB_STORAGE_ENTRY)Node;
        if (Entry == NULL || Entry->Device == NULL) {
            continue;
        }

        if (Entry->Device->UsbDevice == UsbDevice) {
            return TRUE;
        }
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Clear the HALT feature on a USB endpoint.
 * @param Device xHCI controller.
 * @param UsbDevice USB device state.
 * @param EndpointAddress Endpoint address to clear.
 * @return TRUE on success.
 */
static BOOL USBMassStorageClearEndpointHalt(LPXHCI_DEVICE Device,
                                            LPXHCI_USB_DEVICE UsbDevice,
                                            U8 EndpointAddress) {
    USB_SETUP_PACKET Setup;
    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_ENDPOINT;
    Setup.Request = USB_REQUEST_CLEAR_FEATURE;
    Setup.Value = USB_FEATURE_ENDPOINT_HALT;
    Setup.Index = EndpointAddress;
    Setup.Length = 0;

    return XHCI_ControlTransfer(Device, UsbDevice, &Setup, 0, NULL, 0, FALSE);
}

/************************************************************************/

/**
 * @brief Perform BOT reset recovery sequence for a device.
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
static BOOL USBMassStorageResetRecovery(LPUSB_MASS_STORAGE_DEVICE Device) {
    USB_SETUP_PACKET Setup;
    BOOL BulkInOk;
    BOOL BulkOutOk;

    if (Device == NULL || Device->Controller == NULL || Device->UsbDevice == NULL) {
        return FALSE;
    }

    MemorySet(&Setup, 0, sizeof(Setup));
    Setup.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_CLASS | USB_REQUEST_RECIPIENT_INTERFACE;
    Setup.Request = 0xFF;  // Bulk-Only Transport Reset
    Setup.Value = 0;
    Setup.Index = Device->InterfaceNumber;
    Setup.Length = 0;

    if (!XHCI_ControlTransfer(Device->Controller, Device->UsbDevice, &Setup, 0, NULL, 0, FALSE)) {
        WARNING(TEXT("[USBMassStorageResetRecovery] BOT reset failed for interface %u"),
            (UINT)Device->InterfaceNumber);
        return FALSE;
    }

    BulkInOk = USBMassStorageClearEndpointHalt(Device->Controller,
                                               Device->UsbDevice,
                                               Device->BulkInEndpoint->Address);
    BulkOutOk = USBMassStorageClearEndpointHalt(Device->Controller,
                                                Device->UsbDevice,
                                                Device->BulkOutEndpoint->Address);
    if (!BulkInOk || !BulkOutOk) {
        WARNING(TEXT("[USBMassStorageResetRecovery] Clear halt failed in=%x out=%x"),
            (U32)(BulkInOk != FALSE),
            (U32)(BulkOutOk != FALSE));
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Wait for a transfer completion with a timeout.
 * @param Device xHCI controller.
 * @param TrbPhysical TRB physical address.
 * @param TimeoutMilliseconds Timeout in milliseconds.
 * @param CompletionOut Receives completion code when provided.
 * @return TRUE on completion, FALSE on timeout.
 */
static BOOL USBMassStorageWaitCompletion(LPXHCI_DEVICE Device,
                                         U64 TrbPhysical,
                                         UINT TimeoutMilliseconds,
                                         U32* CompletionOut) {
    UINT Remaining = TimeoutMilliseconds;

    while (Remaining > 0) {
        if (XHCI_CheckTransferCompletion(Device, TrbPhysical, CompletionOut)) {
            return TRUE;
        }

        SleepWithSchedulerFrozenSupport(1);
        Remaining--;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Submit a single bulk transfer and wait for completion.
 * @param Device xHCI controller.
 * @param UsbDevice USB device.
 * @param Endpoint Bulk endpoint.
 * @param BufferPhysical Physical address of data buffer.
 * @param BufferLinear Linear address of data buffer.
 * @param Length Transfer length in bytes.
 * @param DirectionIn TRUE for IN transfer.
 * @param TimeoutMilliseconds Timeout in milliseconds.
 * @param CompletionOut Receives completion code.
 * @return TRUE on completion, FALSE otherwise.
 */
static BOOL USBMassStorageBulkTransferOnce(LPXHCI_DEVICE Device,
                                           LPXHCI_USB_DEVICE UsbDevice,
                                           LPXHCI_USB_ENDPOINT Endpoint,
                                           PHYSICAL BufferPhysical,
                                           LINEAR BufferLinear,
                                           UINT Length,
                                           BOOL DirectionIn,
                                           UINT TimeoutMilliseconds,
                                           U32* CompletionOut) {
    if (Device == NULL || UsbDevice == NULL || Endpoint == NULL || BufferPhysical == 0 || BufferLinear == 0) {
        return FALSE;
    }

    if (Length > MAX_U32) {
        return FALSE;
    }

    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(BufferPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(BufferPhysical));
    Trb.Dword2 = (U32)Length;
    Trb.Dword3 = (XHCI_TRB_TYPE_NORMAL << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC;
    if (DirectionIn) {
        Trb.Dword3 |= XHCI_TRB_DIR_IN;
    }

    if (!XHCI_RingEnqueue(Endpoint->TransferRingLinear,
                          Endpoint->TransferRingPhysical,
                          &Endpoint->TransferRingEnqueueIndex,
                          &Endpoint->TransferRingCycleState,
                          XHCI_TRANSFER_RING_TRBS,
                          &Trb,
                          &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, UsbDevice->SlotId, Endpoint->Dci);

    if (!USBMassStorageWaitCompletion(Device, TrbPhysical, TimeoutMilliseconds, CompletionOut)) {
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Submit a bulk transfer with retry and stall recovery.
 * @param Device xHCI controller.
 * @param UsbDevice USB device.
 * @param Endpoint Bulk endpoint.
 * @param BufferPhysical Physical address of data buffer.
 * @param BufferLinear Linear address of data buffer.
 * @param Length Transfer length in bytes.
 * @param DirectionIn TRUE for IN transfer.
 * @return TRUE on success.
 */
static BOOL USBMassStorageBulkTransfer(LPXHCI_DEVICE Device,
                                       LPXHCI_USB_DEVICE UsbDevice,
                                       LPXHCI_USB_ENDPOINT Endpoint,
                                       PHYSICAL BufferPhysical,
                                       LINEAR BufferLinear,
                                       UINT Length,
                                       BOOL DirectionIn) {
    for (UINT Attempt = 0; Attempt < USB_MASS_STORAGE_BULK_RETRIES; Attempt++) {
        U32 Completion = 0;
        if (!USBMassStorageBulkTransferOnce(Device, UsbDevice, Endpoint, BufferPhysical, BufferLinear,
                                            Length,
                                            DirectionIn,
                                            USB_MASS_STORAGE_BULK_TIMEOUT_MILLISECONDS,
                                            &Completion)) {
            (void)USBMassStorageClearEndpointHalt(Device, UsbDevice, Endpoint->Address);
            continue;
        }

        if (Completion == XHCI_COMPLETION_SUCCESS || Completion == XHCI_COMPLETION_SHORT_PACKET) {
            return TRUE;
        }

        if (Completion == XHCI_COMPLETION_STALL_ERROR) {
            (void)USBMassStorageClearEndpointHalt(Device, UsbDevice, Endpoint->Address);
            continue;
        }

        WARNING(TEXT("[USBMassStorageBulkTransfer] Completion %x"), Completion);
        return FALSE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Issue a BOT command (CBW/DATA/CSW).
 * @param Device USB mass storage device context.
 * @param CommandBlock SCSI command buffer.
 * @param CommandBlockLength SCSI command length.
 * @param DataLength Data stage length in bytes.
 * @param DirectionIn TRUE when data stage is IN.
 * @param DataOut Optional output buffer for IN data.
 * @return TRUE on success.
 */
static BOOL USBMassStorageBotCommand(LPUSB_MASS_STORAGE_DEVICE Device,
                                     const U8* CommandBlock,
                                     U8 CommandBlockLength,
                                     UINT DataLength,
                                     BOOL DirectionIn,
                                     LPVOID DataOut) {
    if (Device == NULL || CommandBlock == NULL || CommandBlockLength == 0) {
        return FALSE;
    }

    if (CommandBlockLength > sizeof(((USB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER*)0)->CommandBlock)) {
        return FALSE;
    }

    if (Device->InputOutputBufferLinear == 0 || Device->InputOutputBufferPhysical == 0) {
        return FALSE;
    }

    if (DataLength > PAGE_SIZE || DataLength > MAX_U32) {
        return FALSE;
    }

    USB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER* CommandBlockWrapper =
        (USB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER*)Device->InputOutputBufferLinear;
    MemorySet(CommandBlockWrapper, 0, sizeof(USB_MASS_STORAGE_COMMAND_BLOCK_WRAPPER));
    CommandBlockWrapper->Signature = USB_MASS_STORAGE_COMMAND_BLOCK_SIGNATURE;
    CommandBlockWrapper->Tag = Device->Tag++;
    CommandBlockWrapper->DataTransferLength = (U32)DataLength;
    CommandBlockWrapper->Flags = DirectionIn ? 0x80 : 0x00;
    CommandBlockWrapper->LogicalUnitNumber = 0;
    CommandBlockWrapper->CommandBlockLength = CommandBlockLength;
    MemoryCopy(CommandBlockWrapper->CommandBlock, CommandBlock, CommandBlockLength);

    if (!USBMassStorageBulkTransfer(Device->Controller, Device->UsbDevice, Device->BulkOutEndpoint,
                                    Device->InputOutputBufferPhysical, Device->InputOutputBufferLinear,
                                    USB_MASS_STORAGE_COMMAND_BLOCK_LENGTH, FALSE)) {
        ERROR(TEXT("[USBMassStorageBotCommand] CBW send failed"));
        return FALSE;
    }

    if (DataLength > 0) {
        if (!USBMassStorageBulkTransfer(Device->Controller, Device->UsbDevice,
                                        DirectionIn ? Device->BulkInEndpoint : Device->BulkOutEndpoint,
                                        Device->InputOutputBufferPhysical, Device->InputOutputBufferLinear,
                                        DataLength, DirectionIn)) {
            ERROR(TEXT("[USBMassStorageBotCommand] Data stage failed"));
            return FALSE;
        }

        if (DirectionIn && DataOut != NULL) {
            MemoryCopy(DataOut, (LPVOID)Device->InputOutputBufferLinear, DataLength);
        }
    }

    USB_MASS_STORAGE_COMMAND_STATUS_WRAPPER* CommandStatusWrapper =
        (USB_MASS_STORAGE_COMMAND_STATUS_WRAPPER*)Device->InputOutputBufferLinear;
    if (!USBMassStorageBulkTransfer(Device->Controller, Device->UsbDevice, Device->BulkInEndpoint,
                                    Device->InputOutputBufferPhysical, Device->InputOutputBufferLinear,
                                    USB_MASS_STORAGE_COMMAND_STATUS_LENGTH, TRUE)) {
        ERROR(TEXT("[USBMassStorageBotCommand] CSW read failed"));
        return FALSE;
    }

    if (CommandStatusWrapper->Signature != USB_MASS_STORAGE_COMMAND_STATUS_SIGNATURE ||
        CommandStatusWrapper->Tag != CommandBlockWrapper->Tag) {
        ERROR(TEXT("[USBMassStorageBotCommand] Invalid CSW sig=%x tag=%x"),
              (U32)CommandStatusWrapper->Signature,
              (U32)CommandStatusWrapper->Tag);
        return FALSE;
    }

    if (CommandStatusWrapper->Status != 0) {
        WARNING(TEXT("[USBMassStorageBotCommand] CSW status=%x residue=%u"),
                (U32)CommandStatusWrapper->Status,
                (U32)CommandStatusWrapper->DataResidue);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Run a SCSI INQUIRY command and log basic identification.
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
static BOOL USBMassStorageInquiry(LPUSB_MASS_STORAGE_DEVICE Device) {
    U8 CommandBlock[6];
    U8 InquiryData[36];
    STR Vendor[9];
    STR Product[17];

    MemorySet(CommandBlock, 0, sizeof(CommandBlock));
    CommandBlock[0] = USB_SCSI_INQUIRY;
    CommandBlock[4] = (U8)sizeof(InquiryData);

    if (!USBMassStorageBotCommand(Device, CommandBlock, sizeof(CommandBlock),
                                  sizeof(InquiryData), TRUE, InquiryData)) {
        return FALSE;
    }

    MemorySet(Vendor, 0, sizeof(Vendor));
    MemorySet(Product, 0, sizeof(Product));
    MemoryCopy(Vendor, &InquiryData[8], 8);
    MemoryCopy(Product, &InquiryData[16], 16);

    DEBUG(TEXT("[USBMassStorageInquiry] Vendor=%s Product=%s"), Vendor, Product);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Run SCSI READ CAPACITY(10) and capture block geometry.
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
static BOOL USBMassStorageReadCapacity(LPUSB_MASS_STORAGE_DEVICE Device) {
    U8 CommandBlock[10];
    U8 CapacityData[8];
    U32 LastLogicalBlockAddress;
    UINT BlockSize;
    U32 TemporaryValue;

    MemorySet(CommandBlock, 0, sizeof(CommandBlock));
    CommandBlock[0] = USB_SCSI_READ_CAPACITY_10;

    if (!USBMassStorageBotCommand(Device, CommandBlock, sizeof(CommandBlock),
                                  sizeof(CapacityData), TRUE, CapacityData)) {
        return FALSE;
    }

    TemporaryValue = 0;
    MemoryCopy(&TemporaryValue, &CapacityData[0], sizeof(TemporaryValue));
    LastLogicalBlockAddress = Ntohl(TemporaryValue);
    TemporaryValue = 0;
    MemoryCopy(&TemporaryValue, &CapacityData[4], sizeof(TemporaryValue));
    BlockSize = Ntohl(TemporaryValue);

    if (LastLogicalBlockAddress == 0xFFFFFFFF) {
        ERROR(TEXT("[USBMassStorageReadCapacity] Device too large for READ CAPACITY(10)"));
        return FALSE;
    }

    if (BlockSize != 512 && BlockSize != 4096) {
        ERROR(TEXT("[USBMassStorageReadCapacity] Unsupported block size %u"), BlockSize);
        return FALSE;
    }

    Device->BlockCount = (UINT)LastLogicalBlockAddress + 1U;
    Device->BlockSize = BlockSize;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Build a SCSI READ(10) command block.
 * @param CommandBlock Output command block buffer (10 bytes).
 * @param LogicalBlockAddress Starting logical block address.
 * @param TransferBlocks Block count to read.
 */
static void USBMassStorageBuildRead10(U8* CommandBlock, UINT LogicalBlockAddress, UINT TransferBlocks) {
    MemorySet(CommandBlock, 0, sizeof(CommandBlock));
    CommandBlock[0] = USB_SCSI_READ_10;
    CommandBlock[2] = (U8)((LogicalBlockAddress >> 24) & 0xFF);
    CommandBlock[3] = (U8)((LogicalBlockAddress >> 16) & 0xFF);
    CommandBlock[4] = (U8)((LogicalBlockAddress >> 8) & 0xFF);
    CommandBlock[5] = (U8)(LogicalBlockAddress & 0xFF);
    CommandBlock[7] = (U8)((TransferBlocks >> 8) & 0xFF);
    CommandBlock[8] = (U8)(TransferBlocks & 0xFF);
}

/************************************************************************/

/**
 * @brief Read blocks using SCSI READ(10).
 * @param Device USB mass storage device context.
 * @param LogicalBlockAddress Starting logical block address.
 * @param TransferBlocks Number of blocks to read.
 * @param Output Output buffer for data.
 * @return TRUE on success.
 */
static BOOL USBMassStorageReadBlocks(LPUSB_MASS_STORAGE_DEVICE Device,
                                     UINT LogicalBlockAddress,
                                     UINT TransferBlocks,
                                     LPVOID Output) {
    U8 CommandBlock[10];
    UINT Length = TransferBlocks * Device->BlockSize;

    if (Output == NULL || Length == 0 || Length > PAGE_SIZE) {
        return FALSE;
    }

    if (TransferBlocks > MAX_U16) {
        return FALSE;
    }

    USBMassStorageBuildRead10(CommandBlock, LogicalBlockAddress, TransferBlocks);

    return USBMassStorageBotCommand(Device, CommandBlock, sizeof(CommandBlock), Length, TRUE, Output);
}

/************************************************************************/

/**
 * @brief Allocate and initialize a USB mass storage device object.
 * @return Pointer to allocated device or NULL on failure.
 */
static LPUSB_MASS_STORAGE_DEVICE USBMassStorageAllocateDevice(void) {
    LPUSB_MASS_STORAGE_DEVICE Device =
        (LPUSB_MASS_STORAGE_DEVICE)KernelHeapAlloc(sizeof(USB_MASS_STORAGE_DEVICE));
    if (Device == NULL) {
        return NULL;
    }

    MemorySet(Device, 0, sizeof(USB_MASS_STORAGE_DEVICE));
    Device->Disk.TypeID = KOID_DISK;
    Device->Disk.References = 1;
    Device->Disk.Next = NULL;
    Device->Disk.Prev = NULL;
    Device->Disk.Driver = &USBMassStorageDriverState.Driver;
    Device->Access = DISK_ACCESS_READONLY;
    Device->Tag = 1;
    Device->Ready = FALSE;
    return Device;
}

/************************************************************************/

/**
 * @brief Acquire USB device/interface/endpoint references for a mass storage device.
 * @param Device USB mass storage device context.
 */
static void USBMassStorageAcquireReferences(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL || Device->ReferencesHeld) {
        return;
    }

    XHCI_ReferenceUsbDevice(Device->UsbDevice);
    XHCI_ReferenceUsbInterface(Device->Interface);
    XHCI_ReferenceUsbEndpoint(Device->BulkInEndpoint);
    XHCI_ReferenceUsbEndpoint(Device->BulkOutEndpoint);
    Device->ReferencesHeld = TRUE;
}

/************************************************************************/

/**
 * @brief Release USB device/interface/endpoint references for a mass storage device.
 * @param Device USB mass storage device context.
 */
static void USBMassStorageReleaseReferences(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL || !Device->ReferencesHeld) {
        return;
    }

    XHCI_ReleaseUsbEndpoint(Device->BulkOutEndpoint);
    XHCI_ReleaseUsbEndpoint(Device->BulkInEndpoint);
    XHCI_ReleaseUsbInterface(Device->Interface);
    XHCI_ReleaseUsbDevice(Device->UsbDevice);
    Device->ReferencesHeld = FALSE;
}

/************************************************************************/

/**
 * @brief Free a USB mass storage device object and its resources.
 * @param Device Device to free.
 */
static void USBMassStorageFreeDevice(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    USBMassStorageReleaseReferences(Device);

    if (Device->InputOutputBufferLinear != 0) {
        FreeRegion(Device->InputOutputBufferLinear, PAGE_SIZE);
        Device->InputOutputBufferLinear = 0;
    }
    if (Device->InputOutputBufferPhysical != 0) {
        FreePhysicalPage(Device->InputOutputBufferPhysical);
        Device->InputOutputBufferPhysical = 0;
    }

    if (Device->ListEntry != NULL) {
        Device->ListEntry->Present = FALSE;
        Device->ListEntry->Device = NULL;
        ReleaseKernelObject(Device->ListEntry);
        Device->ListEntry = NULL;
    }

    KernelHeapFree(Device);
}

/************************************************************************/

/**
 * @brief Initialize and register a detected USB mass storage device.
 * @param Controller xHCI controller.
 * @param UsbDevice USB device state.
 * @param Interface Mass storage interface.
 * @param BulkInEndpoint Bulk IN endpoint.
 * @param BulkOutEndpoint Bulk OUT endpoint.
 * @return TRUE on success.
 */
static BOOL USBMassStorageStartDevice(LPXHCI_DEVICE Controller,
                                      LPXHCI_USB_DEVICE UsbDevice,
                                      LPXHCI_USB_INTERFACE Interface,
                                      LPXHCI_USB_ENDPOINT BulkInEndpoint,
                                      LPXHCI_USB_ENDPOINT BulkOutEndpoint) {
    if (Controller == NULL || UsbDevice == NULL || Interface == NULL ||
        BulkInEndpoint == NULL || BulkOutEndpoint == NULL) {
        return FALSE;
    }

    LPUSB_MASS_STORAGE_DEVICE Device = USBMassStorageAllocateDevice();
    if (Device == NULL) {
        ERROR(TEXT("[USBMassStorageStartDevice] Device allocation failed"));
        return FALSE;
    }

    Device->Controller = Controller;
    Device->UsbDevice = UsbDevice;
    Device->Interface = Interface;
    Device->BulkInEndpoint = BulkInEndpoint;
    Device->BulkOutEndpoint = BulkOutEndpoint;
    Device->InterfaceNumber = Interface->Number;
    USBMassStorageAcquireReferences(Device);

    if (!XHCI_AddBulkEndpoint(Controller, UsbDevice, BulkOutEndpoint)) {
        ERROR(TEXT("[USBMassStorageStartDevice] Bulk OUT endpoint setup failed"));
        USBMassStorageFreeDevice(Device);
        return FALSE;
    }

    if (!XHCI_AddBulkEndpoint(Controller, UsbDevice, BulkInEndpoint)) {
        ERROR(TEXT("[USBMassStorageStartDevice] Bulk IN endpoint setup failed"));
        USBMassStorageFreeDevice(Device);
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("USBMassStorageInputOutput"),
                        &Device->InputOutputBufferPhysical,
                        &Device->InputOutputBufferLinear)) {
        ERROR(TEXT("[USBMassStorageStartDevice] IO buffer allocation failed"));
        USBMassStorageFreeDevice(Device);
        return FALSE;
    }

    if (!USBMassStorageInquiry(Device)) {
        WARNING(TEXT("[USBMassStorageStartDevice] INQUIRY failed, attempting reset"));
        if (!USBMassStorageResetRecovery(Device) || !USBMassStorageInquiry(Device)) {
            ERROR(TEXT("[USBMassStorageStartDevice] INQUIRY failed"));
            USBMassStorageFreeDevice(Device);
            return FALSE;
        }
    }

    if (!USBMassStorageReadCapacity(Device)) {
        WARNING(TEXT("[USBMassStorageStartDevice] READ CAPACITY failed, attempting reset"));
        if (!USBMassStorageResetRecovery(Device) || !USBMassStorageReadCapacity(Device)) {
            ERROR(TEXT("[USBMassStorageStartDevice] READ CAPACITY failed"));
            USBMassStorageFreeDevice(Device);
            return FALSE;
        }
    }

    DEBUG(TEXT("[USBMassStorageStartDevice] Capacity blocks=%u block_size=%u"),
          Device->BlockCount,
          Device->BlockSize);

    Device->Ready = TRUE;

    LPUSB_STORAGE_ENTRY Entry =
        (LPUSB_STORAGE_ENTRY)CreateKernelObject(sizeof(USB_STORAGE_ENTRY), KOID_USBSTORAGE);
    if (Entry == NULL) {
        ERROR(TEXT("[USBMassStorageStartDevice] List entry allocation failed"));
        USBMassStorageFreeDevice(Device);
        return FALSE;
    }

    MemorySet(&Entry->Device, 0, sizeof(USB_STORAGE_ENTRY) - sizeof(LISTNODE));
    Entry->Device = Device;
    Entry->Address = UsbDevice->Address;
    Entry->VendorId = UsbDevice->DeviceDescriptor.VendorID;
    Entry->ProductId = UsbDevice->DeviceDescriptor.ProductID;
    Entry->BlockCount = Device->BlockCount;
    Entry->BlockSize = Device->BlockSize;
    Entry->Present = TRUE;
    Device->ListEntry = Entry;

    LPLIST UsbStorageList = GetUsbStorageList();
    if (UsbStorageList == NULL || ListAddItem(UsbStorageList, Entry) == FALSE) {
        ERROR(TEXT("[USBMassStorageStartDevice] Unable to register USB storage list entry"));
        ReleaseKernelObject(Entry);
        KernelHeapFree(Entry);
        USBMassStorageFreeDevice(Device);
        return FALSE;
    }

    LPLIST DiskList = GetDiskList();
    if (DiskList == NULL || ListAddItem(DiskList, Device) == FALSE) {
        ERROR(TEXT("[USBMassStorageStartDevice] Unable to register disk entry"));
        USBMassStorageFreeDevice(Device);
        return FALSE;
    }

    if (FileSystemReady()) {
        LPLIST FileSystemList = GetFileSystemList();
        LPLISTNODE PreviousLast = FileSystemList != NULL ? FileSystemList->Last : NULL;

        DEBUG(TEXT("[USBMassStorageStartDevice] Mounting disk partitions"));
        if (!MountDiskPartitions((LPSTORAGE_UNIT)Device, NULL, 0)) {
            WARNING(TEXT("[USBMassStorageStartDevice] Partition mount failed"));
        }

        UINT MountedCount = USBMassStorageReportMounts(Device, PreviousLast);
        if (MountedCount > 0) {
            BroadcastProcessMessage(ETM_USB_MASS_STORAGE_MOUNTED,
                                    (U32)UsbDevice->Address,
                                    Device->BlockCount);
        }
    } else {
        DEBUG(TEXT("[USBMassStorageStartDevice] Deferred partition mount (filesystem not ready)"));
    }

    DEBUG(TEXT("[USBMassStorageStartDevice] USB disk addr=%x blocks=%u block_size=%u"),
          (U32)UsbDevice->Address,
          Device->BlockCount,
          Device->BlockSize);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Refresh presence flags for registered USB storage devices.
 */
static void USBMassStorageUpdatePresence(void) {
    LPLIST UsbStorageList = GetUsbStorageList();
    if (UsbStorageList == NULL) {
        return;
    }

    for (LPLISTNODE Node = UsbStorageList->First; Node;) {
        LPLISTNODE Next = Node->Next;
        LPUSB_STORAGE_ENTRY Entry = (LPUSB_STORAGE_ENTRY)Node;
        if (Entry == NULL || Entry->Device == NULL) {
            Node = Next;
            continue;
        }

        LPUSB_MASS_STORAGE_DEVICE Device = Entry->Device;
        if (Device->Controller == NULL || Device->UsbDevice == NULL) {
            Entry->Present = FALSE;
            USBMassStorageDetachDevice(Device);
            Node = Next;
            continue;
        }

        Entry->Present = USBMassStorageIsDevicePresent(Device->Controller, Device->UsbDevice);
        if (Entry->Present == FALSE) {
            USBMassStorageDetachDevice(Device);
        }

        Node = Next;
    }
}

/************************************************************************/

/**
 * @brief Scan xHCI controllers for new USB mass storage devices.
 */
static void USBMassStorageScanControllers(void) {
    LPLIST PciList = GetPCIDeviceList();
    if (PciList == NULL) {
        return;
    }

    for (LPLISTNODE Node = PciList->First; Node; Node = Node->Next) {
        LPPCI_DEVICE PciDevice = (LPPCI_DEVICE)Node;
        if (PciDevice->Driver != (LPDRIVER)&XHCIDriver) {
            continue;
        }

        LPXHCI_DEVICE Controller = (LPXHCI_DEVICE)PciDevice;
        SAFE_USE_VALID_ID(Controller, KOID_PCIDEVICE) {
            XHCI_EnsureUsbDevices(Controller);

            LPLIST UsbDeviceList = GetUsbDeviceList();
            if (UsbDeviceList == NULL) {
                continue;
            }
            for (LPLISTNODE UsbNode = UsbDeviceList->First; UsbNode != NULL; UsbNode = UsbNode->Next) {
                LPXHCI_USB_DEVICE UsbDevice = (LPXHCI_USB_DEVICE)UsbNode;
                if (UsbDevice->Controller != Controller) {
                    continue;
                }
                if (!UsbDevice->Present || UsbDevice->IsHub) {
                    continue;
                }

                if (USBMassStorageIsTracked(UsbDevice)) {
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
                    if (!USBMassStorageIsMassStorageInterface(Interface)) {
                        continue;
                    }

                    LPXHCI_USB_ENDPOINT BulkIn = NULL;
                    LPXHCI_USB_ENDPOINT BulkOut = NULL;
                    if (!USBMassStorageFindBulkEndpoints(Interface, &BulkIn, &BulkOut)) {
                        continue;
                    }

                    if (!USBMassStorageStartDevice(Controller, UsbDevice, Interface, BulkIn, BulkOut)) {
                        USBMassStorageDriverState.State.RetryDelay = 50;
                        continue;
                    }

                    break;
                }
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Poll callback to maintain USB storage device list.
 * @param Context Unused.
 */
static void USBMassStoragePoll(LPVOID Context) {
    UNUSED(Context);

    if (USBMassStorageDriverState.State.Initialized == FALSE) {
        return;
    }

    if (USBMassStorageDriverState.State.RetryDelay != 0) {
        USBMassStorageDriverState.State.RetryDelay--;
        return;
    }

    USBMassStorageUpdatePresence();
    USBMassStorageScanControllers();
}

/************************************************************************/

/**
 * @brief Read sectors from a USB mass storage device.
 * @param Control I/O control structure.
 * @return DF_RETURN_SUCCESS on success or error code.
 */
static U32 USBMassStorageRead(LPIOCONTROL Control) {
    if (Control == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPUSB_MASS_STORAGE_DEVICE Device = (LPUSB_MASS_STORAGE_DEVICE)Control->Disk;
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Device->Disk.TypeID != KOID_DISK) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Control->SectorHigh != 0) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Control->Buffer == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (!Device->Ready) {
        return DF_RETURN_NODEVICE;
    }

    if (!USBMassStorageIsDevicePresent(Device->Controller, Device->UsbDevice)) {
        return DF_RETURN_NODEVICE;
    }

    if (Control->NumSectors == 0) {
        return DF_RETURN_SUCCESS;
    }

    if (Control->SectorLow >= Device->BlockCount) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Control->NumSectors > (Device->BlockCount - Control->SectorLow)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Control->NumSectors > (MAX_UINT / Device->BlockSize)) {
        return DF_RETURN_BAD_PARAMETER;
    }

    UINT TotalBytes = Control->NumSectors * Device->BlockSize;
    if (Control->BufferSize < TotalBytes) {
        return DF_RETURN_BAD_PARAMETER;
    }

    UINT Remaining = Control->NumSectors;
    UINT CurrentLogicalBlockAddress = Control->SectorLow;
    U8* Output = (U8*)Control->Buffer;

    while (Remaining > 0) {
        UINT MaximumBlocks = PAGE_SIZE / Device->BlockSize;
        UINT Blocks = Remaining;
        if (Blocks > MaximumBlocks) {
            Blocks = MaximumBlocks;
        }

        if (!USBMassStorageReadBlocks(Device, CurrentLogicalBlockAddress, Blocks, Output)) {
            return DF_RETURN_HARDWARE;
        }

        Output += Blocks * Device->BlockSize;
        CurrentLogicalBlockAddress += Blocks;
        Remaining -= Blocks;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Reject writes to a read-only USB mass storage device.
 * @param Control I/O control structure.
 * @return DF_RETURN_NO_PERMISSION.
 */
static U32 USBMassStorageWrite(LPIOCONTROL Control) {
    UNUSED(Control);
    return DF_RETURN_NO_PERMISSION;
}

/************************************************************************/

/**
 * @brief Populate disk information for a USB mass storage device.
 * @param Info Output disk info structure.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 USBMassStorageGetInfo(LPDISKINFO Info) {
    if (Info == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPUSB_MASS_STORAGE_DEVICE Device = (LPUSB_MASS_STORAGE_DEVICE)Info->Disk;
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Device->Disk.TypeID != KOID_DISK) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Info->Type = DRIVER_TYPE_USB_STORAGE;
    Info->Removable = 1;
    Info->BytesPerSector = Device->BlockSize;
    Info->NumSectors = U64_FromUINT(Device->BlockCount);
    Info->Access = Device->Access;

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Update access flags for a USB mass storage device.
 * @param Access Access request.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 USBMassStorageSetAccess(LPDISKACCESS Access) {
    if (Access == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    LPUSB_MASS_STORAGE_DEVICE Device = (LPUSB_MASS_STORAGE_DEVICE)Access->Disk;
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    if (Device->Disk.TypeID != KOID_DISK) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device->Access = (Access->Access | DISK_ACCESS_READONLY);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Reset readiness state for a USB mass storage device.
 * @param Device USB mass storage device.
 * @return DF_RETURN_SUCCESS on completion.
 */
static U32 USBMassStorageReset(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device->Ready = USBMassStorageIsDevicePresent(Device->Controller, Device->UsbDevice);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Driver command dispatcher for USB mass storage.
 * @param Function Driver function code.
 * @param Parameter Function-specific parameter.
 * @return Driver status or data.
 */
UINT USBMassStorageCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            if ((USBMassStorageDriverState.Driver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            if (USBMassStorageDriverState.State.PollHandle == DEFERRED_WORK_INVALID_HANDLE) {
                USBMassStorageDriverState.State.PollHandle =
                    DeferredWorkRegisterPollOnly(USBMassStoragePoll, NULL, TEXT("USBMassStorage"));
                if (USBMassStorageDriverState.State.PollHandle == DEFERRED_WORK_INVALID_HANDLE) {
                    return DF_RETURN_UNEXPECTED;
                }
            }

            USBMassStorageDriverState.State.Initialized = TRUE;
            USBMassStorageDriverState.Driver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((USBMassStorageDriverState.Driver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            if (USBMassStorageDriverState.State.PollHandle != DEFERRED_WORK_INVALID_HANDLE) {
                DeferredWorkUnregister(USBMassStorageDriverState.State.PollHandle);
                USBMassStorageDriverState.State.PollHandle = DEFERRED_WORK_INVALID_HANDLE;
            }

            USBMassStorageDriverState.State.Initialized = FALSE;
            USBMassStorageDriverState.Driver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(USB_MASS_STORAGE_VER_MAJOR, USB_MASS_STORAGE_VER_MINOR);

        case DF_DISK_RESET:
            return USBMassStorageReset((LPUSB_MASS_STORAGE_DEVICE)Parameter);
        case DF_DISK_READ:
            return USBMassStorageRead((LPIOCONTROL)Parameter);
        case DF_DISK_WRITE:
            return USBMassStorageWrite((LPIOCONTROL)Parameter);
        case DF_DISK_GETINFO:
            return USBMassStorageGetInfo((LPDISKINFO)Parameter);
        case DF_DISK_SETACCESS:
            return USBMassStorageSetAccess((LPDISKACCESS)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/
