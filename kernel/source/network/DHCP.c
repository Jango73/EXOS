
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

#include "network/DHCP.h"
#include "network/UDP.h"
#include "network/UDPContext.h"
#include "network/IPv4.h"
#include "network/ARP.h"
#include "Device.h"
#include "Heap.h"
#include "ID.h"
#include "Log.h"
#include "Memory.h"
#include "CoreString.h"
#include "System.h"
#include "Clock.h"
#include "network/NetworkManager.h"
#include "Kernel.h"

/************************************************************************/
// Global device pointer

static LPDEVICE g_DHCPDevice = NULL;

/************************************************************************/

LPDHCP_CONTEXT DHCP_GetContext(LPDEVICE Device) {
    return (LPDHCP_CONTEXT)GetDeviceContext(Device, KOID_DHCP);
}

/************************************************************************/

/**
 * @brief Retrieves the network device context associated with a device.
 *
 * @param Device Network device.
 * @return Pointer to the NETWORK_DEVICE_CONTEXT or NULL if not found.
 */
static LPNETWORK_DEVICE_CONTEXT DHCP_GetNetworkDeviceContext(LPDEVICE Device) {
    LPLIST NetworkDeviceList;
    LPNETWORK_DEVICE_CONTEXT Result = NULL;

    if (Device == NULL) return NULL;

    NetworkDeviceList = GetNetworkDeviceList();
    SAFE_USE(NetworkDeviceList) {
        for (LPLISTNODE Node = NetworkDeviceList->First; Node != NULL; Node = Node->Next) {
            LPNETWORK_DEVICE_CONTEXT NetCtx = (LPNETWORK_DEVICE_CONTEXT)Node;
            SAFE_USE_VALID_ID(NetCtx, KOID_NETWORKDEVICE) {
                if ((LPDEVICE)NetCtx->Device == Device) {
                    Result = NetCtx;
                    break;
                }
            }
        }
    }

    return Result;
}

/************************************************************************/

/**
 * @brief Clears ARP cache and pending IPv4 packets after configuration changes.
 *
 * @param Device Network device whose caches must be reset.
 */
static void DHCP_ResetRoutingState(LPDEVICE Device) {
    if (Device == NULL) return;

    ARP_FlushCache(Device);
    IPv4_ClearPendingPackets(Device);
    DEBUG(TEXT("[DHCP_ResetRoutingState] Cleared ARP cache and pending IPv4 packets"));
}

/************************************************************************/

/**
 * @brief Applies static network configuration when DHCP exhausts retries.
 *
 * @param Context DHCP context.
 * @return TRUE if fallback applied, FALSE otherwise.
 */
static BOOL DHCP_ApplyStaticFallback(LPDHCP_CONTEXT Context) {
    LPNETWORK_DEVICE_CONTEXT NetCtx;
    U32 LocalIPv4_Be;
    U32 Netmask_Be;
    U32 Gateway_Be;

    if (Context == NULL) return FALSE;

    NetCtx = DHCP_GetNetworkDeviceContext(Context->Device);
    SAFE_USE_VALID_ID(NetCtx, KOID_NETWORKDEVICE) {
        LocalIPv4_Be = NetCtx->StaticConfig.LocalIPv4_Be;
        Netmask_Be = NetCtx->StaticConfig.SubnetMask_Be;
        Gateway_Be = NetCtx->StaticConfig.Gateway_Be;

        if (LocalIPv4_Be == 0 || Netmask_Be == 0) {
            ERROR(TEXT("[DHCP_ApplyStaticFallback] No static configuration available for fallback"));
            return FALSE;
        }

        DHCP_ResetRoutingState(Context->Device);
        IPv4_SetNetworkConfig(Context->Device, LocalIPv4_Be, Netmask_Be, Gateway_Be);

        NetCtx->ActiveConfig.LocalIPv4_Be = LocalIPv4_Be;
        NetCtx->ActiveConfig.SubnetMask_Be = Netmask_Be;
        NetCtx->ActiveConfig.Gateway_Be = Gateway_Be;
        NetCtx->ActiveConfig.DNSServer_Be = NetCtx->StaticConfig.DNSServer_Be;
        NetCtx->IsReady = TRUE;

        U32 IP = Ntohl(LocalIPv4_Be);
        DEBUG(TEXT("[DHCP_ApplyStaticFallback] Applied static fallback IP %u.%u.%u.%u"),
              (IP >> 24) & 0xFF,
              (IP >> 16) & 0xFF,
              (IP >> 8) & 0xFF,
              IP & 0xFF);
        return TRUE;
    }

    ERROR(TEXT("[DHCP_ApplyStaticFallback] Network context unavailable for fallback"));
    return FALSE;
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
 * @brief Calculates the timeout before retrying with capped backoff.
 *
 * @param RetryCount Number of retries already attempted.
 * @return Timeout in milliseconds.
 */

static U32 DHCP_GetRetryTimeout(U32 RetryCount) {
    U32 Shift = RetryCount;

    if (Shift > DHCP_RETRY_BACKOFF_MAX_SHIFT) {
        Shift = DHCP_RETRY_BACKOFF_MAX_SHIFT;
    }

    return DHCP_RETRY_TIMEOUT_MILLIS << Shift;
}

/************************************************************************/

/**
 * @brief Writes the client identifier option (type + MAC address).
 *
 * @param Options    Pointer to options buffer.
 * @param Offset     Current offset in buffer.
 * @param MacAddress MAC address (6 bytes).
 * @return New offset after writing option.
 */

static U32 DHCP_WriteClientIdentifier(U8* Options, U32 Offset, const U8* MacAddress) {
    U8 ClientID[DHCP_CLIENT_IDENTIFIER_LENGTH];

    MemorySet(ClientID, 0, DHCP_CLIENT_IDENTIFIER_LENGTH);
    ClientID[0] = DHCP_HTYPE_ETHERNET;
    MemoryCopy(ClientID + 1, MacAddress, DHCP_HLEN_ETHERNET);

    return DHCP_WriteOption(Options, Offset, DHCP_OPTION_CLIENT_ID, DHCP_CLIENT_IDENTIFIER_LENGTH, ClientID);
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

                case DHCP_OPTION_RENEWAL_TIME:
                    if (Length == 4) {
                        U32 RenewalBe;
                        MemoryCopy(&RenewalBe, Options + Index, 4);
                        Context->RenewalTime = Ntohl(RenewalBe);
                        DEBUG(TEXT("[DHCP_ParseOptions] Renewal Time (T1): %u seconds"), Context->RenewalTime);
                    }
                    break;

                case DHCP_OPTION_REBIND_TIME:
                    if (Length == 4) {
                        U32 RebindBe;
                        MemoryCopy(&RebindBe, Options + Index, 4);
                        Context->RebindTime = Ntohl(RebindBe);
                        DEBUG(TEXT("[DHCP_ParseOptions] Rebind Time (T2): %u seconds"), Context->RebindTime);
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
 * @brief Initializes common DHCP message fields.
 *
 * @param Message    DHCP message to initialize.
 * @param Context    DHCP context.
 * @param Flags      Flags field (host byte order).
 * @param ClientIPBe Client IP address (big-endian).
 */

static void DHCP_InitMessage(LPDHCP_MESSAGE Message, LPDHCP_CONTEXT Context, U16 Flags, U32 ClientIPBe) {
    MemorySet(Message, 0, sizeof(DHCP_MESSAGE));

    Message->Op = DHCP_OP_REQUEST;
    Message->HType = DHCP_HTYPE_ETHERNET;
    Message->HLen = DHCP_HLEN_ETHERNET;
    Message->Hops = 0;
    Message->XID = Htonl(Context->TransactionID);
    Message->Secs = 0;
    Message->Flags = Htons(Flags);
    Message->CIAddr = ClientIPBe;
    Message->YIAddr = 0;
    Message->SIAddr = 0;
    Message->GIAddr = 0;
    MemoryCopy(Message->CHAddr, Context->LocalMacAddress, DHCP_HLEN_ETHERNET);
    Message->MagicCookie = Htonl(DHCP_MAGIC_COOKIE);
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
    U8 ParameterList[6];

    if (Device == NULL) return;

    Context = DHCP_GetContext(Device);
    SAFE_USE(Context) {
        DEBUG(TEXT("[DHCP_SendDiscover] Sending DHCP DISCOVER"));

        DHCP_InitMessage(&Message, Context, DHCP_BROADCAST_FLAG, 0);

        OptionsOffset = 0;

        // Option 53: DHCP Message Type = DISCOVER
        MessageType = DHCP_DISCOVER;
        OptionsOffset = DHCP_WriteOption(Message.Options, OptionsOffset, DHCP_OPTION_MESSAGE_TYPE, 1, &MessageType);

        // Option 61: Client Identifier
        OptionsOffset = DHCP_WriteClientIdentifier(Message.Options, OptionsOffset, Context->LocalMacAddress);

        // Option 55: Parameter Request List
        ParameterList[0] = DHCP_OPTION_SUBNET_MASK;
        ParameterList[1] = DHCP_OPTION_ROUTER;
        ParameterList[2] = DHCP_OPTION_DNS_SERVER;
        ParameterList[3] = DHCP_OPTION_LEASE_TIME;
        ParameterList[4] = DHCP_OPTION_RENEWAL_TIME;
        ParameterList[5] = DHCP_OPTION_REBIND_TIME;
        OptionsOffset = DHCP_WriteOption(Message.Options, OptionsOffset, DHCP_OPTION_PARAMETER_LIST, 6, ParameterList);

        // Option 255: End
        Message.Options[OptionsOffset++] = DHCP_OPTION_END;

        // Send via UDP (broadcast to 255.255.255.255:67)
        UDP_Send(Device, DHCP_BROADCAST_IP, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, (const U8*)&Message, sizeof(DHCP_MESSAGE));

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

static BOOL DHCP_SendRequest(LPDEVICE Device, U32 TargetState) {
    LPDHCP_CONTEXT Context;
    DHCP_MESSAGE Message;
    U32 OptionsOffset;
    U8 MessageType;
    U32 RequestedIP_Be;
    U32 ServerID_Be;
    U16 Flags;
    U32 DestinationIP_Be;
    U32 ClientIP_Be;
    U32 DestinationHostOrder;
    BOOL HasClientIP;

    if (Device == NULL) return FALSE;

    Context = DHCP_GetContext(Device);
    SAFE_USE(Context) {
        Flags = DHCP_BROADCAST_FLAG;
        DestinationIP_Be = DHCP_BROADCAST_IP;
        ClientIP_Be = 0;
        RequestedIP_Be = Context->OfferedIP_Be;
        ServerID_Be = Context->ServerID_Be;
        HasClientIP = (Context->OfferedIP_Be != 0);

        if (TargetState == DHCP_STATE_RENEWING) {
            Flags = 0;
            ClientIP_Be = Context->OfferedIP_Be;
            DestinationIP_Be = (ServerID_Be != 0) ? ServerID_Be : DHCP_BROADCAST_IP;
        } else if (TargetState == DHCP_STATE_REBINDING) {
            Flags = DHCP_BROADCAST_FLAG;
            ClientIP_Be = Context->OfferedIP_Be;
            DestinationIP_Be = DHCP_BROADCAST_IP;
            ServerID_Be = 0;
        }

        if (HasClientIP == FALSE && RequestedIP_Be == 0) {
            ERROR(TEXT("[DHCP_SendRequest] No IP available for REQUEST (state %u)"), TargetState);
            return FALSE;
        }

        if (ServerID_Be == 0 && TargetState != DHCP_STATE_REBINDING) {
            ERROR(TEXT("[DHCP_SendRequest] Missing server identifier for REQUEST"));
            return FALSE;
        }

        DHCP_InitMessage(&Message, Context, Flags, ClientIP_Be);

        // Build options
        OptionsOffset = 0;

        // Option 53: DHCP Message Type = REQUEST
        MessageType = DHCP_REQUEST;
        OptionsOffset = DHCP_WriteOption(Message.Options, OptionsOffset, DHCP_OPTION_MESSAGE_TYPE, 1, &MessageType);

        // Option 61: Client Identifier
        OptionsOffset = DHCP_WriteClientIdentifier(Message.Options, OptionsOffset, Context->LocalMacAddress);

        // Option 50: Requested IP Address
        RequestedIP_Be = Context->OfferedIP_Be;
        if (RequestedIP_Be != 0) {
            OptionsOffset = DHCP_WriteOption(Message.Options, OptionsOffset, DHCP_OPTION_REQUESTED_IP, 4, (const U8*)&RequestedIP_Be);
        }

        // Option 54: Server Identifier
        ServerID_Be = Context->ServerID_Be;
        if (ServerID_Be != 0) {
            OptionsOffset = DHCP_WriteOption(Message.Options, OptionsOffset, DHCP_OPTION_SERVER_ID, 4, (const U8*)&ServerID_Be);
        }

        // Option 255: End
        Message.Options[OptionsOffset++] = DHCP_OPTION_END;

        DestinationHostOrder = Ntohl(DestinationIP_Be);
        DEBUG(TEXT("[DHCP_SendRequest] Sending DHCP REQUEST (state %u) to %u.%u.%u.%u"),
              TargetState,
              (DestinationHostOrder >> 24) & 0xFF,
              (DestinationHostOrder >> 16) & 0xFF,
              (DestinationHostOrder >> 8) & 0xFF,
              DestinationHostOrder & 0xFF);

        // Send via UDP (broadcast/unicast depending on state)
        UDP_Send(Device, DestinationIP_Be, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, (const U8*)&Message, sizeof(DHCP_MESSAGE));

        if (Context->State != TargetState) {
            Context->RetryCount = 0;
        }

        Context->State = TargetState;
        Context->StartMillis = GetSystemTime();
        return TRUE;
    }

    return FALSE;
}

/************************************************************************/

/**
 * @brief Marks the network device as not ready and clears cached IP.
 *
 * @param Context DHCP context.
 */

static void DHCP_ClearNetworkReady(LPDHCP_CONTEXT Context) {
    LPNETWORK_DEVICE_CONTEXT NetCtx;

    if (Context == NULL) return;

    DHCP_ResetRoutingState(Context->Device);

    NetCtx = DHCP_GetNetworkDeviceContext(Context->Device);
    SAFE_USE_VALID_ID(NetCtx, KOID_NETWORKDEVICE) {
        NetCtx->ActiveConfig.LocalIPv4_Be = 0;
        NetCtx->ActiveConfig.SubnetMask_Be = 0;
        NetCtx->ActiveConfig.Gateway_Be = 0;
        NetCtx->ActiveConfig.DNSServer_Be = 0;
        NetCtx->IsReady = FALSE;
        DEBUG(TEXT("[DHCP_ClearNetworkReady] Network device marked not ready"));
    }
}

/************************************************************************/

/**
 * @brief Sends a DHCP RELEASE message if lease is active.
 *
 * @param Device Network device.
 */

static void DHCP_SendRelease(LPDEVICE Device) {
    LPDHCP_CONTEXT Context;
    DHCP_MESSAGE Message;
    U32 OptionsOffset;
    U8 MessageType;
    U32 DestinationIP_Be;
    U32 DestinationHostOrder;

    if (Device == NULL) return;

    Context = DHCP_GetContext(Device);
    SAFE_USE(Context) {
        if (Context->OfferedIP_Be == 0) {
            DEBUG(TEXT("[DHCP_SendRelease] No assigned IP, skipping RELEASE"));
            return;
        }

        DestinationIP_Be = (Context->ServerID_Be != 0) ? Context->ServerID_Be : DHCP_BROADCAST_IP;

        DHCP_InitMessage(&Message, Context, 0, Context->OfferedIP_Be);

        OptionsOffset = 0;

        MessageType = DHCP_RELEASE;
        OptionsOffset = DHCP_WriteOption(Message.Options, OptionsOffset, DHCP_OPTION_MESSAGE_TYPE, 1, &MessageType);

        OptionsOffset = DHCP_WriteClientIdentifier(Message.Options, OptionsOffset, Context->LocalMacAddress);

        if (Context->ServerID_Be != 0) {
            OptionsOffset = DHCP_WriteOption(Message.Options, OptionsOffset, DHCP_OPTION_SERVER_ID, 4, (const U8*)&Context->ServerID_Be);
        }

        Message.Options[OptionsOffset++] = DHCP_OPTION_END;

        DestinationHostOrder = Ntohl(DestinationIP_Be);
        DEBUG(TEXT("[DHCP_SendRelease] Sending DHCP RELEASE to %u.%u.%u.%u"),
              (DestinationHostOrder >> 24) & 0xFF,
              (DestinationHostOrder >> 16) & 0xFF,
              (DestinationHostOrder >> 8) & 0xFF,
              DestinationHostOrder & 0xFF);

        UDP_Send(Device, DestinationIP_Be, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, (const U8*)&Message, sizeof(DHCP_MESSAGE));
        Context->State = DHCP_STATE_INIT;
        Context->RetryCount = 0;
    }
}

/************************************************************************/

/**
 * @brief Applies ACK contents to the network configuration.
 *
 * @param Context DHCP context.
 * @param Message Received DHCP ACK message.
 */

static void DHCP_ApplyAck(LPDHCP_CONTEXT Context, LPDHCP_MESSAGE Message) {
    U32 AssignedIP_Be;
    U32 AssignedIP;
    U32 RenewalSeconds;
    U32 RebindSeconds;
    LPNETWORK_DEVICE_CONTEXT NetCtx;
    LPIPV4_CONTEXT IPv4Context;
    U32 PreviousIP_Be;
    U32 PreviousMask_Be;
    U32 PreviousGateway_Be;
    U32 PreviousDNS_Be;
    BOOL ConfigChanged;
    BOOL DNSChanged;
    BOOL LeaseTransition;

    if (Context == NULL || Message == NULL) return;

    AssignedIP_Be = Message->YIAddr;
    if (AssignedIP_Be == 0) {
        AssignedIP_Be = Context->OfferedIP_Be;
    }

    if (AssignedIP_Be == 0) {
        ERROR(TEXT("[DHCP_ApplyAck] ACK missing assigned IP"));
        return;
    }

    Context->OfferedIP_Be = AssignedIP_Be;
    AssignedIP = Ntohl(Context->OfferedIP_Be);

    DEBUG(TEXT("[DHCP_ApplyAck] Applying ACK: %u.%u.%u.%u"),
          (AssignedIP >> 24) & 0xFF,
          (AssignedIP >> 16) & 0xFF,
          (AssignedIP >> 8) & 0xFF,
          AssignedIP & 0xFF);

    NetCtx = DHCP_GetNetworkDeviceContext(Context->Device);
    IPv4Context = IPv4_GetContext(Context->Device);

    PreviousIP_Be = (IPv4Context != NULL) ? IPv4Context->LocalIPv4_Be : 0;
    PreviousMask_Be = (IPv4Context != NULL) ? IPv4Context->NetmaskBe : 0;
    PreviousGateway_Be = (IPv4Context != NULL) ? IPv4Context->DefaultGatewayBe : 0;
    PreviousDNS_Be = (NetCtx != NULL) ? NetCtx->ActiveConfig.DNSServer_Be : 0;

    ConfigChanged = (PreviousIP_Be != Context->OfferedIP_Be) ||
                    (PreviousMask_Be != Context->SubnetMask_Be) ||
                    (PreviousGateway_Be != Context->Gateway_Be);
    DNSChanged = (PreviousDNS_Be != Context->DNSServer_Be);
    LeaseTransition = (Context->State != DHCP_STATE_BOUND) || ConfigChanged;

    IPv4_SetNetworkConfig(g_DHCPDevice, Context->OfferedIP_Be, Context->SubnetMask_Be, Context->Gateway_Be);

    if (LeaseTransition) {
        DHCP_ResetRoutingState(Context->Device);
    }

    Context->State = DHCP_STATE_BOUND;
    Context->LeaseStartMillis = GetSystemTime();
    Context->RetryCount = 0;

    // Calculate renewal times (T1 = 50% of lease, T2 = 87.5% of lease) with option overrides
    RenewalSeconds = Context->RenewalTime != 0 ? Context->RenewalTime : (Context->LeaseTime / 2);
    RebindSeconds = Context->RebindTime != 0 ? Context->RebindTime : ((Context->LeaseTime * 7) / 8);

    Context->RenewalTime = RenewalSeconds;
    Context->RebindTime = RebindSeconds;

    DEBUG(TEXT("[DHCP_ApplyAck] RenewalTime=%u RebindTime=%u LeaseTime=%u"),
          Context->RenewalTime,
          Context->RebindTime,
          Context->LeaseTime);

    // Mark network device as ready
    SAFE_USE_VALID_ID(NetCtx, KOID_NETWORKDEVICE) {
        NetCtx->ActiveConfig.LocalIPv4_Be = Context->OfferedIP_Be;
        NetCtx->ActiveConfig.SubnetMask_Be = Context->SubnetMask_Be;
        NetCtx->ActiveConfig.Gateway_Be = Context->Gateway_Be;
        NetCtx->ActiveConfig.DNSServer_Be = Context->DNSServer_Be;
        DEBUG(TEXT("[DHCP_ApplyAck] Updated network context IP to %u.%u.%u.%u"),
              (AssignedIP >> 24) & 0xFF,
              (AssignedIP >> 16) & 0xFF,
              (AssignedIP >> 8) & 0xFF,
              AssignedIP & 0xFF);
        if (DNSChanged) {
            U32 DNSHost = Ntohl(Context->DNSServer_Be);
            DEBUG(TEXT("[DHCP_ApplyAck] DNS server set to %u.%u.%u.%u"),
                  (DNSHost >> 24) & 0xFF,
                  (DNSHost >> 16) & 0xFF,
                  (DNSHost >> 8) & 0xFF,
                  DNSHost & 0xFF);
        }
        NetCtx->IsReady = TRUE;
        DEBUG(TEXT("[DHCP_ApplyAck] Network device marked as ready"));
    }

    DEBUG(TEXT("[DHCP_ApplyAck] DHCP configuration complete"));
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

            if (MessageType == DHCP_DECLINE) {
                WARNING(TEXT("[DHCP_OnUDPPacket] Received DECLINE, restarting DHCP"));
                Context->State = DHCP_STATE_INIT;
                DHCP_ClearNetworkReady(Context);
                DHCP_Start(g_DHCPDevice);
                return;
            } else if (MessageType == DHCP_INFORM) {
                DEBUG(TEXT("[DHCP_OnUDPPacket] Received INFORM, ignored for client flow"));
                return;
            }

            // Handle message based on state
            switch (Context->State) {
                case DHCP_STATE_SELECTING:
                    if (MessageType == DHCP_OFFER) {
                        Context->OfferedIP_Be = Message->YIAddr;
                        if (Context->OfferedIP_Be == 0) {
                            ERROR(TEXT("[DHCP_OnUDPPacket] OFFER missing IP address"));
                            break;
                        }

                        if (Context->ServerID_Be == 0) {
                            if (Message->SIAddr != 0) {
                                Context->ServerID_Be = Message->SIAddr;
                            } else if (SourceIP != 0) {
                                Context->ServerID_Be = SourceIP;
                            }
                        }

                        if (Context->ServerID_Be == 0) {
                            ERROR(TEXT("[DHCP_OnUDPPacket] OFFER missing server identifier"));
                            break;
                        }

                        U32 OfferedIP = Ntohl(Context->OfferedIP_Be);
                        DEBUG(TEXT("[DHCP_OnUDPPacket] Received OFFER: %u.%u.%u.%u"),
                              (OfferedIP >> 24) & 0xFF, (OfferedIP >> 16) & 0xFF,
                              (OfferedIP >> 8) & 0xFF, OfferedIP & 0xFF);

                        DHCP_SendRequest(g_DHCPDevice, DHCP_STATE_REQUESTING);
                    }
                    break;

                case DHCP_STATE_REQUESTING:
                case DHCP_STATE_RENEWING:
                case DHCP_STATE_REBINDING:
                    if (MessageType == DHCP_ACK) {
                        U32 AckIP = Message->YIAddr != 0 ? Message->YIAddr : Context->OfferedIP_Be;
                        U32 AckHostOrder = Ntohl(AckIP);
                        DEBUG(TEXT("[DHCP_OnUDPPacket] Received ACK: %u.%u.%u.%u"),
                              (AckHostOrder >> 24) & 0xFF,
                              (AckHostOrder >> 16) & 0xFF,
                              (AckHostOrder >> 8) & 0xFF,
                              AckHostOrder & 0xFF);
                        DHCP_ApplyAck(Context, Message);
                    } else if (MessageType == DHCP_NAK) {
                        ERROR(TEXT("[DHCP_OnUDPPacket] Received NAK, restarting DHCP"));
                        Context->State = DHCP_STATE_INIT;
                        DHCP_ClearNetworkReady(Context);
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
 * @brief Handles retry/backoff for DHCP requests.
 *
 * @param Device  Network device.
 * @param Context DHCP context.
 */

static void DHCP_HandleRequestTimeout(LPDEVICE Device, LPDHCP_CONTEXT Context) {
    UINT CurrentMillis;
    UINT ElapsedMillis;
    UINT TimeoutMillis;
    BOOL FallbackApplied;

    if (Device == NULL || Context == NULL) return;

    CurrentMillis = GetSystemTime();
    ElapsedMillis = CurrentMillis - Context->StartMillis;
    TimeoutMillis = DHCP_GetRetryTimeout(Context->RetryCount);

    if (ElapsedMillis < TimeoutMillis) {
        return;
    }

    if (Context->RetryCount >= DHCP_MAX_RETRIES) {
        DEBUG(TEXT("[DHCP_HandleRequestTimeout] DHCP failed after %u retries in state %u"), Context->RetryCount, Context->State);
        if (Context->State == DHCP_STATE_RENEWING || Context->State == DHCP_STATE_REBINDING) {
            WARNING(TEXT("[DHCP_HandleRequestTimeout] Lease retry limit reached, restarting DHCP"));
            DHCP_ClearNetworkReady(Context);
            DHCP_Start(Device);
        } else {
            FallbackApplied = DHCP_ApplyStaticFallback(Context);
            if (FallbackApplied) {
                Context->State = DHCP_STATE_FAILED;
                DEBUG(TEXT("[DHCP_HandleRequestTimeout] Static fallback applied after DHCP failure"));
            } else {
                Context->State = DHCP_STATE_FAILED;
                WARNING(TEXT("[DHCP_HandleRequestTimeout] DHCP failed and no fallback available"));
            }
        }
        return;
    }

    WARNING(TEXT("[DHCP_HandleRequestTimeout] Timeout in state %u after %u ms (backoff %u ms), retry %u/%u"),
            Context->State,
            ElapsedMillis,
            TimeoutMillis,
            Context->RetryCount + 1,
            DHCP_MAX_RETRIES);

    Context->RetryCount++;

    if (Context->State == DHCP_STATE_SELECTING) {
        DHCP_SendDiscover(Device);
    } else if (Context->State == DHCP_STATE_REQUESTING || Context->State == DHCP_STATE_RENEWING ||
               Context->State == DHCP_STATE_REBINDING) {
        DHCP_SendRequest(Device, Context->State);
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

    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        SAFE_USE_VALID_ID(((LPPCI_DEVICE)Device)->Driver, KOID_DRIVER) {
            if (((LPPCI_DEVICE)Device)->Driver->Command(DF_NT_GETINFO, (UINT)(LPVOID)&GetInfo) == DF_RETURN_SUCCESS) {
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

    SetDeviceContext(Device, KOID_DHCP, (LPVOID)Context);

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
        DHCP_SendRelease(Device);
        DHCP_ClearNetworkReady(Context);
        UDP_UnregisterPortHandler(Device, DHCP_CLIENT_PORT);
        KernelHeapFree(Context);
        SetDeviceContext(Device, KOID_DHCP, NULL);
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
        Context->OfferedIP_Be = 0;
        Context->SubnetMask_Be = 0;
        Context->Gateway_Be = 0;
        Context->DNSServer_Be = 0;
        Context->ServerID_Be = 0;
        Context->LeaseTime = 0;
        Context->RenewalTime = 0;
        Context->RebindTime = 0;
        Context->LeaseStartMillis = 0;
        DHCP_ClearNetworkReady(Context);
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
    UINT CurrentMillis;
    UINT ElapsedMillis;
    UINT ElapsedSeconds;

    if (Device == NULL) return;

    Context = DHCP_GetContext(Device);
    SAFE_USE(Context) {
        CurrentMillis = GetSystemTime();

        switch (Context->State) {
            case DHCP_STATE_SELECTING:
            case DHCP_STATE_REQUESTING:
                DHCP_HandleRequestTimeout(Device, Context);
                break;

            case DHCP_STATE_BOUND:
                ElapsedMillis = CurrentMillis - Context->LeaseStartMillis;
                ElapsedSeconds = ElapsedMillis / 1000;

                if (Context->LeaseTime != 0 && ElapsedSeconds >= Context->LeaseTime) {
                    WARNING(TEXT("[DHCP_Tick] Lease expired, restarting DHCP"));
                    DHCP_ClearNetworkReady(Context);
                    DHCP_Start(Device);
                } else if (Context->RebindTime != 0 && ElapsedSeconds >= Context->RebindTime) {
                    DEBUG(TEXT("[DHCP_Tick] Entering REBINDING state"));
                    DHCP_SendRequest(Device, DHCP_STATE_REBINDING);
                } else if (Context->RenewalTime != 0 && ElapsedSeconds >= Context->RenewalTime) {
                    DEBUG(TEXT("[DHCP_Tick] Entering RENEWING state"));
                    DHCP_SendRequest(Device, DHCP_STATE_RENEWING);
                }
                break;

            case DHCP_STATE_RENEWING:
                DHCP_HandleRequestTimeout(Device, Context);
                ElapsedMillis = CurrentMillis - Context->LeaseStartMillis;
                ElapsedSeconds = ElapsedMillis / 1000;

                if (Context->LeaseTime != 0 && ElapsedSeconds >= Context->LeaseTime) {
                    WARNING(TEXT("[DHCP_Tick] Lease expired during renewal, restarting DHCP"));
                    DHCP_ClearNetworkReady(Context);
                    DHCP_Start(Device);
                } else if (Context->RebindTime != 0 && ElapsedSeconds >= Context->RebindTime) {
                    DEBUG(TEXT("[DHCP_Tick] Renewal timed out, entering REBINDING state"));
                    DHCP_SendRequest(Device, DHCP_STATE_REBINDING);
                }
                break;

            case DHCP_STATE_REBINDING:
                DHCP_HandleRequestTimeout(Device, Context);
                ElapsedMillis = CurrentMillis - Context->LeaseStartMillis;
                ElapsedSeconds = ElapsedMillis / 1000;

                if (Context->LeaseTime != 0 && ElapsedSeconds >= Context->LeaseTime) {
                    WARNING(TEXT("[DHCP_Tick] Lease expired during rebinding, restarting DHCP"));
                    DHCP_ClearNetworkReady(Context);
                    DHCP_Start(Device);
                }
                break;

            default:
                break;
        }
    }
}
