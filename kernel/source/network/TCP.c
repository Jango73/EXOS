
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


    Transmission Control Protocol (TCP)

\************************************************************************/

#include "network/TCP.h"
#include "Kernel.h"
#include "Log.h"
#include "System.h"
#include "network/IPv4.h"
#include "Clock.h"
#include "Socket.h"
#include "Memory.h"
#include "Heap.h"
#include "utils/Notification.h"
#include "utils/Helpers.h"
#include "CoreString.h"
#include "utils/NetworkChecksum.h"
#include "utils/Hysteresis.h"
#include "Device.h"

/************************************************************************/
// Configuration

// Helper to get ephemeral port start from configuration
static U16 TCP_GetEphemeralPortStart(void) {
    LPCSTR configValue = GetConfigurationValue(TEXT(CONFIG_TCP_EPHEMERAL_START));

    SAFE_USE(configValue) {
        U32 port = StringToU32(configValue);
        if (port > 0 && port <= 65535) {
            return (U16)port;
        }
    }

    return TCP_EPHEMERAL_PORT_START_FALLBACK;
}

/************************************************************************/
// Helper to read buffer sizes from configuration with fallback
static UINT TCP_GetConfiguredBufferSize(LPCSTR configKey, U32 fallback, U32 maxLimit) {
    LPCSTR configValue = GetConfigurationValue(configKey);

    SAFE_USE(configValue) {
        U32 parsedValue = StringToU32(configValue);
        if (parsedValue > 0) {
            if (parsedValue > maxLimit) {
                WARNING(TEXT("[TCP_GetConfiguredBufferSize] %s=%u exceeds maximum %u, clamping"),
                        configKey, parsedValue, maxLimit);
                return (UINT)maxLimit;
            }
            return (UINT)parsedValue;
        }

        WARNING(TEXT("[TCP_GetConfiguredBufferSize] %s has invalid value '%s', using fallback"),
                configKey, configValue);
    }

    return (UINT)fallback;
}

/************************************************************************/
// Global TCP state

typedef struct tag_TCP_GLOBAL_STATE {
    U16 NextEphemeralPort;
    UINT SendBufferSize;
    UINT ReceiveBufferSize;
} TCP_GLOBAL_STATE, *LPTCP_GLOBAL_STATE;

TCP_GLOBAL_STATE GlobalTCP;

/************************************************************************/
// State machine definitions

// Forward declarations of state handlers
static void TCP_OnEnterClosed(STATE_MACHINE* SM);
static void TCP_OnEnterListen(STATE_MACHINE* SM);
static void TCP_OnEnterSynSent(STATE_MACHINE* SM);
static void TCP_OnEnterSynReceived(STATE_MACHINE* SM);
static void TCP_OnEnterEstablished(STATE_MACHINE* SM);
static void TCP_OnEnterFinWait1(STATE_MACHINE* SM);
static void TCP_OnEnterFinWait2(STATE_MACHINE* SM);
static void TCP_OnEnterCloseWait(STATE_MACHINE* SM);
static void TCP_OnEnterClosing(STATE_MACHINE* SM);
static void TCP_OnEnterLastAck(STATE_MACHINE* SM);
static void TCP_OnEnterTimeWait(STATE_MACHINE* SM);

// Forward declarations of transition actions
static void TCP_ActionSendSyn(STATE_MACHINE* SM, LPVOID EventData);
static void TCP_ActionSendSynAck(STATE_MACHINE* SM, LPVOID EventData);
static void TCP_ActionSendAck(STATE_MACHINE* SM, LPVOID EventData);
static void TCP_ActionSendFin(STATE_MACHINE* SM, LPVOID EventData);
static void TCP_ActionSendRst(STATE_MACHINE* SM, LPVOID EventData);
static void TCP_IPv4PacketSentCallback(LPNOTIFICATION_DATA NotificationData, LPVOID UserData);
static void TCP_ActionProcessData(STATE_MACHINE* SM, LPVOID EventData);
static void TCP_ActionAbortConnection(STATE_MACHINE* SM, LPVOID EventData);

// Forward declarations of transition conditions
static BOOL TCP_ConditionValidAck(STATE_MACHINE* SM, LPVOID EventData);
static BOOL TCP_ConditionValidSyn(STATE_MACHINE* SM, LPVOID EventData);

// State definitions
static SM_STATE_DEFINITION TCP_States[] = {
    { TCP_STATE_CLOSED,       TCP_OnEnterClosed,       NULL, NULL },
    { TCP_STATE_LISTEN,       TCP_OnEnterListen,       NULL, NULL },
    { TCP_STATE_SYN_SENT,     TCP_OnEnterSynSent,      NULL, NULL },
    { TCP_STATE_SYN_RECEIVED, TCP_OnEnterSynReceived,  NULL, NULL },
    { TCP_STATE_ESTABLISHED,  TCP_OnEnterEstablished,  NULL, NULL },
    { TCP_STATE_FIN_WAIT_1,   TCP_OnEnterFinWait1,     NULL, NULL },
    { TCP_STATE_FIN_WAIT_2,   TCP_OnEnterFinWait2,     NULL, NULL },
    { TCP_STATE_CLOSE_WAIT,   TCP_OnEnterCloseWait,    NULL, NULL },
    { TCP_STATE_CLOSING,      TCP_OnEnterClosing,      NULL, NULL },
    { TCP_STATE_LAST_ACK,     TCP_OnEnterLastAck,      NULL, NULL },
    { TCP_STATE_TIME_WAIT,    TCP_OnEnterTimeWait,     NULL, NULL }
};

// Transition definitions
static SM_TRANSITION TCP_Transitions[] = {
    // From CLOSED
    { TCP_STATE_CLOSED, TCP_EVENT_CONNECT, TCP_STATE_SYN_SENT, NULL, TCP_ActionSendSyn },
    { TCP_STATE_CLOSED, TCP_EVENT_LISTEN, TCP_STATE_LISTEN, NULL, NULL },

    // From LISTEN
    { TCP_STATE_LISTEN, TCP_EVENT_RCV_SYN, TCP_STATE_SYN_RECEIVED, TCP_ConditionValidSyn, TCP_ActionSendSynAck },
    { TCP_STATE_LISTEN, TCP_EVENT_CLOSE, TCP_STATE_CLOSED, NULL, NULL },

    // From SYN_SENT
    { TCP_STATE_SYN_SENT, TCP_EVENT_RCV_SYN, TCP_STATE_SYN_RECEIVED, TCP_ConditionValidSyn, TCP_ActionSendAck },
    { TCP_STATE_SYN_SENT, TCP_EVENT_RCV_ACK, TCP_STATE_ESTABLISHED, TCP_ConditionValidAck, NULL },
    { TCP_STATE_SYN_SENT, TCP_EVENT_CLOSE, TCP_STATE_CLOSED, NULL, TCP_ActionAbortConnection },
    { TCP_STATE_SYN_SENT, TCP_EVENT_RCV_RST, TCP_STATE_CLOSED, NULL, TCP_ActionAbortConnection },

    // From SYN_RECEIVED
    { TCP_STATE_SYN_RECEIVED, TCP_EVENT_RCV_ACK, TCP_STATE_ESTABLISHED, TCP_ConditionValidAck, NULL },
    { TCP_STATE_SYN_RECEIVED, TCP_EVENT_CLOSE, TCP_STATE_FIN_WAIT_1, NULL, TCP_ActionSendFin },
    { TCP_STATE_SYN_RECEIVED, TCP_EVENT_RCV_RST, TCP_STATE_LISTEN, NULL, NULL },

    // From ESTABLISHED
    { TCP_STATE_ESTABLISHED, TCP_EVENT_RCV_DATA, TCP_STATE_ESTABLISHED, NULL, TCP_ActionProcessData },
    { TCP_STATE_ESTABLISHED, TCP_EVENT_RCV_ACK, TCP_STATE_ESTABLISHED, TCP_ConditionValidAck, NULL },
    { TCP_STATE_ESTABLISHED, TCP_EVENT_CLOSE, TCP_STATE_FIN_WAIT_1, NULL, TCP_ActionSendFin },
    { TCP_STATE_ESTABLISHED, TCP_EVENT_RCV_FIN, TCP_STATE_CLOSE_WAIT, NULL, TCP_ActionSendAck },
    { TCP_STATE_ESTABLISHED, TCP_EVENT_RCV_RST, TCP_STATE_CLOSED, NULL, NULL },

    // From FIN_WAIT_1
    { TCP_STATE_FIN_WAIT_1, TCP_EVENT_RCV_ACK, TCP_STATE_FIN_WAIT_2, TCP_ConditionValidAck, NULL },
    { TCP_STATE_FIN_WAIT_1, TCP_EVENT_RCV_FIN, TCP_STATE_CLOSING, NULL, TCP_ActionSendAck },
    { TCP_STATE_FIN_WAIT_1, TCP_EVENT_RCV_RST, TCP_STATE_CLOSED, NULL, NULL },

    // From FIN_WAIT_2
    { TCP_STATE_FIN_WAIT_2, TCP_EVENT_RCV_FIN, TCP_STATE_TIME_WAIT, NULL, TCP_ActionSendAck },
    { TCP_STATE_FIN_WAIT_2, TCP_EVENT_RCV_RST, TCP_STATE_CLOSED, NULL, NULL },

    // From CLOSE_WAIT
    { TCP_STATE_CLOSE_WAIT, TCP_EVENT_CLOSE, TCP_STATE_LAST_ACK, NULL, TCP_ActionSendFin },

    // From CLOSING
    { TCP_STATE_CLOSING, TCP_EVENT_RCV_ACK, TCP_STATE_TIME_WAIT, TCP_ConditionValidAck, NULL },
    { TCP_STATE_CLOSING, TCP_EVENT_RCV_RST, TCP_STATE_CLOSED, NULL, NULL },

    // From LAST_ACK
    { TCP_STATE_LAST_ACK, TCP_EVENT_RCV_ACK, TCP_STATE_CLOSED, TCP_ConditionValidAck, NULL },
    { TCP_STATE_LAST_ACK, TCP_EVENT_RCV_RST, TCP_STATE_CLOSED, NULL, NULL },

    // From TIME_WAIT
    { TCP_STATE_TIME_WAIT, TCP_EVENT_TIMEOUT, TCP_STATE_CLOSED, NULL, NULL }
};

/************************************************************************/

static BOOL TCP_IsPortInUse(U16 port, U32 localIP) {
    LPTCP_CONNECTION conn = (LPTCP_CONNECTION)Kernel.TCPConnection->First;
    while (conn != NULL) {
        if (conn->LocalPort == Htons(port) && conn->LocalIP == localIP) {
            return TRUE;
        }
        conn = (LPTCP_CONNECTION)conn->Next;
    }
    return FALSE;
}

/************************************************************************/

static U16 TCP_GetNextEphemeralPort(U32 localIP) {
    U16 startPort = TCP_GetEphemeralPortStart();
    U16 maxPort = 65535;
    U16 attempts = 0;
    U16 maxAttempts = maxPort - startPort + 1;

    // Initialize with a pseudo-random port if not set
    if (GlobalTCP.NextEphemeralPort == 0) {
        // Simple pseudo-random based on system time and IP
        UINT seed = GetSystemTime() ^ (localIP & 0xFFFF);
        GlobalTCP.NextEphemeralPort = startPort + (seed % (maxPort - startPort + 1));
    }

    U16 port = GlobalTCP.NextEphemeralPort;

    // Find next available port, avoiding conflicts
    while (attempts < maxAttempts) {
        if (!TCP_IsPortInUse(port, localIP)) {
            // Update NextEphemeralPort for next allocation
            GlobalTCP.NextEphemeralPort = port + 1;
            if (GlobalTCP.NextEphemeralPort > maxPort) {
                GlobalTCP.NextEphemeralPort = startPort;
            }
            return port;
        }

        port++;
        if (port > maxPort) {
            port = startPort;
        }
        attempts++;
    }

    // If we get here, all ports are in use (very unlikely)
    DEBUG(TEXT("[TCP_GetNextEphemeralPort] All ephemeral ports exhausted!"));
    return startPort; // Return start port as fallback
}

/************************************************************************/

static int TCP_SendPacket(LPTCP_CONNECTION Conn, U8 Flags, const U8* Payload, U32 PayloadLength) {
    TCP_HEADER Header;
    U8 Options[4] = {0}; // MSS option: 4 bytes
    U32 OptionsLength = 0;

    // Add MSS option for SYN packets
    if (Flags & TCP_FLAG_SYN) {
        Options[0] = 2;    // MSS option type
        Options[1] = 4;    // MSS option length
        Options[2] = 0x05; // MSS = 1460 (0x05B4) in network byte order
        Options[3] = 0xB4;
        OptionsLength = 4;
    }

    U32 HeaderLength = sizeof(TCP_HEADER) + OptionsLength;
    U32 TotalLength = HeaderLength + PayloadLength;
    U8 Packet[TotalLength];

    // Fill TCP header (ports already in network byte order)
    Header.SourcePort = Conn->LocalPort;
    Header.DestinationPort = Conn->RemotePort;
    Header.SequenceNumber = Htonl(Conn->SendNext);
    Header.AckNumber = Htonl(Conn->RecvNext);
    Header.DataOffset = ((HeaderLength / 4) << 4); // Data offset in 4-byte words, shifted to upper nibble
    Header.Flags = Flags;
    // Always calculate window based on actual TCP buffer space, not cached value
    UINT AvailableSpace = (Conn->RecvBufferCapacity > Conn->RecvBufferUsed)
                          ? (Conn->RecvBufferCapacity - Conn->RecvBufferUsed)
                          : 0;
    U16 ActualWindow = (AvailableSpace > 0xFFFFU) ? 0xFFFFU : (U16)AvailableSpace;
    Header.WindowSize = Htons(ActualWindow);
    Header.UrgentPointer = 0;
    Header.Checksum = 0;

    // Copy header, options, and payload to packet
    MemoryCopy(Packet, &Header, sizeof(TCP_HEADER));
    if (OptionsLength > 0) {
        MemoryCopy(Packet + sizeof(TCP_HEADER), Options, OptionsLength);
    }
    if (Payload && PayloadLength > 0) {
        MemoryCopy(Packet + HeaderLength, Payload, PayloadLength);
    }

    // Calculate checksum
    ((LPTCP_HEADER)Packet)->Checksum = TCP_CalculateChecksum((LPTCP_HEADER)Packet,
        Payload, PayloadLength, Conn->LocalIP, Conn->RemoteIP);

    // Debug: Show the actual TCP header being sent (convert from network to host order for display)
    LPTCP_HEADER TcpHdr = (LPTCP_HEADER)Packet;

    DEBUG(TEXT("[TCP_SendPacket] TCP Header: SrcPort=%d DestPort=%d Seq=%u Ack=%u Flags=%x Window=%d Checksum=%x HeaderLen=%d"),
          Ntohs(TcpHdr->SourcePort), Ntohs(TcpHdr->DestinationPort),
          Ntohl(TcpHdr->SequenceNumber), Ntohl(TcpHdr->AckNumber),
          TcpHdr->Flags, Ntohs(TcpHdr->WindowSize), Ntohs(TcpHdr->Checksum), HeaderLength);

    // Send via IPv4 through connection's network device
    int SendResult = 0;
    LPDEVICE Device = Conn->Device;

    if (Device == NULL) {
        return 0;
    }

    LockMutex(&(Device->Mutex), INFINITY);
    SendResult = IPv4_Send(Device, Conn->RemoteIP, IPV4_PROTOCOL_TCP, Packet, HeaderLength + PayloadLength);
    UnlockMutex(&(Device->Mutex));

    // Update sequence number if data was sent
    if (PayloadLength > 0 || (Flags & (TCP_FLAG_SYN | TCP_FLAG_FIN))) {
        Conn->SendNext += PayloadLength;
        if (Flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) {
            Conn->SendNext++;
        }
    }

    return SendResult;
}

/************************************************************************/
// State handlers

static void TCP_OnEnterClosed(STATE_MACHINE* SM) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    DEBUG(TEXT("TCP: Connection entered CLOSED state"));

    // Clear all timers and counters to prevent zombie retransmissions
    Conn->RetransmitTimer = 0;
    Conn->RetransmitCount = 0;
    Conn->TimeWaitTimer = 0;

    // Note: We don't need to unregister from global IPv4 notifications
    // as the callback will check the connection state
}

static void TCP_OnEnterListen(STATE_MACHINE* SM) {
    (void)SM;
    DEBUG(TEXT("TCP: Connection entered LISTEN state"));
}

static void TCP_OnEnterSynSent(STATE_MACHINE* SM) {
    (void)SM;
    DEBUG(TEXT("TCP: Connection entered SYN_SENT state"));
}

static void TCP_OnEnterSynReceived(STATE_MACHINE* SM) {
    (void)SM;
    DEBUG(TEXT("TCP: Connection entered SYN_RECEIVED state"));
}

static void TCP_OnEnterEstablished(STATE_MACHINE* SM) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    DEBUG(TEXT("[TCP_OnEnterEstablished] Connection established"));

    // Notify upper layers that connection is established
    // Only send notification if we're coming from another state (not a re-entry)
    if (Conn->NotificationContext != NULL && SM->PreviousState != TCP_STATE_ESTABLISHED) {
        Notification_Send(Conn->NotificationContext, NOTIF_EVENT_TCP_CONNECTED, NULL, 0);
        DEBUG(TEXT("[TCP_OnEnterEstablished] Sent TCP_CONNECTED notification"));
    }
}

static void TCP_OnEnterFinWait1(STATE_MACHINE* SM) {
    (void)SM;
    DEBUG(TEXT("TCP: Connection entered FIN_WAIT_1 state"));
}

static void TCP_OnEnterFinWait2(STATE_MACHINE* SM) {
    (void)SM;
    DEBUG(TEXT("TCP: Connection entered FIN_WAIT_2 state"));
}

static void TCP_OnEnterCloseWait(STATE_MACHINE* SM) {
    (void)SM;
    DEBUG(TEXT("TCP: Connection entered CLOSE_WAIT state"));
}

static void TCP_OnEnterClosing(STATE_MACHINE* SM) {
    (void)SM;
    DEBUG(TEXT("TCP: Connection entered CLOSING state"));
}

static void TCP_OnEnterLastAck(STATE_MACHINE* SM) {
    (void)SM;
    DEBUG(TEXT("TCP: Connection entered LAST_ACK state"));
}

static void TCP_OnEnterTimeWait(STATE_MACHINE* SM) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    DEBUG(TEXT("TCP: Connection entered TIME_WAIT state"));
    Conn->TimeWaitTimer = GetSystemTime() + TCP_TIME_WAIT_TIMEOUT;
}

/************************************************************************/
// Transition actions

static void TCP_ActionSendSyn(STATE_MACHINE* SM, LPVOID EventData) {
    (void)EventData;
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);

    DEBUG(TEXT("[TCP_ActionSendSyn] Sending SYN"));

    Conn->SendNext = 1000; // Initial sequence number
    Conn->RetransmitCount = 0;

    // Send packet and only start timer if actually sent (not queued)
    int SendResult = TCP_SendPacket(Conn, TCP_FLAG_SYN, NULL, 0);
    if (SendResult == IPV4_SEND_IMMEDIATE) {
        // Packet was actually sent immediately (ARP resolved)
        Conn->RetransmitTimer = GetSystemTime() + TCP_RETRANSMIT_TIMEOUT;
        DEBUG(TEXT("[TCP_ActionSendSyn] SYN sent immediately, timer set to %u"), Conn->RetransmitTimer);
    } else if (SendResult == IPV4_SEND_PENDING) {
        // Packet queued for later (ARP pending) - timer will be set via notification
        Conn->RetransmitTimer = 0;
        DEBUG(TEXT("[TCP_ActionSendSyn] SYN queued for ARP resolution, timer will be set when packet is sent"));
    } else {
        // Failed to send
        Conn->RetransmitTimer = 0;
        DEBUG(TEXT("[TCP_ActionSendSyn] SYN send failed"));
    }
}

/************************************************************************/

static void TCP_ActionSendSynAck(STATE_MACHINE* SM, LPVOID EventData) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    LPTCP_PACKET_EVENT Event = (LPTCP_PACKET_EVENT)EventData;

    DEBUG(TEXT("[TCP_ActionSendSynAck] Sending SYN+ACK"));
    Conn->SendNext = 2000; // Initial sequence number
    Conn->RecvNext = Ntohl(Event->Header->SequenceNumber) + 1;

    int SendResult = TCP_SendPacket(Conn, TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0);
    if (SendResult < 0) {
        ERROR(TEXT("[TCP_ActionSendSynAck] Failed to send SYN+ACK packet"));
        SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
    }
}

/************************************************************************/

static void TCP_ActionSendAck(STATE_MACHINE* SM, LPVOID EventData) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    LPTCP_PACKET_EVENT Event = (LPTCP_PACKET_EVENT)EventData;

    DEBUG(TEXT("[TCP_ActionSendAck] Sending ACK"));
    if (Event && Event->Header) {
        U32 SeqNum = Ntohl(Event->Header->SequenceNumber);
        U8 Flags = Event->Header->Flags;

        // Calculate expected next sequence number
        Conn->RecvNext = SeqNum + Event->PayloadLength;
        if (Flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) {
            Conn->RecvNext++;
        }
    }

    int SendResult = TCP_SendPacket(Conn, TCP_FLAG_ACK, NULL, 0);
    if (SendResult < 0) {
        ERROR(TEXT("[TCP_ActionSendAck] Failed to send ACK packet"));
        SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
    }
}

/************************************************************************/

static void TCP_ActionSendFin(STATE_MACHINE* SM, LPVOID EventData) {
    (void)EventData;
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    DEBUG(TEXT("[TCP_ActionSendFin] Sending FIN"));

    int SendResult = TCP_SendPacket(Conn, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    if (SendResult < 0) {
        ERROR(TEXT("[TCP_ActionSendFin] Failed to send FIN packet"));
        SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
    }
}

/************************************************************************/

static void TCP_ActionSendRst(STATE_MACHINE* SM, LPVOID EventData) {
    (void)EventData;
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    DEBUG(TEXT("[TCP_ActionSendRst] Sending RST"));
    TCP_SendPacket(Conn, TCP_FLAG_RST, NULL, 0);
}

/************************************************************************/

static void TCP_ActionProcessData(STATE_MACHINE* SM, LPVOID EventData) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    LPTCP_PACKET_EVENT Event = (LPTCP_PACKET_EVENT)EventData;

    if (!Event || !Event->Header) {
        return;
    }

    U8 Flags = Event->Header->Flags;
    U32 SeqNum = Ntohl(Event->Header->SequenceNumber);
    U32 AckTarget = Conn->RecvNext;
    U32 BytesAccepted = 0;
    const U8* PayloadPtr = Event->Payload;
    U32 PayloadLength = Event->PayloadLength;

    if (PayloadLength > 0 && PayloadPtr) {
        if (SeqNum < Conn->RecvNext) {
            U32 AlreadyAcked = Conn->RecvNext - SeqNum;
            if (AlreadyAcked >= PayloadLength) {
                DEBUG(TEXT("[TCP_ActionProcessData] Duplicate payload ignored (Seq=%u, Length=%u)"), SeqNum, PayloadLength);

                int SendResult = TCP_SendPacket(Conn, TCP_FLAG_ACK, NULL, 0);
                if (SendResult < 0) {
                    ERROR(TEXT("[TCP_ActionProcessData] Failed to send ACK for duplicate segment"));
                    SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
                }
                return;
            }

            SeqNum += AlreadyAcked;
            PayloadPtr += AlreadyAcked;
            PayloadLength -= AlreadyAcked;
        }

        if (SeqNum > Conn->RecvNext) {
            DEBUG(TEXT("[TCP_ActionProcessData] Out-of-order segment received (expected=%u, got=%u)"), Conn->RecvNext, SeqNum);

            int SendResult = TCP_SendPacket(Conn, TCP_FLAG_ACK, NULL, 0);
            if (SendResult < 0) {
                ERROR(TEXT("[TCP_ActionProcessData] Failed to send ACK for out-of-order segment"));
                SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
            }
            return;
        }

        DEBUG(TEXT("[TCP_ActionProcessData] Processing %u bytes of in-order data"), PayloadLength);

        if (Conn->RecvBufferUsed >= Conn->RecvBufferCapacity) {
            WARNING(TEXT("[TCP_ActionProcessData] Receive buffer full, advertising zero window"));

            int SendResult = TCP_SendPacket(Conn, TCP_FLAG_ACK, NULL, 0);
            if (SendResult < 0) {
                ERROR(TEXT("[TCP_ActionProcessData] Failed to send zero window ACK"));
                SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
            }
            return;
        }

        UINT SpaceAvailable = (Conn->RecvBufferCapacity > Conn->RecvBufferUsed)
                              ? (Conn->RecvBufferCapacity - Conn->RecvBufferUsed)
                              : 0;
        U32 CopyLength = (PayloadLength > (U32)SpaceAvailable) ? (U32)SpaceAvailable : PayloadLength;

        if (CopyLength > 0) {
            BytesAccepted = SocketTCPReceiveCallback(Conn, PayloadPtr, CopyLength);

            if (BytesAccepted > 0) {
                MemoryCopy(Conn->RecvBuffer + Conn->RecvBufferUsed, PayloadPtr, BytesAccepted);
                Conn->RecvBufferUsed += BytesAccepted;
            }
        }

        if (BytesAccepted == 0) {
            DEBUG(TEXT("[TCP_ActionProcessData] No payload accepted from current segment"));
        }
    }

    if (BytesAccepted > 0) {
        U32 Candidate = SeqNum + BytesAccepted;
        if (Candidate > AckTarget) {
            AckTarget = Candidate;
        }
    }

    if ((Flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) != 0) {
        if (PayloadLength == 0 || BytesAccepted == PayloadLength) {
            AckTarget++;
        }
    }

    if (AckTarget > Conn->RecvNext) {
        Conn->RecvNext = AckTarget;
    }

    int SendResult = TCP_SendPacket(Conn, TCP_FLAG_ACK, NULL, 0);
    if (SendResult < 0) {
        ERROR(TEXT("[TCP_ActionProcessData] Failed to send ACK packet"));
        SM_ProcessEvent(SM, TCP_EVENT_RCV_RST, NULL);
    }
}

/************************************************************************/

static void TCP_ActionAbortConnection(STATE_MACHINE* SM, LPVOID EventData) {
    (void)EventData;
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    DEBUG(TEXT("[TCP_ActionAbortConnection] Aborting connection - clearing timers"));

    // Immediately clear all retransmit timers to stop sending packets
    Conn->RetransmitTimer = 0;
    Conn->RetransmitCount = 0;
    Conn->TimeWaitTimer = 0;
}

/************************************************************************/

static void TCP_IPv4PacketSentCallback(LPNOTIFICATION_DATA NotificationData, LPVOID UserData) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)UserData;
    LPIPV4_PACKET_SENT_DATA PacketData;

    if (!NotificationData || !Conn) return;
    if (NotificationData->EventID != NOTIF_EVENT_IPV4_PACKET_SENT) return;
    if (!NotificationData->Data) return;

    // Check if connection is still active
    if (Conn->StateMachine.CurrentState == TCP_STATE_CLOSED) return;

    PacketData = (LPIPV4_PACKET_SENT_DATA)NotificationData->Data;

    // Check if this packet is for our connection
    if (PacketData->DestinationIP == Conn->RemoteIP && PacketData->Protocol == IPV4_PROTOCOL_TCP) {
        // This is a TCP packet to our destination - start retransmit timer if not set
        if (Conn->RetransmitTimer == 0 && Conn->StateMachine.CurrentState == TCP_STATE_SYN_SENT) {
            Conn->RetransmitTimer = GetSystemTime() + TCP_RETRANSMIT_TIMEOUT;
            DEBUG(TEXT("[TCP_IPv4PacketSentCallback] SYN packet sent, timer set to %u"), Conn->RetransmitTimer);
        }
    }
}

/************************************************************************/
// Helper function to validate sequence numbers within receive window

static BOOL TCP_IsSequenceInWindow(U32 SequenceNumber, U32 WindowStart, U16 WindowSize) {
    // Handle sequence number wrap-around by using modular arithmetic
    U32 WindowEnd = WindowStart + WindowSize;

    // Check if sequence number is within the window
    if (WindowStart <= WindowEnd) {
        // No wrap-around case
        return (SequenceNumber >= WindowStart && SequenceNumber < WindowEnd);
    } else {
        // Wrap-around case
        return (SequenceNumber >= WindowStart || SequenceNumber < WindowEnd);
    }
}

/************************************************************************/
// Transition conditions

static BOOL TCP_ConditionValidAck(STATE_MACHINE* SM, LPVOID EventData) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    LPTCP_PACKET_EVENT Event = (LPTCP_PACKET_EVENT)EventData;

    if (!Event || !Event->Header) return FALSE;

    U32 AckNum = Ntohl(Event->Header->AckNumber);
    U32 SeqNum = Ntohl(Event->Header->SequenceNumber);
    U8 Flags = Event->Header->Flags;

    DEBUG(TEXT("[TCP_ConditionValidAck] Received ACK %u, expected %u, SeqNum %u, Flags 0x%x"), AckNum, Conn->SendNext, SeqNum, Flags);

    // Validate ACK number
    BOOL ValidAck = (AckNum == Conn->SendNext);

    // For SYN+ACK, accept any sequence number and update RecvNext
    BOOL ValidSeq;
    if ((Flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        ValidSeq = TRUE;
        Conn->RecvNext = SeqNum + 1;
        DEBUG(TEXT("[TCP_ConditionValidAck] SYN+ACK: Updated RecvNext to %u"), Conn->RecvNext);
    } else {
        // Regular ACK - validate sequence number is within receive window
        ValidSeq = TCP_IsSequenceInWindow(SeqNum, Conn->RecvNext, Conn->RecvWindow);
        if (!ValidSeq) {
            DEBUG(TEXT("[TCP_ConditionValidAck] Sequence number %u outside receive window [%u, %u)"),
                  SeqNum, Conn->RecvNext, Conn->RecvNext + Conn->RecvWindow);
        }
    }

    BOOL Valid = ValidAck && ValidSeq;

    if (Valid) {
        // Clear retransmit timer on valid ACK
        Conn->RetransmitTimer = 0;
        Conn->RetransmitCount = 0;
        DEBUG(TEXT("[TCP_ConditionValidAck] Valid ACK received, cleared timer"));
    }

    return Valid;
}

/************************************************************************/

static BOOL TCP_ConditionValidSyn(STATE_MACHINE* SM, LPVOID EventData) {
    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)SM_GetContext(SM);
    LPTCP_PACKET_EVENT Event = (LPTCP_PACKET_EVENT)EventData;

    if (!Event || !Event->Header) return FALSE;

    // Check SYN flag
    BOOL HasSyn = (Event->Header->Flags & TCP_FLAG_SYN) != 0;
    if (!HasSyn) return FALSE;

    // For SYN packets, additional security checks can be added here
    U32 SeqNum = Ntohl(Event->Header->SequenceNumber);

    // In LISTEN state, we accept any valid SYN
    if (SM_GetCurrentState(&Conn->StateMachine) == TCP_STATE_LISTEN) {
        DEBUG(TEXT("[TCP_ConditionValidSyn] Valid SYN received in LISTEN state, SeqNum %u"), SeqNum);
        return TRUE;
    }

    // In other states, validate sequence number against receive window
    BOOL ValidSeq = TCP_IsSequenceInWindow(SeqNum, Conn->RecvNext, Conn->RecvWindow);

    if (!ValidSeq) {
        DEBUG(TEXT("[TCP_ConditionValidSyn] SYN sequence number %u outside receive window [%u, %u)"),
              SeqNum, Conn->RecvNext, Conn->RecvNext + Conn->RecvWindow);
    }

    return ValidSeq;
}

/************************************************************************/
// TCP Options parsing

typedef struct tag_TCP_OPTIONS {
    BOOL HasMSS;
    U16 MSS;
    BOOL HasWindowScale;
    U8 WindowScale;
    BOOL HasTimestamp;
    U32 TSVal;
    U32 TSEcr;
} TCP_OPTIONS, *LPTCP_OPTIONS;

static void TCP_ParseOptions(const U8* OptionsData, U32 OptionsLength, LPTCP_OPTIONS ParsedOptions) {
    MemorySet(ParsedOptions, 0, sizeof(TCP_OPTIONS));

    U32 Offset = 0;
    while (Offset < OptionsLength) {
        U8 OptionType = OptionsData[Offset];

        // End of option list
        if (OptionType == 0) {
            break;
        }

        // No-operation (padding)
        if (OptionType == 1) {
            Offset++;
            continue;
        }

        // All other options have a length field
        if (Offset + 1 >= OptionsLength) {
            DEBUG(TEXT("[TCP_ParseOptions] Truncated option at offset %u"), Offset);
            break;
        }

        U8 OptionLength = OptionsData[Offset + 1];
        if (OptionLength < 2 || Offset + OptionLength > OptionsLength) {
            DEBUG(TEXT("[TCP_ParseOptions] Invalid option length %u at offset %u"), OptionLength, Offset);
            break;
        }

        switch (OptionType) {
            case 2: // Maximum Segment Size
                if (OptionLength == 4 && Offset + 4 <= OptionsLength) {
                    ParsedOptions->HasMSS = TRUE;
                    ParsedOptions->MSS = (OptionsData[Offset + 2] << 8) | OptionsData[Offset + 3];
                    DEBUG(TEXT("[TCP_ParseOptions] MSS option: %u"), ParsedOptions->MSS);
                }
                break;

            case 3: // Window Scale
                if (OptionLength == 3 && Offset + 3 <= OptionsLength) {
                    ParsedOptions->HasWindowScale = TRUE;
                    ParsedOptions->WindowScale = OptionsData[Offset + 2];
                    DEBUG(TEXT("[TCP_ParseOptions] Window scale option: %u"), ParsedOptions->WindowScale);
                }
                break;

            case 8: // Timestamp
                if (OptionLength == 10 && Offset + 10 <= OptionsLength) {
                    ParsedOptions->HasTimestamp = TRUE;
                    ParsedOptions->TSVal = (OptionsData[Offset + 2] << 24) |
                                         (OptionsData[Offset + 3] << 16) |
                                         (OptionsData[Offset + 4] << 8) |
                                         OptionsData[Offset + 5];
                    ParsedOptions->TSEcr = (OptionsData[Offset + 6] << 24) |
                                         (OptionsData[Offset + 7] << 16) |
                                         (OptionsData[Offset + 8] << 8) |
                                         OptionsData[Offset + 9];
                    DEBUG(TEXT("[TCP_ParseOptions] Timestamp option: TSVal=%u TSEcr=%u"),
                          ParsedOptions->TSVal, ParsedOptions->TSEcr);
                }
                break;

            default:
                DEBUG(TEXT("[TCP_ParseOptions] Unknown option type %u"), OptionType);
                break;
        }

        Offset += OptionLength;
    }
}

/************************************************************************/

U16 TCP_CalculateChecksum(TCP_HEADER* Header, const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP) {
    U32 HeaderLength = (Header->DataOffset >> 4) * 4;
    U32 TCPTotalLength = HeaderLength + PayloadLength;
    U32 Accumulator = 0;

    // Build IPv4 pseudo-header on stack (12 bytes)
    U8 PseudoHeader[12];
    *((U32*)(PseudoHeader + 0)) = SourceIP;                    // Source IP (already in network order)
    *((U32*)(PseudoHeader + 4)) = DestinationIP;              // Destination IP (already in network order)
    PseudoHeader[8] = 0;                                       // Zero byte
    PseudoHeader[9] = 6;                                       // TCP protocol
    *((U16*)(PseudoHeader + 10)) = Htons((U16)TCPTotalLength); // TCP length

    // Save and clear checksum field
    U16 SavedChecksum = Header->Checksum;
    Header->Checksum = 0;

    // Accumulate pseudo-header
    Accumulator = NetworkChecksum_Calculate_Accumulate(PseudoHeader, 12, Accumulator);

    // Accumulate TCP header
    Accumulator = NetworkChecksum_Calculate_Accumulate((const U8*)Header, HeaderLength, Accumulator);

    // Accumulate payload if present
    if (Payload && PayloadLength > 0) {
        Accumulator = NetworkChecksum_Calculate_Accumulate(Payload, PayloadLength, Accumulator);
    }

    // Restore original checksum
    Header->Checksum = SavedChecksum;

    // Finalize checksum
    return NetworkChecksum_Finalize(Accumulator);
}

/************************************************************************/

int TCP_ValidateChecksum(TCP_HEADER* Header, const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP) {
    U16 ReceivedChecksum = Ntohs(Header->Checksum);

    U32 SrcIPHost = Ntohl(SourceIP);
    U32 DstIPHost = Ntohl(DestinationIP);

    DEBUG(TEXT("[TCP_ValidateChecksum] Validating TCP checksum"));
    DEBUG(TEXT("[TCP_ValidateChecksum] Src=%u.%u.%u.%u:%u Dst=%u.%u.%u.%u:%u"),
          (SrcIPHost >> 24) & 0xFF, (SrcIPHost >> 16) & 0xFF, (SrcIPHost >> 8) & 0xFF, SrcIPHost & 0xFF,
          Ntohs(Header->SourcePort),
          (DstIPHost >> 24) & 0xFF, (DstIPHost >> 16) & 0xFF, (DstIPHost >> 8) & 0xFF, DstIPHost & 0xFF,
          Ntohs(Header->DestinationPort));
    DEBUG(TEXT("[TCP_ValidateChecksum] Received checksum: 0x%04X"), ReceivedChecksum);

    // Calculate expected checksum using the proper TCP checksum function
    U16 CalculatedChecksum = TCP_CalculateChecksum(Header, Payload, PayloadLength, SourceIP, DestinationIP);
    CalculatedChecksum = Ntohs(CalculatedChecksum);

    BOOL IsValid = (CalculatedChecksum == ReceivedChecksum);

    DEBUG(TEXT("[TCP_ValidateChecksum] Calculated checksum: 0x%04X, valid: %s"),
          CalculatedChecksum, IsValid ? "YES" : "NO");

    if (!IsValid) {
        DEBUG(TEXT("[TCP_ValidateChecksum] CHECKSUM MISMATCH - packet may be corrupted"));
        DEBUG(TEXT("[TCP_ValidateChecksum] Expected: 0x%04X, Received: 0x%04X"), CalculatedChecksum, ReceivedChecksum);
    }

    return IsValid ? 1 : 0;
}

/************************************************************************/

static void TCP_SendRstToUnknownConnection(LPDEVICE Device, U32 LocalIP, U16 LocalPort, U32 RemoteIP, U16 RemotePort, U32 AckNumber) {
    TCP_HEADER Header;
    U8 Packet[sizeof(TCP_HEADER)];

    DEBUG(TEXT("[TCP_SendRstToUnknownConnection] Sending RST to unknown connection"));

    // Fill TCP header for RST response
    Header.SourcePort = LocalPort;       // Already in network byte order
    Header.DestinationPort = RemotePort; // Already in network byte order
    Header.SequenceNumber = 0;          // RST packets typically use seq=0
    Header.AckNumber = Htonl(AckNumber);
    Header.DataOffset = 0x50; // Data offset = 5 (20 bytes), Reserved = 0
    Header.Flags = TCP_FLAG_RST | TCP_FLAG_ACK;
    Header.WindowSize = 0;
    Header.UrgentPointer = 0;
    Header.Checksum = 0; // Calculate later

    // Copy header to packet
    MemoryCopy(Packet, &Header, sizeof(TCP_HEADER));

    // Calculate checksum
    ((LPTCP_HEADER)Packet)->Checksum = TCP_CalculateChecksum((LPTCP_HEADER)Packet,
        NULL, 0, LocalIP, RemoteIP);

    // Send via IPv4 through specified network device
    if (Device == NULL) {
        return;
    }

    LockMutex(&(Device->Mutex), INFINITY);
    IPv4_Send(Device, RemoteIP, IPV4_PROTOCOL_TCP, Packet, sizeof(TCP_HEADER));
    UnlockMutex(&(Device->Mutex));
}

/************************************************************************/
// Public API implementation

void TCP_Initialize(void) {
    MemorySet(&GlobalTCP, 0, sizeof(TCP_GLOBAL_STATE));
    GlobalTCP.NextEphemeralPort = TCP_GetEphemeralPortStart();
    GlobalTCP.SendBufferSize = TCP_GetConfiguredBufferSize(TEXT(CONFIG_TCP_SEND_BUFFER_SIZE),
                                                          TCP_SEND_BUFFER_SIZE,
                                                          TCP_SEND_BUFFER_SIZE);
    GlobalTCP.ReceiveBufferSize = TCP_GetConfiguredBufferSize(TEXT(CONFIG_TCP_RECEIVE_BUFFER_SIZE),
                                                             TCP_RECV_BUFFER_SIZE,
                                                             TCP_RECV_BUFFER_SIZE);


    // TCP protocol handler will be registered later when devices are initialized

    DEBUG(TEXT("[TCP_Initialize] Done (send buffer=%lu bytes, receive buffer=%lu bytes, next ephemeral port=%u)"),
          GlobalTCP.SendBufferSize, GlobalTCP.ReceiveBufferSize, GlobalTCP.NextEphemeralPort);
}

/************************************************************************/

LPTCP_CONNECTION TCP_CreateConnection(LPDEVICE Device, U32 LocalIP, U16 LocalPort, U32 RemoteIP, U16 RemotePort) {
    if (Device == NULL) {
        DEBUG(TEXT("[TCP_CreateConnection] Device is NULL"));
        return NULL;
    }

    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)CreateKernelObject(sizeof(TCP_CONNECTION), KOID_TCP);
    if (Conn == NULL) {
        DEBUG(TEXT("[TCP_CreateConnection] Failed to allocate TCP connection"));
        return NULL;
    }

    // Initialize TCP-specific fields (LISTNODE_FIELDS already initialized by CreateKernelObject)
    MemorySet(&Conn->Device, 0, sizeof(TCP_CONNECTION) - sizeof(LISTNODE));

    Conn->Device = Device;

    // Set connection parameters - resolve LocalIP if it's 0 (any address)
    if (LocalIP == 0) {
        // Use device's local IP address
        LPIPV4_CONTEXT IPv4Context = IPv4_GetContext(Device);

        SAFE_USE(IPv4Context) {
            Conn->LocalIP = IPv4Context->LocalIPv4_Be;
            DEBUG(TEXT("[TCP_CreateConnection] Using device IP for LocalIP=0: %d.%d.%d.%d"),
                  (Ntohl(Conn->LocalIP) >> 24) & 0xFF, (Ntohl(Conn->LocalIP) >> 16) & 0xFF,
                  (Ntohl(Conn->LocalIP) >> 8) & 0xFF, Ntohl(Conn->LocalIP) & 0xFF);
        } else {
            Conn->LocalIP = 0;
            DEBUG(TEXT("[TCP_CreateConnection] Warning: No IPv4 context found for device"));
        }
    } else {
        Conn->LocalIP = LocalIP;
    }
    Conn->LocalPort = (LocalPort == 0) ? Htons(TCP_GetNextEphemeralPort(Conn->LocalIP)) : LocalPort;
    Conn->RemoteIP = RemoteIP;
    Conn->RemotePort = RemotePort; // RemotePort should already be in network byte order from socket layer
    Conn->SendBufferCapacity = GlobalTCP.SendBufferSize;
    Conn->RecvBufferCapacity = GlobalTCP.ReceiveBufferSize;
    Conn->SendWindow = (Conn->SendBufferCapacity > 0xFFFFU) ? 0xFFFFU : (U16)Conn->SendBufferCapacity;
    Conn->RecvWindow = (Conn->RecvBufferCapacity > 0xFFFFU) ? 0xFFFFU : (U16)Conn->RecvBufferCapacity;
    Conn->RetransmitTimer = 0;
    Conn->RetransmitCount = 0;

    // Initialize sliding window with hysteresis
    TCP_InitSlidingWindow(Conn);

    // Create notification context for this connection
    Conn->NotificationContext = Notification_CreateContext();
    if (Conn->NotificationContext == NULL) {
        ERROR(TEXT("[TCP_CreateConnection] Failed to create notification context"));
        KernelHeapFree(Conn);
        return NULL;
    }
    DEBUG(TEXT("[TCP_CreateConnection] Created notification context %X for connection %X"), (U32)Conn->NotificationContext, (U32)Conn);

    // Register for IPv4 packet sent events on the connection's network device
    LockMutex(&(Conn->Device->Mutex), INFINITY);
    IPv4_RegisterNotification(Conn->Device, NOTIF_EVENT_IPV4_PACKET_SENT,
                             TCP_IPv4PacketSentCallback, Conn);
    UnlockMutex(&(Conn->Device->Mutex));

    // Initialize state machine
    SM_Initialize(&Conn->StateMachine, TCP_Transitions,
                  sizeof(TCP_Transitions) / sizeof(SM_TRANSITION),
                  TCP_States, sizeof(TCP_States) / sizeof(SM_STATE_DEFINITION),
                  TCP_STATE_CLOSED, Conn);

    // Add to connections list
    ListAddTail(Kernel.TCPConnection, Conn);

    // Convert to host byte order for debug display
    U32 LocalIPHost = Ntohl(LocalIP);
    U32 RemoteIPHost = Ntohl(RemoteIP);
    DEBUG(TEXT("[TCP_CreateConnection] Created connection %X (%d.%d.%d.%d:%d -> %d.%d.%d.%d:%d)"),
        (U32)Conn,
        (LocalIPHost >> 24) & 0xFF, (LocalIPHost >> 16) & 0xFF, (LocalIPHost >> 8) & 0xFF, LocalIPHost & 0xFF, Ntohs(Conn->LocalPort),
        (RemoteIPHost >> 24) & 0xFF, (RemoteIPHost >> 16) & 0xFF, (RemoteIPHost >> 8) & 0xFF, RemoteIPHost & 0xFF, Ntohs(RemotePort));

    return Conn;
}

/************************************************************************/

void TCP_DestroyConnection(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        SM_Destroy(&Connection->StateMachine);

        // Destroy notification context
        SAFE_USE (Connection->NotificationContext) {
            Notification_DestroyContext(Connection->NotificationContext);
            Connection->NotificationContext = NULL;
            DEBUG(TEXT("[TCP_DestroyConnection] Destroyed notification context for connection %X"), (U32)Connection);
        }

        // Remove from connections list
        ListRemove(Kernel.TCPConnection, Connection);

        // Mark ID
        Connection->TypeID = KOID_NONE;

        // Free the connection memory
        KernelHeapFree(Connection);

        DEBUG(TEXT("[TCP_DestroyConnection] Destroyed connection %X"), (U32)Connection);
    }
}

/************************************************************************/

int TCP_Connect(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        return SM_ProcessEvent(&Connection->StateMachine, TCP_EVENT_CONNECT, NULL) ? 0 : -1;
    }
    return -1;
}

/************************************************************************/

int TCP_Listen(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        return SM_ProcessEvent(&Connection->StateMachine, TCP_EVENT_LISTEN, NULL) ? 0 : -1;
    }
    return -1;
}

/************************************************************************/

int TCP_Send(LPTCP_CONNECTION Connection, const U8* Data, U32 Length) {
    if (!Data || Length == 0) return -1;

    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        if (SM_GetCurrentState(&Connection->StateMachine) != TCP_STATE_ESTABLISHED) {
            DEBUG(TEXT("[TCP_Send] Cannot send data, connection not established"));
            return -1;
        }

        UINT Capacity = Connection->SendBufferCapacity;
        U32 MaxChunk = (Capacity > (UINT)MAX_U32) ? MAX_U32 : (U32)Capacity;
        if (MaxChunk == 0) {
            MaxChunk = TCP_SEND_BUFFER_SIZE;
        }

        const U8* CurrentData = Data;
        U32 Remaining = Length;

        while (Remaining > 0) {
            U32 ChunkSize = (Remaining > MaxChunk) ? MaxChunk : Remaining;
            int SendResult = TCP_SendPacket(Connection, TCP_FLAG_PSH | TCP_FLAG_ACK, CurrentData, ChunkSize);
            if (SendResult < 0) {
                ERROR(TEXT("[TCP_Send] Failed to send %u bytes chunk"), ChunkSize);
                return -1;
            }

            CurrentData += ChunkSize;
            Remaining -= ChunkSize;
        }

        return (int)Length;
    }
    return -1;
}

/************************************************************************/

int TCP_Receive(LPTCP_CONNECTION Connection, U8* Buffer, U32 BufferSize) {
    if (!Buffer || BufferSize == 0) return -1;

    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        if (Connection->RecvBufferUsed == 0) return 0;

        UINT Used = Connection->RecvBufferUsed;
        U32 CopyLength = (Used > BufferSize) ? BufferSize : (U32)Used;
        MemoryCopy(Buffer, Connection->RecvBuffer, CopyLength);

        // Move remaining data to beginning of buffer
        if (CopyLength < Used) {
            MemoryMove(Connection->RecvBuffer, Connection->RecvBuffer + CopyLength, (U32)(Used - CopyLength));
        }

        TCP_HandleApplicationRead(Connection, CopyLength);

        return CopyLength;
    }
    return -1;
}

/************************************************************************/

int TCP_Close(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        DEBUG(TEXT("[TCP_Close] Closing connection %X, current state=%d"), (U32)Connection, SM_GetCurrentState(&Connection->StateMachine));
        BOOL result = SM_ProcessEvent(&Connection->StateMachine, TCP_EVENT_CLOSE, NULL);
        DEBUG(TEXT("[TCP_Close] Close event processed, result=%d, new state=%d"), result, SM_GetCurrentState(&Connection->StateMachine));

        return result ? 0 : -1;
    }
    DEBUG(TEXT("[TCP_Close] Invalid connection %X"), (U32)Connection);
    return -1;
}

/************************************************************************/

SM_STATE TCP_GetState(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        return SM_GetCurrentState(&Connection->StateMachine);
    }
    return SM_INVALID_STATE;
}

/************************************************************************/

void TCP_OnIPv4Packet(const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP) {
    if (PayloadLength < sizeof(TCP_HEADER)) {
        DEBUG(TEXT("[TCP_OnIPv4Packet] Packet too small (%u bytes)"), PayloadLength);
        return;
    }

    const TCP_HEADER* Header = (const TCP_HEADER*)Payload;
    U32 HeaderLength = (Header->DataOffset >> 4) * 4;

    // Validate header length
    if (HeaderLength < sizeof(TCP_HEADER) || HeaderLength > PayloadLength) {
        DEBUG(TEXT("[TCP_OnIPv4Packet] Invalid header length %u"), HeaderLength);
        return;
    }

    const U8* Data = Payload + HeaderLength;
    U32 DataLength = PayloadLength - HeaderLength;

    // Parse TCP options if present
    TCP_OPTIONS ParsedOptions;
    if (HeaderLength > sizeof(TCP_HEADER)) {
        U32 OptionsLength = HeaderLength - sizeof(TCP_HEADER);
        const U8* OptionsData = Payload + sizeof(TCP_HEADER);
        TCP_ParseOptions(OptionsData, OptionsLength, &ParsedOptions);
        DEBUG(TEXT("[TCP_OnIPv4Packet] Parsed %u bytes of TCP options"), OptionsLength);
    } else {
        MemorySet(&ParsedOptions, 0, sizeof(TCP_OPTIONS));
    }

    DEBUG(TEXT("[TCP_OnIPv4Packet] Received packet: Src=%d.%d.%d.%d:%d Dst=%d.%d.%d.%d:%d Flags=0x%02X Seq=%u Ack=%u"),
        (SourceIP >> 24) & 0xFF, (SourceIP >> 16) & 0xFF, (SourceIP >> 8) & 0xFF, SourceIP & 0xFF, Ntohs(Header->SourcePort),
        (DestinationIP >> 24) & 0xFF, (DestinationIP >> 16) & 0xFF, (DestinationIP >> 8) & 0xFF, DestinationIP & 0xFF, Ntohs(Header->DestinationPort),
        Header->Flags, Ntohl(Header->SequenceNumber), Ntohl(Header->AckNumber));

    // Validate checksum
    if (!TCP_ValidateChecksum((TCP_HEADER*)Header, Data, DataLength, SourceIP, DestinationIP)) {
        DEBUG(TEXT("[TCP_OnIPv4Packet] Invalid checksum"));
        return;
    }

    // Find matching connection
    LPTCP_CONNECTION Conn = NULL;
    LPTCP_CONNECTION Current = (LPTCP_CONNECTION)Kernel.TCPConnection->First;
    while (Current != NULL) {
        if (Current->LocalPort == Header->DestinationPort &&
            Current->RemotePort == Header->SourcePort &&
            Current->RemoteIP == SourceIP &&
            Current->LocalIP == DestinationIP) {
            Conn = Current;
            DEBUG(TEXT("[TCP_OnIPv4Packet] Found matching connection %X"), (U32)Conn);
            break;
        }
        Current = (LPTCP_CONNECTION)Current->Next;
    }

    if (Conn == NULL) {
        DEBUG(TEXT("[TCP_OnIPv4Packet] No matching connection found for port %d->%d"), Ntohs(Header->SourcePort), Ntohs(Header->DestinationPort));

        // Send RST for packets received on unknown connections (except RST packets)
        if (!(Header->Flags & TCP_FLAG_RST)) {
            U32 AckNum = Ntohl(Header->SequenceNumber) + DataLength;
            if (Header->Flags & (TCP_FLAG_SYN | TCP_FLAG_FIN)) {
                AckNum++;
            }
            // TODO: TCP_SendRstToUnknownConnection needs device parameter
            // TCP_SendRstToUnknownConnection(Device, DestinationIP, Header->DestinationPort,
            //                              SourceIP, Header->SourcePort, AckNum);
        }
        return;
    }

    // Create event data
    TCP_PACKET_EVENT Event;
    Event.Header = Header;
    Event.Payload = Data;
    Event.PayloadLength = DataLength;
    Event.SourceIP = SourceIP;
    Event.DestinationIP = DestinationIP;

    // Determine event type based on flags and data length
    U8 Flags = Header->Flags;
    SM_EVENT EventType = TCP_EVENT_RCV_DATA;
    BOOL ProcessResult = FALSE;

    if (DataLength > 0) {
        // Process data
        EventType = TCP_EVENT_RCV_DATA;
        DEBUG(TEXT("[TCP_OnIPv4Packet] Processing DATA event (%d bytes)"), DataLength);
        ProcessResult = SM_ProcessEvent(&Conn->StateMachine, EventType, &Event);
        DEBUG(TEXT("[TCP_OnIPv4Packet] State machine DATA processing result: %s"), ProcessResult ? "SUCCESS" : "FAILED");
    }

    if (Flags & TCP_FLAG_RST) {
        EventType = TCP_EVENT_RCV_RST;
        DEBUG(TEXT("[TCP_OnIPv4Packet] Processing RST event"));
    } else if ((Flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        EventType = TCP_EVENT_RCV_ACK;
        DEBUG(TEXT("[TCP_OnIPv4Packet] Processing SYN+ACK event"));
    } else if (Flags & TCP_FLAG_SYN) {
        EventType = TCP_EVENT_RCV_SYN;
        DEBUG(TEXT("[TCP_OnIPv4Packet] Processing SYN event"));
    } else if (Flags & TCP_FLAG_FIN) {
        EventType = TCP_EVENT_RCV_FIN;
        DEBUG(TEXT("[TCP_OnIPv4Packet] Processing FIN event"));
    } else if (Flags & TCP_FLAG_ACK) {
        EventType = TCP_EVENT_RCV_ACK;
        DEBUG(TEXT("[TCP_OnIPv4Packet] Processing ACK event"));
    }

    DEBUG(TEXT("[TCP_OnIPv4Packet] Processing event (%d bytes)"), DataLength);
    ProcessResult = SM_ProcessEvent(&Conn->StateMachine, EventType, &Event);

    DEBUG(TEXT("[TCP_OnIPv4Packet] State machine processing result: %s"), ProcessResult ? "SUCCESS" : "FAILED");
}

/************************************************************************/

void TCP_Update(void) {
    UINT CurrentTime = GetSystemTime();

    LPTCP_CONNECTION Conn = (LPTCP_CONNECTION)Kernel.TCPConnection->First;
    while (Conn != NULL) {
        LPTCP_CONNECTION Next = (LPTCP_CONNECTION)Conn->Next;
        SM_STATE CurrentState = SM_GetCurrentState(&Conn->StateMachine);

        // Check TIME_WAIT timeout
        if (CurrentState == TCP_STATE_TIME_WAIT &&
            Conn->TimeWaitTimer > 0 &&
            CurrentTime >= Conn->TimeWaitTimer) {
            DEBUG(TEXT("[TCP_Update] TIME_WAIT timeout reached for connection %X"), (U32)Conn);
            SM_ProcessEvent(&Conn->StateMachine, TCP_EVENT_TIMEOUT, NULL);
        }

        // Safety check: if in TIME_WAIT state but timer is invalid, force close
        if (CurrentState == TCP_STATE_TIME_WAIT && Conn->TimeWaitTimer == 0) {
            WARNING(TEXT("[TCP_Update] TIME_WAIT state with invalid timer, forcing close for connection %X"), (U32)Conn);
            SM_ProcessEvent(&Conn->StateMachine, TCP_EVENT_TIMEOUT, NULL);
        }

        // Check retransmit timeout for SYN_SENT state
        // Only retransmit if we're still in SYN_SENT and haven't been closed
        if (CurrentState == TCP_STATE_SYN_SENT &&
            Conn->RetransmitTimer > 0 &&
            CurrentTime >= Conn->RetransmitTimer) {

            if (Conn->RetransmitCount < TCP_MAX_RETRANSMITS) {
                DEBUG(TEXT("[TCP_Update] Retransmitting SYN (attempt %u)"), Conn->RetransmitCount + 1);

                // Retransmit SYN and check if it was actually sent
                int SendResult = TCP_SendPacket(Conn, TCP_FLAG_SYN, NULL, 0);

                if (SendResult >= 0) {
                    // Packet was sent or queued successfully
                    Conn->RetransmitCount++;
                    Conn->RetransmitTimer = CurrentTime + TCP_RETRANSMIT_TIMEOUT;
                    DEBUG(TEXT("[TCP_Update] SYN retransmitted successfully"));
                } else {
                    // Failed to send - try again later
                    Conn->RetransmitTimer = CurrentTime + TCP_RETRANSMIT_TIMEOUT;
                    DEBUG(TEXT("[TCP_Update] SYN retransmit failed, will retry"));
                }
            } else {
                DEBUG(TEXT("[TCP_Update] Maximum retransmits reached, connection failed"));

                // Clear retransmit timer to stop further attempts
                Conn->RetransmitTimer = 0;
                Conn->RetransmitCount = 0;

                // Notify upper layers of connection failure
                SAFE_USE(Conn->NotificationContext) {
                    Notification_Send(Conn->NotificationContext, NOTIF_EVENT_TCP_FAILED, NULL, 0);
                }

                // Transition to CLOSED state
                SM_ProcessEvent(&Conn->StateMachine, TCP_EVENT_RCV_RST, NULL);
            }
        }

        // Update state machine
        SM_Update(&Conn->StateMachine);

        Conn = Next;
    }
}

/************************************************************************/

void TCP_SetNotificationContext(LPTCP_CONNECTION Connection, LPNOTIFICATION_CONTEXT Context) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        Connection->NotificationContext = Context;
        DEBUG(TEXT("[TCP_SetNotificationContext] Set notification context %X for connection %X"), (U32)Context, (U32)Connection);
    }
}

/************************************************************************/

U32 TCP_RegisterCallback(LPTCP_CONNECTION Connection, U32 Event, NOTIFICATION_CALLBACK Callback, LPVOID UserData) {
    if (Connection == NULL || Connection->NotificationContext == NULL) {
        ERROR(TEXT("[TCP_RegisterCallback] Invalid connection or no notification context"));
        return 1;
    }

    U32 Result = Notification_Register(Connection->NotificationContext, Event, Callback, UserData);
    if (Result != 0) {
        DEBUG(TEXT("[TCP_RegisterCallback] Registered callback for event %u on connection %X"), Event, (U32)Connection);
        return 0; // Success
    } else {
        ERROR(TEXT("[TCP_RegisterCallback] Failed to register callback for event %u on connection %X"), Event, (U32)Connection);
        return 1; // Error
    }
}

/************************************************************************/


/**
 * @brief Initialize sliding window with hysteresis thresholds
 * @param Connection The TCP connection to initialize
 */
void TCP_InitSlidingWindow(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        UINT Capacity = Connection->RecvBufferCapacity;
        U32 MaxWindow = (Capacity > (UINT)MAX_U32) ? MAX_U32 : (U32)Capacity;
        if (MaxWindow == 0) {
            MaxWindow = TCP_RECV_BUFFER_SIZE;
        }
        U32 LowThreshold = MaxWindow / 3;      // 1/3 threshold
        U32 HighThreshold = (MaxWindow * 2) / 3; // 2/3 threshold

        Hysteresis_Initialize(&Connection->WindowHysteresis, LowThreshold, HighThreshold, MaxWindow);

        DEBUG(TEXT("[TCP_InitSlidingWindow] Initialized hysteresis: max=%u, low=%u, high=%u for connection %X"),
              MaxWindow, LowThreshold, HighThreshold, (U32)Connection);
    }
}

/************************************************************************/

/**
 * @brief Process data consumption and update window with hysteresis
 * @param Connection The TCP connection
 * @param DataConsumed Amount of data consumed by application
 */
void TCP_ProcessDataConsumption(LPTCP_CONNECTION Connection, U32 DataConsumed) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        // NOTE: RecvBufferUsed is already updated by caller, just calculate window
        UINT AvailableSpace = (Connection->RecvBufferCapacity > Connection->RecvBufferUsed)
                              ? (Connection->RecvBufferCapacity - Connection->RecvBufferUsed)
                              : 0;
        U16 NewWindow = (AvailableSpace > 0xFFFFU) ? 0xFFFFU : (U16)AvailableSpace;

        // Update hysteresis with new window size
        BOOL StateChanged = Hysteresis_Update(&Connection->WindowHysteresis, NewWindow);

        // Note: RecvWindow is no longer used - window is calculated dynamically in TCP_SendPacket

        DEBUG(TEXT("[TCP_ProcessDataConsumption] DataConsumed=%u, BufferUsed=%lu, Window=%u, StateChanged=%d"),
              DataConsumed, Connection->RecvBufferUsed, NewWindow, StateChanged);

        if (StateChanged) {
            BOOL NewState = Hysteresis_GetState(&Connection->WindowHysteresis);
            DEBUG(TEXT("[TCP_ProcessDataConsumption] Window state transition to %s"),
                  NewState ? TEXT("HIGH") : TEXT("LOW"));
        }
    }
}

/************************************************************************/

/**
 * @brief Check if window update ACK should be sent based on hysteresis
 * @param Connection The TCP connection
 * @return TRUE if window update should be sent, FALSE otherwise
 */
BOOL TCP_ShouldSendWindowUpdate(LPTCP_CONNECTION Connection) {
    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        BOOL ShouldSend = Hysteresis_IsTransitionPending(&Connection->WindowHysteresis);

        if (ShouldSend) {
            BOOL CurrentState = Hysteresis_GetState(&Connection->WindowHysteresis);
            U32 CurrentWindow = Hysteresis_GetValue(&Connection->WindowHysteresis);

            DEBUG(TEXT("[TCP_ShouldSendWindowUpdate] Window update needed: state=%s, window=%u"),
                  CurrentState ? TEXT("HIGH") : TEXT("LOW"), CurrentWindow);

            // Clear the transition flag since we're about to send the update
            Hysteresis_ClearTransition(&Connection->WindowHysteresis);
        }

        return ShouldSend;
    }
    return FALSE;
}

/************************************************************************/

void TCP_HandleApplicationRead(LPTCP_CONNECTION Connection, U32 BytesConsumed) {
    if (BytesConsumed == 0) {
        return;
    }

    SAFE_USE_VALID_ID(Connection, KOID_TCP) {
        UINT PreviousUsed = Connection->RecvBufferUsed;

        if (BytesConsumed > (U32)PreviousUsed) {
            BytesConsumed = (U32)PreviousUsed;
        }

        if (BytesConsumed == 0) {
            return;
        }

        Connection->RecvBufferUsed -= BytesConsumed;

        TCP_ProcessDataConsumption(Connection, BytesConsumed);

        BOOL ShouldSend = TCP_ShouldSendWindowUpdate(Connection);
        if (!ShouldSend && PreviousUsed == Connection->RecvBufferCapacity &&
            Connection->RecvBufferUsed < Connection->RecvBufferCapacity) {
            ShouldSend = TRUE;
        }

        if (ShouldSend) {
            DEBUG(TEXT("[TCP_HandleApplicationRead] Sending window update ACK after consuming %u bytes"), BytesConsumed);
            if (TCP_SendPacket(Connection, TCP_FLAG_ACK, NULL, 0) < 0) {
                ERROR(TEXT("[TCP_HandleApplicationRead] Failed to transmit window update ACK"));
            }
        }
    }
}
