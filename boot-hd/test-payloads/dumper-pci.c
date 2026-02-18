/************************************************************************\

    EXOS Interrupt Dump Payload
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


    PCI helpers for test payloads

\************************************************************************/

#include "dumper-pci.h"

/************************************************************************/
// Macros

#define PCI_CONFIG_ADDRESS 0x0CF8
#define PCI_CONFIG_DATA 0x0CFC

#define PCI_MAX_BUS 0x100
#define PCI_MAX_DEVICE 0x20
#define PCI_MAX_FUNCTION 0x08

#define PCI_HEADER_TYPE_MULTI_FUNCTION 0x80
#define PCI_INVALID_VENDOR_ID 0xFFFF

#define PCI_BAR0_OFFSET 0x10
#define PCI_BAR5_OFFSET 0x24
#define PCI_INTERRUPT_LINE_OFFSET 0x3C
#define PCI_INTERRUPT_PIN_OFFSET 0x3D

/************************************************************************/

/**
 * @brief Write a 32-bit value to an I/O port.
 * @param Port I/O port address.
 * @param Value Value to write.
 */
static void OutPortU32(U16 Port, U32 Value) {
    __asm__ __volatile__("outl %0, %1" ::"a"(Value), "Nd"(Port));
}

/************************************************************************/

/**
 * @brief Read a 32-bit value from an I/O port.
 * @param Port I/O port address.
 * @return Value read.
 */
static U32 InPortU32(U16 Port) {
    U32 Value;

    __asm__ __volatile__("inl %1, %0" : "=a"(Value) : "Nd"(Port));

    return Value;
}

/************************************************************************/

/**
 * @brief Read a PCI configuration dword.
 * @param Bus PCI bus number.
 * @param Device PCI device number.
 * @param Function PCI function number.
 * @param Offset Configuration offset.
 * @return 32-bit register value.
 */
static U32 PciReadU32(U8 Bus, U8 Device, U8 Function, U8 Offset) {
    U32 Address = 0x80000000u |
        ((U32)Bus << 16) |
        ((U32)Device << 11) |
        ((U32)Function << 8) |
        ((U32)Offset & 0xFC);

    OutPortU32(PCI_CONFIG_ADDRESS, Address);

    return InPortU32(PCI_CONFIG_DATA);
}

/************************************************************************/

/**
 * @brief Read vendor and device identifiers from PCI configuration space.
 * @param Bus PCI bus number.
 * @param Device PCI device number.
 * @param Function PCI function number.
 * @param VendorId Output vendor identifier.
 * @param DeviceId Output device identifier.
 * @return TRUE when the function exists.
 */
static BOOL PciReadVendorDevice(U8 Bus, U8 Device, U8 Function, U16* VendorId, U16* DeviceId) {
    U32 Value = PciReadU32(Bus, Device, Function, 0x00);
    U16 Vendor = (U16)(Value & 0xFFFF);

    if (Vendor == PCI_INVALID_VENDOR_ID) {
        return FALSE;
    }

    if (VendorId != NULL) {
        *VendorId = Vendor;
    }

    if (DeviceId != NULL) {
        *DeviceId = (U16)((Value >> 16) & 0xFFFF);
    }

    return TRUE;
}

/************************************************************************/

static void FillControllerInfo(
    LPPCI_CONTROLLER_INFO Info,
    U8 Bus,
    U8 Device,
    U8 Function,
    U16 VendorId,
    U16 DeviceId,
    U32 ClassReg) {
    if (Info == NULL) {
        return;
    }

    Info->Bus = Bus;
    Info->Device = Device;
    Info->Function = Function;
    Info->VendorId = VendorId;
    Info->DeviceId = DeviceId;
    Info->ClassCode = (U8)((ClassReg >> 24) & 0xFF);
    Info->SubClass = (U8)((ClassReg >> 16) & 0xFF);
    Info->ProgrammingInterface = (U8)((ClassReg >> 8) & 0xFF);
    Info->Bar0Base = PciReadU32(Bus, Device, Function, PCI_BAR0_OFFSET) & 0xFFFFFFF0u;
    Info->Bar5Base = PciReadU32(Bus, Device, Function, PCI_BAR5_OFFSET) & 0xFFFFFFF0u;
    Info->InterruptLine = (U8)(PciReadU32(Bus, Device, Function, PCI_INTERRUPT_LINE_OFFSET) & 0xFF);
    Info->InterruptPin = (U8)(PciReadU32(Bus, Device, Function, PCI_INTERRUPT_PIN_OFFSET) & 0xFF);
}

/************************************************************************/

BOOL FindPciControllerByClass(
    U8 ClassCode,
    U8 SubClass,
    U8 ProgrammingInterface,
    LPPCI_CONTROLLER_INFO Info,
    UINT* ControllerCount) {
    UINT Count = 0x00;

    for (UINT BusIndex = 0x00; BusIndex < PCI_MAX_BUS; BusIndex++) {
        U8 Bus = (U8)BusIndex;

        for (UINT DeviceIndex = 0x00; DeviceIndex < PCI_MAX_DEVICE; DeviceIndex++) {
            U8 Device = (U8)DeviceIndex;
            U16 VendorId;
            U16 DeviceId;

            if (!PciReadVendorDevice(Bus, Device, 0x00, &VendorId, &DeviceId)) {
                continue;
            }

            U32 HeaderValue = PciReadU32(Bus, Device, 0x00, 0x0C);
            U8 HeaderType = (U8)((HeaderValue >> 16) & 0xFF);
            UINT FunctionLimit = (HeaderType & PCI_HEADER_TYPE_MULTI_FUNCTION) ? PCI_MAX_FUNCTION : 0x01;

            for (UINT FunctionIndex = 0x00; FunctionIndex < FunctionLimit; FunctionIndex++) {
                U8 Function = (U8)FunctionIndex;
                U16 FunctionVendorId;
                U16 FunctionDeviceId;

                if (!PciReadVendorDevice(Bus, Device, Function, &FunctionVendorId, &FunctionDeviceId)) {
                    continue;
                }

                U32 ClassReg = PciReadU32(Bus, Device, Function, 0x08);
                U8 DeviceClass = (U8)((ClassReg >> 24) & 0xFF);
                U8 DeviceSubClass = (U8)((ClassReg >> 16) & 0xFF);
                U8 DeviceProgrammingInterface = (U8)((ClassReg >> 8) & 0xFF);

                if (DeviceClass != ClassCode ||
                    DeviceSubClass != SubClass ||
                    DeviceProgrammingInterface != ProgrammingInterface) {
                    continue;
                }

                Count++;
                if (Count == 0x01) {
                    FillControllerInfo(
                        Info,
                        Bus,
                        Device,
                        Function,
                        FunctionVendorId,
                        FunctionDeviceId,
                        ClassReg);
                }
            }
        }
    }

    if (ControllerCount != NULL) {
        *ControllerCount = Count;
    }

    return Count != 0x00;
}
