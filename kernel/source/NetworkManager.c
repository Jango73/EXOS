
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
#include "../include/UDP.h"
#include "../include/DHCP.h"
#include "../include/DHCPContext.h"
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
    SAFE_USE(configValue) {
        U32 parsedIP = ParseIPAddress(configValue);
        if (parsedIP != 0) {
            return parsedIP;
        }
    }
    return fallbackValue;
}

/************************************************************************/

// Helper function to get per-device network configuration
static U32 NetworkManager_GetDeviceConfigIP(LPCSTR deviceName, LPCSTR configKey, LPCSTR fallbackGlobalKey, U32 fallbackValue) {
    STR path[128];
    U32 index = 0;

    // Try to find the device in [[NetworkInterface]] sections
    while (index < LOOP_LIMIT) {
        // Check if this NetworkInterface has a DeviceName that matches
        StringPrintFormat(path, TEXT("NetworkInterface.%u.DeviceName"), index);

        LPCSTR configDeviceName = GetConfigurationValue(path);
        if (configDeviceName == NULL) break; // No more NetworkInterface entries

        // Check if this is the device we're looking for
        if (STRINGS_EQUAL(configDeviceName, deviceName)) {
            // Found the device, get the requested config value
            StringPrintFormat(path, TEXT("NetworkInterface.%u.%s"), index, configKey);

            LPCSTR configValue = GetConfigurationValue(path);
            SAFE_USE(configValue) {
                U32 parsedIP = ParseIPAddress(configValue);
                if (parsedIP != 0) {
                    DEBUG(TEXT("[NetworkManager_GetDeviceConfigIP] Device %s: %s = %s"), deviceName, configKey, configValue);
                    return parsedIP;
                }
            }
            break;
        }
        index++;
    }

    // Fall back to global configuration
    SAFE_USE(fallbackGlobalKey) {
        U32 globalIP = NetworkManager_GetConfigIP(fallbackGlobalKey, fallbackValue);
        DEBUG(TEXT("[NetworkManager_GetDeviceConfigIP] Device %s: %s = global"), deviceName, configKey);
        return globalIP;
    }

    DEBUG(TEXT("[NetworkManager_GetDeviceConfigIP] Device %s: Using fallback value for %s"), deviceName, configKey);
    return fallbackValue;
}

/************************************************************************/

// Forward declaration
static void NetworkManager_RxCallback(const U8 *Frame, U32 Length, LPVOID UserData);

/**
 * @brief Internal frame reception handler that dispatches to protocol layers.
 *
 * @param Frame Pointer to the received ethernet frame
 * @param Length Length of the frame in bytes
 * @param UserData Pointer to the NETWORK_DEVICE_CONTEXT
 */
static void NetworkManager_RxCallback(const U8 *Frame, U32 Length, LPVOID UserData) {
    LPNETWORK_DEVICE_CONTEXT Context = (LPNETWORK_DEVICE_CONTEXT)UserData;
    LPDEVICE Device = NULL;

    DEBUG(TEXT("[NetworkManager_RxCallback] Entry Context=%X Frame=%X Length=%u"), (U32)Context, (U32)Frame, Length);

    SAFE_USE_VALID_ID(Context, ID_NETWORKDEVICE) {
        Device = (LPDEVICE)Context->Device;
    }

    if (!Device || !Frame || Length < 14U) {
        DEBUG(TEXT("[NetworkManager_RxCallback] Bad parameters or frame too short"));
        return;
    }

    DEBUG(TEXT("[NetworkManager_RxCallback] Device=%X"), (U32)Device);

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

            for (Node = Kernel.PCIDevice->First; Node != NULL; Node = Node->Next) {
                LPPCI_DEVICE Device = (LPPCI_DEVICE)Node;

                SAFE_USE_VALID_ID(Device, ID_PCIDEVICE) {
                    SAFE_USE_VALID_ID(Device->Driver, ID_DRIVER) {

                        if (Device->Driver->Type == DRIVER_TYPE_NETWORK) {
                            // Allocate a new network device context
                            LPNETWORK_DEVICE_CONTEXT Context = (LPNETWORK_DEVICE_CONTEXT)
                                CreateKernelObject(sizeof(NETWORK_DEVICE_CONTEXT), ID_NETWORKDEVICE);

                            SAFE_USE(Context) {
                                Context->Device = Device;

                                // Generate device name
                                GetDefaultDeviceName(Device->Name, (LPDEVICE)Device, DRIVER_TYPE_NETWORK);

                                // Use per-device configuration with fallback to global config
                                Context->LocalIPv4_Be = NetworkManager_GetDeviceConfigIP(Device->Name, TEXT("LocalIP"), TEXT(CONFIG_NETWORK_LOCAL_IP), Htonl(0xC0A8380AU + Count));
                                Context->IsInitialized = FALSE;
                                Context->OriginalCallback = NULL;

                                // Add to kernel network device list (thread-safe with MUTEX_KERNEL)
                                LockMutex(MUTEX_KERNEL, INFINITY);
                                ListAddTail(Kernel.NetworkDevice, (LPVOID)Context);
                                UnlockMutex(MUTEX_KERNEL);

                                Count++;
                                DEBUG(TEXT("[NetworkManager_FindNetworkDevices] Found network device %u: %s with IP fallback base+%u"), Count-1, Device->Driver->Product, Count-1);
                            } else {
                                ERROR(TEXT("[NetworkManager_FindNetworkDevices] Failed to allocate network device context"));
                            }
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

    DEBUG(TEXT("[NetworkManager_FindNetworkDevices] Found %u network devices"), Kernel.NetworkDevice->NumItems);
    return Kernel.NetworkDevice->NumItems;
}

/************************************************************************/

void InitializeNetwork(void) {
    DEBUG(TEXT("[InitializeNetwork] Enter"));

    // Find all network devices
    NetworkManager_FindNetworkDevices();

    if (Kernel.NetworkDevice->NumItems == 0) {
        WARNING(TEXT("[InitializeNetwork] No network devices found"));
        return;
    }

    // Initialize each network device
    SAFE_USE(Kernel.NetworkDevice) {
        for (LPLISTNODE Node = Kernel.NetworkDevice->First; Node != NULL; Node = Node->Next) {
            LPNETWORK_DEVICE_CONTEXT Ctx = (LPNETWORK_DEVICE_CONTEXT)Node;
            SAFE_USE_VALID_ID(Ctx, ID_NETWORKDEVICE) {
                NetworkManager_InitializeDevice(Ctx->Device, Ctx->LocalIPv4_Be);
            }
        }
    }

    DEBUG(TEXT("[InitializeNetwork] Initialized %u network devices"), Kernel.NetworkDevice->NumItems);
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

            // Find device context in the network device list
            LPNETWORK_DEVICE_CONTEXT DeviceContext = NULL;
            SAFE_USE(Kernel.NetworkDevice) {
                for (LPLISTNODE Node = Kernel.NetworkDevice->First; Node != NULL; Node = Node->Next) {
                    LPNETWORK_DEVICE_CONTEXT Ctx = (LPNETWORK_DEVICE_CONTEXT)Node;
                    SAFE_USE_VALID_ID(Ctx, ID_NETWORKDEVICE) {
                        if (Ctx->Device == Device) {
                            DeviceContext = Ctx;
                            break;
                        }
                    }
                }
            }

            if (DeviceContext == NULL) {
                ERROR(TEXT("[NetworkManager_InitializeDevice] Device %X not found in network device list!"), (U32)Device);
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

            // Initialize UDP subsystem for this device
            DEBUG(TEXT("[NetworkManager_InitializeDevice] Initializing UDP layer"));
            UDP_Initialize((LPDEVICE)Device);

            // Initialize DHCP subsystem if enabled in configuration
            LPCSTR UseDHCP = GetConfigurationValue(TEXT(CONFIG_NETWORK_USE_DHCP));
            if (UseDHCP != NULL && STRINGS_EQUAL(UseDHCP, TEXT("1"))) {
                DEBUG(TEXT("[NetworkManager_InitializeDevice] Initializing DHCP layer"));
                DHCP_Initialize((LPDEVICE)Device);
                DHCP_Start((LPDEVICE)Device);
                DEBUG(TEXT("[NetworkManager_InitializeDevice] DHCP started for device %s"), Device->Driver->Product);
            } else {
                DEBUG(TEXT("[NetworkManager_InitializeDevice] DHCP disabled, using static IP configuration"));
            }

            // Configure network settings from TOML configuration (per-device with global fallback)
            U32 NetmaskBe = NetworkManager_GetDeviceConfigIP(Device->Name, TEXT("Netmask"), TEXT(CONFIG_NETWORK_NETMASK), Htonl(0xFFFFFF00));
            U32 GatewayBe = NetworkManager_GetDeviceConfigIP(Device->Name, TEXT("Gateway"), TEXT(CONFIG_NETWORK_GATEWAY), Htonl(0xC0A83801));
            IPv4_SetNetworkConfig((LPDEVICE)Device, LocalIPv4_Be, NetmaskBe, GatewayBe);

            // Initialize TCP subsystem (global for all devices)
            static BOOL TCPInitialized = FALSE;
            if (!TCPInitialized) {
                DEBUG(TEXT("[NetworkManager_InitializeDevice] Initializing TCP layer"));
                TCP_Initialize();
                TCPInitialized = TRUE;
            }

            // Install RX callback with device context as UserData
            NETWORKSETRXCB SetRxCb = {.Device = Device, .Callback = NetworkManager_RxCallback, .UserData = (LPVOID)DeviceContext};
            DEBUG(TEXT("[NetworkManager_InitializeDevice] Installing RX callback %X with UserData %X"), (U32)NetworkManager_RxCallback, (U32)DeviceContext);
            U32 Result = Device->Driver->Command(DF_NT_SETRXCB, (U32)(LPVOID)&SetRxCb);
            DEBUG(TEXT("[NetworkManager_InitializeDevice] RX callback installation result: %u"), Result);

            // Mark device as initialized
            DeviceContext->IsInitialized = TRUE;

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
        SAFE_USE(Kernel.NetworkDevice) {
            for (LPLISTNODE Node = Kernel.NetworkDevice->First; Node != NULL; Node = Node->Next) {
                LPNETWORK_DEVICE_CONTEXT Ctx = (LPNETWORK_DEVICE_CONTEXT)Node;
                SAFE_USE_VALID_ID(Ctx, ID_NETWORKDEVICE) {
                    if (Ctx->IsInitialized) {
                        SAFE_USE_VALID_ID(Ctx->Device, ID_PCIDEVICE) {
                            SAFE_USE_VALID_ID(Ctx->Device->Driver, ID_DRIVER) {
                                NETWORKPOLL poll = {.Device = Ctx->Device};
                                Ctx->Device->Driver->Command(DF_NT_POLL, (U32)(LPVOID)&poll);
                            }
                        }
                    }
                }
            }
        }

        // Update ARP cache and TCP timers every 100 polls (approximately 1 second)
        if ((tickCount % 100) == 0) {
            // Update ARP cache for each device
            SAFE_USE(Kernel.NetworkDevice) {
                for (LPLISTNODE Node = Kernel.NetworkDevice->First; Node != NULL; Node = Node->Next) {
                    LPNETWORK_DEVICE_CONTEXT Ctx = (LPNETWORK_DEVICE_CONTEXT)Node;
                    SAFE_USE_VALID_ID(Ctx, ID_NETWORKDEVICE) {
                        if (Ctx->IsInitialized) {
                            ARP_Tick((LPDEVICE)Ctx->Device);
                            DHCP_Tick((LPDEVICE)Ctx->Device);
                        }
                    }
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
    SAFE_USE(Kernel.NetworkDevice) {
        for (LPLISTNODE Node = Kernel.NetworkDevice->First; Node != NULL; Node = Node->Next) {
            LPNETWORK_DEVICE_CONTEXT Ctx = (LPNETWORK_DEVICE_CONTEXT)Node;
            SAFE_USE_VALID_ID(Ctx, ID_NETWORKDEVICE) {
                if (Ctx->IsInitialized) {
                    return Ctx->Device;
                }
            }
        }
    }
    return NULL;
}
