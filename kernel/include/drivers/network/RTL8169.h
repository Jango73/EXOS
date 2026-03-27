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


    RTL8169

\************************************************************************/

#ifndef RTL8169_H_INCLUDED
#define RTL8169_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Driver.h"
#include "drivers/bus/PCI.h"
#include "network/Network.h"

/***************************************************************************/

#define RTL8169_VENDOR_REALTEK 0x10EC
#define RTL8169_DEVICE_8161 0x8161
#define RTL8169_DEVICE_8168 0x8168

#define RTL8169_REG_MAC0 0x00
#define RTL8169_REG_MAC4 0x04
#define RTL8169_REG_MAR0 0x08
#define RTL8169_REG_CHIPCMD 0x37
#define RTL8169_REG_TXPOLL 0x38
#define RTL8169_REG_INTRMASK 0x3C
#define RTL8169_REG_INTRSTATUS 0x3E
#define RTL8169_REG_TXCONFIG 0x40
#define RTL8169_REG_RXCONFIG 0x44
#define RTL8169_REG_CFG9346 0x50
#define RTL8169_REG_PHYAR 0x60
#define RTL8169_REG_PHYSTATUS 0x6C
#define RTL8169_REG_RXMAXSIZE 0xDA
#define RTL8169_REG_CPLUSCMD 0xE0
#define RTL8169_REG_RXDESCADDRLOW 0xE4
#define RTL8169_REG_RXDESCADDRHIGH 0xE8
#define RTL8169_REG_MAXTXPACKETSIZE 0xEC

#define RTL8169_CHIPCMD_RESET 0x10
#define RTL8169_CHIPCMD_RX_ENABLE 0x08
#define RTL8169_CHIPCMD_TX_ENABLE 0x04

#define RTL8169_CFG9346_LOCK 0x00
#define RTL8169_CFG9346_UNLOCK 0xC0

#define RTL8169_RXCONFIG_FIFO_SHIFT 13
#define RTL8169_RXCONFIG_FIFO_UNLIMITED (7 << RTL8169_RXCONFIG_FIFO_SHIFT)
#define RTL8169_RXCONFIG_DMA_SHIFT 8
#define RTL8169_RXCONFIG_DMA_UNLIMITED (7 << RTL8169_RXCONFIG_DMA_SHIFT)
#define RTL8169_RXCONFIG_ACCEPT_MASK 0x0000003F
#define RTL8169_RXCONFIG_ACCEPT_PHYSICAL 0x00000002
#define RTL8169_RXCONFIG_ACCEPT_BROADCAST 0x00000008
#define RTL8169_RXCONFIG_ACCEPT_MULTICAST 0x00000004

#define RTL8169_DESCRIPTOR_RING_ALIGN 256
#define RTL8169_MAXIMUM_MTU 1500

/***************************************************************************/

#define RTL8169_MATCH_ENTRY(DeviceID) \
    { RTL8169_VENDOR_REALTEK, DeviceID, PCI_CLASS_NETWORK, PCI_SUBCLASS_ETHERNET, PCI_ANY_CLASS }

/***************************************************************************/

typedef struct tag_RTL8169_TX_DESCRIPTOR {
    U32 CommandStatus;
    U32 VLANInformation;
    U32 BufferAddressLow;
    U32 BufferAddressHigh;
} RTL8169_TX_DESCRIPTOR, *LPRTL8169_TX_DESCRIPTOR;

typedef struct tag_RTL8169_RX_DESCRIPTOR {
    U32 CommandStatus;
    U32 VLANInformation;
    U32 BufferAddressLow;
    U32 BufferAddressHigh;
} RTL8169_RX_DESCRIPTOR, *LPRTL8169_RX_DESCRIPTOR;

/***************************************************************************/

typedef enum tag_RTL8169_DEVICE_FAMILY {
    RTL8169_DEVICE_FAMILY_UNKNOWN = 0,
    RTL8169_DEVICE_FAMILY_8111,
    RTL8169_DEVICE_FAMILY_8168,
    RTL8169_DEVICE_FAMILY_8411,
} RTL8169_DEVICE_FAMILY;

typedef enum tag_RTL8169_QUIRK_FLAGS {
    RTL8169_QUIRK_NONE = 0x00000000,
    RTL8169_QUIRK_PCIE_GIGABIT = 0x00000001,
    RTL8169_QUIRK_REVISION_BY_MAC_VERSION = 0x00000002,
    RTL8169_QUIRK_SHARED_8111_8168_REGISTERS = 0x00000004,
    RTL8169_QUIRK_SHARED_8411_REGISTERS = 0x00000008,
} RTL8169_QUIRK_FLAGS;

typedef struct tag_RTL8169_DEVICE_INFO {
    U16 VendorID;
    U16 DeviceID;
    RTL8169_DEVICE_FAMILY Family;
    U32 QuirkFlags;
    LPCSTR ProductName;
} RTL8169_DEVICE_INFO, *LPRTL8169_DEVICE_INFO;

/***************************************************************************/

extern PCI_DRIVER RTL8169Driver;
LPDRIVER RTL8169GetDriver(void);

/***************************************************************************/

#endif  // RTL8169_H_INCLUDED
