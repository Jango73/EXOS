
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

#include "../include/NetworkManager.h"

#include "../include/ARP.h"
#include "../include/IPv4.h"
#include "../include/TCP.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Memory.h"
#include "../include/Network.h"
#include "../include/Driver.h"
#include "../include/Endianness.h"
#include "../include/Socket.h"
#include "../include/Helpers.h"
#include "../include/String.h"
#include "../include/TOML.h"

/************************************************************************/

// Helper function to get network configuration from TOML with fallback
static U32 NetworkManager_GetConfigIP(LPCSTR configPath, U32 fallbackValue) {
    LPCSTR configValue = GetConfigurationValue(configPath);
    if (configValue != NULL) {
        U32 parsedIP = ParseIPAddress(configValue);
        if (parsedIP != 0) {
            return parsedIP;
        }
    }
    return fallbackValue;
}

// Helper function to get per-device network configuration
// For now, fall back to global configuration since TOML array access is complex
// TODO: Implement proper per-device configuration when TOML API is enhanced
static U32 NetworkManager_GetDeviceConfigIP(U32 deviceIndex, LPCSTR configKey, LPCSTR fallbackGlobalKey, U32 fallbackValue) {
    // For now, just use global configuration with device index as IP offset
    if (fallbackGlobalKey != NULL) {
        U32 baseIP = NetworkManager_GetConfigIP(fallbackGlobalKey, fallbackValue);
        // If this is LocalIP, offset by device index to avoid conflicts
        if (StringCompare(configKey, TEXT("LocalIP")) == 0) {
            U32 hostPart = Ntohl(baseIP) + deviceIndex;
            DEBUG(TEXT("[NetworkManager_GetDeviceConfigIP] Device %u: %s = base + %u"), deviceIndex, configKey, deviceIndex);
            return Htonl(hostPart);
        }
        DEBUG(TEXT("[NetworkManager_GetDeviceConfigIP] Device %u: %s = global"), deviceIndex, configKey);
        return baseIP;
    }

    DEBUG(TEXT("[NetworkManager_GetDeviceConfigIP] Device %u: Using fallback value for %s"), deviceIndex, configKey);
    return fallbackValue;
}

// Maximum number of network devices we can manage
#define MAX_NETWORK_DEVICES 8

/************************************************************************/

typedef struct tag_NETWORK_DEVICE_CONTEXT {
    LPPCI_DEVICE Device;
    U32 LocalIPv4_Be;
    BOOL IsInitialized;
    NT_RXCB OriginalCallback;
} NETWORK_DEVICE_CONTEXT, *LPNETWORK_DEVICE_CONTEXT;

static NETWORK_DEVICE_CONTEXT NetworkDevices[MAX_NETWORK_DEVICES];
static U32 NetworkDeviceCount = 0;

/************************************************************************/

// Forward declaration
static void NetworkManager_RxCallback(LPDEVICE Device, const U8 *Frame, U32 Length);

/**
 * @brief Frame reception callback wrapper for a specific device.
 *
 * These callbacks are created dynamically for each device to maintain device context.
 */
static void NetworkManager_RxCallback_Device0(const U8 *Frame, U32 Length) {
    DEBUG(TEXT("[NetworkManager_RxCallback_Device0] ENTRY Frame=%X Length=%u"), (U32)Frame, Length);
    if (NetworkDeviceCount > 0 && NetworkDevices[0].IsInitialized) {
        NetworkManager_RxCallback((LPDEVICE)NetworkDevices[0].Device, Frame, Length);
    } else {
        DEBUG(TEXT("[NetworkManager_RxCallback_Device0] ERROR: Device not initialized"));
    }
}

static void NetworkManager_RxCallback_Device1(const U8 *Frame, U32 Length) {
    if (NetworkDeviceCount > 1 && NetworkDevices[1].IsInitialized) {
        NetworkManager_RxCallback((LPDEVICE)NetworkDevices[1].Device, Frame, Length);
    }
}

static void NetworkManager_RxCallback_Device2(const U8 *Frame, U32 Length) {
    if (NetworkDeviceCount > 2 && NetworkDevices[2].IsInitialized) {
        NetworkManager_RxCallback((LPDEVICE)NetworkDevices[2].Device, Frame, Length);
    }
}

static void NetworkManager_RxCallback_Device3(const U8 *Frame, U32 Length) {
    if (NetworkDeviceCount > 3 && NetworkDevices[3].IsInitialized) {
        NetworkManager_RxCallback((LPDEVICE)NetworkDevices[3].Device, Frame, Length);
    }
}

static void NetworkManager_RxCallback_Device4(const U8 *Frame, U32 Length) {
    if (NetworkDeviceCount > 4 && NetworkDevices[4].IsInitialized) {
        NetworkManager_RxCallback((LPDEVICE)NetworkDevices[4].Device, Frame, Length);
    }
}

static void NetworkManager_RxCallback_Device5(const U8 *Frame, U32 Length) {
    if (NetworkDeviceCount > 5 && NetworkDevices[5].IsInitialized) {
        NetworkManager_RxCallback((LPDEVICE)NetworkDevices[5].Device, Frame, Length);
    }
}

static void NetworkManager_RxCallback_Device6(const U8 *Frame, U32 Length) {
    if (NetworkDeviceCount > 6 && NetworkDevices[6].IsInitialized) {
        NetworkManager_RxCallback((LPDEVICE)NetworkDevices[6].Device, Frame, Length);
    }
}

static void NetworkManager_RxCallback_Device7(const U8 *Frame, U32 Length) {
    if (NetworkDeviceCount > 7 && NetworkDevices[7].IsInitialized) {
        NetworkManager_RxCallback((LPDEVICE)NetworkDevices[7].Device, Frame, Length);
    }
}

static NT_RXCB DeviceCallbacks[MAX_NETWORK_DEVICES] = {
    NetworkManager_RxCallback_Device0,
    NetworkManager_RxCallback_Device1,
    NetworkManager_RxCallback_Device2,
    NetworkManager_RxCallback_Device3,
    NetworkManager_RxCallback_Device4,
    NetworkManager_RxCallback_Device5,
    NetworkManager_RxCallback_Device6,
    NetworkManager_RxCallback_Device7
};

/**
 * @brief Internal frame reception handler that dispatches to protocol layers.
 *
 * @param Device Pointer to the device that received the frame
 * @param Frame Pointer to the received ethernet frame
 * @param Length Length of the frame in bytes
 */
static void NetworkManager_RxCallback(LPDEVICE Device, const U8 *Frame, U32 Length) {
    DEBUG(TEXT("[NetworkManager_RxCallback] Entry Device=%X Frame=%X Length=%u"), (U32)Device, (U32)Frame, Length);

    if (!Device || !Frame || Length < 14U) {
        DEBUG(TEXT("[NetworkManager_RxCallback] Bad parameters or frame too short"));
        return;
    }

    U16 EthType = (U16)((Frame[12] << 8) | Frame[13]);
    DEBUG(TEXT("[NetworkManager_RxCallback] Frame len=%u, ethType=%X"), Length, EthType);

    // Debug: Show first few bytes of all received frames
    if (Length >= 20) {
        DEBUG(TEXT("[NetworkManager_RxCallback] Frame bytes: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X"),
              Frame[0], Frame[1], Frame[2], Frame[3], Frame[4], Frame[5],
              Frame[6], Frame[7], Frame[8], Frame[9], Frame[10], Frame[11], Frame[12], Frame[13]);
    }

    // Dispatch to protocol layers
    DEBUG(TEXT("[NetworkManager_RxCallback] About to switch on EthType=%X (ARP=%X IPV4=%X)"), EthType, ETHTYPE_ARP, ETHTYPE_IPV4);
    switch (EthType) {
        case ETHTYPE_ARP:
            DEBUG(TEXT("[NetworkManager_RxCallback] Dispatching ARP frame"));
            ARP_OnEthernetFrame(Device, Frame, Length);
            break;
        case ETHTYPE_IPV4:
            DEBUG(TEXT("[NetworkManager_RxCallback] Dispatching IPv4 frame"));
            IPv4_OnEthernetFrame(Device, Frame, Length);
            break;
        default:
            DEBUG(TEXT("[NetworkManager_RxCallback] Unknown EthType: %X"), EthType);
            break;
    }
}

/************************************************************************/

/**
 * @brief Find all network devices in the PCI device list.
 *
 * @return Number of network devices found
 */
static U32 NetworkManager_FindNetworkDevices(void) {
    LPLISTNODE Node;
    U32 Count = 0;

    DEBUG(TEXT("[NetworkManager_FindNetworkDevices] Enter"));

    SAFE_USE(Kernel.PCIDevice) {
        SAFE_USE_VALID_ID(Kernel.PCIDevice->First, ID_PCIDEVICE) {
            for (Node = Kernel.PCIDevice->First; Node && Count < MAX_NETWORK_DEVICES; Node = Node->Next) {
                LPPCI_DEVICE Device = (LPPCI_DEVICE)Node;
                SAFE_USE_VALID_ID(Device, ID_PCIDEVICE) {
                    SAFE_USE_VALID_ID(Device->Driver, ID_DRIVER) {
                        if (Device->Driver->Type == DRIVER_TYPE_NETWORK) {
                            NetworkDevices[Count].Device = Device;
                            // Use per-device configuration with fallback to global config
                            NetworkDevices[Count].LocalIPv4_Be = NetworkManager_GetDeviceConfigIP(Count, TEXT("LocalIP"), TEXT(CONFIG_NETWORK_LOCAL_IP), Htonl(0xC0A8380AU + Count));
                            NetworkDevices[Count].IsInitialized = FALSE;
                            NetworkDevices[Count].OriginalCallback = NULL;
                            Count++;
                            DEBUG(TEXT("[NetworkManager_FindNetworkDevices] Found network device %u: %s with IP fallback base+%u"), Count-1, Device->Driver->Product, Count-1);
                        }
                    }
                }
            }
        } else {
            ERROR(TEXT("[NetworkManager_FindNetworkDevices] Kernel.PCIDevice->First is NULL"));
        }
    } else {
        ERROR(TEXT("[NetworkManager_FindNetworkDevices] Kernel.PCIDevice is NULL"));
    }

    DEBUG(TEXT("[NetworkManager_FindNetworkDevices] Found %u network devices"), Count);
    return Count;
}

/************************************************************************/

void InitializeNetwork(void) {
    DEBUG(TEXT("[InitializeNetwork] Enter"));

    // Clear device list
    MemorySet(NetworkDevices, 0, sizeof(NetworkDevices));
    NetworkDeviceCount = 0;

    // Find all network devices
    NetworkDeviceCount = NetworkManager_FindNetworkDevices();

    if (NetworkDeviceCount == 0) {
        WARNING(TEXT("[InitializeNetwork] No network devices found"));
        return;
    }

    // Initialize each network device
    for (U32 i = 0; i < NetworkDeviceCount; i++) {
        NetworkManager_InitializeDevice(NetworkDevices[i].Device, NetworkDevices[i].LocalIPv4_Be);
    }

    DEBUG(TEXT("[InitializeNetwork] Initialized %u network devices"), NetworkDeviceCount);
}

/************************************************************************/

void NetworkManager_InitializeDevice(LPPCI_DEVICE Device, U32 LocalIPv4_Be) {
    DEBUG(TEXT("[NetworkManager_InitializeDevice] Enter for device %s"), Device->Driver->Product);

    SAFE_USE_VALID_ID(Device, ID_PCIDEVICE) {
        SAFE_USE_VALID_ID(Device->Driver, ID_DRIVER) {
            if (Device->Driver->Type != DRIVER_TYPE_NETWORK) {
                ERROR(TEXT("[NetworkManager_InitializeDevice] Device is not a network device"));
                return;
            }

            // Reset the device
            NETWORKRESET Reset = {.Device = Device};
            Device->Driver->Command(DF_NT_RESET, (U32)(LPVOID)&Reset);

            // Get device information
            NETWORKINFO Info;
            MemorySet(&Info, 0, sizeof(Info));
            NETWORKGETINFO GetInfo = {.Device = Device, .Info = &Info};
            Device->Driver->Command(DF_NT_GETINFO, (U32)(LPVOID)&GetInfo);

            DEBUG(TEXT("[NetworkManager_InitializeDevice] MAC=%x:%x:%x:%x:%x:%x Link=%s Speed=%u Duplex=%s MTU=%u"),
                  (U32)Info.MAC[0], (U32)Info.MAC[1], (U32)Info.MAC[2],
                  (U32)Info.MAC[3], (U32)Info.MAC[4], (U32)Info.MAC[5],
                  Info.LinkUp ? "UP" : "DOWN", Info.SpeedMbps,
                  Info.DuplexFull ? "FULL" : "HALF", Info.MTU);

            // Initialize ARP subsystem for this device
            DEBUG(TEXT("[NetworkManager_InitializeDevice] Initializing ARP layer"));
            ARP_Initialize((LPDEVICE)Device, LocalIPv4_Be);

            // Initialize IPv4 subsystem for this device
            DEBUG(TEXT("[NetworkManager_InitializeDevice] Initializing IPv4 layer"));
            IPv4_Initialize((LPDEVICE)Device, LocalIPv4_Be);

            // Find device index to get device-specific configuration
            U32 DeviceIndex = MAX_NETWORK_DEVICES;
            for (U32 i = 0; i < NetworkDeviceCount; i++) {
                if (NetworkDevices[i].Device == Device) {
                    DeviceIndex = i;
                    break;
                }
            }

            // Configure network settings from TOML configuration (per-device with global fallback)
            U32 NetmaskBe = (DeviceIndex < MAX_NETWORK_DEVICES) ?
                NetworkManager_GetDeviceConfigIP(DeviceIndex, TEXT("Netmask"), TEXT(CONFIG_NETWORK_NETMASK), Htonl(0xFFFFFF00)) :
                NetworkManager_GetConfigIP(CONFIG_NETWORK_NETMASK, Htonl(0xFFFFFF00));
            U32 GatewayBe = (DeviceIndex < MAX_NETWORK_DEVICES) ?
                NetworkManager_GetDeviceConfigIP(DeviceIndex, TEXT("Gateway"), TEXT(CONFIG_NETWORK_GATEWAY), Htonl(0xC0A83801)) :
                NetworkManager_GetConfigIP(CONFIG_NETWORK_GATEWAY, Htonl(0xC0A83801));
            IPv4_SetNetworkConfig((LPDEVICE)Device, LocalIPv4_Be, NetmaskBe, GatewayBe);

            // Initialize TCP subsystem (global for all devices)
            static BOOL TCPInitialized = FALSE;
            if (!TCPInitialized) {
                DEBUG(TEXT("[NetworkManager_InitializeDevice] Initializing TCP layer"));
                TCP_Initialize();
                TCPInitialized = TRUE;
            }

            // Install RX callback - use device index found earlier
            DEBUG(TEXT("[NetworkManager_InitializeDevice] Using device index %u for RX callback"), DeviceIndex);

            if (DeviceIndex < MAX_NETWORK_DEVICES) {
                NETWORKSETRXCB SetRxCb = {.Device = Device, .Callback = DeviceCallbacks[DeviceIndex]};
                DEBUG(TEXT("[NetworkManager_InitializeDevice] Installing RX callback %X for device index %u"), (U32)DeviceCallbacks[DeviceIndex], DeviceIndex);
                U32 Result = Device->Driver->Command(DF_NT_SETRXCB, (U32)(LPVOID)&SetRxCb);
                DEBUG(TEXT("[NetworkManager_InitializeDevice] RX callback installation result: %u"), Result);
            } else {
                ERROR(TEXT("[NetworkManager_InitializeDevice] Device %X not found in device list!"), (U32)Device);
            }

            // Mark device as initialized
            for (U32 i = 0; i < NetworkDeviceCount; i++) {
                if (NetworkDevices[i].Device == Device) {
                    NetworkDevices[i].IsInitialized = TRUE;
                    break;
                }
            }

            // Register TCP protocol handler now that device is initialized
            IPv4_RegisterProtocolHandler((LPDEVICE)Device, IPV4_PROTOCOL_TCP, TCP_OnIPv4Packet);
            DEBUG(TEXT("[NetworkManager_InitializeDevice] TCP handler registered for protocol %d on device %x"), IPV4_PROTOCOL_TCP, (U32)Device);

            DEBUG(TEXT("[NetworkManager_InitializeDevice] Network stack initialized for device %s"),
                  Device->Driver->Product);
        }
    }
}

/************************************************************************/

/**
 * @brief Network manager task function.
 *
 * This task runs periodically to maintain the network stack
 * (RX polling, ARP cache aging, TCP timer updates).
 *
 * @param param Unused parameter
 * @return Always returns 0
 */
U32 NetworkManagerTask(LPVOID param) {
    UNUSED(param);

    U32 tickCount = 0;

    while (1) {
        // Poll all network devices for received packets
        for (U32 i = 0; i < NetworkDeviceCount; i++) {
            if (NetworkDevices[i].IsInitialized) {
                SAFE_USE_VALID_ID(NetworkDevices[i].Device, ID_PCIDEVICE) {
                    SAFE_USE_VALID_ID(NetworkDevices[i].Device->Driver, ID_DRIVER) {
                        NETWORKPOLL poll = {.Device = NetworkDevices[i].Device};
                        NetworkDevices[i].Device->Driver->Command(DF_NT_POLL, (U32)(LPVOID)&poll);
                    }
                }
            }
        }

        // Update ARP cache and TCP timers every 100 polls (approximately 1 second)
        if ((tickCount % 100) == 0) {
            // Update ARP cache for each device
            for (U32 i = 0; i < NetworkDeviceCount; i++) {
                if (NetworkDevices[i].IsInitialized) {
                    ARP_Tick((LPDEVICE)NetworkDevices[i].Device);
                }
            }
            TCP_Update();
            SocketUpdate();
        }

        tickCount++;

        DoSystemCall(SYSCALL_Sleep, 5);
    }

    return 0;
}

/************************************************************************/

LPPCI_DEVICE NetworkManager_GetPrimaryDevice(void) {
    // Return the first initialized network device
    for (U32 i = 0; i < NetworkDeviceCount; i++) {
        if (NetworkDevices[i].IsInitialized) {
            return NetworkDevices[i].Device;
        }
    }
    return NULL;
}
