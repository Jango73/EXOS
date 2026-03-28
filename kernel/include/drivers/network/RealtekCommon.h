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


    Realtek network common helpers

\************************************************************************/

#ifndef REALTEKCOMMON_H_INCLUDED
#define REALTEKCOMMON_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "drivers/bus/PCI.h"
#include "drivers/interrupts/DeviceInterrupt.h"
#include "network/Network.h"

/***************************************************************************/

#define REALTEK_NETWORK_VENDOR_ID 0x10EC

#define REALTEK_NETWORK_MATCH_ENTRY(DeviceID) \
    { REALTEK_NETWORK_VENDOR_ID, DeviceID, PCI_CLASS_NETWORK, PCI_SUBCLASS_ETHERNET, PCI_ANY_CLASS }

#define REALTEK_NETWORK_COMMON_DEVICE_FIELDS \
    PCI_DEVICE_FIELDS                        \
    LPCSTR ProductName;                      \
    U8 RegisterBarIndex;                     \
    U8 RegisterAccessMode;                   \
    U16 PCICommand;                          \
    PHYSICAL RegisterBase;                   \
    UINT RegisterSize;                       \
    LINEAR RegisterLinear;                   \
    U32 RegisterPort;                        \
    U8 Mac[6];                               \
    NT_RXCB RxCallback;                      \
    LPVOID RxUserData;

/***************************************************************************/

typedef enum tag_REALTEK_REGISTER_ACCESS_MODE {
    REALTEK_REGISTER_ACCESS_MODE_NONE = 0,
    REALTEK_REGISTER_ACCESS_MODE_IO = 1,
    REALTEK_REGISTER_ACCESS_MODE_MMIO = 2,
} REALTEK_REGISTER_ACCESS_MODE;

/***************************************************************************/

typedef struct tag_REALTEK_NETWORK_COMMON_DEVICE {
    REALTEK_NETWORK_COMMON_DEVICE_FIELDS
} REALTEK_NETWORK_COMMON_DEVICE, *LPREALTEK_NETWORK_COMMON_DEVICE;

/***************************************************************************/

LPPCI_DEVICE RealtekNetworkAttachCommon(UINT DeviceSize, LPPCI_DEVICE PciDevice, LPCSTR FunctionName);
U32 RealtekNetworkInitializeRegisterWindow(
    LPREALTEK_NETWORK_COMMON_DEVICE Device,
    REALTEK_REGISTER_ACCESS_MODE PreferredMode,
    REALTEK_REGISTER_ACCESS_MODE FallbackMode,
    U16 ValidationRegisterOffset,
    LPCSTR FunctionName);
U8 RealtekNetworkReadRegister8(LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 RegisterOffset);
U16 RealtekNetworkReadRegister16(LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 RegisterOffset);
U32 RealtekNetworkReadRegister32(LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 RegisterOffset);
void RealtekNetworkWriteRegister8(LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 RegisterOffset, U8 Value);
void RealtekNetworkWriteRegister16(LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 RegisterOffset, U16 Value);
void RealtekNetworkWriteRegister32(LPREALTEK_NETWORK_COMMON_DEVICE Device, U16 RegisterOffset, U32 Value);
void RealtekNetworkBuildPlaceholderMac(LPREALTEK_NETWORK_COMMON_DEVICE Device);
U32 RealtekNetworkOnReset(const NETWORK_RESET* Reset);
U32 RealtekNetworkOnGetInfo(
    const NETWORK_GET_INFO* GetInfo,
    BOOL LinkUp,
    U32 SpeedMbps,
    BOOL DuplexFull,
    U32 Mtu);
U32 RealtekNetworkOnSetReceiveCallback(const NETWORK_SET_RX_CB* Set);
U32 RealtekNetworkOnSendNotImplemented(const NETWORK_SEND* Send);
U32 RealtekNetworkOnPollIdle(const NETWORK_POLL* Poll);
U32 RealtekNetworkOnEnableInterruptsPollingOnly(DEVICE_INTERRUPT_CONFIG* Config);
U32 RealtekNetworkOnDisableInterrupts(DEVICE_INTERRUPT_CONFIG* Config);
U32 RealtekNetworkOnLoad(void);
U32 RealtekNetworkOnUnload(void);
U32 RealtekNetworkOnGetCaps(void);
U32 RealtekNetworkOnGetLastFunction(void);

/***************************************************************************/

#endif  // REALTEKCOMMON_H_INCLUDED
