
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

#include "drivers/storage/USBStorage-Private.h"

#include "Clock.h"
#include "Endianness.h"
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"
#include "process/Task.h"

/************************************************************************/

#define USB_STORAGE_WAIT_LOG_IMMEDIATE_BUDGET 1
#define USB_STORAGE_WAIT_LOG_INTERVAL_MS 1000

/************************************************************************/

/**
 * @brief Check whether one BOT transfer trace line should be emitted.
 * @param SuppressedOut Receives suppressed count.
 * @return TRUE when one line can be emitted.
 */
static BOOL USBStorageShouldTraceTransfer(U32* SuppressedOut) {
    static RATE_LIMITER DATA_SECTION TransferTraceLimiter = {0};
    static BOOL DATA_SECTION TransferTraceLimiterInitAttempted = FALSE;

    if (SuppressedOut == NULL) {
        return FALSE;
    }

    if (TransferTraceLimiter.Initialized == FALSE && TransferTraceLimiterInitAttempted == FALSE) {
        TransferTraceLimiterInitAttempted = TRUE;
        if (RateLimiterInit(&TransferTraceLimiter,
                            USB_STORAGE_WAIT_LOG_IMMEDIATE_BUDGET,
                            USB_STORAGE_WAIT_LOG_INTERVAL_MS) == FALSE) {
            return FALSE;
        }
    }

    return RateLimiterShouldTrigger(&TransferTraceLimiter, GetSystemTime(), SuppressedOut);
}

/**
 * @brief Check whether an interface matches USB mass storage BOT.
 * @param Interface USB interface descriptor.
 * @return TRUE when the interface matches BOT class/subclass/protocol.
 */
BOOL USBStorageIsMassStorageInterface(LPXHCI_USB_INTERFACE Interface) {
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
BOOL USBStorageFindBulkEndpoints(LPXHCI_USB_INTERFACE Interface,
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
BOOL USBStorageIsDevicePresent(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
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
BOOL USBStorageIsTracked(LPXHCI_USB_DEVICE UsbDevice) {
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
static BOOL USBStorageClearEndpointHalt(LPXHCI_DEVICE Device,
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
BOOL USBStorageResetRecovery(LPUSB_MASS_STORAGE_DEVICE Device) {
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
        WARNING(TEXT("[USBStorageResetRecovery] BOT reset failed for interface %u"),
            (UINT)Device->InterfaceNumber);
        return FALSE;
    }

    BulkInOk = USBStorageClearEndpointHalt(Device->Controller,
                                               Device->UsbDevice,
                                               Device->BulkInEndpoint->Address);
    BulkOutOk = USBStorageClearEndpointHalt(Device->Controller,
                                                Device->UsbDevice,
                                                Device->BulkOutEndpoint->Address);
    if (!BulkInOk || !BulkOutOk) {
        WARNING(TEXT("[USBStorageResetRecovery] Clear halt failed in=%x out=%x"),
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
static BOOL USBStorageWaitCompletion(LPXHCI_DEVICE Device,
                                     LPXHCI_USB_DEVICE UsbDevice,
                                     LPXHCI_USB_ENDPOINT Endpoint,
                                     U64 TrbPhysical,
                                     UINT TimeoutMilliseconds,
                                     U32* CompletionOut) {
    UINT Remaining = TimeoutMilliseconds;
    UINT Elapsed = 0;
    U32 Suppressed = 0;
    BOOL UsedRouteFallback = FALSE;
    U64 ObservedTrbPhysical = U64_0;

    if (USBStorageShouldTraceTransfer(&Suppressed)) {
        DEBUG(TEXT("[USBStorageWaitCompletion] Begin Timeout=%u Trb=%x:%x suppressed=%u"),
              TimeoutMilliseconds,
              U64_High32(TrbPhysical),
              U64_Low32(TrbPhysical),
              Suppressed);
    }

    while (Remaining > 0) {
        if (XHCI_CheckTransferCompletionRouted(Device,
                                               TrbPhysical,
                                               (UsbDevice != NULL) ? UsbDevice->SlotId : 0,
                                               (Endpoint != NULL) ? Endpoint->Dci : 0,
                                               CompletionOut,
                                               &UsedRouteFallback,
                                               &ObservedTrbPhysical)) {
            if (UsedRouteFallback && USBStorageShouldTraceTransfer(&Suppressed)) {
                DEBUG(TEXT("[USBStorageWaitCompletion] Transfer event TRB pointer mismatch Slot=%x Dci=%u Expected=%x:%x Observed=%x:%x Completion=%x"),
                      (UsbDevice != NULL) ? (U32)UsbDevice->SlotId : 0,
                      (Endpoint != NULL) ? (U32)Endpoint->Dci : 0,
                      U64_High32(TrbPhysical),
                      U64_Low32(TrbPhysical),
                      U64_High32(ObservedTrbPhysical),
                      U64_Low32(ObservedTrbPhysical),
                      (CompletionOut != NULL) ? *CompletionOut : 0);
            }
            if (USBStorageShouldTraceTransfer(&Suppressed)) {
                DEBUG(TEXT("[USBStorageWaitCompletion] Completed Elapsed=%u Completion=%x Trb=%x:%x suppressed=%u"),
                      Elapsed,
                      (CompletionOut != NULL) ? *CompletionOut : 0,
                      U64_High32(TrbPhysical),
                      U64_Low32(TrbPhysical),
                      Suppressed);
            }
            return TRUE;
        }

        Sleep(1);
        Remaining--;
        Elapsed++;
    }

    if (USBStorageShouldTraceTransfer(&Suppressed)) {
        DEBUG(TEXT("[USBStorageWaitCompletion] Timeout Elapsed=%u Trb=%x:%x suppressed=%u"),
              Elapsed,
              U64_High32(TrbPhysical),
              U64_Low32(TrbPhysical),
              Suppressed);
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
static BOOL USBStorageBulkTransferOnce(LPXHCI_DEVICE Device,
                                           LPXHCI_USB_DEVICE UsbDevice,
                                           LPXHCI_USB_ENDPOINT Endpoint,
                                           PHYSICAL BufferPhysical,
                                           LINEAR BufferLinear,
                                           UINT Length,
                                           BOOL DirectionIn,
                                           UINT TimeoutMilliseconds,
                                           U8 ScsiOpCode,
                                           U32* CompletionOut) {
    if (Device == NULL || UsbDevice == NULL || Endpoint == NULL || BufferPhysical == 0 || BufferLinear == 0) {
        return FALSE;
    }

    if (Length > MAX_U32) {
        return FALSE;
    }

    U64 TrbPhysical = U64_0;
    if (!XHCI_SubmitNormalTransfer(Device,
                                   UsbDevice,
                                   Endpoint,
                                   BufferPhysical,
                                   (U32)Length,
                                   DirectionIn,
                                   &TrbPhysical)) {
        return FALSE;
    }

    if (!USBStorageWaitCompletion(Device, UsbDevice, Endpoint, TrbPhysical, TimeoutMilliseconds, CompletionOut)) {
        DEBUG(TEXT("[USBStorageBulkTransferOnce] Timeout Op=%x Slot=%x Port=%u Addr=%u Ep=%x Dci=%u DirIn=%u Len=%u Trb=%p"),
              (U32)ScsiOpCode,
              (U32)UsbDevice->SlotId,
              (U32)UsbDevice->PortNumber,
              (U32)UsbDevice->Address,
              (U32)Endpoint->Address,
              (U32)Endpoint->Dci,
              (U32)(DirectionIn != FALSE),
              Length,
              (LPVOID)(UINT)U64_Low32(TrbPhysical));
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
static BOOL USBStorageBulkTransfer(LPXHCI_DEVICE Device,
                                       LPXHCI_USB_DEVICE UsbDevice,
                                       LPXHCI_USB_ENDPOINT Endpoint,
                                       PHYSICAL BufferPhysical,
                                       LINEAR BufferLinear,
                                       UINT Length,
                                       BOOL DirectionIn,
                                       U8 ScsiOpCode) {
    U32 Suppressed = 0;
    for (UINT Attempt = 0; Attempt < USB_MASS_STORAGE_BULK_RETRIES; Attempt++) {
        U32 Completion = 0;
        if (ScsiOpCode == USB_SCSI_READ_CAPACITY_10 && USBStorageShouldTraceTransfer(&Suppressed)) {
            DEBUG(TEXT("[USBStorageBulkTransfer] Op=%x Attempt=%u/%u Slot=%x Ep=%x Dci=%u DirIn=%u Len=%u suppressed=%u"),
                  (U32)ScsiOpCode,
                  Attempt + 1,
                  USB_MASS_STORAGE_BULK_RETRIES,
                  (U32)UsbDevice->SlotId,
                  (U32)Endpoint->Address,
                  (U32)Endpoint->Dci,
                  (U32)(DirectionIn != FALSE),
                  Length,
                  Suppressed);
        }

        if (!USBStorageBulkTransferOnce(Device, UsbDevice, Endpoint, BufferPhysical, BufferLinear,
                                            Length,
                                            DirectionIn,
                                            USB_MASS_STORAGE_BULK_TIMEOUT_MILLISECONDS,
                                            ScsiOpCode,
                                            &Completion)) {
            if (USBStorageShouldTraceTransfer(&Suppressed)) {
                DEBUG(TEXT("[USBStorageBulkTransfer] Attempt=%u/%u failed Slot=%x Port=%u Addr=%u Ep=%x DirIn=%u suppressed=%u"),
                      Attempt + 1,
                      USB_MASS_STORAGE_BULK_RETRIES,
                      (U32)UsbDevice->SlotId,
                      (U32)UsbDevice->PortNumber,
                      (U32)UsbDevice->Address,
                      (U32)Endpoint->Address,
                      (U32)(DirectionIn != FALSE),
                      Suppressed);
            }
            if (!XHCI_ResetTransferEndpoint(Device, UsbDevice, Endpoint, FALSE)) {
                if (USBStorageShouldTraceTransfer(&Suppressed)) {
                    DEBUG(TEXT("[USBStorageBulkTransfer] xHCI endpoint reset failed Slot=%x Dci=%u Ep=%x suppressed=%u"),
                          (U32)UsbDevice->SlotId,
                          (U32)Endpoint->Dci,
                          (U32)Endpoint->Address,
                          Suppressed);
                }
            }
            (void)USBStorageClearEndpointHalt(Device, UsbDevice, Endpoint->Address);
            continue;
        }

        if (Completion == XHCI_COMPLETION_SUCCESS || Completion == XHCI_COMPLETION_SHORT_PACKET) {
            return TRUE;
        }

        if (Completion == XHCI_COMPLETION_STALL_ERROR) {
            if (!XHCI_ResetTransferEndpoint(Device, UsbDevice, Endpoint, TRUE)) {
                if (USBStorageShouldTraceTransfer(&Suppressed)) {
                    DEBUG(TEXT("[USBStorageBulkTransfer] xHCI endpoint reset failed after stall Slot=%x Dci=%u Ep=%x suppressed=%u"),
                          (U32)UsbDevice->SlotId,
                          (U32)Endpoint->Dci,
                          (U32)Endpoint->Address,
                          Suppressed);
                }
            }
            (void)USBStorageClearEndpointHalt(Device, UsbDevice, Endpoint->Address);
            continue;
        }

        if (USBStorageShouldTraceTransfer(&Suppressed)) {
            DEBUG(TEXT("[USBStorageBulkTransfer] Completion=%x Attempt=%u/%u Slot=%x Port=%u Addr=%u Ep=%x DirIn=%u Len=%u suppressed=%u"),
                  Completion,
                  Attempt + 1,
                  USB_MASS_STORAGE_BULK_RETRIES,
                  (U32)UsbDevice->SlotId,
                  (U32)UsbDevice->PortNumber,
                  (U32)UsbDevice->Address,
                  (U32)Endpoint->Address,
                  (U32)(DirectionIn != FALSE),
                  Length,
                  Suppressed);
        }
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
static BOOL USBStorageBotCommand(LPUSB_MASS_STORAGE_DEVICE Device,
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
    Device->LastScsiOpCode = CommandBlock[0];
    Device->LastBotStage = 1;
    Device->LastCswStatus = 0xFF;
    Device->LastCswResidue = 0;

    DEBUG(TEXT("[USBStorageBotCommand] Op=%x CdbLen=%u DataLen=%u DirIn=%u Tag=%x Slot=%x Port=%u Addr=%u OutEp=%x InEp=%x"),
          (U32)CommandBlock[0],
          (U32)CommandBlockLength,
          DataLength,
          (U32)(DirectionIn != FALSE),
          (U32)CommandBlockWrapper->Tag,
          (U32)Device->UsbDevice->SlotId,
          (U32)Device->UsbDevice->PortNumber,
          (U32)Device->UsbDevice->Address,
          (U32)Device->BulkOutEndpoint->Address,
          (U32)Device->BulkInEndpoint->Address);

    if (!USBStorageBulkTransfer(Device->Controller, Device->UsbDevice, Device->BulkOutEndpoint,
                                    Device->InputOutputBufferPhysical, Device->InputOutputBufferLinear,
                                    USB_MASS_STORAGE_COMMAND_BLOCK_LENGTH, FALSE, CommandBlock[0])) {
        Device->LastBotStage = 2;
        ERROR(TEXT("[USBStorageBotCommand] CBW send failed Op=%x Tag=%x"), (U32)CommandBlock[0], (U32)CommandBlockWrapper->Tag);
        return FALSE;
    }

    if (DataLength > 0) {
        Device->LastBotStage = 3;
        if (!USBStorageBulkTransfer(Device->Controller, Device->UsbDevice,
                                        DirectionIn ? Device->BulkInEndpoint : Device->BulkOutEndpoint,
                                        Device->InputOutputBufferPhysical, Device->InputOutputBufferLinear,
                                        DataLength, DirectionIn, CommandBlock[0])) {
            Device->LastBotStage = 4;
            ERROR(TEXT("[USBStorageBotCommand] Data stage failed Op=%x Tag=%x DirIn=%u Len=%u"),
                  (U32)CommandBlock[0],
                  (U32)CommandBlockWrapper->Tag,
                  (U32)(DirectionIn != FALSE),
                  DataLength);
            return FALSE;
        }

        if (DirectionIn && DataOut != NULL) {
            MemoryCopy(DataOut, (LPVOID)Device->InputOutputBufferLinear, DataLength);
        }
    }

    USB_MASS_STORAGE_COMMAND_STATUS_WRAPPER* CommandStatusWrapper =
        (USB_MASS_STORAGE_COMMAND_STATUS_WRAPPER*)Device->InputOutputBufferLinear;
    Device->LastBotStage = 5;
    if (!USBStorageBulkTransfer(Device->Controller, Device->UsbDevice, Device->BulkInEndpoint,
                                    Device->InputOutputBufferPhysical, Device->InputOutputBufferLinear,
                                    USB_MASS_STORAGE_COMMAND_STATUS_LENGTH, TRUE, CommandBlock[0])) {
        Device->LastBotStage = 6;
        ERROR(TEXT("[USBStorageBotCommand] CSW read failed Op=%x Tag=%x"), (U32)CommandBlock[0], (U32)CommandBlockWrapper->Tag);
        return FALSE;
    }
    Device->LastBotStage = 7;
    Device->LastCswStatus = CommandStatusWrapper->Status;
    Device->LastCswResidue = CommandStatusWrapper->DataResidue;

    if (CommandStatusWrapper->Signature != USB_MASS_STORAGE_COMMAND_STATUS_SIGNATURE ||
        CommandStatusWrapper->Tag != CommandBlockWrapper->Tag) {
        ERROR(TEXT("[USBStorageBotCommand] Invalid CSW Op=%x Sig=%x Tag=%x ExpectedTag=%x Status=%x Residue=%u"),
              (U32)CommandBlock[0],
              (U32)CommandStatusWrapper->Signature,
              (U32)CommandStatusWrapper->Tag,
              (U32)CommandBlockWrapper->Tag,
              (U32)CommandStatusWrapper->Status,
              (U32)CommandStatusWrapper->DataResidue);
        return FALSE;
    }

    if (CommandStatusWrapper->Status != 0) {
        WARNING(TEXT("[USBStorageBotCommand] CSW status=%x residue=%u Op=%x Tag=%x"),
                (U32)CommandStatusWrapper->Status,
                (U32)CommandStatusWrapper->DataResidue,
                (U32)CommandBlock[0],
                (U32)CommandBlockWrapper->Tag);
        return FALSE;
    }

    Device->LastBotStage = 8;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Run a SCSI INQUIRY command and log basic identification.
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
BOOL USBStorageInquiry(LPUSB_MASS_STORAGE_DEVICE Device) {
    U8 CommandBlock[6];
    U8 InquiryData[36];
    STR Vendor[9];
    STR Product[17];

    MemorySet(CommandBlock, 0, sizeof(CommandBlock));
    CommandBlock[0] = USB_SCSI_INQUIRY;
    CommandBlock[4] = (U8)sizeof(InquiryData);

    if (!USBStorageBotCommand(Device, CommandBlock, sizeof(CommandBlock),
                                  sizeof(InquiryData), TRUE, InquiryData)) {
        return FALSE;
    }

    MemorySet(Vendor, 0, sizeof(Vendor));
    MemorySet(Product, 0, sizeof(Product));
    MemoryCopy(Vendor, &InquiryData[8], 8);
    MemoryCopy(Product, &InquiryData[16], 16);

    DEBUG(TEXT("[USBStorageInquiry] Vendor=%s Product=%s"), Vendor, Product);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Run SCSI REQUEST SENSE and log sense data.
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
BOOL USBStorageRequestSense(LPUSB_MASS_STORAGE_DEVICE Device) {
    U8 CommandBlock[6];
    U8 SenseData[18];
    U8 ResponseCode;
    U8 SenseKey;
    U8 AdditionalSenseCode;
    U8 AdditionalSenseCodeQualifier;

    MemorySet(CommandBlock, 0, sizeof(CommandBlock));
    CommandBlock[0] = USB_SCSI_REQUEST_SENSE;
    CommandBlock[4] = (U8)sizeof(SenseData);

    if (!USBStorageBotCommand(Device, CommandBlock, sizeof(CommandBlock),
                              sizeof(SenseData), TRUE, SenseData)) {
        WARNING(TEXT("[USBStorageRequestSense] REQUEST SENSE failed LastOp=%x Stage=%u LastCSW=%x Residue=%u"),
                (U32)Device->LastScsiOpCode,
                (U32)Device->LastBotStage,
                (U32)Device->LastCswStatus,
                Device->LastCswResidue);
        return FALSE;
    }

    ResponseCode = (U8)(SenseData[0] & 0x7F);
    SenseKey = (U8)(SenseData[2] & 0x0F);
    AdditionalSenseCode = SenseData[12];
    AdditionalSenseCodeQualifier = SenseData[13];

    WARNING(TEXT("[USBStorageRequestSense] Response=%x SenseKey=%x ASC=%x ASCQ=%x LastOp=%x"),
            (U32)ResponseCode,
            (U32)SenseKey,
            (U32)AdditionalSenseCode,
            (U32)AdditionalSenseCodeQualifier,
            (U32)Device->LastScsiOpCode);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Run SCSI READ CAPACITY(10) and capture block geometry.
 * @param Device USB mass storage device context.
 * @return TRUE on success.
 */
BOOL USBStorageReadCapacity(LPUSB_MASS_STORAGE_DEVICE Device) {
    U8 CommandBlock[10];
    U8 CapacityData[8];
    U32 LastLogicalBlockAddress;
    UINT BlockSize;
    U32 TemporaryValue;

    MemorySet(CommandBlock, 0, sizeof(CommandBlock));
    CommandBlock[0] = USB_SCSI_READ_CAPACITY_10;

    if (!USBStorageBotCommand(Device, CommandBlock, sizeof(CommandBlock),
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
        ERROR(TEXT("[USBStorageReadCapacity] Device too large for READ CAPACITY(10)"));
        return FALSE;
    }

    if (BlockSize != 512 && BlockSize != 4096) {
        ERROR(TEXT("[USBStorageReadCapacity] Unsupported block size %u"), BlockSize);
        return FALSE;
    }

    Device->BlockCount = (UINT)LastLogicalBlockAddress + 1U;
    Device->BlockSize = BlockSize;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Build a SCSI READ(10) command block.
 * @param CommandBlock Output command block buffer.
 * @param CommandBlockLength Command block buffer length in bytes.
 * @param LogicalBlockAddress Starting logical block address.
 * @param TransferBlocks Block count to read.
 * @return TRUE on success.
 */
static BOOL USBStorageBuildRead10(U8* CommandBlock,
                                  UINT CommandBlockLength,
                                  UINT LogicalBlockAddress,
                                  UINT TransferBlocks) {
    if (CommandBlock == NULL || CommandBlockLength < USB_SCSI_READ_10_COMMAND_BLOCK_LENGTH) {
        return FALSE;
    }

    MemorySet(CommandBlock, 0, CommandBlockLength);
    CommandBlock[0] = USB_SCSI_READ_10;
    CommandBlock[2] = (U8)((LogicalBlockAddress >> 24) & 0xFF);
    CommandBlock[3] = (U8)((LogicalBlockAddress >> 16) & 0xFF);
    CommandBlock[4] = (U8)((LogicalBlockAddress >> 8) & 0xFF);
    CommandBlock[5] = (U8)(LogicalBlockAddress & 0xFF);
    CommandBlock[7] = (U8)((TransferBlocks >> 8) & 0xFF);
    CommandBlock[8] = (U8)(TransferBlocks & 0xFF);

    return TRUE;
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
BOOL USBStorageReadBlocks(LPUSB_MASS_STORAGE_DEVICE Device,
                                     UINT LogicalBlockAddress,
                                     UINT TransferBlocks,
                                     LPVOID Output) {
    U8 CommandBlock[USB_SCSI_READ_10_COMMAND_BLOCK_LENGTH];
    UINT Length = TransferBlocks * Device->BlockSize;

    if (Output == NULL || Length == 0 || Length > PAGE_SIZE) {
        return FALSE;
    }

    if (TransferBlocks > MAX_U16) {
        return FALSE;
    }

    if (!USBStorageBuildRead10(CommandBlock,
                               sizeof(CommandBlock),
                               LogicalBlockAddress,
                               TransferBlocks)) {
        return FALSE;
    }

    return USBStorageBotCommand(Device, CommandBlock, sizeof(CommandBlock), Length, TRUE, Output);
}

/************************************************************************/
