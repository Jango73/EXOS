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

#include "drivers/XHCI.h"

#include "Base.h"
#include "CoreString.h"
#include "DriverEnum.h"
#include "Kernel.h"
#include "KernelData.h"
#include "Log.h"
#include "Memory.h"
#include "User.h"
#include "drivers/USB.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/
// xHCI capability registers

#define XHCI_CAPLENGTH 0x00
#define XHCI_HCSPARAMS1 0x04
#define XHCI_HCSPARAMS2 0x08
#define XHCI_HCSPARAMS3 0x0C
#define XHCI_HCCPARAMS1 0x10
#define XHCI_DBOFF 0x14
#define XHCI_RTSOFF 0x18
#define XHCI_HCCPARAMS2 0x1C

#define XHCI_HCSPARAMS1_MAXSLOTS_MASK 0x000000FF
#define XHCI_HCSPARAMS1_MAXINTRS_MASK 0x0007FF00
#define XHCI_HCSPARAMS1_MAXINTRS_SHIFT 8
#define XHCI_HCSPARAMS1_MAXPORTS_MASK 0xFF000000
#define XHCI_HCSPARAMS1_MAXPORTS_SHIFT 24
#define XHCI_HCSPARAMS1_PPC 0x00000010

#define XHCI_HCCPARAMS1_AC64 0x00000001
#define XHCI_HCCPARAMS1_CSZ 0x00000004

/************************************************************************/
// xHCI operational registers (offset from operational base)

#define XHCI_OP_USBCMD 0x00
#define XHCI_OP_USBSTS 0x04
#define XHCI_OP_PAGESIZE 0x08
#define XHCI_OP_DNCTRL 0x14
#define XHCI_OP_CRCR 0x18
#define XHCI_OP_DCBAAP 0x30
#define XHCI_OP_CONFIG 0x38

#define XHCI_USBCMD_RS 0x00000001
#define XHCI_USBCMD_HCRST 0x00000002

#define XHCI_USBSTS_HCH 0x00000001
#define XHCI_USBSTS_CNR 0x00000800

/************************************************************************/
// xHCI port registers (offset from operational base)

#define XHCI_PORTSC_BASE 0x400
#define XHCI_PORTSC_STRIDE 0x10

#define XHCI_PORTSC_CCS 0x00000001
#define XHCI_PORTSC_PED 0x00000002
#define XHCI_PORTSC_PR 0x00000010
#define XHCI_PORTSC_PP 0x00000200
#define XHCI_PORTSC_PLS_MASK 0x000001E0
#define XHCI_PORTSC_SPEED_MASK 0x00003C00
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_W1C_MASK 0x00FE0000

/************************************************************************/
// xHCI runtime registers

#define XHCI_RT_MFINDEX 0x00
#define XHCI_RT_INTERRUPTER_BASE 0x20
#define XHCI_RT_INTERRUPTER_STRIDE 0x20

#define XHCI_IMAN 0x00
#define XHCI_IMOD 0x04
#define XHCI_ERSTSZ 0x08
#define XHCI_ERSTBA 0x10
#define XHCI_ERDP 0x18

/************************************************************************/
// xHCI doorbell registers

#define XHCI_DOORBELL_TARGET_MASK 0x000000FF

/************************************************************************/
// TRB definitions

#define XHCI_TRB_TYPE_SHIFT 10
#define XHCI_TRB_TYPE_LINK 6

#define XHCI_TRB_CYCLE 0x00000001
#define XHCI_TRB_TOGGLE_CYCLE 0x00000002
#define XHCI_TRB_IOC 0x00000020
#define XHCI_TRB_IDT 0x00000040
#define XHCI_TRB_DIR_IN 0x00010000

#define XHCI_TRB_TYPE_SETUP_STAGE 2
#define XHCI_TRB_TYPE_DATA_STAGE 3
#define XHCI_TRB_TYPE_STATUS_STAGE 4
#define XHCI_TRB_TYPE_ENABLE_SLOT 9
#define XHCI_TRB_TYPE_ADDRESS_DEVICE 11
#define XHCI_TRB_TYPE_EVALUATE_CONTEXT 13
#define XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT 33
#define XHCI_TRB_TYPE_TRANSFER_EVENT 32

#define XHCI_COMPLETION_SUCCESS 1
#define XHCI_COMPLETION_STALL_ERROR 6
#define XHCI_COMPLETION_SHORT_PACKET 13

#define XHCI_EP0_DCI 1

#define XHCI_COMMAND_RING_TRBS 256
#define XHCI_EVENT_RING_TRBS 256
#define XHCI_TRANSFER_RING_TRBS 256

/************************************************************************/

#define XHCI_RESET_TIMEOUT 1000000
#define XHCI_HALT_TIMEOUT 1000000
#define XHCI_RUN_TIMEOUT 1000000
#define XHCI_PORT_RESET_TIMEOUT 1000000
#define XHCI_EVENT_TIMEOUT_MS 1000

/************************************************************************/

typedef struct tag_XHCI_TRB {
    U32 Dword0;
    U32 Dword1;
    U32 Dword2;
    U32 Dword3;
} XHCI_TRB, *LPXHCI_TRB;

typedef struct tag_XHCI_CONTEXT_32 {
    U32 Dword0;
    U32 Dword1;
    U32 Dword2;
    U32 Dword3;
    U32 Dword4;
    U32 Dword5;
    U32 Dword6;
    U32 Dword7;
} XHCI_CONTEXT_32, *LPXHCI_CONTEXT_32;

typedef struct tag_XHCI_USB_ENDPOINT {
    U8 Address;
    U8 Attributes;
    U16 MaxPacketSize;
    U8 Interval;
} XHCI_USB_ENDPOINT, *LPXHCI_USB_ENDPOINT;

typedef struct tag_XHCI_USB_INTERFACE {
    U8 Number;
    U8 AlternateSetting;
    U8 NumEndpoints;
    U8 InterfaceClass;
    U8 InterfaceSubClass;
    U8 InterfaceProtocol;
    U8 InterfaceIndex;
    UINT EndpointCount;
    LPXHCI_USB_ENDPOINT Endpoints;
} XHCI_USB_INTERFACE, *LPXHCI_USB_INTERFACE;

typedef struct tag_XHCI_USB_CONFIGURATION {
    U8 ConfigurationValue;
    U8 ConfigurationIndex;
    U8 Attributes;
    U8 MaxPower;
    U8 NumInterfaces;
    U16 TotalLength;
    UINT InterfaceCount;
    LPXHCI_USB_INTERFACE Interfaces;
} XHCI_USB_CONFIGURATION, *LPXHCI_USB_CONFIGURATION;

typedef struct tag_XHCI_ERST_ENTRY {
    U64 SegmentBase;
    U16 SegmentSize;
    U16 Reserved;
    U32 Reserved2;
} XHCI_ERST_ENTRY, *LPXHCI_ERST_ENTRY;

typedef struct tag_XHCI_USB_DEVICE {
    BOOL Present;
    U8 PortNumber;
    U8 SlotId;
    USB_ADDRESS Address;
    U8 SpeedId;
    U16 MaxPacketSize0;
    USB_DEVICE_DESCRIPTOR DeviceDescriptor;
    U8 SelectedConfigValue;
    U8 StringManufacturer;
    U8 StringProduct;
    U8 StringSerial;
    UINT ConfigCount;
    LPXHCI_USB_CONFIGURATION Configs;
    PHYSICAL InputContextPhysical;
    LINEAR InputContextLinear;
    PHYSICAL DeviceContextPhysical;
    LINEAR DeviceContextLinear;
    PHYSICAL TransferRingPhysical;
    LINEAR TransferRingLinear;
    U32 TransferRingCycleState;
    U32 TransferRingEnqueueIndex;
} XHCI_USB_DEVICE, *LPXHCI_USB_DEVICE;

struct tag_XHCI_DEVICE {
    PCI_DEVICE_FIELDS

    LINEAR MmioBase;
    U32 MmioSize;

    U8 CapLength;
    U16 HciVersion;
    U8 MaxSlots;
    U8 MaxPorts;
    U16 MaxInterrupters;
    U32 HccParams1;
    U32 ContextSize;

    LINEAR OpBase;
    LINEAR RuntimeBase;
    LINEAR DoorbellBase;

    PHYSICAL DcbaaPhysical;
    LINEAR DcbaaLinear;

    PHYSICAL CommandRingPhysical;
    LINEAR CommandRingLinear;
    U32 CommandRingCycleState;
    U32 CommandRingEnqueueIndex;

    PHYSICAL EventRingPhysical;
    LINEAR EventRingLinear;
    PHYSICAL EventRingTablePhysical;
    LINEAR EventRingTableLinear;

    U32 EventRingDequeueIndex;
    U32 EventRingCycleState;

    LPXHCI_USB_DEVICE UsbDevices;
};

/************************************************************************/

#pragma pack(pop)

/************************************************************************/
// MMIO access

/**
 * @brief Read a 32-bit xHCI MMIO register.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @return Register value.
 */
static U32 XHCI_Read32(LINEAR Base, U32 Offset) {
    return *(volatile U32 *)((U8 *)Base + Offset);
}

/************************************************************************/

/**
 * @brief Write a 32-bit xHCI MMIO register.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @param Value Value to write.
 */
static void XHCI_Write32(LINEAR Base, U32 Offset, U32 Value) {
    *(volatile U32 *)((U8 *)Base + Offset) = Value;
}

/************************************************************************/

/**
 * @brief Write a 64-bit xHCI MMIO register.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @param Value Value to write.
 */
static void XHCI_Write64(LINEAR Base, U32 Offset, U64 Value) {
    XHCI_Write32(Base, Offset, U64_Low32(Value));
    XHCI_Write32(Base, (U32)(Offset + 4), U64_High32(Value));
}

/************************************************************************/

/**
 * @brief Get pointer to an xHCI context within a context array.
 * @param Base Base of the context array.
 * @param ContextSize Context size in bytes.
 * @param Index Context index.
 * @return Pointer to context.
 */
static LPXHCI_CONTEXT_32 XHCI_GetContextPointer(LINEAR Base, U32 ContextSize, U32 Index) {
    return (LPXHCI_CONTEXT_32)((U8 *)Base + (ContextSize * Index));
}

/************************************************************************/

/**
 * @brief Extract xHCI TRB type from Dword3.
 * @param Dword3 TRB Dword3 value.
 * @return TRB type.
 */
static U32 XHCI_GetTrbType(U32 Dword3) {
    return (Dword3 >> XHCI_TRB_TYPE_SHIFT) & 0x3F;
}

/************************************************************************/

/**
 * @brief Extract xHCI completion code from Dword2.
 * @param Dword2 TRB Dword2 value.
 * @return Completion code.
 */
static U32 XHCI_GetCompletionCode(U32 Dword2) {
    return (Dword2 >> 24) & 0xFF;
}

/************************************************************************/

/**
 * @brief Ring an xHCI doorbell.
 * @param Device xHCI device.
 * @param DoorbellIndex Doorbell index (slot ID).
 * @param Target Target endpoint.
 */
static void XHCI_RingDoorbell(LPXHCI_DEVICE Device, U32 DoorbellIndex, U32 Target) {
    U32 Value = Target & XHCI_DOORBELL_TARGET_MASK;
    XHCI_Write32(Device->DoorbellBase, DoorbellIndex * sizeof(U32), Value);
}

/************************************************************************/

/**
 * @brief Enqueue a TRB on the command ring.
 * @param Device xHCI device.
 * @param Trb TRB to enqueue.
 * @param PhysicalOut Receives physical address of the enqueued TRB.
 * @return TRUE on success.
 */
static BOOL XHCI_CommandRingEnqueue(LPXHCI_DEVICE Device, const XHCI_TRB* Trb, U64* PhysicalOut) {
    if (Device == NULL || Trb == NULL) {
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)Device->CommandRingLinear;
    U32 Index = Device->CommandRingEnqueueIndex;
    U32 LinkIndex = XHCI_COMMAND_RING_TRBS - 1;

    if (Index >= LinkIndex) {
        Index = 0;
        Device->CommandRingEnqueueIndex = 0;
    }

    XHCI_TRB Local = *Trb;
    Local.Dword3 |= (Device->CommandRingCycleState ? XHCI_TRB_CYCLE : 0);

    Ring[Index] = Local;

    if (PhysicalOut != NULL) {
        *PhysicalOut = U64_FromUINT(Device->CommandRingPhysical + (Index * sizeof(XHCI_TRB)));
    }

    Index++;
    if (Index == LinkIndex) {
        Ring[LinkIndex].Dword3 = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                                 (Device->CommandRingCycleState ? XHCI_TRB_CYCLE : 0) |
                                 XHCI_TRB_TOGGLE_CYCLE;
        Device->CommandRingCycleState ^= 1;
        Index = 0;
    }

    Device->CommandRingEnqueueIndex = Index;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Enqueue a TRB on a transfer ring.
 * @param UsbDevice USB device state.
 * @param Trb TRB to enqueue.
 * @param PhysicalOut Receives physical address of the enqueued TRB.
 * @return TRUE on success.
 */
static BOOL XHCI_TransferRingEnqueue(LPXHCI_USB_DEVICE UsbDevice, const XHCI_TRB* Trb, U64* PhysicalOut) {
    if (UsbDevice == NULL || Trb == NULL) {
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)UsbDevice->TransferRingLinear;
    U32 Index = UsbDevice->TransferRingEnqueueIndex;
    U32 LinkIndex = XHCI_TRANSFER_RING_TRBS - 1;

    if (Index >= LinkIndex) {
        Index = 0;
        UsbDevice->TransferRingEnqueueIndex = 0;
    }

    XHCI_TRB Local = *Trb;
    Local.Dword3 |= (UsbDevice->TransferRingCycleState ? XHCI_TRB_CYCLE : 0);

    Ring[Index] = Local;

    if (PhysicalOut != NULL) {
        *PhysicalOut = U64_FromUINT(UsbDevice->TransferRingPhysical + (Index * sizeof(XHCI_TRB)));
    }

    Index++;
    if (Index == LinkIndex) {
        Ring[LinkIndex].Dword3 = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) |
                                 (UsbDevice->TransferRingCycleState ? XHCI_TRB_CYCLE : 0) |
                                 XHCI_TRB_TOGGLE_CYCLE;
        UsbDevice->TransferRingCycleState ^= 1;
        Index = 0;
    }

    UsbDevice->TransferRingEnqueueIndex = Index;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Dequeue one event TRB if available.
 * @param Device xHCI device.
 * @param EventOut Receives the event TRB.
 * @return TRUE if an event was dequeued.
 */
static BOOL XHCI_DequeueEvent(LPXHCI_DEVICE Device, XHCI_TRB* EventOut) {
    if (Device == NULL || EventOut == NULL) {
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)Device->EventRingLinear;
    U32 Index = Device->EventRingDequeueIndex;
    XHCI_TRB Event = Ring[Index];

    if (((Event.Dword3 & XHCI_TRB_CYCLE) != 0) != (Device->EventRingCycleState != 0)) {
        return FALSE;
    }

    *EventOut = Event;

    Index++;
    if (Index >= XHCI_EVENT_RING_TRBS) {
        Index = 0;
        Device->EventRingCycleState ^= 1;
    }

    Device->EventRingDequeueIndex = Index;

    {
        LINEAR InterrupterBase = Device->RuntimeBase + XHCI_RT_INTERRUPTER_BASE;
        U64 Erdp = U64_FromUINT(Device->EventRingPhysical + (Index * sizeof(XHCI_TRB)));
        XHCI_Write64(InterrupterBase, XHCI_ERDP, Erdp);
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Busy-wait for a register to match a value.
 * @param Base MMIO base address.
 * @param Offset Register offset.
 * @param Mask Mask applied to register.
 * @param Value Expected value after masking.
 * @param Timeout Loop bound.
 * @return TRUE on success, FALSE on timeout.
 */
static BOOL XHCI_WaitForRegister(LINEAR Base, U32 Offset, U32 Mask, U32 Value, U32 Timeout) {
    U32 Count = 0;
    while (Count < Timeout) {
        if ((XHCI_Read32(Base, Offset) & Mask) == Value) {
            return TRUE;
        }
        Count++;
    }
    return FALSE;
}

/************************************************************************/

/**
 * @brief Allocate and map a single physical page.
 * @param Tag Allocation tag.
 * @param PhysicalOut Receives physical address.
 * @param LinearOut Receives linear address.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_AllocPage(LPCSTR Tag, PHYSICAL *PhysicalOut, LINEAR *LinearOut) {
    if (PhysicalOut == NULL || LinearOut == NULL) {
        return FALSE;
    }

    PHYSICAL Physical = AllocPhysicalPage();
    if (Physical == 0) {
        return FALSE;
    }

    LINEAR Linear = AllocKernelRegion(Physical, PAGE_SIZE, ALLOC_PAGES_COMMIT | ALLOC_PAGES_READWRITE, Tag);
    if (Linear == 0) {
        FreePhysicalPage(Physical);
        return FALSE;
    }

    MemorySet((LPVOID)Linear, 0, PAGE_SIZE);

    *PhysicalOut = Physical;
    *LinearOut = Linear;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Free USB configuration tree.
 * @param UsbDevice USB device state.
 */
static void XHCI_FreeUsbTree(LPXHCI_USB_DEVICE UsbDevice) {
    if (UsbDevice == NULL) {
        return;
    }

    if (UsbDevice->Configs != NULL) {
        for (UINT ConfigIndex = 0; ConfigIndex < UsbDevice->ConfigCount; ConfigIndex++) {
            LPXHCI_USB_CONFIGURATION Config = &UsbDevice->Configs[ConfigIndex];
            if (Config->Interfaces != NULL) {
                for (UINT InterfaceIndex = 0; InterfaceIndex < Config->InterfaceCount; InterfaceIndex++) {
                    LPXHCI_USB_INTERFACE Interface = &Config->Interfaces[InterfaceIndex];
                    if (Interface->Endpoints != NULL) {
                        KernelHeapFree(Interface->Endpoints);
                        Interface->Endpoints = NULL;
                    }
                }
                KernelHeapFree(Config->Interfaces);
                Config->Interfaces = NULL;
            }
        }
        KernelHeapFree(UsbDevice->Configs);
        UsbDevice->Configs = NULL;
    }

    UsbDevice->ConfigCount = 0;
    UsbDevice->SelectedConfigValue = 0;
}

/************************************************************************/

/**
 * @brief Free xHCI allocations and MMIO mapping.
 * @param Device xHCI device.
 */
static void XHCI_FreeResources(LPXHCI_DEVICE Device) {
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        if (Device->UsbDevices != NULL) {
            for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
                LPXHCI_USB_DEVICE UsbDevice = &Device->UsbDevices[PortIndex];
                XHCI_FreeUsbTree(UsbDevice);
                if (UsbDevice->TransferRingLinear) {
                    FreeRegion(UsbDevice->TransferRingLinear, PAGE_SIZE);
                    UsbDevice->TransferRingLinear = 0;
                }
                if (UsbDevice->TransferRingPhysical) {
                    FreePhysicalPage(UsbDevice->TransferRingPhysical);
                    UsbDevice->TransferRingPhysical = 0;
                }
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
            }
            KernelHeapFree(Device->UsbDevices);
            Device->UsbDevices = NULL;
        }
        if (Device->EventRingTableLinear) {
            FreeRegion(Device->EventRingTableLinear, PAGE_SIZE);
            Device->EventRingTableLinear = 0;
        }
        if (Device->EventRingTablePhysical) {
            FreePhysicalPage(Device->EventRingTablePhysical);
            Device->EventRingTablePhysical = 0;
        }
        if (Device->EventRingLinear) {
            FreeRegion(Device->EventRingLinear, PAGE_SIZE);
            Device->EventRingLinear = 0;
        }
        if (Device->EventRingPhysical) {
            FreePhysicalPage(Device->EventRingPhysical);
            Device->EventRingPhysical = 0;
        }
        if (Device->CommandRingLinear) {
            FreeRegion(Device->CommandRingLinear, PAGE_SIZE);
            Device->CommandRingLinear = 0;
        }
        if (Device->CommandRingPhysical) {
            FreePhysicalPage(Device->CommandRingPhysical);
            Device->CommandRingPhysical = 0;
        }
        if (Device->DcbaaLinear) {
            FreeRegion(Device->DcbaaLinear, PAGE_SIZE);
            Device->DcbaaLinear = 0;
        }
        if (Device->DcbaaPhysical) {
            FreePhysicalPage(Device->DcbaaPhysical);
            Device->DcbaaPhysical = 0;
        }
        if (Device->MmioBase != 0 && Device->MmioSize != 0) {
            UnMapIOMemory(Device->MmioBase, Device->MmioSize);
            Device->MmioBase = 0;
            Device->MmioSize = 0;
        }
    }
}

/************************************************************************/

/**
 * @brief Convert an xHCI speed ID to a human readable name.
 * @param SpeedId Raw PORTSC speed value.
 * @return Speed string.
 */
static LPCSTR XHCI_SpeedToString(U32 SpeedId) {
    switch (SpeedId) {
        case 1:
            return TEXT("FS");
        case 2:
            return TEXT("LS");
        case 3:
            return TEXT("HS");
        case 4:
            return TEXT("SS");
        case 5:
            return TEXT("SS+");
        default:
            return TEXT("Unknown");
    }
}

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
            DEBUG(TEXT("[XHCI_ForEachDescriptor] Invalid descriptor length=%u type=%u"), DescLength, DescType);
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

typedef struct tag_XHCI_DESC_COUNT_INTERFACE {
    UINT ConfigIndex;
    UINT ConfigCount;
    UINT InterfaceCount;
    UINT* ConfigInterfaceCounts;
} XHCI_DESC_COUNT_INTERFACE, *LPXHCI_DESC_COUNT_INTERFACE;

static BOOL XHCI_CountInterfaceCallback(const U8* Descriptor, U8 Length, void* Context) {
    LPXHCI_DESC_COUNT_INTERFACE Ctx = (LPXHCI_DESC_COUNT_INTERFACE)Context;
    UNUSED(Length);

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_CONFIGURATION) {
        if (Ctx->ConfigIndex < Ctx->ConfigCount) {
            Ctx->ConfigIndex++;
        }
        return TRUE;
    }

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_INTERFACE) {
        if (Ctx->ConfigIndex == 0 || Ctx->ConfigIndex > Ctx->ConfigCount) {
            return TRUE;
        }
        Ctx->ConfigInterfaceCounts[Ctx->ConfigIndex - 1]++;
        Ctx->InterfaceCount++;
    }

    return TRUE;
}

/************************************************************************/

typedef struct tag_XHCI_DESC_COUNT_ENDPOINT {
    UINT InterfaceIndex;
    UINT InterfaceCount;
    UINT* InterfaceEndpointCounts;
} XHCI_DESC_COUNT_ENDPOINT, *LPXHCI_DESC_COUNT_ENDPOINT;

static BOOL XHCI_CountEndpointCallback(const U8* Descriptor, U8 Length, void* Context) {
    LPXHCI_DESC_COUNT_ENDPOINT Ctx = (LPXHCI_DESC_COUNT_ENDPOINT)Context;
    UNUSED(Length);

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_INTERFACE) {
        if (Ctx->InterfaceIndex < Ctx->InterfaceCount) {
            Ctx->InterfaceIndex++;
        }
        return TRUE;
    }

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_ENDPOINT) {
        if (Ctx->InterfaceIndex == 0 || Ctx->InterfaceIndex > Ctx->InterfaceCount) {
            return TRUE;
        }
        Ctx->InterfaceEndpointCounts[Ctx->InterfaceIndex - 1]++;
    }

    return TRUE;
}

/************************************************************************/

typedef struct tag_XHCI_DESC_FILL_CONTEXT {
    LPXHCI_USB_DEVICE UsbDevice;
    LPXHCI_USB_CONFIGURATION Configs;
    UINT ConfigCount;
    UINT ConfigIndex;
    UINT InterfaceGlobalIndex;
    UINT InterfaceIndexInConfig;
    UINT EndpointIndexInInterface;
    UINT* ConfigInterfaceCounts;
    UINT* InterfaceEndpointCounts;
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
        Config->InterfaceCount = Ctx->ConfigInterfaceCounts[Ctx->ConfigIndex];

        if (Config->InterfaceCount > 0) {
            Config->Interfaces = (LPXHCI_USB_INTERFACE)KernelHeapAlloc(sizeof(XHCI_USB_INTERFACE) * Config->InterfaceCount);
            if (Config->Interfaces == NULL) {
                ERROR(TEXT("[XHCI_FillDescriptorCallback] Interface allocation failed"));
                return FALSE;
            }
            MemorySet(Config->Interfaces, 0, sizeof(XHCI_USB_INTERFACE) * Config->InterfaceCount);
        }

        Ctx->InterfaceIndexInConfig = 0;
        Ctx->ConfigIndex++;
        return TRUE;
    }

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_INTERFACE) {
        if (Length < sizeof(USB_INTERFACE_DESCRIPTOR) || Ctx->ConfigIndex == 0) {
            return TRUE;
        }

        LPXHCI_USB_CONFIGURATION Config = &Ctx->Configs[Ctx->ConfigIndex - 1];
        if (Ctx->InterfaceIndexInConfig >= Config->InterfaceCount) {
            return TRUE;
        }

        const USB_INTERFACE_DESCRIPTOR* IfDesc = (const USB_INTERFACE_DESCRIPTOR*)Descriptor;
        LPXHCI_USB_INTERFACE Interface = &Config->Interfaces[Ctx->InterfaceIndexInConfig];

        Interface->Number = IfDesc->InterfaceNumber;
        Interface->AlternateSetting = IfDesc->AlternateSetting;
        Interface->NumEndpoints = IfDesc->NumEndpoints;
        Interface->InterfaceClass = IfDesc->InterfaceClass;
        Interface->InterfaceSubClass = IfDesc->InterfaceSubClass;
        Interface->InterfaceProtocol = IfDesc->InterfaceProtocol;
        Interface->InterfaceIndex = IfDesc->InterfaceIndex;
        Interface->EndpointCount = Ctx->InterfaceEndpointCounts[Ctx->InterfaceGlobalIndex];

        if (Interface->EndpointCount > 0) {
            Interface->Endpoints =
                (LPXHCI_USB_ENDPOINT)KernelHeapAlloc(sizeof(XHCI_USB_ENDPOINT) * Interface->EndpointCount);
            if (Interface->Endpoints == NULL) {
                ERROR(TEXT("[XHCI_FillDescriptorCallback] Endpoint allocation failed"));
                return FALSE;
            }
            MemorySet(Interface->Endpoints, 0, sizeof(XHCI_USB_ENDPOINT) * Interface->EndpointCount);
        }

        Ctx->EndpointIndexInInterface = 0;
        Ctx->InterfaceIndexInConfig++;
        Ctx->InterfaceGlobalIndex++;
        return TRUE;
    }

    if (Descriptor[1] == USB_DESCRIPTOR_TYPE_ENDPOINT) {
        if (Length < sizeof(USB_ENDPOINT_DESCRIPTOR) || Ctx->ConfigIndex == 0) {
            return TRUE;
        }

        LPXHCI_USB_CONFIGURATION Config = &Ctx->Configs[Ctx->ConfigIndex - 1];
        if (Ctx->InterfaceIndexInConfig == 0) {
            return TRUE;
        }

        LPXHCI_USB_INTERFACE Interface = &Config->Interfaces[Ctx->InterfaceIndexInConfig - 1];
        if (Ctx->EndpointIndexInInterface >= Interface->EndpointCount) {
            return TRUE;
        }

        const USB_ENDPOINT_DESCRIPTOR* EpDesc = (const USB_ENDPOINT_DESCRIPTOR*)Descriptor;
        LPXHCI_USB_ENDPOINT Endpoint = &Interface->Endpoints[Ctx->EndpointIndexInInterface];

        Endpoint->Address = EpDesc->EndpointAddress;
        Endpoint->Attributes = EpDesc->Attributes;
        Endpoint->MaxPacketSize = EpDesc->MaxPacketSize;
        Endpoint->Interval = EpDesc->Interval;

        Ctx->EndpointIndexInInterface++;
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
    XHCI_DESC_COUNT_INTERFACE InterfaceCountContext;
    XHCI_DESC_COUNT_ENDPOINT EndpointCountContext;
    XHCI_DESC_FILL_CONTEXT FillContext;
    UINT* ConfigInterfaceCounts = NULL;
    UINT* InterfaceEndpointCounts = NULL;

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

    ConfigInterfaceCounts =
        (UINT*)KernelHeapAlloc(sizeof(UINT) * ConfigCountContext.ConfigCount);
    if (ConfigInterfaceCounts == NULL) {
        return FALSE;
    }
    MemorySet(ConfigInterfaceCounts, 0, sizeof(UINT) * ConfigCountContext.ConfigCount);

    MemorySet(&InterfaceCountContext, 0, sizeof(InterfaceCountContext));
    InterfaceCountContext.ConfigCount = ConfigCountContext.ConfigCount;
    InterfaceCountContext.ConfigInterfaceCounts = ConfigInterfaceCounts;

    if (!XHCI_ForEachDescriptor(Buffer, Length, XHCI_CountInterfaceCallback, &InterfaceCountContext)) {
        KernelHeapFree(ConfigInterfaceCounts);
        return FALSE;
    }

    if (InterfaceCountContext.InterfaceCount > 0) {
        InterfaceEndpointCounts =
            (UINT*)KernelHeapAlloc(sizeof(UINT) * InterfaceCountContext.InterfaceCount);
        if (InterfaceEndpointCounts == NULL) {
            KernelHeapFree(ConfigInterfaceCounts);
            return FALSE;
        }
        MemorySet(InterfaceEndpointCounts, 0, sizeof(UINT) * InterfaceCountContext.InterfaceCount);

        MemorySet(&EndpointCountContext, 0, sizeof(EndpointCountContext));
        EndpointCountContext.InterfaceCount = InterfaceCountContext.InterfaceCount;
        EndpointCountContext.InterfaceEndpointCounts = InterfaceEndpointCounts;

        if (!XHCI_ForEachDescriptor(Buffer, Length, XHCI_CountEndpointCallback, &EndpointCountContext)) {
            KernelHeapFree(InterfaceEndpointCounts);
            KernelHeapFree(ConfigInterfaceCounts);
            return FALSE;
        }
    }

    UsbDevice->Configs =
        (LPXHCI_USB_CONFIGURATION)KernelHeapAlloc(sizeof(XHCI_USB_CONFIGURATION) * ConfigCountContext.ConfigCount);
    if (UsbDevice->Configs == NULL) {
        if (InterfaceEndpointCounts) KernelHeapFree(InterfaceEndpointCounts);
        KernelHeapFree(ConfigInterfaceCounts);
        return FALSE;
    }
    MemorySet(UsbDevice->Configs, 0, sizeof(XHCI_USB_CONFIGURATION) * ConfigCountContext.ConfigCount);
    UsbDevice->ConfigCount = ConfigCountContext.ConfigCount;

    MemorySet(&FillContext, 0, sizeof(FillContext));
    FillContext.UsbDevice = UsbDevice;
    FillContext.Configs = UsbDevice->Configs;
    FillContext.ConfigCount = UsbDevice->ConfigCount;
    FillContext.ConfigInterfaceCounts = ConfigInterfaceCounts;
    FillContext.InterfaceEndpointCounts = InterfaceEndpointCounts;

    if (!XHCI_ForEachDescriptor(Buffer, Length, XHCI_FillDescriptorCallback, &FillContext)) {
        XHCI_FreeUsbTree(UsbDevice);
        if (InterfaceEndpointCounts) KernelHeapFree(InterfaceEndpointCounts);
        KernelHeapFree(ConfigInterfaceCounts);
        return FALSE;
    }

    if (InterfaceEndpointCounts) KernelHeapFree(InterfaceEndpointCounts);
    KernelHeapFree(ConfigInterfaceCounts);
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
/**
 * @brief Read a port status register.
 * @param Device xHCI device.
 * @param PortIndex Port index (0-based).
 * @return PORTSC value.
 */
static U32 XHCI_ReadPortStatus(LPXHCI_DEVICE Device, U32 PortIndex) {
    U32 Offset = XHCI_PORTSC_BASE + (PortIndex * XHCI_PORTSC_STRIDE);
    return XHCI_Read32(Device->OpBase, Offset);
}

/************************************************************************/

/**
 * @brief Power on a port if supported by the controller.
 * @param Device xHCI device.
 * @param PortIndex Port index (0-based).
 */
static void XHCI_PowerPort(LPXHCI_DEVICE Device, U32 PortIndex) {
    U32 Offset = XHCI_PORTSC_BASE + (PortIndex * XHCI_PORTSC_STRIDE);
    U32 PortStatus = XHCI_Read32(Device->OpBase, Offset);

    if ((PortStatus & XHCI_PORTSC_PP) != 0) {
        return;
    }

    U32 WriteValue = PortStatus | XHCI_PORTSC_PP;
    WriteValue &= ~XHCI_PORTSC_W1C_MASK;
    XHCI_Write32(Device->OpBase, Offset, WriteValue);
}

/************************************************************************/

/**
 * @brief Log port status to kernel log.
 * @param Device xHCI device.
 */
static void XHCI_LogPorts(LPXHCI_DEVICE Device) {
    for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
        U32 PortStatus = XHCI_ReadPortStatus(Device, PortIndex);
        U32 SpeedId = (PortStatus & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
        BOOL Connected = (PortStatus & XHCI_PORTSC_CCS) != 0;
        BOOL Enabled = (PortStatus & XHCI_PORTSC_PED) != 0;

        DEBUG(TEXT("[XHCI_LogPorts] Port %u CCS=%u PED=%u Speed=%s Raw=%x"),
              PortIndex + 1,
              Connected ? 1U : 0U,
              Enabled ? 1U : 0U,
              XHCI_SpeedToString(SpeedId),
              PortStatus);
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

    if (!XHCI_WaitForRegister(Device->OpBase, Offset, XHCI_PORTSC_PR, 0, XHCI_PORT_RESET_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetPort] Port %u reset timeout"), PortIndex + 1);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize a transfer ring for a USB device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_InitTransferRing(LPXHCI_USB_DEVICE UsbDevice) {
    if (!XHCI_AllocPage(TEXT("XHCI_TransferRing"), &UsbDevice->TransferRingPhysical, &UsbDevice->TransferRingLinear)) {
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)UsbDevice->TransferRingLinear;
    MemorySet(Ring, 0, PAGE_SIZE);

    U32 LinkIndex = XHCI_TRANSFER_RING_TRBS - 1;
    U64 RingAddress = U64_FromUINT(UsbDevice->TransferRingPhysical);
    Ring[LinkIndex].Dword0 = U64_Low32(RingAddress);
    Ring[LinkIndex].Dword1 = U64_High32(RingAddress);
    Ring[LinkIndex].Dword2 = 0;
    Ring[LinkIndex].Dword3 = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE;

    UsbDevice->TransferRingCycleState = 1;
    UsbDevice->TransferRingEnqueueIndex = 0;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Wait for a command completion event.
 * @param Device xHCI device.
 * @param TrbPhysical Command TRB physical address.
 * @param SlotIdOut Receives slot ID when provided.
 * @param CompletionOut Receives completion code when provided.
 * @return TRUE on success.
 */
static BOOL XHCI_WaitForCommandCompletion(LPXHCI_DEVICE Device, U64 TrbPhysical, U8* SlotIdOut, U32* CompletionOut) {
    XHCI_TRB Event;
    U32 Timeout = XHCI_EVENT_TIMEOUT_MS;

    while (Timeout > 0) {
        while (XHCI_DequeueEvent(Device, &Event)) {
            if (XHCI_GetTrbType(Event.Dword3) != XHCI_TRB_TYPE_COMMAND_COMPLETION_EVENT) {
                continue;
            }

            U64 Pointer = U64_Make(Event.Dword1, Event.Dword0);
            if (!U64_EQUAL(Pointer, TrbPhysical)) {
                continue;
            }

            if (CompletionOut != NULL) {
                *CompletionOut = XHCI_GetCompletionCode(Event.Dword2);
            }

            if (SlotIdOut != NULL) {
                *SlotIdOut = (U8)((Event.Dword3 >> 24) & 0xFF);
            }

            return TRUE;
        }

        Sleep(1);
        Timeout--;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Wait for a transfer completion event.
 * @param Device xHCI device.
 * @param TrbPhysical Status TRB physical address.
 * @param CompletionOut Receives completion code when provided.
 * @return TRUE on success.
 */
static BOOL XHCI_WaitForTransferCompletion(LPXHCI_DEVICE Device, U64 TrbPhysical, U32* CompletionOut) {
    XHCI_TRB Event;
    U32 Timeout = XHCI_EVENT_TIMEOUT_MS;

    while (Timeout > 0) {
        while (XHCI_DequeueEvent(Device, &Event)) {
            if (XHCI_GetTrbType(Event.Dword3) != XHCI_TRB_TYPE_TRANSFER_EVENT) {
                continue;
            }

            U64 Pointer = U64_Make(Event.Dword1, Event.Dword0);
            if (!U64_EQUAL(Pointer, TrbPhysical)) {
                continue;
            }

            if (CompletionOut != NULL) {
                *CompletionOut = XHCI_GetCompletionCode(Event.Dword2);
            }

            return TRUE;
        }

        Sleep(1);
        Timeout--;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Initialize USB device state for a port.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_InitUsbDeviceState(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    UNUSED(Device);
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

    return TRUE;
}

/************************************************************************/

/**
 * @brief Populate an input context for Address Device.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 */
static void XHCI_BuildInputContextForAddress(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);

    LPXHCI_CONTEXT_32 Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 0) | (1U << 1);

    LPXHCI_CONTEXT_32 Slot = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 1);
    Slot->Dword0 = ((U32)UsbDevice->SpeedId << 20) | (1U << 27);
    Slot->Dword1 = ((U32)UsbDevice->PortNumber << 16);

    LPXHCI_CONTEXT_32 Ep0 = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 2);
    Ep0->Dword1 = (4U << 3) | ((U32)UsbDevice->MaxPacketSize0 << 16);

    {
        U64 Dequeue = U64_FromUINT(UsbDevice->TransferRingPhysical);
        Ep0->Dword2 = (U32)(U64_Low32(Dequeue) & ~0xFU);
        Ep0->Dword2 |= (UsbDevice->TransferRingCycleState ? 1U : 0U);
        Ep0->Dword3 = U64_High32(Dequeue);
        Ep0->Dword4 = 8;
    }
}

/************************************************************************/

/**
 * @brief Populate an input context for updating EP0.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 */
static void XHCI_BuildInputContextForEp0(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    MemorySet((LPVOID)UsbDevice->InputContextLinear, 0, PAGE_SIZE);

    LPXHCI_CONTEXT_32 Control = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 0);
    Control->Dword1 = (1U << 1);

    LPXHCI_CONTEXT_32 Ep0 = XHCI_GetContextPointer(UsbDevice->InputContextLinear, Device->ContextSize, 2);
    Ep0->Dword1 = (4U << 3) | ((U32)UsbDevice->MaxPacketSize0 << 16);

    {
        U64 Dequeue = U64_FromUINT(UsbDevice->TransferRingPhysical);
        Ep0->Dword2 = (U32)(U64_Low32(Dequeue) & ~0xFU);
        Ep0->Dword2 |= (UsbDevice->TransferRingCycleState ? 1U : 0U);
        Ep0->Dword3 = U64_High32(Dequeue);
        Ep0->Dword4 = 8;
    }
}

/************************************************************************/

/**
 * @brief Enable a new device slot.
 * @param Device xHCI device.
 * @param SlotIdOut Receives allocated slot ID.
 * @return TRUE on success.
 */
static BOOL XHCI_EnableSlot(LPXHCI_DEVICE Device, U8* SlotIdOut) {
    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U8 SlotId = 0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword3 = (XHCI_TRB_TYPE_ENABLE_SLOT << XHCI_TRB_TYPE_SHIFT);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, &SlotId, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        ERROR(TEXT("[XHCI_EnableSlot] Completion code %u"), Completion);
        return FALSE;
    }

    if (SlotIdOut != NULL) {
        *SlotIdOut = SlotId;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Address a device with a prepared input context.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_AddressDevice(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword3 = (XHCI_TRB_TYPE_ADDRESS_DEVICE << XHCI_TRB_TYPE_SHIFT) | ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        ERROR(TEXT("[XHCI_AddressDevice] Completion code %u"), Completion);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Evaluate context to update EP0 parameters.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @return TRUE on success.
 */
static BOOL XHCI_EvaluateContext(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice) {
    XHCI_TRB Trb;
    U64 TrbPhysical = U64_0;
    U32 Completion = 0;

    MemorySet(&Trb, 0, sizeof(Trb));
    Trb.Dword0 = U64_Low32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword1 = U64_High32(U64_FromUINT(UsbDevice->InputContextPhysical));
    Trb.Dword3 = (XHCI_TRB_TYPE_EVALUATE_CONTEXT << XHCI_TRB_TYPE_SHIFT) | ((U32)UsbDevice->SlotId << 24);

    if (!XHCI_CommandRingEnqueue(Device, &Trb, &TrbPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, 0, 0);

    if (!XHCI_WaitForCommandCompletion(Device, TrbPhysical, NULL, &Completion)) {
        return FALSE;
    }

    if (Completion != XHCI_COMPLETION_SUCCESS) {
        ERROR(TEXT("[XHCI_EvaluateContext] Completion code %u"), Completion);
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Perform a control transfer on EP0.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param Setup Setup packet.
 * @param Buffer Data buffer (optional).
 * @param Length Data length.
 * @param DirectionIn TRUE if data is IN.
 * @return TRUE on success.
 */
static BOOL XHCI_ControlTransfer(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, const USB_SETUP_PACKET* Setup,
                                 PHYSICAL BufferPhysical, LPVOID BufferLinear, U16 Length, BOOL DirectionIn) {
    XHCI_TRB SetupTrb;
    XHCI_TRB DataTrb;
    XHCI_TRB StatusTrb;
    U64 StatusPhysical = U64_0;
    U32 Completion = 0;

    if (Setup == NULL || UsbDevice == NULL) {
        return FALSE;
    }

    MemorySet(&SetupTrb, 0, sizeof(SetupTrb));
    MemoryCopy(&SetupTrb.Dword0, Setup, sizeof(USB_SETUP_PACKET));
    SetupTrb.Dword2 = 8;
    SetupTrb.Dword3 = (XHCI_TRB_TYPE_SETUP_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IDT;

    if (!XHCI_TransferRingEnqueue(UsbDevice, &SetupTrb, NULL)) {
        return FALSE;
    }

    if (Length > 0 && BufferLinear != NULL && BufferPhysical != 0) {
        MemorySet(&DataTrb, 0, sizeof(DataTrb));
        DataTrb.Dword0 = U64_Low32(U64_FromUINT(BufferPhysical));
        DataTrb.Dword1 = U64_High32(U64_FromUINT(BufferPhysical));
        DataTrb.Dword2 = Length;
        DataTrb.Dword3 = (XHCI_TRB_TYPE_DATA_STAGE << XHCI_TRB_TYPE_SHIFT);
        if (DirectionIn) {
            DataTrb.Dword3 |= XHCI_TRB_DIR_IN;
        }

        if (!XHCI_TransferRingEnqueue(UsbDevice, &DataTrb, NULL)) {
            return FALSE;
        }
    }

    MemorySet(&StatusTrb, 0, sizeof(StatusTrb));
    StatusTrb.Dword3 = (XHCI_TRB_TYPE_STATUS_STAGE << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_IOC;
    if (Length > 0) {
        if (!DirectionIn) {
            StatusTrb.Dword3 |= XHCI_TRB_DIR_IN;
        }
    } else {
        StatusTrb.Dword3 |= XHCI_TRB_DIR_IN;
    }

    if (!XHCI_TransferRingEnqueue(UsbDevice, &StatusTrb, &StatusPhysical)) {
        return FALSE;
    }

    XHCI_RingDoorbell(Device, UsbDevice->SlotId, XHCI_EP0_DCI);

    if (!XHCI_WaitForTransferCompletion(Device, StatusPhysical, &Completion)) {
        return FALSE;
    }

    if (Completion == XHCI_COMPLETION_SUCCESS || Completion == XHCI_COMPLETION_SHORT_PACKET) {
        return TRUE;
    }

    if (Completion == XHCI_COMPLETION_STALL_ERROR) {
        USB_SETUP_PACKET ClearFeature;
        MemorySet(&ClearFeature, 0, sizeof(ClearFeature));
        ClearFeature.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_ENDPOINT;
        ClearFeature.Request = USB_REQUEST_CLEAR_FEATURE;
        ClearFeature.Value = USB_FEATURE_ENDPOINT_HALT;
        ClearFeature.Index = 0;
        ClearFeature.Length = 0;
        (void)XHCI_ControlTransfer(Device, UsbDevice, &ClearFeature, 0, NULL, 0, FALSE);
    }

    ERROR(TEXT("[XHCI_ControlTransfer] Completion code %u"), Completion);
    return FALSE;
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
        DEBUG(TEXT("[XHCI_ReadConfigDescriptor] Truncated config descriptor %u -> %u"), TotalLength, PAGE_SIZE);
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
 * @brief Probe a port and fetch descriptors.
 * @param Device xHCI device.
 * @param UsbDevice USB device state.
 * @param PortIndex Port index (0-based).
 * @return TRUE on success.
 */
static BOOL XHCI_ProbePort(LPXHCI_DEVICE Device, LPXHCI_USB_DEVICE UsbDevice, U32 PortIndex) {
    U32 PortStatus = XHCI_ReadPortStatus(Device, PortIndex);
    U32 SpeedId = (PortStatus & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
    PHYSICAL ConfigPhysical = 0;
    LINEAR ConfigLinear = 0;
    U16 ConfigLength = 0;

    if ((PortStatus & XHCI_PORTSC_CCS) == 0) {
        return FALSE;
    }

    UsbDevice->PortNumber = (U8)(PortIndex + 1);
    UsbDevice->SpeedId = (U8)SpeedId;
    UsbDevice->MaxPacketSize0 = XHCI_GetDefaultMaxPacketSize0(UsbDevice->SpeedId);

    if (UsbDevice->Present) {
        return TRUE;
    }

    if (!XHCI_ResetPort(Device, PortIndex)) {
        return FALSE;
    }

    if (!XHCI_InitUsbDeviceState(Device, UsbDevice)) {
        ERROR(TEXT("[XHCI_ProbePort] Port %u state init failed"), PortIndex + 1);
        return FALSE;
    }

    if (!XHCI_EnableSlot(Device, &UsbDevice->SlotId)) {
        ERROR(TEXT("[XHCI_ProbePort] Port %u enable slot failed"), PortIndex + 1);
        return FALSE;
    }

    ((U64 *)Device->DcbaaLinear)[UsbDevice->SlotId] = U64_FromUINT(UsbDevice->DeviceContextPhysical);

    XHCI_BuildInputContextForAddress(Device, UsbDevice);

    if (!XHCI_AddressDevice(Device, UsbDevice)) {
        ERROR(TEXT("[XHCI_ProbePort] Port %u address device failed"), PortIndex + 1);
        return FALSE;
    }

    UsbDevice->Address = UsbDevice->SlotId;

    if (!XHCI_GetDeviceDescriptor(Device, UsbDevice)) {
        ERROR(TEXT("[XHCI_ProbePort] Port %u get device descriptor failed"), PortIndex + 1);
        return FALSE;
    }

    UsbDevice->StringManufacturer = UsbDevice->DeviceDescriptor.ManufacturerIndex;
    UsbDevice->StringProduct = UsbDevice->DeviceDescriptor.ProductIndex;
    UsbDevice->StringSerial = UsbDevice->DeviceDescriptor.SerialNumberIndex;

    UsbDevice->MaxPacketSize0 = XHCI_ComputeMaxPacketSize0(
        UsbDevice->SpeedId,
        UsbDevice->DeviceDescriptor.MaxPacketSize0);

    XHCI_BuildInputContextForEp0(Device, UsbDevice);
    (void)XHCI_EvaluateContext(Device, UsbDevice);

    if (!XHCI_ReadConfigDescriptor(Device, UsbDevice, &ConfigPhysical, &ConfigLinear, &ConfigLength)) {
        ERROR(TEXT("[XHCI_ProbePort] Port %u get config descriptor failed"), PortIndex + 1);
        return FALSE;
    }

    if (!XHCI_ParseConfigDescriptor(UsbDevice, (const U8*)ConfigLinear, ConfigLength)) {
        ERROR(TEXT("[XHCI_ProbePort] Port %u parse config descriptor failed"), PortIndex + 1);
        FreeRegion(ConfigLinear, PAGE_SIZE);
        FreePhysicalPage(ConfigPhysical);
        return FALSE;
    }

    FreeRegion(ConfigLinear, PAGE_SIZE);
    FreePhysicalPage(ConfigPhysical);

    if (UsbDevice->ConfigCount > 0) {
        UsbDevice->SelectedConfigValue = UsbDevice->Configs[0].ConfigurationValue;
    }

    if (UsbDevice->SelectedConfigValue != 0) {
        USB_SETUP_PACKET Setup;
        MemorySet(&Setup, 0, sizeof(Setup));
        Setup.RequestType = USB_REQUEST_DIRECTION_OUT | USB_REQUEST_TYPE_STANDARD | USB_REQUEST_RECIPIENT_DEVICE;
        Setup.Request = USB_REQUEST_SET_CONFIGURATION;
        Setup.Value = UsbDevice->SelectedConfigValue;
        Setup.Index = 0;
        Setup.Length = 0;

        if (!XHCI_ControlTransfer(Device, UsbDevice, &Setup, 0, NULL, 0, FALSE)) {
            ERROR(TEXT("[XHCI_ProbePort] Port %u set configuration failed"), PortIndex + 1);
            return FALSE;
        }
    }

    UsbDevice->Present = TRUE;

    DEBUG(TEXT("[XHCI_ProbePort] Port %u VID=%x PID=%x"),
          PortIndex + 1,
          UsbDevice->DeviceDescriptor.VendorID,
          UsbDevice->DeviceDescriptor.ProductID);

    DEBUG(TEXT("[XHCI_ProbePort] Port %u Configs=%u SelectedConfig=%u"),
          PortIndex + 1,
          UsbDevice->ConfigCount,
          UsbDevice->SelectedConfigValue);

    return TRUE;
}

/************************************************************************/

/**
 * @brief Enumerate connected devices on all ports.
 * @param Device xHCI device.
 */
static void XHCI_EnsureUsbDevices(LPXHCI_DEVICE Device) {
    if (Device == NULL || Device->UsbDevices == NULL) {
        return;
    }

    for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
        LPXHCI_USB_DEVICE UsbDevice = &Device->UsbDevices[PortIndex];
        if (!UsbDevice->Present) {
            (void)XHCI_ProbePort(Device, UsbDevice, PortIndex);
        }
    }
}

/************************************************************************/

/**
 * @brief Initialize the command ring.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_InitCommandRing(LPXHCI_DEVICE Device) {
    if (!XHCI_AllocPage(TEXT("XHCI_CommandRing"), &Device->CommandRingPhysical, &Device->CommandRingLinear)) {
        ERROR(TEXT("[XHCI_InitCommandRing] Command ring allocation failed"));
        return FALSE;
    }

    LPXHCI_TRB Ring = (LPXHCI_TRB)Device->CommandRingLinear;
    MemorySet(Ring, 0, PAGE_SIZE);

    U32 LinkIndex = XHCI_COMMAND_RING_TRBS - 1;
    U64 RingAddress = U64_FromUINT(Device->CommandRingPhysical);
    Ring[LinkIndex].Dword0 = U64_Low32(RingAddress);
    Ring[LinkIndex].Dword1 = U64_High32(RingAddress);
    Ring[LinkIndex].Dword2 = 0;
    Ring[LinkIndex].Dword3 = (XHCI_TRB_TYPE_LINK << XHCI_TRB_TYPE_SHIFT) | XHCI_TRB_CYCLE | XHCI_TRB_TOGGLE_CYCLE;

    Device->CommandRingCycleState = 1;
    Device->CommandRingEnqueueIndex = 0;
    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize the event ring and interrupter 0.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_InitEventRing(LPXHCI_DEVICE Device) {
    if (!XHCI_AllocPage(TEXT("XHCI_EventRing"), &Device->EventRingPhysical, &Device->EventRingLinear)) {
        ERROR(TEXT("[XHCI_InitEventRing] Event ring allocation failed"));
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_EventRingTable"), &Device->EventRingTablePhysical, &Device->EventRingTableLinear)) {
        ERROR(TEXT("[XHCI_InitEventRing] ERST allocation failed"));
        return FALSE;
    }

    LPXHCI_ERST_ENTRY Entries = (LPXHCI_ERST_ENTRY)Device->EventRingTableLinear;
    MemorySet(Entries, 0, PAGE_SIZE);
    Entries[0].SegmentBase = U64_FromUINT(Device->EventRingPhysical);
    Entries[0].SegmentSize = XHCI_EVENT_RING_TRBS;
    Entries[0].Reserved = 0;
    Entries[0].Reserved2 = 0;

    LINEAR InterrupterBase = Device->RuntimeBase + XHCI_RT_INTERRUPTER_BASE;
    XHCI_Write32(InterrupterBase, XHCI_IMAN, 0);
    XHCI_Write32(InterrupterBase, XHCI_IMOD, 0);
    XHCI_Write32(InterrupterBase, XHCI_ERSTSZ, 1);
    XHCI_Write64(InterrupterBase, XHCI_ERSTBA, U64_FromUINT(Device->EventRingTablePhysical));
    XHCI_Write64(InterrupterBase, XHCI_ERDP, U64_FromUINT(Device->EventRingPhysical));

    Device->EventRingDequeueIndex = 0;
    Device->EventRingCycleState = 1;

    return TRUE;
}

/************************************************************************/

/**
 * @brief Reset and start the xHCI controller.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_ResetAndStart(LPXHCI_DEVICE Device) {
    U32 Command = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    Command &= ~XHCI_USBCMD_RS;
    XHCI_Write32(Device->OpBase, XHCI_OP_USBCMD, Command);

    if (!XHCI_WaitForRegister(Device->OpBase, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, XHCI_USBSTS_HCH, XHCI_HALT_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetAndStart] Halt timeout"));
        return FALSE;
    }

    Command |= XHCI_USBCMD_HCRST;
    XHCI_Write32(Device->OpBase, XHCI_OP_USBCMD, Command);

    if (!XHCI_WaitForRegister(Device->OpBase, XHCI_OP_USBCMD, XHCI_USBCMD_HCRST, 0, XHCI_RESET_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetAndStart] Reset bit timeout"));
        return FALSE;
    }

    if (!XHCI_WaitForRegister(Device->OpBase, XHCI_OP_USBSTS, XHCI_USBSTS_CNR, 0, XHCI_RESET_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetAndStart] Controller not ready"));
        return FALSE;
    }

    if (!XHCI_AllocPage(TEXT("XHCI_DCBAA"), &Device->DcbaaPhysical, &Device->DcbaaLinear)) {
        ERROR(TEXT("[XHCI_ResetAndStart] DCBAA allocation failed"));
        return FALSE;
    }

    if (!XHCI_InitCommandRing(Device)) {
        return FALSE;
    }

    if (!XHCI_InitEventRing(Device)) {
        return FALSE;
    }

    XHCI_Write64(Device->OpBase, XHCI_OP_DCBAAP, U64_FromUINT(Device->DcbaaPhysical));

    {
        U64 Crcr = U64_FromUINT(Device->CommandRingPhysical);
        U32 Low = U64_Low32(Crcr) | XHCI_TRB_CYCLE;
        U32 High = U64_High32(Crcr);
        XHCI_Write32(Device->OpBase, XHCI_OP_CRCR, Low);
        XHCI_Write32(Device->OpBase, (U32)(XHCI_OP_CRCR + 4), High);
    }

    XHCI_Write32(Device->OpBase, XHCI_OP_CONFIG, Device->MaxSlots);

    Command = XHCI_Read32(Device->OpBase, XHCI_OP_USBCMD);
    Command |= XHCI_USBCMD_RS;
    XHCI_Write32(Device->OpBase, XHCI_OP_USBCMD, Command);

    if (!XHCI_WaitForRegister(Device->OpBase, XHCI_OP_USBSTS, XHCI_USBSTS_HCH, 0, XHCI_RUN_TIMEOUT)) {
        ERROR(TEXT("[XHCI_ResetAndStart] Run timeout"));
        return FALSE;
    }

    return TRUE;
}

/************************************************************************/

/**
 * @brief Initialize xHCI MMIO offsets and controller capabilities.
 * @param Device xHCI device.
 * @return TRUE on success, FALSE otherwise.
 */
static BOOL XHCI_InitController(LPXHCI_DEVICE Device) {
    U32 CapLengthReg = XHCI_Read32(Device->MmioBase, XHCI_CAPLENGTH);
    Device->CapLength = (U8)(CapLengthReg & MAX_U8);
    Device->HciVersion = (U16)((CapLengthReg >> 16) & 0xFFFF);

    U32 HcsParams1 = XHCI_Read32(Device->MmioBase, XHCI_HCSPARAMS1);
    Device->MaxSlots = (U8)(HcsParams1 & XHCI_HCSPARAMS1_MAXSLOTS_MASK);
    Device->MaxInterrupters = (U16)((HcsParams1 & XHCI_HCSPARAMS1_MAXINTRS_MASK) >> XHCI_HCSPARAMS1_MAXINTRS_SHIFT);
    Device->MaxPorts = (U8)((HcsParams1 & XHCI_HCSPARAMS1_MAXPORTS_MASK) >> XHCI_HCSPARAMS1_MAXPORTS_SHIFT);
    Device->HccParams1 = XHCI_Read32(Device->MmioBase, XHCI_HCCPARAMS1);
    Device->ContextSize = ((Device->HccParams1 & XHCI_HCCPARAMS1_CSZ) != 0) ? 64 : 32;

    Device->OpBase = Device->MmioBase + Device->CapLength;

    U32 DbOff = XHCI_Read32(Device->MmioBase, XHCI_DBOFF);
    U32 RtOff = XHCI_Read32(Device->MmioBase, XHCI_RTSOFF);
    Device->DoorbellBase = Device->MmioBase + (DbOff & 0xFFFFFFFC);
    Device->RuntimeBase = Device->MmioBase + (RtOff & 0xFFFFFFE0);

    DEBUG(TEXT("[XHCI_InitController] CapLen=%u HciVer=%x MaxSlots=%u MaxPorts=%u MaxIntrs=%u"),
          Device->CapLength,
          Device->HciVersion,
          Device->MaxSlots,
          Device->MaxPorts,
          Device->MaxInterrupters);

    U32 PageSize = XHCI_Read32(Device->OpBase, XHCI_OP_PAGESIZE);
    DEBUG(TEXT("[XHCI_InitController] PageSize bitmap=%x"), PageSize);

    if ((Device->HccParams1 & XHCI_HCCPARAMS1_AC64) == 0) {
        DEBUG(TEXT("[XHCI_InitController] 64-bit addressing not supported"));
    }

    if (!XHCI_ResetAndStart(Device)) {
        return FALSE;
    }

    if ((HcsParams1 & XHCI_HCSPARAMS1_PPC) != 0) {
        for (U32 PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
            XHCI_PowerPort(Device, PortIndex);
        }
    }

    XHCI_LogPorts(Device);
    return TRUE;
}

/************************************************************************/

/**
 * @brief Probe callback used by PCI subsystem.
 * @param PciInfo PCI device info.
 * @return DF_RET_SUCCESS when supported, DF_RET_NOTIMPL otherwise.
 */
static U32 XHCI_OnProbe(const PCI_INFO *PciInfo) {
    if (PciInfo == NULL) return DF_RET_BADPARAM;
    if (PciInfo->BaseClass != XHCI_CLASS_SERIAL_BUS) return DF_RET_NOTIMPL;
    if (PciInfo->SubClass != XHCI_SUBCLASS_USB) return DF_RET_NOTIMPL;
    if (PciInfo->ProgIF != XHCI_PROGIF_XHCI) return DF_RET_NOTIMPL;
    return DF_RET_SUCCESS;
}

/************************************************************************/

/**
 * @brief Load callback for driver.
 * @return DF_RET_SUCCESS.
 */
static U32 XHCI_OnLoad(void) { return DF_RET_SUCCESS; }

/************************************************************************/

/**
 * @brief Unload callback for driver.
 * @return DF_RET_SUCCESS.
 */
static U32 XHCI_OnUnload(void) { return DF_RET_SUCCESS; }

/************************************************************************/

/**
 * @brief Version callback for driver.
 * @return Encoded version.
 */
static U32 XHCI_OnGetVersion(void) { return MAKE_VERSION(1, 0); }

/************************************************************************/

/**
 * @brief Capabilities callback for driver.
 * @return Capability bitmask.
 */
static U32 XHCI_OnGetCaps(void) { return 0; }

/************************************************************************/

/**
 * @brief Last function callback.
 * @return Last function ID.
 */
static U32 XHCI_OnGetLastFunc(void) { return DF_PROBE; }

/************************************************************************/

static U32 XHCI_EnumNext(LPDRIVER_ENUM_NEXT Next) {
    if (Next == NULL || Next->Query == NULL || Next->Item == NULL) {
        return DF_RET_BADPARAM;
    }
    if (Next->Query->Header.Size < sizeof(DRIVER_ENUM_QUERY) ||
        Next->Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RET_BADPARAM;
    }

    if (Next->Query->Domain != ENUM_DOMAIN_XHCI_PORT &&
        Next->Query->Domain != ENUM_DOMAIN_USB_DEVICE &&
        Next->Query->Domain != ENUM_DOMAIN_USB_NODE) {
        return DF_RET_NOTIMPL;
    }

    LPLIST PciList = GetPCIDeviceList();
    if (PciList == NULL) {
        return DF_RET_NO_MORE;
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
                    return DF_RET_SUCCESS;
                }

                    MatchIndex++;
                }
            } else if (Next->Query->Domain == ENUM_DOMAIN_USB_DEVICE) {
                XHCI_EnsureUsbDevices(Device);

                for (UINT PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
                    LPXHCI_USB_DEVICE UsbDevice = &Device->UsbDevices[PortIndex];
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
                        return DF_RET_SUCCESS;
                    }

                    MatchIndex++;
                }
            } else if (Next->Query->Domain == ENUM_DOMAIN_USB_NODE) {
                XHCI_EnsureUsbDevices(Device);

                for (UINT PortIndex = 0; PortIndex < Device->MaxPorts; PortIndex++) {
                    LPXHCI_USB_DEVICE UsbDevice = &Device->UsbDevices[PortIndex];
                    if (!UsbDevice->Present) {
                        continue;
                    }

                    if (MatchIndex == Next->Query->Index) {
                        DRIVER_ENUM_USB_NODE Data;
                        XHCI_InitUsbNodeData(&Data, Device, UsbDevice);
                        Data.NodeType = USB_NODE_DEVICE;

                        XHCI_FillEnumItem(Next->Item, ENUM_DOMAIN_USB_NODE, Next->Query->Index, &Data, sizeof(Data));

                        Next->Query->Index++;
                        return DF_RET_SUCCESS;
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
                            return DF_RET_SUCCESS;
                        }
                        MatchIndex++;

                        for (UINT IfIndex = 0; IfIndex < Config->InterfaceCount; IfIndex++) {
                            LPXHCI_USB_INTERFACE Interface = &Config->Interfaces[IfIndex];

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
                                return DF_RET_SUCCESS;
                            }
                            MatchIndex++;

                            for (UINT EpIndex = 0; EpIndex < Interface->EndpointCount; EpIndex++) {
                                LPXHCI_USB_ENDPOINT Endpoint = &Interface->Endpoints[EpIndex];

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
                                    return DF_RET_SUCCESS;
                                }
                                MatchIndex++;
                            }
                        }
                    }
                }
            } else {
                return DF_RET_NOTIMPL;
            }
        }
    }

    return DF_RET_NO_MORE;
}

/************************************************************************/

static U32 XHCI_EnumPretty(LPDRIVER_ENUM_PRETTY Pretty) {
    if (Pretty == NULL || Pretty->Item == NULL || Pretty->Buffer == NULL || Pretty->BufferSize == 0) {
        return DF_RET_BADPARAM;
    }
    if (Pretty->Item->Header.Size < sizeof(DRIVER_ENUM_ITEM)) {
        return DF_RET_BADPARAM;
    }

    if (Pretty->Item->Domain == ENUM_DOMAIN_XHCI_PORT) {
        if (Pretty->Item->DataSize < sizeof(DRIVER_ENUM_XHCI_PORT)) {
            return DF_RET_BADPARAM;
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
        return DF_RET_SUCCESS;
    }

    if (Pretty->Item->Domain == ENUM_DOMAIN_USB_DEVICE) {
        if (Pretty->Item->DataSize < sizeof(DRIVER_ENUM_USB_DEVICE)) {
            return DF_RET_BADPARAM;
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
        return DF_RET_SUCCESS;
    }

    if (Pretty->Item->Domain == ENUM_DOMAIN_USB_NODE) {
        if (Pretty->Item->DataSize < sizeof(DRIVER_ENUM_USB_NODE)) {
            return DF_RET_BADPARAM;
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
                return DF_RET_SUCCESS;
            case USB_NODE_CONFIG:
                StringPrintFormat(Pretty->Buffer,
                                  TEXT("  Config %u Attr=%x MaxPower=%u"),
                                  (U32)Data->ConfigValue,
                                  (U32)Data->ConfigAttributes,
                                  (U32)Data->ConfigMaxPower);
                return DF_RET_SUCCESS;
            case USB_NODE_INTERFACE:
                StringPrintFormat(Pretty->Buffer,
                                  TEXT("    Interface %u Alt=%u Class=%x/%x/%x"),
                                  (U32)Data->InterfaceNumber,
                                  (U32)Data->AlternateSetting,
                                  (U32)Data->InterfaceClass,
                                  (U32)Data->InterfaceSubClass,
                                  (U32)Data->InterfaceProtocol);
                return DF_RET_SUCCESS;
            case USB_NODE_ENDPOINT:
                StringPrintFormat(Pretty->Buffer,
                                  TEXT("      Endpoint %x %s %s MaxPacket=%u Interval=%u"),
                                  (U32)Data->EndpointAddress,
                                  (Data->EndpointAddress & 0x80) ? TEXT("IN") : TEXT("OUT"),
                                  XHCI_EndpointTypeToString(Data->EndpointAttributes),
                                  (U32)Data->EndpointMaxPacketSize,
                                  (U32)Data->EndpointInterval);
                return DF_RET_SUCCESS;
        }
    }

    return DF_RET_BADPARAM;
}

/************************************************************************/

/**
 * @brief Driver command handler.
 * @param Function Function identifier.
 * @param Param Function parameter.
 * @return DF_RET_* code.
 */
static UINT XHCI_Commands(UINT Function, UINT Param) {
    switch (Function) {
        case DF_LOAD:
            return XHCI_OnLoad();
        case DF_UNLOAD:
            return XHCI_OnUnload();
        case DF_GETVERSION:
            return XHCI_OnGetVersion();
        case DF_GETCAPS:
            return XHCI_OnGetCaps();
        case DF_GETLASTFUNC:
            return XHCI_OnGetLastFunc();
        case DF_PROBE:
            return XHCI_OnProbe((const PCI_INFO *)(LPVOID)Param);
        case DF_ENUM_NEXT:
            return XHCI_EnumNext((LPDRIVER_ENUM_NEXT)(LPVOID)Param);
        case DF_ENUM_PRETTY:
            return XHCI_EnumPretty((LPDRIVER_ENUM_PRETTY)(LPVOID)Param);
    }

    return DF_RET_NOTIMPL;
}

/************************************************************************/

/**
 * @brief Attach routine used by the PCI subsystem.
 * @param PciDevice PCI device to attach.
 * @return Pointer to device cast as LPPCI_DEVICE.
 */
static LPPCI_DEVICE XHCI_Attach(LPPCI_DEVICE PciDevice) {
    if (PciDevice == NULL) {
        return NULL;
    }

    DEBUG(TEXT("[XHCI_Attach] New device %x:%x.%u"),
          (U32)PciDevice->Info.Bus,
          (U32)PciDevice->Info.Dev,
          (U32)PciDevice->Info.Func);

    LPXHCI_DEVICE Device = (LPXHCI_DEVICE)KernelHeapAlloc(sizeof(XHCI_DEVICE));
    if (Device == NULL) {
        return NULL;
    }

    MemorySet(Device, 0, sizeof(XHCI_DEVICE));
    MemoryCopy(Device, PciDevice, sizeof(PCI_DEVICE));
    InitMutex(&(Device->Mutex));

    U32 Bar0Raw = Device->Info.BAR[0];
    U32 Bar1Raw = Device->Info.BAR[1];
    U32 Bar0Base = PCI_GetBARBase(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);
    U32 Bar0Size = PCI_GetBARSize(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, 0);
    BOOL Is64Bit = ((Bar0Raw & 0x6) == 0x4);

    if (Is64Bit && Bar1Raw != 0) {
        ERROR(TEXT("[XHCI_Attach] 64-bit BAR above 4GB not supported (BAR1=%x)"), Bar1Raw);
        KernelHeapFree(Device);
        return NULL;
    }

    if (Bar0Base == 0 || Bar0Size == 0) {
        ERROR(TEXT("[XHCI_Attach] Invalid BAR0"));
        KernelHeapFree(Device);
        return NULL;
    }

    Device->MmioBase = MapIOMemory(Bar0Base, Bar0Size);
    Device->MmioSize = Bar0Size;

    if (Device->MmioBase == 0) {
        ERROR(TEXT("[XHCI_Attach] MapIOMemory failed"));
        KernelHeapFree(Device);
        return NULL;
    }

    PCI_EnableBusMaster(Device->Info.Bus, Device->Info.Dev, Device->Info.Func, TRUE);

    if (!XHCI_InitController(Device)) {
        ERROR(TEXT("[XHCI_Attach] Controller init failed"));
        XHCI_FreeResources(Device);
        KernelHeapFree(Device);
        return NULL;
    }

    if (Device->MaxPorts > 0) {
        U32 PortCount = Device->MaxPorts;
        Device->UsbDevices = (LPXHCI_USB_DEVICE)KernelHeapAlloc(sizeof(XHCI_USB_DEVICE) * PortCount);
        if (Device->UsbDevices == NULL) {
            ERROR(TEXT("[XHCI_Attach] USB device state allocation failed"));
            XHCI_FreeResources(Device);
            KernelHeapFree(Device);
            return NULL;
        }
        MemorySet(Device->UsbDevices, 0, sizeof(XHCI_USB_DEVICE) * PortCount);
    }

    DEBUG(TEXT("[XHCI_Attach] Attached MMIO=%p Size=%u MaxPorts=%u"),
          Device->MmioBase,
          Device->MmioSize,
          Device->MaxPorts);

    return (LPPCI_DEVICE)Device;
}

/************************************************************************/

static DRIVER_MATCH XHCI_MatchTable[] = {
    {PCI_ANY_ID, PCI_ANY_ID, XHCI_CLASS_SERIAL_BUS, XHCI_SUBCLASS_USB, XHCI_PROGIF_XHCI}
};

PCI_DRIVER DATA_SECTION XHCIDriver = {
    .TypeID = KOID_DRIVER,
    .References = 1,
    .Next = NULL,
    .Prev = NULL,
    .Type = DRIVER_TYPE_OTHER,
    .VersionMajor = 1,
    .VersionMinor = 0,
    .Designer = "Jango73",
    .Manufacturer = "USB-IF",
    .Product = "xHCI",
    .Command = XHCI_Commands,
    .EnumDomainCount = 3,
    .EnumDomains = {ENUM_DOMAIN_XHCI_PORT, ENUM_DOMAIN_USB_DEVICE, ENUM_DOMAIN_USB_NODE},
    .Matches = XHCI_MatchTable,
    .MatchCount = sizeof(XHCI_MatchTable) / sizeof(XHCI_MatchTable[0]),
    .Attach = XHCI_Attach
};
