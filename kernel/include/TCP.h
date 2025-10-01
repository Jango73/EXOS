
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

#ifndef TCP_H_INCLUDED
#define TCP_H_INCLUDED

#include "../include/Base.h"
#include "../include/StateMachine.h"
#include "../include/IPv4.h"
#include "../include/Notification.h"
#include "../include/List.h"
#include "../include/Hysteresis.h"
#include "Endianness.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/
// TCP Flags

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20
#define TCP_FLAG_ECE 0x40
#define TCP_FLAG_CWR 0x80

/************************************************************************/
// TCP States (using state machine framework)

#define TCP_STATE_CLOSED        0
#define TCP_STATE_LISTEN        1
#define TCP_STATE_SYN_SENT      2
#define TCP_STATE_SYN_RECEIVED  3
#define TCP_STATE_ESTABLISHED   4
#define TCP_STATE_FIN_WAIT_1    5
#define TCP_STATE_FIN_WAIT_2    6
#define TCP_STATE_CLOSE_WAIT    7
#define TCP_STATE_CLOSING       8
#define TCP_STATE_LAST_ACK      9
#define TCP_STATE_TIME_WAIT     10

/************************************************************************/
// TCP Events (using state machine framework)

#define TCP_EVENT_CONNECT       0
#define TCP_EVENT_LISTEN        1
#define TCP_EVENT_CLOSE         2
#define TCP_EVENT_RCV_SYN       3
#define TCP_EVENT_RCV_ACK       4
#define TCP_EVENT_RCV_FIN       5
#define TCP_EVENT_RCV_RST       6
#define TCP_EVENT_RCV_DATA      7
#define TCP_EVENT_SEND_DATA     8
#define TCP_EVENT_TIMEOUT       9

/************************************************************************/

#define TCP_RETRANSMIT_TIMEOUT 3000  // 3 seconds in milliseconds
#define TCP_TIME_WAIT_TIMEOUT 30000  // 30 seconds in milliseconds
#define TCP_MAX_RETRANSMITS 5       // Maximum retransmission attempts

/************************************************************************/
// TCP Header Structure

typedef struct tag_TCP_HEADER {
    U16 SourcePort;         // Source port (big-endian)
    U16 DestinationPort;    // Destination port (big-endian)
    U32 SequenceNumber;     // Sequence number (big-endian)
    U32 AckNumber;          // Acknowledgment number (big-endian)
    U8  DataOffset;         // Data offset (4 bits) + Reserved (4 bits)
    U8  Flags;              // CWR, ECE, URG, ACK, PSH, RST, SYN, FIN flags
    U16 WindowSize;         // Window size (big-endian)
    U16 Checksum;           // Checksum (big-endian)
    U16 UrgentPointer;      // Urgent pointer (big-endian)
} TCP_HEADER, *LPTCP_HEADER;

/************************************************************************/
// TCP Connection Block

#define TCP_SEND_BUFFER_SIZE 8192
#define TCP_RECV_BUFFER_SIZE 32768

typedef struct tag_TCP_CONNECTION {
    LISTNODE_FIELDS

    // Connection identification
    LPDEVICE Device;        // Network device for this connection
    U32 LocalIP;            // Local IP address (network byte order)
    U16 LocalPort;          // Local port (network byte order)
    U32 RemoteIP;           // Remote IP address (network byte order)
    U16 RemotePort;         // Remote port (network byte order)

    // Sequence numbers
    U32 SendNext;           // Next sequence number to send
    U32 SendUnacked;        // Oldest unacknowledged sequence number
    U32 RecvNext;           // Next expected sequence number

    // Window management
    U16 SendWindow;         // Send window size
    U16 RecvWindow;         // Receive window size
    HYSTERESIS WindowHysteresis;  // Hysteresis for window updates

    // Buffers
    U8 SendBuffer[TCP_SEND_BUFFER_SIZE];
    U32 SendBufferUsed;
    U8 RecvBuffer[TCP_RECV_BUFFER_SIZE];
    U32 RecvBufferUsed;

    // State machine
    STATE_MACHINE StateMachine;

    // Timers
    U32 RetransmitTimer;
    U32 TimeWaitTimer;
    U32 RetransmitCount;

    // Notification context for this connection
    LPNOTIFICATION_CONTEXT NotificationContext;
} TCP_CONNECTION, *LPTCP_CONNECTION;

/************************************************************************/
// TCP Packet Event Data

typedef struct tag_TCP_PACKET_EVENT {
    const TCP_HEADER* Header;
    const U8* Payload;
    U32 PayloadLength;
    U32 SourceIP;
    U32 DestinationIP;
} TCP_PACKET_EVENT, *LPTCP_PACKET_EVENT;

/************************************************************************/
// Public API

// Initialize TCP subsystem
void TCP_Initialize(void);

// Create new TCP connection (returns connection pointer)
LPTCP_CONNECTION TCP_CreateConnection(LPDEVICE Device, U32 LocalIP, U16 LocalPort, U32 RemoteIP, U16 RemotePort);

// Destroy TCP connection
void TCP_DestroyConnection(LPTCP_CONNECTION Connection);

// Connect to remote host (active open)
int TCP_Connect(LPTCP_CONNECTION Connection);

// Listen for incoming connections (passive open)
int TCP_Listen(LPTCP_CONNECTION Connection);

// Send data
int TCP_Send(LPTCP_CONNECTION Connection, const U8* Data, U32 Length);

// Receive data
int TCP_Receive(LPTCP_CONNECTION Connection, U8* Buffer, U32 BufferSize);

// Close connection
int TCP_Close(LPTCP_CONNECTION Connection);

// Get connection state
SM_STATE TCP_GetState(LPTCP_CONNECTION Connection);

// Process incoming IPv4 packet (registered as IPv4 protocol handler)
void TCP_OnIPv4Packet(const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP);

// Update TCP subsystem (call periodically for timers)
void TCP_Update(void);

// Set notification context for a connection
void TCP_SetNotificationContext(LPTCP_CONNECTION Connection, LPNOTIFICATION_CONTEXT Context);

// Register callback for TCP events
U32 TCP_RegisterCallback(LPTCP_CONNECTION Connection, U32 Event, NOTIFICATION_CALLBACK Callback, LPVOID UserData);


// Initialize sliding window with hysteresis
void TCP_InitSlidingWindow(LPTCP_CONNECTION Connection);

// Process data consumption and update window with hysteresis
void TCP_ProcessDataConsumption(LPTCP_CONNECTION Connection, U32 DataConsumed);

// Check if window update ACK should be sent based on hysteresis
BOOL TCP_ShouldSendWindowUpdate(LPTCP_CONNECTION Connection);
void TCP_HandleSocketDataConsumed(LPTCP_CONNECTION Connection, U32 BytesConsumed);

// Utility functions
U16 TCP_CalculateChecksum(TCP_HEADER* Header, const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP);
int TCP_ValidateChecksum(TCP_HEADER* Header, const U8* Payload, U32 PayloadLength, U32 SourceIP, U32 DestinationIP);

/************************************************************************/

#pragma pack(pop)

#endif // TCP_H_INCLUDED
