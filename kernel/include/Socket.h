
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


    Berkeley Socket Implementation

\************************************************************************/

#ifndef SOCKET_H_INCLUDED
#define SOCKET_H_INCLUDED

#include "Base.h"
#include "List.h"
#include "Network.h"
#include "TCP.h"
#include "ID.h"
#include "CircularBuffer.h"

/************************************************************************/
// Socket Address Family

#define SOCKET_AF_UNSPEC    0
#define SOCKET_AF_INET      2
#define SOCKET_AF_INET6     10

/************************************************************************/
// Socket Type

#define SOCKET_TYPE_STREAM     1  // TCP
#define SOCKET_TYPE_DGRAM      2  // UDP
#define SOCKET_TYPE_RAW        3  // Raw socket

/************************************************************************/
// Socket Protocol

#define SOCKET_PROTOCOL_IP     0
#define SOCKET_PROTOCOL_TCP    6
#define SOCKET_PROTOCOL_UDP    17

/************************************************************************/
// Socket States

#define SOCKET_STATE_CLOSED       0
#define SOCKET_STATE_CREATED      1
#define SOCKET_STATE_BOUND        2
#define SOCKET_STATE_LISTENING    3
#define SOCKET_STATE_CONNECTING   4
#define SOCKET_STATE_CONNECTED    5
#define SOCKET_STATE_CLOSING      6

/************************************************************************/
// Socket Error Codes

#define SOCKET_ERROR_NONE         0
#define SOCKET_ERROR_INVALID      -1
#define SOCKET_ERROR_NOMEM        -2
#define SOCKET_ERROR_INUSE        -3
#define SOCKET_ERROR_NOTBOUND     -4
#define SOCKET_ERROR_NOTLISTENING -5
#define SOCKET_ERROR_NOTCONNECTED -6
#define SOCKET_ERROR_WOULDBLOCK   -7
#define SOCKET_ERROR_CONNREFUSED  -8
#define SOCKET_ERROR_TIMEOUT      -9
#define SOCKET_ERROR_MSGSIZE      -10

/************************************************************************/
// Socket Options

#define SOL_SOCKET                1
#define SO_RCVTIMEO               20

/************************************************************************/
// Socket Shutdown Types

#define SOCKET_SHUTDOWN_READ      0
#define SOCKET_SHUTDOWN_WRITE     1
#define SOCKET_SHUTDOWN_BOTH      2

/************************************************************************/
// Socket Buffer Structure

#define SOCKET_BUFFER_SIZE 8192

/************************************************************************/
// Socket Control Block

typedef struct tag_SOCKET {
    LISTNODE_FIELDS

    // Socket identification
    U16 AddressFamily;
    U16 SocketType;
    U16 Protocol;
    U32 State;

    // Address binding
    SOCKET_ADDRESS_INET LocalAddress;
    SOCKET_ADDRESS_INET RemoteAddress;

    // Connection management
    LPTCP_CONNECTION TCPConnection;    // Pointer to TCP connection (if TCP)
    U32 ListenBacklog;      // Maximum pending connections
    LPLIST PendingConnections; // Queue of pending connections

    // Data buffers
    CIRCULAR_BUFFER ReceiveBuffer;
    U8 ReceiveBufferData[SOCKET_BUFFER_SIZE];
    CIRCULAR_BUFFER SendBuffer;
    U8 SendBufferData[SOCKET_BUFFER_SIZE];

    // Socket options
    BOOL ReuseAddress;
    BOOL KeepAlive;
    BOOL NoDelay;
    U32  ReceiveTimeout;
    U32  SendTimeout;
    U32  ReceiveTimeoutStartTime;  // When timeout started

    // Statistics
    U32 BytesSent;
    U32 BytesReceived;
    U32 PacketsSent;
    U32 PacketsReceived;
} SOCKET, *LPSOCKET;

/************************************************************************/
// Berkeley Socket API Functions

// Socket creation and destruction
U32 SocketCreate(U16 AddressFamily, U16 SocketType, U16 Protocol);
U32 SocketClose(U32 SocketHandle);
U32 SocketShutdown(U32 SocketHandle, U32 How);

// Address binding and connection
U32 SocketBind(U32 SocketHandle, LPSOCKET_ADDRESS Address, U32 AddressLength);
U32 SocketListen(U32 SocketHandle, U32 Backlog);
U32 SocketAccept(U32 SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength);
U32 SocketConnect(U32 SocketHandle, LPSOCKET_ADDRESS Address, U32 AddressLength);

// Data transmission
I32 SocketSend(U32 SocketHandle, const void* Buffer, U32 Length, U32 Flags);
I32 SocketReceive(U32 SocketHandle, void* Buffer, U32 Length, U32 Flags);
I32 SocketSendTo(U32 SocketHandle, const void* Buffer, U32 Length, U32 Flags, LPSOCKET_ADDRESS DestinationAddress, U32 AddressLength);
I32 SocketReceiveFrom(U32 SocketHandle, void* Buffer, U32 Length, U32 Flags, LPSOCKET_ADDRESS SourceAddress, U32* AddressLength);

// Socket options and information
U32 SocketGetOption(U32 SocketHandle, U32 Level, U32 OptionName, void* OptionValue, U32* OptionLength);
U32 SocketSetOption(U32 SocketHandle, U32 Level, U32 OptionName, const void* OptionValue, U32 OptionLength);
U32 SocketGetPeerName(U32 SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength);
U32 SocketGetSocketName(U32 SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength);

// System functions
void SocketUpdate(void);

// Utility functions
U32 SocketAddressInetMake(U32 IPAddress, U16 Port, LPSOCKET_ADDRESS_INET Address);
U32 SocketAddressInetToGeneric(LPSOCKET_ADDRESS_INET InetAddress, LPSOCKET_ADDRESS GenericAddress);
U32 SocketAddressGenericToInet(LPSOCKET_ADDRESS GenericAddress, LPSOCKET_ADDRESS_INET InetAddress);

// Internal functions
void SocketTCPNotificationCallback(LPNOTIFICATION_DATA NotificationData, LPVOID UserData);
U32 SocketTCPReceiveCallback(LPTCP_CONNECTION TCPConnection, const U8* Data, U32 DataLength);
void SocketDestructor(LPVOID Item);

/************************************************************************/

#endif // SOCKET_H_INCLUDED
