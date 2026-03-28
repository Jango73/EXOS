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

#include "drivers/network/RealtekCommon.h"

#include "CoreString.h"
#include "Kernel.h"
#include "Log.h"
#include "Memory.h"

/************************************************************************/

/**
 * @brief Creates and initializes the common EXOS-facing Realtek PCI device state.
 * @param DeviceSize Final driver device object size.
 * @param PciDevice Source PCI function descriptor.
 * @param FunctionName Caller function name for diagnostics.
 * @return Heap-allocated PCI device object or NULL on failure.
 */
LPPCI_DEVICE RealtekNetworkAttachCommon(UINT DeviceSize, LPPCI_DEVICE PciDevice, LPCSTR FunctionName) {
    LPREALTEK_NETWORK_COMMON_DEVICE Device;

    if (DeviceSize < sizeof(REALTEK_NETWORK_COMMON_DEVICE) || PciDevice == NULL || StringEmpty(FunctionName)) {
        return NULL;
    }

    Device = (LPREALTEK_NETWORK_COMMON_DEVICE)CreateKernelObject(DeviceSize, KOID_PCIDEVICE);
    if (Device == NULL) {
        ERROR(TEXT("[%s] Failed to allocate device object"), FunctionName);
        return NULL;
    }

    Device->Driver = PciDevice->Driver;
    Device->Info = PciDevice->Info;
    MemoryCopy(Device->BARPhys, PciDevice->BARPhys, sizeof(Device->BARPhys));
    MemoryCopy((LPVOID)Device->BARMapped, (LPVOID)PciDevice->BARMapped, sizeof(Device->BARMapped));
    MemoryCopy(Device->Name, PciDevice->Name, sizeof(Device->Name));
    InitMutex(&(Device->Mutex));
    Device->ProductName = TEXT("Realtek Network Family");
    RealtekNetworkBuildPlaceholderMac(Device);
    return (LPPCI_DEVICE)Device;
}

/************************************************************************/

/**
 * @brief Builds a deterministic placeholder MAC address for pre-hardware bring-up.
 * @param Device Target common device state.
 */
void RealtekNetworkBuildPlaceholderMac(LPREALTEK_NETWORK_COMMON_DEVICE Device) {
    if (Device == NULL) {
        return;
    }

    Device->Mac[0] = 0x02;
    Device->Mac[1] = 0x10;
    Device->Mac[2] = 0xEC;
    Device->Mac[3] = Device->Info.Bus;
    Device->Mac[4] = Device->Info.Dev;
    Device->Mac[5] = Device->Info.Func;
}

/************************************************************************/

/**
 * @brief Validates a generic network reset request.
 * @param Reset Reset request.
 * @return DF_RETURN_SUCCESS when the device handle is valid.
 */
U32 RealtekNetworkOnReset(const NETWORK_RESET* Reset) {
    if (Reset == NULL || Reset->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Fills NETWORK_INFO from the common Realtek device state.
 * @param GetInfo Information request.
 * @param LinkUp Link status to report.
 * @param SpeedMbps Link speed to report.
 * @param DuplexFull Duplex state to report.
 * @param Mtu MTU to report.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
U32 RealtekNetworkOnGetInfo(
    const NETWORK_GET_INFO* GetInfo,
    BOOL LinkUp,
    U32 SpeedMbps,
    BOOL DuplexFull,
    U32 Mtu) {
    LPREALTEK_NETWORK_COMMON_DEVICE Device;

    if (GetInfo == NULL || GetInfo->Device == NULL || GetInfo->Info == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPREALTEK_NETWORK_COMMON_DEVICE)GetInfo->Device;
    MemoryCopy(GetInfo->Info->MAC, Device->Mac, sizeof(Device->Mac));
    GetInfo->Info->LinkUp = LinkUp;
    GetInfo->Info->SpeedMbps = SpeedMbps;
    GetInfo->Info->DuplexFull = DuplexFull;
    GetInfo->Info->MTU = Mtu;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Stores the receive callback for later RX-path implementation.
 * @param Set Callback registration request.
 * @return DF_RETURN_SUCCESS on success or an error code.
 */
U32 RealtekNetworkOnSetReceiveCallback(const NETWORK_SET_RX_CB* Set) {
    LPREALTEK_NETWORK_COMMON_DEVICE Device;

    if (Set == NULL || Set->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Device = (LPREALTEK_NETWORK_COMMON_DEVICE)Set->Device;
    Device->RxCallback = Set->Callback;
    Device->RxUserData = Set->UserData;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shared TX stub used until hardware-specific transmit support exists.
 * @param Send Send request.
 * @return DF_RETURN_NOT_IMPLEMENTED for the early integration skeleton.
 */
U32 RealtekNetworkOnSendNotImplemented(const NETWORK_SEND* Send) {
    if (Send == NULL || Send->Device == NULL || Send->Data == NULL || Send->Length == 0) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Shared polling stub used before a receive path exists.
 * @param Poll Poll request.
 * @return DF_RETURN_SUCCESS when the request is structurally valid.
 */
U32 RealtekNetworkOnPollIdle(const NETWORK_POLL* Poll) {
    if (Poll == NULL || Poll->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shared interrupt-enable stub for polling-only bring-up.
 * @param Config Interrupt configuration request.
 * @return DF_RETURN_NOT_IMPLEMENTED so polling remains active.
 */
U32 RealtekNetworkOnEnableInterruptsPollingOnly(DEVICE_INTERRUPT_CONFIG* Config) {
    if (Config == NULL || Config->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Config->VectorSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Config->InterruptEnabled = FALSE;
    return DF_RETURN_NOT_IMPLEMENTED;
}

/************************************************************************/

/**
 * @brief Shared interrupt-disable stub for polling-only bring-up.
 * @param Config Interrupt configuration request.
 * @return DF_RETURN_SUCCESS when the request is structurally valid.
 */
U32 RealtekNetworkOnDisableInterrupts(DEVICE_INTERRUPT_CONFIG* Config) {
    if (Config == NULL || Config->Device == NULL) {
        return DF_RETURN_BAD_PARAMETER;
    }

    Config->VectorSlot = DEVICE_INTERRUPT_INVALID_SLOT;
    Config->InterruptEnabled = FALSE;
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shared driver-load callback for early Realtek drivers.
 * @return DF_RETURN_SUCCESS.
 */
U32 RealtekNetworkOnLoad(void) {
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shared driver-unload callback for early Realtek drivers.
 * @return DF_RETURN_SUCCESS.
 */
U32 RealtekNetworkOnUnload(void) {
    return DF_RETURN_SUCCESS;
}

/************************************************************************/

/**
 * @brief Shared capability query for early Realtek drivers.
 * @return Zero because advanced capabilities are not exposed yet.
 */
U32 RealtekNetworkOnGetCaps(void) {
    return 0;
}

/************************************************************************/

/**
 * @brief Shared highest-implemented function identifier.
 * @return DF_DEV_DISABLE_INTERRUPT for the polling-first skeleton.
 */
U32 RealtekNetworkOnGetLastFunction(void) {
    return DF_DEV_DISABLE_INTERRUPT;
}
