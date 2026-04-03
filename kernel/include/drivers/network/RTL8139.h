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


    RTL8139

\************************************************************************/

#ifndef RTL8139_H_INCLUDED
#define RTL8139_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "core/Driver.h"
#include "drivers/bus/PCI.h"
#include "drivers/network/RealtekCommon.h"
#include "utils/DMABuffer.h"

/***************************************************************************/

#define RTL8139_VENDOR_REALTEK REALTEK_NETWORK_VENDOR_ID
#define RTL8139_DEVICE_8139 0x8139

#define RTL8139_REG_IDR0 0x00
#define RTL8139_REG_IDR4 0x04
#define RTL8139_REG_MAR0 0x08
#define RTL8139_REG_TXSTATUS0 0x10
#define RTL8139_REG_TXSTATUS1 0x14
#define RTL8139_REG_TXSTATUS2 0x18
#define RTL8139_REG_TXSTATUS3 0x1C
#define RTL8139_REG_TXADDR0 0x20
#define RTL8139_REG_TXADDR1 0x24
#define RTL8139_REG_TXADDR2 0x28
#define RTL8139_REG_TXADDR3 0x2C
#define RTL8139_REG_RXBUFSTART 0x30
#define RTL8139_REG_CHIPCMD 0x37
#define RTL8139_REG_CAPR 0x38
#define RTL8139_REG_CBR 0x3A
#define RTL8139_REG_INTRMASK 0x3C
#define RTL8139_REG_INTRSTATUS 0x3E
#define RTL8139_REG_TXCONFIG 0x40
#define RTL8139_REG_RXCONFIG 0x44
#define RTL8139_REG_CFG9346 0x50
#define RTL8139_REG_CONFIG1 0x52
#define RTL8139_REG_MEDIASTATUS 0x58
#define RTL8139_REG_MII_BMCR 0x62
#define RTL8139_REG_MII_BMSR 0x64
#define RTL8139_REG_ANAR 0x66
#define RTL8139_REG_ANLPAR 0x68

#define RTL8139_TXCONFIG_IFG_SHIFT 24
#define RTL8139_TXCONFIG_IFG_NORMAL (3 << RTL8139_TXCONFIG_IFG_SHIFT)
#define RTL8139_TXCONFIG_DMA_SHIFT 8
#define RTL8139_TXCONFIG_DMA_1024 (6 << RTL8139_TXCONFIG_DMA_SHIFT)

#define RTL8139_CHIPCMD_RESET 0x10
#define RTL8139_CHIPCMD_RX_ENABLE 0x08
#define RTL8139_CHIPCMD_TX_ENABLE 0x04
#define RTL8139_CHIPCMD_RX_BUFFER_EMPTY 0x01

#define RTL8139_CFG9346_LOCK 0x00
#define RTL8139_CFG9346_UNLOCK 0xC0

#define RTL8139_RXCONFIG_ACCEPT_PHYSICAL 0x00000002
#define RTL8139_RXCONFIG_ACCEPT_MULTICAST 0x00000004
#define RTL8139_RXCONFIG_ACCEPT_BROADCAST 0x00000008
#define RTL8139_RXCONFIG_WRAP 0x00000080

#define RTL8139_TXSTATUS_OWN 0x00002000
#define RTL8139_TXSTATUS_OK 0x00008000

#define RTL8139_TX_SLOT_COUNT 4
#define RTL8139_TX_BUFFER_SIZE 1792
#define RTL8139_RX_RING_SIZE (8 * 1024)
#define RTL8139_RX_BUFFER_SIZE (RTL8139_RX_RING_SIZE + 16 + 1500)
#define RTL8139_RX_READ_POINTER_ADJUST 16
#define RTL8139_CAPR_INITIAL_VALUE 0xFFF0
#define RTL8139_MAXIMUM_MTU 1500

#define RTL8139_RX_STATUS_OK 0x0001

#define RTL8139_INTERRUPT_RX_OK 0x0001
#define RTL8139_INTERRUPT_RX_ERROR 0x0002
#define RTL8139_INTERRUPT_TX_OK 0x0004
#define RTL8139_INTERRUPT_TX_ERROR 0x0008
#define RTL8139_INTERRUPT_RX_OVERFLOW 0x0010
#define RTL8139_INTERRUPT_LINK_CHANGE 0x0020
#define RTL8139_INTERRUPT_RX_FIFO_OVERFLOW 0x0040
#define RTL8139_INTERRUPT_SYSTEM_ERROR 0x8000
#define RTL8139_INTERRUPT_ENABLE_MASK \
    (RTL8139_INTERRUPT_RX_OK | RTL8139_INTERRUPT_RX_ERROR | RTL8139_INTERRUPT_TX_OK | RTL8139_INTERRUPT_TX_ERROR | \
     RTL8139_INTERRUPT_RX_OVERFLOW | RTL8139_INTERRUPT_LINK_CHANGE | RTL8139_INTERRUPT_RX_FIFO_OVERFLOW |          \
     RTL8139_INTERRUPT_SYSTEM_ERROR)
#define RTL8139_INTERRUPT_RELEVANT_MASK \
    (RTL8139_INTERRUPT_RX_OK | RTL8139_INTERRUPT_RX_ERROR | RTL8139_INTERRUPT_TX_OK | RTL8139_INTERRUPT_TX_ERROR | \
     RTL8139_INTERRUPT_RX_OVERFLOW | RTL8139_INTERRUPT_LINK_CHANGE | RTL8139_INTERRUPT_RX_FIFO_OVERFLOW |          \
     RTL8139_INTERRUPT_SYSTEM_ERROR)
#define RTL8139_INTERRUPT_ACKNOWLEDGE_AFTER_POLL_MASK \
    (RTL8139_INTERRUPT_RX_OVERFLOW | RTL8139_INTERRUPT_RX_FIFO_OVERFLOW)

#define RTL8139_MII_BMSR_LINK_STATUS 0x0004
#define RTL8139_ANLPAR_10_HALF 0x0020
#define RTL8139_ANLPAR_10_FULL 0x0040
#define RTL8139_ANLPAR_100_HALF 0x0080
#define RTL8139_ANLPAR_100_FULL 0x0100

/***************************************************************************/

typedef struct tag_RTL8139_RX_PACKET_HEADER {
    U16 ReceiveStatus;
    U16 ReceiveLength;
} PACKED RTL8139_RX_PACKET_HEADER, *LPRTL8139_RX_PACKET_HEADER;

typedef struct tag_RTL8139_TX_SLOT_INFO {
    U16 StatusRegisterOffset;
    U16 AddressRegisterOffset;
} RTL8139_TX_SLOT_INFO, *LPRTL8139_TX_SLOT_INFO;

/***************************************************************************/

typedef enum tag_RTL8139_DEVICE_FAMILY {
    RTL8139_DEVICE_FAMILY_UNKNOWN = 0,
    RTL8139_DEVICE_FAMILY_8139,
} RTL8139_DEVICE_FAMILY;

typedef enum tag_RTL8139_QUIRK_FLAGS {
    RTL8139_QUIRK_NONE = 0x00000000,
    RTL8139_QUIRK_LEGACY_PCI_FAST_ETHERNET = 0x00000001,
    RTL8139_QUIRK_SHARED_QEMU_MODEL = 0x00000002,
    RTL8139_QUIRK_RX_BUFFER_RING = 0x00000004,
} RTL8139_QUIRK_FLAGS;

typedef struct tag_RTL8139_DEVICE_INFO {
    U16 VendorID;
    U16 DeviceID;
    RTL8139_DEVICE_FAMILY Family;
    U32 QuirkFlags;
    LPCSTR ProductName;
} RTL8139_DEVICE_INFO, *LPRTL8139_DEVICE_INFO;

/***************************************************************************/

extern PCI_DRIVER RTL8139Driver;
LPDRIVER RTL8139GetDriver(void);

/***************************************************************************/

#endif  // RTL8139_H_INCLUDED
