
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

#include "drivers/storage/USBStorage.h"

#include "Clock.h"
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
#include "drivers/usb/XHCI-Internal.h"
#include "process/Task-Messaging.h"
#include "utils/Helpers.h"
#include "utils/RateLimiter.h"

/************************************************************************/

#define USB_MASS_STORAGE_VER_MAJOR 1
#define USB_MASS_STORAGE_VER_MINOR 0

#define USB_MASS_STORAGE_SUBCLASS_SCSI 0x06
#define USB_MASS_STORAGE_PROTOCOL_BOT 0x50
#define USB_MASS_STORAGE_PROTOCOL_UAS 0x62

#define USB_MASS_STORAGE_COMMAND_BLOCK_SIGNATURE 0x43425355
#define USB_MASS_STORAGE_COMMAND_STATUS_SIGNATURE 0x53425355
#define USB_MASS_STORAGE_COMMAND_BLOCK_LENGTH 31
#define USB_MASS_STORAGE_COMMAND_STATUS_LENGTH 13

#define USB_SCSI_INQUIRY 0x12
#define USB_SCSI_READ_CAPACITY_10 0x25
#define USB_SCSI_READ_10 0x28

#define USB_MASS_STORAGE_BULK_TIMEOUT_MILLISECONDS 1000
#define USB_MASS_STORAGE_BULK_RETRIES 3
#define USB_MASS_STORAGE_SCAN_LOG_IMMEDIATE_BUDGET 1
#define USB_MASS_STORAGE_SCAN_LOG_INTERVAL_MS 2000

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
    BOOL MountPending;
    BOOL ReferencesHeld;
    LPUSB_STORAGE_ENTRY ListEntry;
} USB_MASS_STORAGE_DEVICE, *LPUSB_MASS_STORAGE_DEVICE;

typedef struct tag_USB_MASS_STORAGE_STATE {
    BOOL Initialized;
    U32 PollHandle;
    UINT RetryDelay;
    RATE_LIMITER ScanLogLimiter;
} USB_MASS_STORAGE_STATE, *LPUSB_MASS_STORAGE_STATE;

typedef struct tag_USB_MASS_STORAGE_DRIVER {
    DRIVER Driver;
    USB_MASS_STORAGE_STATE State;
} USB_MASS_STORAGE_DRIVER, *LPUSB_MASS_STORAGE_DRIVER;

/************************************************************************/

UINT USBStorageCommands(UINT Function, UINT Parameter);
BOOL USBStorageIsMassStorageInterface(LPXHCI_USB_INTERFACE Interface);
BOOL USBStorageFindBulkEndpoints(LPXHCI_USB_INTERFACE Interface,
                                 LPXHCI_USB_ENDPOINT* BulkInOut,
                                 LPXHCI_USB_ENDPOINT* BulkOutOut);
BOOL USBStorageIsDevicePresent(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice);
BOOL USBStorageIsTracked(LPXHCI_USB_DEVICE UsbDevice);
BOOL USBStorageResetRecovery(LPUSB_MASS_STORAGE_DEVICE Device);
BOOL USBStorageInquiry(LPUSB_MASS_STORAGE_DEVICE Device);
BOOL USBStorageReadCapacity(LPUSB_MASS_STORAGE_DEVICE Device);
BOOL USBStorageReadBlocks(LPUSB_MASS_STORAGE_DEVICE Device,
                          UINT LogicalBlockAddress,
                          UINT TransferBlocks,
                          LPVOID Output);

static USB_MASS_STORAGE_DRIVER DATA_SECTION USBStorageDriverState = {
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
        .Alias = "usb_storage",
        .Flags = 0,
        .Command = USBStorageCommands
    },
    .State = {
        .Initialized = FALSE,
        .PollHandle = DEFERRED_WORK_INVALID_HANDLE,
        .RetryDelay = 0,
        .ScanLogLimiter = {0}
    }
};

/************************************************************************/

/**
 * @brief Emit rate-limited scan diagnostics for unsupported mass-storage interfaces.
 * @param UsbDevice USB device.
 * @param Interface Interface descriptor.
 * @param Reason Short reason label.
 */
static void USBStorageLogScan(LPXHCI_USB_DEVICE UsbDevice, LPXHCI_USB_INTERFACE Interface, LPCSTR Reason) {
    U32 Suppressed = 0;

    if (UsbDevice == NULL || Interface == NULL) {
        return;
    }

    if (!RateLimiterShouldTrigger(&USBStorageDriverState.State.ScanLogLimiter, GetSystemTime(), &Suppressed)) {
        return;
    }

    WARNING(TEXT("[USBStorageScan] Port=%u Addr=%u If=%u Class=%x/%x/%x reason=%s suppressed=%u"),
            (U32)UsbDevice->PortNumber,
            (U32)UsbDevice->Address,
            (U32)Interface->Number,
            (U32)Interface->InterfaceClass,
            (U32)Interface->InterfaceSubClass,
            (U32)Interface->InterfaceProtocol,
            (Reason != NULL) ? Reason : TEXT("?"),
            Suppressed);
}

/************************************************************************/

static UINT USBStorageReportMounts(LPUSB_MASS_STORAGE_DEVICE Device, LPLISTNODE PreviousLast) {
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
 * @brief Attempt partition mounting for one USB storage device when possible.
 * @param Device USB storage device.
 * @return Number of newly mounted filesystems.
 */
static UINT USBStorageTryMountPending(LPUSB_MASS_STORAGE_DEVICE Device) {
    LPLIST FileSystemList = NULL;
    LPLISTNODE PreviousLast = NULL;
    UINT MountedCount = 0;

    if (Device == NULL || Device->Ready == FALSE || Device->MountPending == FALSE) {
        return 0;
    }
    if (!FileSystemReady()) {
        return 0;
    }

    FileSystemList = GetFileSystemList();
    PreviousLast = (FileSystemList != NULL) ? FileSystemList->Last : NULL;

    if (!MountDiskPartitions((LPSTORAGE_UNIT)Device, NULL, 0)) {
        WARNING(TEXT("[USBStorageTryMountPending] Partition mount failed"));
        return 0;
    }

    MountedCount = USBStorageReportMounts(Device, PreviousLast);
    if (MountedCount != 0) {
        Device->MountPending = FALSE;
        if (Device->ListEntry != NULL) {
            BroadcastProcessMessage(ETM_USB_MASS_STORAGE_MOUNTED,
                                    (U32)Device->ListEntry->Address,
                                    Device->BlockCount);
        }
    }

    return MountedCount;
}

/************************************************************************/

/**
 * @brief Unmount and release filesystems associated with a USB disk.
 * @param Disk USB disk to detach.
 */
static void USBStorageDetachFileSystems(LPSTORAGE_UNIT Disk, U32 UsbAddress) {
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
static void USBStorageDetachDevice(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    Device->Ready = FALSE;

    if (Device->ListEntry != NULL) {
        USBStorageDetachFileSystems((LPSTORAGE_UNIT)Device, (U32)Device->ListEntry->Address);
    } else {
        USBStorageDetachFileSystems((LPSTORAGE_UNIT)Device, 0);
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
LPDRIVER USBStorageGetDriver(void) {
    return &USBStorageDriverState.Driver;
}

/************************************************************************/


/**
 * @brief Allocate and initialize a USB mass storage device object.
 * @return Pointer to allocated device or NULL on failure.
 */
static LPUSB_MASS_STORAGE_DEVICE USBStorageAllocateDevice(void) {
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
    Device->Disk.Driver = &USBStorageDriverState.Driver;
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
static void USBStorageAcquireReferences(LPUSB_MASS_STORAGE_DEVICE Device) {
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
static void USBStorageReleaseReferences(LPUSB_MASS_STORAGE_DEVICE Device) {
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
static void USBStorageFreeDevice(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    USBStorageReleaseReferences(Device);

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
static BOOL USBStorageStartDevice(LPXHCI_DEVICE Controller,
                                      LPXHCI_USB_DEVICE UsbDevice,
                                      LPXHCI_USB_INTERFACE Interface,
                                      LPXHCI_USB_ENDPOINT BulkInEndpoint,
                                      LPXHCI_USB_ENDPOINT BulkOutEndpoint) {
    if (Controller == NULL || UsbDevice == NULL || Interface == NULL ||
        BulkInEndpoint == NULL || BulkOutEndpoint == NULL) {
        return FALSE;
    }

    LPUSB_MASS_STORAGE_DEVICE Device = USBStorageAllocateDevice();
    if (Device == NULL) {
        ERROR(TEXT("[USBStorageStartDevice] Device allocation failed"));
        return FALSE;
    }

    Device->Controller = Controller;
    Device->UsbDevice = UsbDevice;
    Device->Interface = Interface;
    Device->BulkInEndpoint = BulkInEndpoint;
    Device->BulkOutEndpoint = BulkOutEndpoint;
    Device->InterfaceNumber = Interface->Number;
    USBStorageAcquireReferences(Device);

    if (!XHCI_AddBulkEndpoint(Controller, UsbDevice, BulkOutEndpoint)) {
        ERROR(TEXT("[USBStorageStartDevice] Bulk OUT endpoint setup failed"));
        USBStorageFreeDevice(Device);
        return FALSE;
    }

    if (!XHCI_AddBulkEndpoint(Controller, UsbDevice, BulkInEndpoint)) {
        ERROR(TEXT("[USBStorageStartDevice] Bulk IN endpoint setup failed"));
        USBStorageFreeDevice(Device);
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("USBStorageInputOutput"),
                        &Device->InputOutputBufferPhysical,
                        &Device->InputOutputBufferLinear)) {
        ERROR(TEXT("[USBStorageStartDevice] IO buffer allocation failed"));
        USBStorageFreeDevice(Device);
        return FALSE;
    }

    if (!USBStorageInquiry(Device)) {
        WARNING(TEXT("[USBStorageStartDevice] INQUIRY failed, attempting reset"));
        if (!USBStorageResetRecovery(Device) || !USBStorageInquiry(Device)) {
            ERROR(TEXT("[USBStorageStartDevice] INQUIRY failed"));
            USBStorageFreeDevice(Device);
            return FALSE;
        }
    }

    if (!USBStorageReadCapacity(Device)) {
        WARNING(TEXT("[USBStorageStartDevice] READ CAPACITY failed, attempting reset"));
        if (!USBStorageResetRecovery(Device) || !USBStorageReadCapacity(Device)) {
            ERROR(TEXT("[USBStorageStartDevice] READ CAPACITY failed"));
            USBStorageFreeDevice(Device);
            return FALSE;
        }
    }

    DEBUG(TEXT("[USBStorageStartDevice] Capacity blocks=%u block_size=%u"),
          Device->BlockCount,
          Device->BlockSize);

    Device->Ready = TRUE;
    Device->MountPending = TRUE;

    LPUSB_STORAGE_ENTRY Entry =
        (LPUSB_STORAGE_ENTRY)CreateKernelObject(sizeof(USB_STORAGE_ENTRY), KOID_USBSTORAGE);
    if (Entry == NULL) {
        ERROR(TEXT("[USBStorageStartDevice] List entry allocation failed"));
        USBStorageFreeDevice(Device);
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
        ERROR(TEXT("[USBStorageStartDevice] Unable to register USB storage list entry"));
        ReleaseKernelObject(Entry);
        KernelHeapFree(Entry);
        USBStorageFreeDevice(Device);
        return FALSE;
    }

    LPLIST DiskList = GetDiskList();
    if (DiskList == NULL || ListAddItem(DiskList, Device) == FALSE) {
        ERROR(TEXT("[USBStorageStartDevice] Unable to register disk entry"));
        USBStorageFreeDevice(Device);
        return FALSE;
    }

    if (FileSystemReady()) {
        DEBUG(TEXT("[USBStorageStartDevice] Mounting disk partitions"));
        (void)USBStorageTryMountPending(Device);
    } else {
        DEBUG(TEXT("[USBStorageStartDevice] Deferred partition mount (filesystem not ready)"));
    }

    DEBUG(TEXT("[USBStorageStartDevice] USB disk addr=%x blocks=%u block_size=%u"),
          (U32)UsbDevice->Address,
          Device->BlockCount,
          Device->BlockSize);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Refresh presence flags for registered USB storage devices.
 */
static void USBStorageUpdatePresence(void) {
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
            USBStorageDetachDevice(Device);
            Node = Next;
            continue;
        }

        Entry->Present = USBStorageIsDevicePresent(Device->Controller, Device->UsbDevice);
        if (Entry->Present == FALSE) {
            USBStorageDetachDevice(Device);
        }

        Node = Next;
    }
}

/************************************************************************/

/**
 * @brief Scan xHCI controllers for new USB mass storage devices.
 */
static void USBStorageScanControllers(void) {
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

                if (USBStorageIsTracked(UsbDevice)) {
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
                    if (Interface->InterfaceClass != USB_CLASS_MASS_STORAGE) {
                        continue;
                    }
                    if (Interface->InterfaceSubClass != USB_MASS_STORAGE_SUBCLASS_SCSI) {
                        USBStorageLogScan(UsbDevice, Interface, TEXT("UnsupportedSubclass"));
                        continue;
                    }
                    if (Interface->InterfaceProtocol == USB_MASS_STORAGE_PROTOCOL_UAS) {
                        USBStorageLogScan(UsbDevice, Interface, TEXT("UASNotSupported"));
                        continue;
                    }
                    if (!USBStorageIsMassStorageInterface(Interface)) {
                        USBStorageLogScan(UsbDevice, Interface, TEXT("UnsupportedProtocol"));
                        continue;
                    }

                    LPXHCI_USB_ENDPOINT BulkIn = NULL;
                    LPXHCI_USB_ENDPOINT BulkOut = NULL;
                    if (!USBStorageFindBulkEndpoints(Interface, &BulkIn, &BulkOut)) {
                        USBStorageLogScan(UsbDevice, Interface, TEXT("MissingBulkEndpoints"));
                        continue;
                    }

                    if (!USBStorageStartDevice(Controller, UsbDevice, Interface, BulkIn, BulkOut)) {
                        USBStorageLogScan(UsbDevice, Interface, TEXT("StartDeviceFailed"));
                        USBStorageDriverState.State.RetryDelay = 50;
                        continue;
                    }

                    USBStorageLogScan(UsbDevice, Interface, TEXT("Attached"));
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
static void USBStoragePoll(LPVOID Context) {
    UNUSED(Context);

    if (USBStorageDriverState.State.Initialized == FALSE) {
        return;
    }

    if (USBStorageDriverState.State.RetryDelay != 0) {
        USBStorageDriverState.State.RetryDelay--;
        return;
    }

    USBStorageUpdatePresence();
    USBStorageScanControllers();

    LPLIST UsbStorageList = GetUsbStorageList();
    if (UsbStorageList == NULL) {
        return;
    }

    for (LPLISTNODE Node = UsbStorageList->First; Node != NULL; Node = Node->Next) {
        LPUSB_STORAGE_ENTRY Entry = (LPUSB_STORAGE_ENTRY)Node;
        LPUSB_MASS_STORAGE_DEVICE Device = NULL;
        if (Entry == NULL || Entry->Device == NULL || Entry->Present == FALSE) {
            continue;
        }

        Device = (LPUSB_MASS_STORAGE_DEVICE)Entry->Device;
        if (Device->MountPending == FALSE) {
            continue;
        }

        (void)USBStorageTryMountPending(Device);
    }
}

/************************************************************************/

/**
 * @brief Read sectors from a USB mass storage device.
 * @param Control I/O control structure.
 * @return DF_RETURN_SUCCESS on success or error code.
 */
static U32 USBStorageRead(LPIOCONTROL Control) {
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

    if (!USBStorageIsDevicePresent(Device->Controller, Device->UsbDevice)) {
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

        if (!USBStorageReadBlocks(Device, CurrentLogicalBlockAddress, Blocks, Output)) {
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
static U32 USBStorageWrite(LPIOCONTROL Control) {
    UNUSED(Control);
    return DF_RETURN_NO_PERMISSION;
}

/************************************************************************/

/**
 * @brief Populate disk information for a USB mass storage device.
 * @param Info Output disk info structure.
 * @return DF_RETURN_SUCCESS on success.
 */
static U32 USBStorageGetInfo(LPDISKINFO Info) {
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
static U32 USBStorageSetAccess(LPDISKACCESS Access) {
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
static U32 USBStorageReset(LPUSB_MASS_STORAGE_DEVICE Device) {
    if (Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device->Ready = USBStorageIsDevicePresent(Device->Controller, Device->UsbDevice);
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Driver command dispatcher for USB mass storage.
 * @param Function Driver function code.
 * @param Parameter Function-specific parameter.
 * @return Driver status or data.
 */
UINT USBStorageCommands(UINT Function, UINT Parameter) {
    switch (Function) {
        case DF_LOAD:
            if ((USBStorageDriverState.Driver.Flags & DRIVER_FLAG_READY) != 0) {
                return DF_RETURN_SUCCESS;
            }

            (void)RateLimiterInit(&USBStorageDriverState.State.ScanLogLimiter,
                                  USB_MASS_STORAGE_SCAN_LOG_IMMEDIATE_BUDGET,
                                  USB_MASS_STORAGE_SCAN_LOG_INTERVAL_MS);

            if (USBStorageDriverState.State.PollHandle == DEFERRED_WORK_INVALID_HANDLE) {
                USBStorageDriverState.State.PollHandle =
                    DeferredWorkRegisterPollOnly(USBStoragePoll, NULL, TEXT("USBStorage"));
                if (USBStorageDriverState.State.PollHandle == DEFERRED_WORK_INVALID_HANDLE) {
                    return DF_RETURN_UNEXPECTED;
                }
            }

            USBStorageDriverState.State.Initialized = TRUE;
            USBStorageDriverState.Driver.Flags |= DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_UNLOAD:
            if ((USBStorageDriverState.Driver.Flags & DRIVER_FLAG_READY) == 0) {
                return DF_RETURN_SUCCESS;
            }

            if (USBStorageDriverState.State.PollHandle != DEFERRED_WORK_INVALID_HANDLE) {
                DeferredWorkUnregister(USBStorageDriverState.State.PollHandle);
                USBStorageDriverState.State.PollHandle = DEFERRED_WORK_INVALID_HANDLE;
            }

            USBStorageDriverState.State.Initialized = FALSE;
            USBStorageDriverState.Driver.Flags &= ~DRIVER_FLAG_READY;
            return DF_RETURN_SUCCESS;

        case DF_GET_VERSION:
            return MAKE_VERSION(USB_MASS_STORAGE_VER_MAJOR, USB_MASS_STORAGE_VER_MINOR);

        case DF_DISK_RESET:
            return USBStorageReset((LPUSB_MASS_STORAGE_DEVICE)Parameter);
        case DF_DISK_READ:
            return USBStorageRead((LPIOCONTROL)Parameter);
        case DF_DISK_WRITE:
            return USBStorageWrite((LPIOCONTROL)Parameter);
        case DF_DISK_GETINFO:
            return USBStorageGetInfo((LPDISKINFO)Parameter);
        case DF_DISK_SETACCESS:
            return USBStorageSetAccess((LPDISKACCESS)Parameter);
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/
