
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


    Network Manager

\************************************************************************/

#ifndef NETWORKMANAGER_H_INCLUDED
#define NETWORKMANAGER_H_INCLUDED

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

#include "Base.h"
#include "PCI.h"
#include "Network.h"

/************************************************************************/

typedef struct tag_NETWORK_DEVICE_CONTEXT {
    LISTNODE_FIELDS
    LPPCI_DEVICE Device;
    U32 LocalIPv4_Be;
    BOOL IsInitialized;
    BOOL IsReady;
    NT_RXCB OriginalCallback;
} NETWORK_DEVICE_CONTEXT, *LPNETWORK_DEVICE_CONTEXT;

/************************************************************************/

/**
 * @brief Initialize network stack for all network devices in the kernel.
 *
 * This function scans all PCI devices and initializes the network stack
 * (ARP, IPv4, TCP layers) for each network device found.
 */
void InitializeNetwork(void);

/**
 * @brief Initialize network stack for a specific network device.
 *
 * @param Device Pointer to the network PCI device
 * @param LocalIPv4_Be Local IPv4 address in big-endian format
 */
void NetworkManager_InitializeDevice(LPPCI_DEVICE Device, U32 LocalIPv4_Be);

/**
 * @brief Network manager task function.
 *
 * This task runs periodically to maintain the network stack.
 *
 * @param param Unused parameter
 * @return Always returns 0
 */
U32 NetworkManagerTask(LPVOID param);

/**
 * @brief Get the primary network device for global protocols like TCP.
 *
 * @return Pointer to the primary network device or NULL if none available
 */
LPPCI_DEVICE NetworkManager_GetPrimaryDevice(void);

/**
 * @brief Check if a specific network device is ready for use.
 *
 * Returns TRUE if static configuration is used or if DHCP has completed.
 *
 * @param Device Pointer to the network device
 * @return TRUE if network device is ready, FALSE otherwise
 */
BOOL NetworkManager_IsDeviceReady(LPDEVICE Device);

/************************************************************************/

#pragma pack(pop)

#endif  // NETWORKMANAGER_H_INCLUDED
