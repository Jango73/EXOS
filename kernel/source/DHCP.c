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


    Dynamic Host Configuration Protocol (DHCP)

\************************************************************************/

#include "../include/DHCP.h"
#include "../include/UDP.h"
#include "../include/UDPContext.h"
#include "../include/IPv4.h"
#include "../include/Device.h"
#include "../include/Heap.h"
#include "../include/ID.h"
#include "../include/Log.h"
#include "../include/Memory.h"
#include "../include/String.h"
#include "../include/System.h"
#include "../include/Clock.h"
#include "../include/NetworkManager.h"
#include "../include/Kernel.h"

/************************************************************************/
// Global device pointer

static LPDEVICE g_DHCPDevice = NULL;

/************************************************************************/

LPDHCP_CONTEXT DHCP_GetContext(LPDEVICE Device) {
    return (LPDHCP_CONTEXT)GetDeviceContext(Device, ID_DHCP);
}

/************************************************************************/

/**
 * @brief Generates a pseudo-random transaction ID.
 *
 * @return Transaction ID.
 */

static U32 DHCP_GenerateXID(void) {
    static U32 Counter = 0x12345678;
    Counter = (Counter * 1103515245 + 12345) & 0x7FFFFFFF;
    return Counter;
}

/************************************************************************/

/**
 * @brief Writes a DHCP option to the options buffer.
 *
 * @param Options     Pointer to options buffer.
 * @param Offset      Current offset in buffer.
 * @param Code        Option code.
 * @param Length      Option length.
 * @param Data        Option data.
 * @return New offset after writing option.
 */

static U32 DHCP_WriteOption(U8* Options, U32 Offset, U8 Code, U8 Length, const U8* Data) {
    Options[Offset++] = Code;
    Options[Offset++] = Length;
    MemoryCopy(Options + Offset, Data, Length);
    return Offset + Length;
}

/************************************************************************/

/**
 * @brief Parses DHCP options from a message.
 *
 * @param Context       DHCP context.
 * @param Options       Options buffer.
 * @param OptionsLength Length of options buffer.
 * @param MessageType   Output: message type.
 * @return 1 on success, 0 on failure.
 */

static int DHCP_ParseOptions(LPDHCP_CONTEXT Context, const U8* Options, U32 OptionsLength, U8* MessageType) {
    U32 Index = 0;
    U8 Code, Length;

    SAFE_USE_2(Context, Options) {
        *MessageType = 0;

        while (Index < OptionsLength) {
            Code = Options[Index++];

            if (Code == DHCP_OPTION_END) {
                break;
            }

            if (Code == DHCP_OPTION_PAD) {
                continue;
            }

            if (Index >= OptionsLength) {
                ERROR(TEXT("[DHCP_ParseOptions] Truncated option"));
                return 0;
            }

            Length = Options[Index++];

            if (Index + Length > OptionsLength) {
                ERROR(TEXT("[DHCP_ParseOptions] Option length exceeds buffer"));
                return 0;
            }

            switch (Code) {
                case DHCP_OPTION_MESSAGE_TYPE:
                    if (Length == 1) {
                        *MessageType = Options[Index];
                        DEBUG(TEXT("[DHCP_ParseOptions] Message Type: %u"), *MessageType);
                    }
                    break;

                case DHCP_OPTION_SUBNET_MASK:
                    if (Length == 4) {
                        MemoryCopy(&Context->SubnetMask_Be, Options + Index, 4);
                        U32 Mask = Ntohl(Context->SubnetMask_Be);
                        DEBUG(TEXT("[DHCP_ParseOptions] Subnet Mask: %u.%u.%u.%u"),
                              (Mask >> 24) & 0xFF, (Mask >> 16) & 0xFF, (Mask >> 8) & 0xFF, Mask & 0xFF);
                    }
                    break;

                case DHCP_OPTION_ROUTER:
                    if (Length >= 4) {
                        MemoryCopy(&Context->Gateway_Be, Options + Index, 4);
                        U32 GW = Ntohl(Context->Gateway_Be);
                        DEBUG(TEXT("[DHCP_ParseOptions] Gateway: %u.%u.%u.%u"),
                              (GW >> 24) & 0xFF, (GW >> 16) & 0xFF, (GW >> 8) & 0xFF, GW & 0xFF);
                    }
                    break;

                case DHCP_OPTION_DNS_SERVER:
                    if (Length >= 4) {
                        MemoryCopy(&Context->DNSServer_Be, Options + Index, 4);
                        U32 DNS = Ntohl(Context->DNSServer_Be);
                        DEBUG(TEXT("[DHCP_ParseOptions] DNS Server: %u.%u.%u.%u"),
                              (DNS >> 24) & 0xFF, (DNS >> 16) & 0xFF, (DNS >> 8) & 0xFF, DNS & 0xFF);
                    }
                    break;

                case DHCP_OPTION_LEASE_TIME:
                    if (Length == 4) {
                        U32 LeaseTimeBe;
                        MemoryCopy(&LeaseTimeBe, Options + Index, 4);
                        Context->LeaseTime = Ntohl(LeaseTimeBe);
                        DEBUG(TEXT("[DHCP_ParseOptions] Lease Time: %u seconds"), Context->LeaseTime);
                    }
                    break;

                case DHCP_OPTION_SERVER_ID:
                    if (Length == 4) {
                        MemoryCopy(&Context->ServerID_Be, Options + Index, 4);
                        U32 SID = Ntohl(Context->ServerID_Be);
                        DEBUG(TEXT("[DHCP_ParseOptions] Server ID: %u.%u.%u.%u"),
                              (SID >> 24) & 0xFF, (SID >> 16) & 0xFF, (SID >> 8) & 0xFF, SID & 0xFF);
                    }
                    break;

                default:
                    DEBUG(TEXT("[DHCP_ParseOptions] Skipping option %u (length %u)"), Code, Length);
                    break;
            }

            Index += Length;
        }

        return 1;
    }

    return 0;
}

/************************************************************************/

/**
 * @brief Sends a DHCP DISCOVER message.
 *
 * @param Device Network device.
 */

static void DHCP_SendDiscover(LPDEVICE Device) {
    LPDHCP_CONTEXT Context;
    DHCP_MESSAGE Message;
    U32 OptionsOffset;
    U8 MessageType;
    U8 ParameterList[4];

    if (Device == NULL) return;

    Context = DHCP_GetContext(Device);
    SAFE_USE(Context) {
        DEBUG(TEXT("[DHCP_SendDiscover] Sending DHCP DISCOVER"));

        MemorySet(&Message, 0, sizeof(DHCP_MESSAGE));

        Message.Op = DHCP_OP_REQUEST;
        Message.HType = DHCP_HTYPE_ETHERNET;
        Message.HLen = DHCP_HLEN_ETHERNET;
        Message.Hops = 0;
        Message.XID = Htonl(Context->TransactionID);
        Message.Secs = 0;
        Message.Flags = Htons(0x8000);  // Broadcast flag
        Message.CIAddr = 0;
        Message.YIAddr = 0;
        Message.SIAddr = 0;
        Message.GIAddr = 0;
        MemoryCopy(Message.CHAddr, Context->LocalMacAddress, 6);
        Message.MagicCookie = Htonl(DHCP_MAGIC_COOKIE);

        // Build options
        OptionsOffset = 0;

        // Option 53: DHCP Message Type = DISCOVER
        MessageType = DHCP_DISCOVER;
        OptionsOffset = DHCP_WriteOption(Message.Options, OptionsOffset, DHCP_OPTION_MESSAGE_TYPE, 1, &MessageType);

        // Option 55: Parameter Request List
        ParameterList[0] = DHCP_OPTION_SUBNET_MASK;
        ParameterList[1] = DHCP_OPTION_ROUTER;
        ParameterList[2] = DHCP_OPTION_DNS_SERVER;
        ParameterList[3] = DHCP_OPTION_LEASE_TIME;
        OptionsOffset = DHCP_WriteOption(Message.Options, OptionsOffset, DHCP_OPTION_PARAMETER_LIST, 4, ParameterList);

        // Option 255: End
        Message.Options[OptionsOffset++] = DHCP_OPTION_END;

        // Send via UDP (broadcast to 255.255.255.255:67)
        UDP_Send(Device, 0xFFFFFFFF, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, (const U8*)&Message, sizeof(DHCP_MESSAGE));

        Context->State = DHCP_STATE_SELECTING;
        Context->StartMillis = GetSystemTime();
    }
}

/************************************************************************/

/**
 * @brief Sends a DHCP REQUEST message.
 *
 * @param Device Network device.
 */

static void DHCP_SendRequest(LPDEVICE Device) {
    LPDHCP_CONTEXT Context;
    DHCP_MESSAGE Message;
    U32 OptionsOffset;
    U8 MessageType;
    U32 RequestedIP_Be;
    U32 ServerID_Be;

    if (Device == NULL) return;

    Context = DHCP_GetContext(Device);
    SAFE_USE(Context) {
        DEBUG(TEXT("[DHCP_SendRequest] Sending DHCP REQUEST"));

        MemorySet(&Message, 0, sizeof(DHCP_MESSAGE));

        Message.Op = DHCP_OP_REQUEST;
        Message.HType = DHCP_HTYPE_ETHERNET;
        Message.HLen = DHCP_HLEN_ETHERNET;
        Message.Hops = 0;
        Message.XID = Htonl(Context->TransactionID);
        Message.Secs = 0;
        Message.Flags = Htons(0x8000);  // Broadcast flag
        Message.CIAddr = 0;
        Message.YIAddr = 0;
        Message.SIAddr = 0;
        Message.GIAddr = 0;
        MemoryCopy(Message.CHAddr, Context->LocalMacAddress, 6);
        Message.MagicCookie = Htonl(DHCP_MAGIC_COOKIE);

        // Build options
        OptionsOffset = 0;

        // Option 53: DHCP Message Type = REQUEST
        MessageType = DHCP_REQUEST;
        OptionsOffset = DHCP_WriteOption(Message.Options, OptionsOffset, DHCP_OPTION_MESSAGE_TYPE, 1, &MessageType);

        // Option 50: Requested IP Address
        RequestedIP_Be = Context->OfferedIP_Be;
        OptionsOffset = DHCP_WriteOption(Message.Options, OptionsOffset, DHCP_OPTION_REQUESTED_IP, 4, (const U8*)&RequestedIP_Be);

        // Option 54: Server Identifier
        ServerID_Be = Context->ServerID_Be;
        OptionsOffset = DHCP_WriteOption(Message.Options, OptionsOffset, DHCP_OPTION_SERVER_ID, 4, (const U8*)&ServerID_Be);

        // Option 255: End
        Message.Options[OptionsOffset++] = DHCP_OPTION_END;

        // Send via UDP (broadcast to 255.255.255.255:67)
        UDP_Send(Device, 0xFFFFFFFF, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, (const U8*)&Message, sizeof(DHCP_MESSAGE));

        Context->State = DHCP_STATE_REQUESTING;
        Context->StartMillis = GetSystemTime();
    }
}

/************************************************************************/

/**
 * @brief Handles incoming DHCP messages.
 *
 * @param Device          Network device.
 * @param SourceIP        Source IP address (big-endian).
 * @param SourcePort      Source port (host byte order).
 * @param DestinationPort Destination port (host byte order).
 * @param Payload         UDP payload.
 * @param PayloadLength   Payload length.
 */

void DHCP_OnUDPPacket(U32 SourceIP, U16 SourcePort, U16 DestinationPort, const U8* Payload, U32 PayloadLength) {
    LPDHCP_CONTEXT Context;
    LPDHCP_MESSAGE Message;
    U8 MessageType;
    U32 XID;

    UNUSED(SourceIP);
    UNUSED(SourcePort);
    UNUSED(DestinationPort);

    if (g_DHCPDevice == NULL) return;

    Context = DHCP_GetContext(g_DHCPDevice);
    SAFE_USE(Context) {
        SAFE_USE_2(Context, Payload) {
            // Minimum DHCP packet size: fixed fields up to MagicCookie
            if (PayloadLength < DHCP_FIXED_FIELDS_SIZE) {
                ERROR(TEXT("[DHCP_OnUDPPacket] Packet too small: %u bytes"), PayloadLength);
                return;
            }

            Message = (LPDHCP_MESSAGE)Payload;

            // Validate magic cookie
            if (Ntohl(Message->MagicCookie) != DHCP_MAGIC_COOKIE) {
                ERROR(TEXT("[DHCP_OnUDPPacket] Invalid magic cookie: %x"), Ntohl(Message->MagicCookie));
                return;
            }

            // Check transaction ID
            XID = Ntohl(Message->XID);
            if (XID != Context->TransactionID) {
                DEBUG(TEXT("[DHCP_OnUDPPacket] Transaction ID mismatch: expected %x, got %x"),
                      Context->TransactionID, XID);
                return;
            }

            // Parse options (actual options length = total payload - fixed fields)
            U32 OptionsLength = PayloadLength - DHCP_FIXED_FIELDS_SIZE;
            if (!DHCP_ParseOptions(Context, Message->Options, OptionsLength, &MessageType)) {
                ERROR(TEXT("[DHCP_OnUDPPacket] Failed to parse options"));
                return;
            }

            DEBUG(TEXT("[DHCP_OnUDPPacket] Received message type %u in state %u"), MessageType, Context->State);

            // Handle message based on state
            switch (Context->State) {
                case DHCP_STATE_SELECTING:
                    if (MessageType == DHCP_OFFER) {
                        Context->OfferedIP_Be = Message->YIAddr;
                        U32 OfferedIP = Ntohl(Context->OfferedIP_Be);
                        DEBUG(TEXT("[DHCP_OnUDPPacket] Received OFFER: %u.%u.%u.%u"),
                              (OfferedIP >> 24) & 0xFF, (OfferedIP >> 16) & 0xFF,
                              (OfferedIP >> 8) & 0xFF, OfferedIP & 0xFF);
                        DHCP_SendRequest(g_DHCPDevice);
                    }
                    break;

                case DHCP_STATE_REQUESTING:
                    if (MessageType == DHCP_ACK) {
                        Context->OfferedIP_Be = Message->YIAddr;
                        U32 AssignedIP = Ntohl(Context->OfferedIP_Be);
                        DEBUG(TEXT("[DHCP_OnUDPPacket] Received ACK: %u.%u.%u.%u"),
                              (AssignedIP >> 24) & 0xFF, (AssignedIP >> 16) & 0xFF,
                              (AssignedIP >> 8) & 0xFF, AssignedIP & 0xFF);

                        // Configure IPv4 with received parameters
                        IPv4_SetNetworkConfig(g_DHCPDevice, Context->OfferedIP_Be, Context->SubnetMask_Be, Context->Gateway_Be);

                        Context->State = DHCP_STATE_BOUND;
                        Context->LeaseStartMillis = GetSystemTime();

                        // Calculate renewal times (T1 = 50% of lease, T2 = 87.5% of lease)
                        Context->RenewalTime = Context->LeaseTime / 2;
                        Context->RebindTime = (Context->LeaseTime * 7) / 8;

                        // Mark network device as ready
                        SAFE_USE(Kernel.NetworkDevice) {
                            for (LPLISTNODE Node = Kernel.NetworkDevice->First; Node != NULL; Node = Node->Next) {
                                LPNETWORK_DEVICE_CONTEXT NetCtx = (LPNETWORK_DEVICE_CONTEXT)Node;
                                SAFE_USE_VALID_ID(NetCtx, ID_NETWORKDEVICE) {
                                    if ((LPDEVICE)NetCtx->Device == g_DHCPDevice) {
                                        NetCtx->LocalIPv4_Be = Context->OfferedIP_Be;
                                        U32 AssignedHost = Ntohl(Context->OfferedIP_Be);
                                        DEBUG(TEXT("[DHCP_OnUDPPacket] Updated network context IP to %u.%u.%u.%u"),
                                              (AssignedHost >> 24) & 0xFF,
                                              (AssignedHost >> 16) & 0xFF,
                                              (AssignedHost >> 8) & 0xFF,
                                              AssignedHost & 0xFF);
                                        NetCtx->IsReady = TRUE;
                                        DEBUG(TEXT("[DHCP_OnUDPPacket] Network device marked as ready"));
                                        break;
                                    }
                                }
                            }
                        }

                        DEBUG(TEXT("[DHCP_OnUDPPacket] DHCP configuration complete"));
                    } else if (MessageType == DHCP_NAK) {
                        ERROR(TEXT("[DHCP_OnUDPPacket] Received NAK, restarting DHCP"));
                        Context->State = DHCP_STATE_INIT;
                        DHCP_Start(g_DHCPDevice);
                    }
                    break;

                default:
                    DEBUG(TEXT("[DHCP_OnUDPPacket] Ignoring message in state %u"), Context->State);
                    break;
            }
        }
    }
}

/************************************************************************/

/**
 * @brief Initializes DHCP context for a device.
 *
 * @param Device Network device.
 */

void DHCP_Initialize(LPDEVICE Device) {
    LPDHCP_CONTEXT Context;
    NETWORKGETINFO GetInfo;
    NETWORKINFO Info;

    if (Device == NULL) return;

    Context = (LPDHCP_CONTEXT)KernelHeapAlloc(sizeof(DHCP_CONTEXT));
    if (Context == NULL) {
        ERROR(TEXT("[DHCP_Initialize] Failed to allocate DHCP context"));
        return;
    }

    MemorySet(Context, 0, sizeof(DHCP_CONTEXT));
    Context->Device = Device;
    Context->State = DHCP_STATE_INIT;
    Context->TransactionID = DHCP_GenerateXID();

    // Get MAC address
    MemorySet(&GetInfo, 0, sizeof(GetInfo));
    MemorySet(&Info, 0, sizeof(Info));
    GetInfo.Device = (LPPCI_DEVICE)Device;
    GetInfo.Info = &Info;

    SAFE_USE_VALID_ID(Device, ID_PCIDEVICE) {
        SAFE_USE_VALID_ID(((LPPCI_DEVICE)Device)->Driver, ID_DRIVER) {
            if (((LPPCI_DEVICE)Device)->Driver->Command(DF_NT_GETINFO, (U32)(LPVOID)&GetInfo) == DF_ERROR_SUCCESS) {
                MemoryCopy(Context->LocalMacAddress, Info.MAC, 6);
                DEBUG(TEXT("[DHCP_Initialize] MAC: %x:%x:%x:%x:%x:%x"),
                      (U32)Info.MAC[0], (U32)Info.MAC[1], (U32)Info.MAC[2],
                      (U32)Info.MAC[3], (U32)Info.MAC[4], (U32)Info.MAC[5]);
            } else {
                ERROR(TEXT("[DHCP_Initialize] DF_NT_GETINFO failed"));
                KernelHeapFree(Context);
                return;
            }
        }
    }

    SetDeviceContext(Device, ID_DHCP, (LPVOID)Context);

    // Store global device reference
    g_DHCPDevice = Device;

    // Register UDP port handler for DHCP client port
    UDP_RegisterPortHandler(Device, DHCP_CLIENT_PORT, DHCP_OnUDPPacket);

    DEBUG(TEXT("[DHCP_Initialize] DHCP initialized for device"));
}

/************************************************************************/

/**
 * @brief Destroys DHCP context for a device.
 *
 * @param Device Network device.
 */

void DHCP_Destroy(LPDEVICE Device) {
    LPDHCP_CONTEXT Context;

    if (Device == NULL) return;

    Context = DHCP_GetContext(Device);
    SAFE_USE(Context) {
        UDP_UnregisterPortHandler(Device, DHCP_CLIENT_PORT);
        KernelHeapFree(Context);
        SetDeviceContext(Device, ID_DHCP, NULL);
        DEBUG(TEXT("[DHCP_Destroy] DHCP context destroyed"));
    }
}

/************************************************************************/

/**
 * @brief Starts DHCP discovery process.
 *
 * @param Device Network device.
 */

void DHCP_Start(LPDEVICE Device) {
    LPDHCP_CONTEXT Context;

    if (Device == NULL) return;

    Context = DHCP_GetContext(Device);
    SAFE_USE(Context) {
        DEBUG(TEXT("[DHCP_Start] Starting DHCP discovery"));
        Context->State = DHCP_STATE_INIT;
        Context->TransactionID = DHCP_GenerateXID();
        Context->RetryCount = 0;
        DHCP_SendDiscover(Device);
    }
}

/************************************************************************/

/**
 * @brief Periodic tick for DHCP state management.
 *
 * @param Device Network device.
 */

void DHCP_Tick(LPDEVICE Device) {
    LPDHCP_CONTEXT Context;
    U32 CurrentMillis;
    U32 ElapsedMillis;
    U32 ElapsedSeconds;

    if (Device == NULL) return;

    Context = DHCP_GetContext(Device);
    SAFE_USE(Context) {
        CurrentMillis = GetSystemTime();

        switch (Context->State) {
            case DHCP_STATE_SELECTING:
            case DHCP_STATE_REQUESTING:
                ElapsedMillis = CurrentMillis - Context->StartMillis;
                if (ElapsedMillis > DHCP_RETRY_TIMEOUT_MILLIS) {
                    Context->RetryCount++;
                    if (Context->RetryCount >= DHCP_MAX_RETRIES) {
                        DEBUG(TEXT("[DHCP_Tick] DHCP failed after %u retries"), Context->RetryCount);
                        Context->State = DHCP_STATE_FAILED;
                    } else {
                        WARNING(TEXT("[DHCP_Tick] DHCP timeout, retry %u/%u"), Context->RetryCount, DHCP_MAX_RETRIES);
                        if (Context->State == DHCP_STATE_SELECTING) {
                            DHCP_SendDiscover(Device);
                        } else {
                            DHCP_SendRequest(Device);
                        }
                    }
                }
                break;

            case DHCP_STATE_BOUND:
                ElapsedMillis = CurrentMillis - Context->LeaseStartMillis;
                ElapsedSeconds = ElapsedMillis;

                if (ElapsedSeconds >= Context->LeaseTime) {
                    WARNING(TEXT("[DHCP_Tick] Lease expired, restarting DHCP"));
                    DHCP_Start(Device);
                } else if (ElapsedSeconds >= Context->RebindTime) {
                    DEBUG(TEXT("[DHCP_Tick] Entering REBINDING state"));
                    Context->State = DHCP_STATE_REBINDING;
                    // TODO: Implement rebinding (broadcast REQUEST)
                } else if (ElapsedSeconds >= Context->RenewalTime) {
                    DEBUG(TEXT("[DHCP_Tick] Entering RENEWING state"));
                    Context->State = DHCP_STATE_RENEWING;
                    // TODO: Implement renewal (unicast REQUEST to server)
                }
                break;

            default:
                break;
        }
    }
}
