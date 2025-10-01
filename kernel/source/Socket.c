
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

#include "../include/Socket.h"

#include "../include/Clock.h"
#include "../include/Heap.h"
#include "../include/ID.h"
#include "../include/IPv4.h"
#include "../include/Kernel.h"
#include "../include/Log.h"
#include "../include/Memory.h"
#include "../include/NetworkManager.h"
#include "../include/System.h"
#include "../include/TCP.h"
#include "../include/CircularBuffer.h"

/************************************************************************/
// Global socket management

/**
 * @brief Destructor function for socket control blocks
 *
 * This function is called when a socket control block is being destroyed.
 * It cleans up any allocated resources including pending connections list
 * and TCP connections.
 *
 * @param Item Pointer to the socket control block to destroy
 */
void SocketDestructor(LPVOID Item) {
    LPSOCKET Socket = (LPSOCKET)Item;

    SAFE_USE_VALID_ID(Socket, ID_SOCKET) {
        if (Socket->PendingConnections) {
            DeleteList(Socket->PendingConnections);
        }

        if (Socket->TCPConnection != NULL && Socket->SocketType == SOCKET_TYPE_STREAM) {
            TCP_DestroyConnection(Socket->TCPConnection);
        }
    }
}

/************************************************************************/

/**
 * @brief Create a new socket
 *
 * This function creates a new socket of the specified type and protocol.
 * Currently supports AF_INET address family with TCP and UDP protocols.
 *
 * @param AddressFamily Address family (SOCKET_AF_INET)
 * @param SocketType Socket type (SOCKET_TYPE_STREAM or SOCKET_TYPE_DGRAM)
 * @param Protocol Protocol (SOCKET_PROTOCOL_TCP or SOCKET_PROTOCOL_UDP)
 * @return Socket descriptor on success, or negative error code on failure
 */
U32 SocketCreate(U16 AddressFamily, U16 SocketType, U16 Protocol) {
    DEBUG(TEXT("[SocketCreate] Creating socket: AF=%d, Type=%d, Protocol=%d"), AddressFamily, SocketType, Protocol);

    // Validate parameters
    if (AddressFamily != SOCKET_AF_INET) {
        ERROR(TEXT("[SocketCreate] Unsupported address family: %d"),AddressFamily);
        return SOCKET_ERROR_INVALID;
    }

    if (SocketType != SOCKET_TYPE_STREAM && SocketType != SOCKET_TYPE_DGRAM) {
        ERROR(TEXT("[SocketCreate] Unsupported socket type: %d"),SocketType);
        return SOCKET_ERROR_INVALID;
    }

    // Allocate socket control block
    LPSOCKET Socket = (LPSOCKET)CreateKernelObject(sizeof(SOCKET), ID_SOCKET);
    if (!Socket) {
        ERROR(TEXT("[SocketCreate] Failed to allocate socket control block"));
        return SOCKET_ERROR_NOMEM;
    }

    // Initialize socket-specific fields (LISTNODE_FIELDS already initialized by CreateKernelObject)
    MemorySet(&Socket->AddressFamily, 0, sizeof(SOCKET) - sizeof(LISTNODE));
    Socket->AddressFamily = AddressFamily;
    Socket->SocketType = SocketType;
    Socket->Protocol = Protocol;
    Socket->State = SOCKET_STATE_CREATED;

    // Set default socket options
    Socket->ReuseAddress = FALSE;
    Socket->KeepAlive = FALSE;
    Socket->NoDelay = FALSE;
    Socket->ReceiveTimeout = 0;
    Socket->SendTimeout = 0;
    Socket->ReceiveTimeoutStartTime = 0;

    // Initialize buffers
    CircularBuffer_Initialize(&Socket->ReceiveBuffer, Socket->ReceiveBufferData, SOCKET_BUFFER_SIZE);
    CircularBuffer_Initialize(&Socket->SendBuffer, Socket->SendBufferData, SOCKET_BUFFER_SIZE);

    // Add to socket list
    if (ListAddTail(Kernel.Socket, Socket) == 0) {
        ERROR(TEXT("[SocketCreate] Failed to add socket to list"));
        KernelHeapFree(Socket);
        return SOCKET_ERROR_NOMEM;
    }

    DEBUG(TEXT("[SocketCreate] Socket created at %p"), Socket);
    return (U32)Socket;
}

/************************************************************************/

/**
 * @brief Close a socket
 *
 * This function closes a socket and releases all associated resources.
 * Any pending TCP connections are also closed.
 *
 * @param SocketHandle The socket descriptor to close
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketClose(U32 SocketHandle) {
    DEBUG(TEXT("[SocketClose] Closing socket %d"), SocketHandle);

    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    SAFE_USE_VALID_ID(Socket, ID_SOCKET) {
        // Close TCP connection if exists
        SAFE_USE_VALID_ID(Socket->TCPConnection, ID_TCP) {
            if (Socket->SocketType == SOCKET_TYPE_STREAM) {
                TCP_Close(Socket->TCPConnection);
            }
        }

        // Update socket state
        Socket->State = SOCKET_STATE_CLOSED;

        // Remove from list (this will call the destructor)
        ListErase(Kernel.Socket, Socket);

        DEBUG(TEXT("[SocketClose] Socket %d closed"), SocketHandle);
        return SOCKET_ERROR_NONE;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Shutdown a socket connection
 *
 * This function shuts down part or all of a socket connection. For TCP sockets,
 * it gracefully closes the connection.
 *
 * @param SocketHandle The socket descriptor to shutdown
 * @param How How to shutdown (SOCKET_SHUTDOWN_READ, SOCKET_SHUTDOWN_WRITE, or SOCKET_SHUTDOWN_BOTH)
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketShutdown(U32 SocketHandle, U32 How) {
    UNUSED(How);
    DEBUG(TEXT("[SocketShutdown] Shutting down socket %x, how=%d"), SocketHandle, How);

    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    SAFE_USE_VALID_ID(Socket, ID_SOCKET) {
        DEBUG(TEXT("[SocketShutdown] Socket state=%d, type=%d, TCPConnection=%x"), Socket->State, Socket->SocketType, Socket->TCPConnection);

        // Allow shutdown on connecting sockets too (not just connected ones)
        if (Socket->State == SOCKET_STATE_CLOSED) {
            ERROR(TEXT("[SocketShutdown] Socket %x already closed"), SocketHandle);
            return SOCKET_ERROR_NOTCONNECTED;
        }

        // For TCP sockets, gracefully close the connection
        if (Socket->SocketType == SOCKET_TYPE_STREAM && Socket->TCPConnection != NULL) {
            DEBUG(TEXT("[SocketShutdown] Calling TCP_Close for connection %x"), Socket->TCPConnection);
            TCP_Close(Socket->TCPConnection);
            Socket->State = SOCKET_STATE_CLOSING;
            DEBUG(TEXT("[SocketShutdown] Socket state changed to CLOSING"));
        } else {
            DEBUG(TEXT("[SocketShutdown] Not calling TCP_Close - type=%d, TCPConnection=%x"), Socket->SocketType, Socket->TCPConnection);
        }

        return SOCKET_ERROR_NONE;
    }

    ERROR(TEXT("[SocketShutdown] SAFE_USE_VALID_ID failed for socket %x"), SocketHandle);
    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Create an IPv4 socket address structure
 *
 * This function creates and initializes an IPv4 socket address structure
 * with the specified IP address and port.
 *
 * @param IPAddress IPv4 address in network byte order
 * @param Port Port number in network byte order
 * @param Address Pointer to the address structure to initialize
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketAddressInetMake(U32 IPAddress, U16 Port, LPSOCKET_ADDRESS_INET Address) {
    if (!Address) return SOCKET_ERROR_INVALID;

    MemorySet(Address, 0, sizeof(SOCKET_ADDRESS_INET));
    Address->AddressFamily = SOCKET_AF_INET;
    Address->Port = Port;
    Address->Address = IPAddress;

    return SOCKET_ERROR_NONE;
}

/************************************************************************/

/**
 * @brief Convert IPv4 address to generic socket address
 *
 * This function converts an IPv4-specific socket address structure to
 * a generic socket address structure.
 *
 * @param InetAddress Pointer to the IPv4 address structure
 * @param GenericAddress Pointer to the generic address structure
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketAddressInetToGeneric(LPSOCKET_ADDRESS_INET InetAddress, LPSOCKET_ADDRESS GenericAddress) {
    if (!InetAddress || !GenericAddress) return SOCKET_ERROR_INVALID;

    MemoryCopy(GenericAddress, InetAddress, sizeof(SOCKET_ADDRESS_INET));
    return SOCKET_ERROR_NONE;
}

/************************************************************************/

/**
 * @brief Convert generic socket address to IPv4 address
 *
 * This function converts a generic socket address structure to an
 * IPv4-specific socket address structure.
 *
 * @param GenericAddress Pointer to the generic address structure
 * @param InetAddress Pointer to the IPv4 address structure
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketAddressGenericToInet(LPSOCKET_ADDRESS GenericAddress, LPSOCKET_ADDRESS_INET InetAddress) {
    if (!GenericAddress || !InetAddress) return SOCKET_ERROR_INVALID;

    if (GenericAddress->AddressFamily != SOCKET_AF_INET) {
        return SOCKET_ERROR_INVALID;
    }

    MemoryCopy(InetAddress, GenericAddress, sizeof(SOCKET_ADDRESS_INET));
    return SOCKET_ERROR_NONE;
}

/************************************************************************/

/**
 * @brief Update all sockets
 *
 * This function updates all active sockets, checking for timeouts and
 * handling state transitions. Should be called periodically by the system.
 */
void SocketUpdate(void) {
    // Update all sockets (check timeouts, handle state changes, etc.)
    if (!Kernel.Socket) return;

    LPSOCKET Socket = (LPSOCKET)Kernel.Socket->First;

    while (Socket) {
        LPSOCKET NextSocket = (LPSOCKET)Socket->Next;

        SAFE_USE(Socket) {
            // Handle timeouts and state transitions
            if (Socket->SocketType == SOCKET_TYPE_STREAM && Socket->TCPConnection != NULL) {
                SM_STATE TCPState = TCP_GetState(Socket->TCPConnection);

                // Update socket state based on TCP state
                switch (TCPState) {
                    case TCP_STATE_ESTABLISHED:
                        if (Socket->State == SOCKET_STATE_CONNECTING) {
                            Socket->State = SOCKET_STATE_CONNECTED;
                        }
                        break;

                    case TCP_STATE_CLOSED:
                        if (Socket->State != SOCKET_STATE_CLOSED) {
                            Socket->State = SOCKET_STATE_CLOSED;
                            DEBUG(TEXT("[SocketUpdate] Socket %x closed"), (U32)Socket);
                        }
                        break;
                }
            }
        }

        Socket = NextSocket;
    }
}

/************************************************************************/

/**
 * @brief Bind a socket to a local address
 *
 * This function binds a socket to a specified local address and port.
 * The socket must be in the created state and the address must not be in use.
 *
 * @param SocketHandle The socket descriptor to bind
 * @param Address Pointer to the local address to bind to
 * @param AddressLength Size of the address structure
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketBind(U32 SocketHandle, LPSOCKET_ADDRESS Address, U32 AddressLength) {
    DEBUG(TEXT("[SocketBind] Binding socket %x"), SocketHandle);

    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    if (!Address || AddressLength < sizeof(SOCKET_ADDRESS_INET)) {
        ERROR(TEXT("[SocketBind] Invalid address or length"));
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, ID_SOCKET) {
        if (Socket->State != SOCKET_STATE_CREATED) {
            ERROR(TEXT("[SocketBind] Socket %x already bound or in invalid state"), SocketHandle);
            return SOCKET_ERROR_INUSE;
        }

        // Convert generic address to inet address
        SOCKET_ADDRESS_INET InetAddress;
        if (SocketAddressGenericToInet(Address, &InetAddress) != SOCKET_ERROR_NONE) {
            ERROR(TEXT("[SocketBind] Failed to convert address"));
            return SOCKET_ERROR_INVALID;
        }

        // Check if address is already in use (simple check)
        LPSOCKET ExistingSocket = (LPSOCKET)Kernel.Socket->First;

        while (ExistingSocket) {
            SAFE_USE(ExistingSocket) {
                if (ExistingSocket != Socket &&
                    ExistingSocket->State >= SOCKET_STATE_BOUND &&
                    ExistingSocket->LocalAddress.Port == InetAddress.Port &&
                    (ExistingSocket->LocalAddress.Address == InetAddress.Address ||
                     ExistingSocket->LocalAddress.Address == 0 ||
                     InetAddress.Address == 0)) {
                    if (!Socket->ReuseAddress) {
                        ERROR(TEXT("[SocketBind] Address already in use"));
                        return SOCKET_ERROR_INUSE;
                    }
                }
                ExistingSocket = (LPSOCKET)ExistingSocket->Next;
            } else {
                break;
            }
        }

        // Bind the address
        MemoryCopy(&Socket->LocalAddress, &InetAddress, sizeof(SOCKET_ADDRESS_INET));
        Socket->State = SOCKET_STATE_BOUND;

        DEBUG(TEXT("[SocketBind] Socket %x bound to %d.%d.%d.%d:%d"),
              SocketHandle,
              (InetAddress.Address >> 0) & 0xFF,
              (InetAddress.Address >> 8) & 0xFF,
              (InetAddress.Address >> 16) & 0xFF,
              (InetAddress.Address >> 24) & 0xFF,
              Htons(InetAddress.Port));

        return SOCKET_ERROR_NONE;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Set a socket to listen for incoming connections
 *
 * This function configures a TCP socket to listen for incoming connections.
 * The socket must be bound to a local address before calling this function.
 *
 * @param SocketHandle The socket descriptor to set to listen mode
 * @param Backlog Maximum number of pending connections in the queue
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketListen(U32 SocketHandle, U32 Backlog) {
    DEBUG(TEXT("[SocketListen] Setting socket %x to listen with backlog %d"), SocketHandle, Backlog);

    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    SAFE_USE_VALID_ID(Socket, ID_SOCKET) {
        if (Socket->State != SOCKET_STATE_BOUND) {
            ERROR(TEXT("[SocketListen] Socket %x not bound"), SocketHandle);
            return SOCKET_ERROR_NOTBOUND;
        }

        if (Socket->SocketType != SOCKET_TYPE_STREAM) {
            ERROR(TEXT("[SocketListen] Socket %x is not a stream socket"), SocketHandle);
            return SOCKET_ERROR_INVALID;
        }

        // Create pending connections queue
        if (!Socket->PendingConnections) {
            Socket->PendingConnections = NewList(NULL, KernelHeapAlloc, KernelHeapFree);
            if (!Socket->PendingConnections) {
                ERROR(TEXT("[SocketListen] Failed to create pending connections queue"));
                return SOCKET_ERROR_NOMEM;
            }
        }

        // Create TCP connection for listening
        Socket->TCPConnection = TCP_CreateConnection(
            (LPDEVICE)NetworkManager_GetPrimaryDevice(),
            Socket->LocalAddress.Address,
            Socket->LocalAddress.Port,
            0, 0);

        if (Socket->TCPConnection == NULL) {
            ERROR(TEXT("[SocketListen] Failed to create TCP connection for listening"));
            return SOCKET_ERROR_INVALID;
        }

        // Start listening
        if (TCP_Listen(Socket->TCPConnection) != 0) {
            ERROR(TEXT("[SocketListen] Failed to start TCP listening"));
            TCP_DestroyConnection(Socket->TCPConnection);
            Socket->TCPConnection = NULL;
            return SOCKET_ERROR_INVALID;
        }

        Socket->ListenBacklog = Backlog;
        Socket->State = SOCKET_STATE_LISTENING;

        DEBUG(TEXT("[SocketListen] Socket %x now listening"), SocketHandle);
        return SOCKET_ERROR_NONE;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Accept an incoming connection
 *
 * This function accepts a pending connection on a listening socket and
 * creates a new socket for the established connection.
 *
 * @param SocketHandle The listening socket descriptor
 * @param Address Pointer to store the remote address of the accepted connection
 * @param AddressLength Pointer to the size of the address buffer
 * @return New socket descriptor for the accepted connection, or error code on failure
 */
U32 SocketAccept(U32 SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength) {
    DEBUG(TEXT("[SocketAccept] Accepting connection on socket %x"), SocketHandle);

    LPSOCKET ListenSocket = (LPSOCKET)SocketHandle;

    SAFE_USE_VALID_ID(ListenSocket, ID_SOCKET) {
        if (ListenSocket->State != SOCKET_STATE_LISTENING) {
            ERROR(TEXT("[SocketAccept] Socket %x not listening"), SocketHandle);
            return SOCKET_ERROR_NOTLISTENING;
        }

        // Check for pending connections
        if (!ListenSocket->PendingConnections || ListenSocket->PendingConnections->NumItems == 0) {
            // No pending connections, would block
            DEBUG(TEXT("[SocketAccept] No pending connections on socket %x"), SocketHandle);
            return SOCKET_ERROR_WOULDBLOCK;
        }

        // Get the first pending connection
        LPSOCKET PendingSocket = (LPSOCKET)ListenSocket->PendingConnections->First;
        if (!PendingSocket) {
            ERROR(TEXT("[SocketAccept] No pending connection found"));
            return SOCKET_ERROR_WOULDBLOCK;
        }

        // Remove from pending queue
        ListRemove(ListenSocket->PendingConnections, PendingSocket);

        // Create new socket for the accepted connection
        U32 NewSocketDescriptor = SocketCreate(SOCKET_AF_INET, SOCKET_TYPE_STREAM, SOCKET_PROTOCOL_TCP);
        if (NewSocketDescriptor == (U32)SOCKET_ERROR_INVALID) {
            ERROR(TEXT("[SocketAccept] Failed to create new socket for accepted connection"));
            KernelHeapFree(PendingSocket);
            return SOCKET_ERROR_NOMEM;
        }

        LPSOCKET NewSocket = (LPSOCKET)NewSocketDescriptor;

        SAFE_USE_VALID_ID(NewSocket, ID_SOCKET) {
            // Copy connection information
            MemoryCopy(&NewSocket->LocalAddress, &ListenSocket->LocalAddress, sizeof(SOCKET_ADDRESS_INET));
            MemoryCopy(&NewSocket->RemoteAddress, &PendingSocket->RemoteAddress, sizeof(SOCKET_ADDRESS_INET));
            NewSocket->TCPConnection = PendingSocket->TCPConnection;
            NewSocket->State = SOCKET_STATE_CONNECTED;

            // Return remote address if requested
            if (Address && AddressLength && *AddressLength >= sizeof(SOCKET_ADDRESS_INET)) {
                SocketAddressInetToGeneric(&NewSocket->RemoteAddress, Address);
                *AddressLength = sizeof(SOCKET_ADDRESS_INET);
            }

            KernelHeapFree(PendingSocket);

            DEBUG(TEXT("[SocketAccept] Connection accepted on socket %x, new socket %x"),SocketHandle, NewSocketDescriptor);
            return NewSocketDescriptor;
        } else {
            // SAFE_USE_VALID_ID failed, cleanup and return error
            SocketClose(NewSocketDescriptor);
            KernelHeapFree(PendingSocket);
            ERROR(TEXT("[SocketAccept] Failed to validate new socket"));
            return SOCKET_ERROR_INVALID;
        }
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Connect a socket to a remote address
 *
 * This function initiates a connection to a remote address. For TCP sockets,
 * this performs the three-way handshake to establish a connection.
 *
 * @param SocketHandle The socket descriptor to connect
 * @param Address Pointer to the remote address to connect to
 * @param AddressLength Size of the address structure
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketConnect(U32 SocketHandle, LPSOCKET_ADDRESS Address, U32 AddressLength) {
    DEBUG(TEXT("[SocketConnect] Connecting socket %x"), SocketHandle);

    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    if (!Address || AddressLength < sizeof(SOCKET_ADDRESS_INET)) {
        ERROR(TEXT("[SocketConnect] Invalid address or length"));
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, ID_SOCKET) {
        if (Socket->State != SOCKET_STATE_CREATED && Socket->State != SOCKET_STATE_BOUND) {
            ERROR(TEXT("[SocketConnect] Socket %x in invalid state for connect"), SocketHandle);
            return SOCKET_ERROR_INVALID;
        }

        if (Socket->SocketType != SOCKET_TYPE_STREAM) {
            ERROR(TEXT("[SocketConnect] Socket %x is not a stream socket"), SocketHandle);
            return SOCKET_ERROR_INVALID;
        }

        // Convert generic address to inet address
        SOCKET_ADDRESS_INET RemoteAddress;
        if (SocketAddressGenericToInet(Address, &RemoteAddress) != SOCKET_ERROR_NONE) {
            ERROR(TEXT("[SocketConnect] Failed to convert remote address"));
            return SOCKET_ERROR_INVALID;
        }

        // If socket is not bound, bind to any local address
        if (Socket->State == SOCKET_STATE_CREATED) {
            SOCKET_ADDRESS_INET LocalAddress;
            SocketAddressInetMake(0, 0, &LocalAddress); // Any address, any port
            MemoryCopy(&Socket->LocalAddress, &LocalAddress, sizeof(SOCKET_ADDRESS_INET));
            Socket->State = SOCKET_STATE_BOUND;
        }

        // Store remote address
        MemoryCopy(&Socket->RemoteAddress, &RemoteAddress, sizeof(SOCKET_ADDRESS_INET));

        // Get network device and check if ready
        LPDEVICE NetworkDevice = (LPDEVICE)NetworkManager_GetPrimaryDevice();
        if (NetworkDevice == NULL) {
            ERROR(TEXT("[SocketConnect] No network device available"));
            return SOCKET_ERROR_INVALID;
        }

        // Wait for network to be ready with timeout
        U32 WaitStartMillis = GetSystemTime();
        U32 TimeoutMs = 30000; // 30 seconds timeout
        while (!NetworkManager_IsDeviceReady(NetworkDevice)) {
            U32 ElapsedMs = GetSystemTime() - WaitStartMillis;
            if (ElapsedMs > TimeoutMs) {
                ERROR(TEXT("[SocketConnect] Timeout waiting for network to be ready"));
                return SOCKET_ERROR_TIMEOUT;
            }
            DEBUG(TEXT("[SocketConnect] Waiting for network to be ready..."));
            DoSystemCall(SYSCALL_Sleep, 100);
        }

        // Create TCP connection
        Socket->TCPConnection = TCP_CreateConnection(
            NetworkDevice,
            Socket->LocalAddress.Address,
            Socket->LocalAddress.Port,
            RemoteAddress.Address,
            RemoteAddress.Port);

        if (Socket->TCPConnection == NULL) {
            ERROR(TEXT("[SocketConnect] Failed to create TCP connection"));
            return SOCKET_ERROR_INVALID;
        }

        // Register for TCP connection events
        if (TCP_RegisterCallback(Socket->TCPConnection, NOTIF_EVENT_TCP_CONNECTED, SocketTCPNotificationCallback, Socket) != 0) {
            ERROR(TEXT("[SocketConnect] Failed to register TCP notification"));
        } else {
            DEBUG(TEXT("[SocketConnect] Registered TCP notification callback for socket %x"), (U32)Socket);
        }

        // Initiate TCP connection
        if (TCP_Connect(Socket->TCPConnection) != 0) {
            ERROR(TEXT("[SocketConnect] Failed to initiate TCP connection"));
            TCP_DestroyConnection(Socket->TCPConnection);
            Socket->TCPConnection = NULL;
            // Reset socket state to allow retry
            Socket->State = SOCKET_STATE_BOUND;
            return SOCKET_ERROR_CONNREFUSED;
        }

        Socket->State = SOCKET_STATE_CONNECTING;

        DEBUG(TEXT("[SocketConnect] Socket %x connecting to %d.%d.%d.%d:%d"),
              SocketHandle,
              (RemoteAddress.Address >> 0) & 0xFF,
              (RemoteAddress.Address >> 8) & 0xFF,
              (RemoteAddress.Address >> 16) & 0xFF,
              (RemoteAddress.Address >> 24) & 0xFF,
              Htons(RemoteAddress.Port));

        return SOCKET_ERROR_NONE;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Send data on a connected socket
 *
 * This function sends data on a connected socket. For TCP sockets,
 * the data is sent reliably and in order.
 *
 * @param SocketHandle The socket descriptor to send data on
 * @param Buffer Pointer to the data to send
 * @param Length Number of bytes to send
 * @param Flags Send flags (currently unused)
 * @return Number of bytes sent on success, or negative error code on failure
 */
I32 SocketSend(U32 SocketHandle, const void* Buffer, U32 Length, U32 Flags) {
    UNUSED(Flags);
    if (!Buffer || Length == 0) {
        ERROR(TEXT("[SocketSend] Invalid buffer or length"));
        return SOCKET_ERROR_INVALID;
    }

    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    SAFE_USE_VALID_ID(Socket, ID_SOCKET) {
        if (Socket->State != SOCKET_STATE_CONNECTED) {
            ERROR(TEXT("[SocketSend] Socket %x not connected"), SocketHandle);
            return SOCKET_ERROR_NOTCONNECTED;
        }

        if (Socket->SocketType == SOCKET_TYPE_STREAM && Socket->TCPConnection != NULL) {
            // Send via TCP
            I32 Result = TCP_Send(Socket->TCPConnection, (const U8*)Buffer, Length);
            if (Result > 0) {
                Socket->BytesSent += Result;
                Socket->PacketsSent++;
                DEBUG(TEXT("[SocketSend] Sent %d bytes on socket %x"),Result, SocketHandle);
            }
            return Result;
        } else {
            ERROR(TEXT("[SocketSend] Unsupported socket type for send"));
            return SOCKET_ERROR_INVALID;
        }
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Receive data from a connected socket
 *
 * This function receives data from a connected socket. For TCP sockets,
 * data is received from the internal buffer.
 *
 * @param SocketHandle The socket descriptor to receive data from
 * @param Buffer Pointer to the buffer to store received data
 * @param Length Maximum number of bytes to receive
 * @param Flags Receive flags (currently unused)
 * @return Number of bytes received on success, or negative error code on failure
 */
I32 SocketReceive(U32 SocketHandle, void* Buffer, U32 Length, U32 Flags) {
    UNUSED(Flags);
    if (!Buffer || Length == 0) {
        ERROR(TEXT("[SocketReceive] Invalid buffer or length"));
        return SOCKET_ERROR_INVALID;
    }

    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    SAFE_USE_VALID_ID(Socket, ID_SOCKET) {
        if (Socket->State != SOCKET_STATE_CONNECTED && Socket->State != SOCKET_STATE_CLOSED) {
            ERROR(TEXT("[SocketReceive] Socket %x not connected (state=%d)"), SocketHandle, Socket->State);
            return SOCKET_ERROR_NOTCONNECTED;
        }

        if (Socket->SocketType == SOCKET_TYPE_STREAM && Socket->TCPConnection != NULL) {
            // Check receive buffer first
            U32 AvailableData = CircularBuffer_GetAvailableData(&Socket->ReceiveBuffer);
            if (AvailableData > 0) {
                U32 BytesToCopy = CircularBuffer_Read(&Socket->ReceiveBuffer, (U8*)Buffer, Length);

                Socket->BytesReceived += BytesToCopy;
                Socket->ReceiveTimeoutStartTime = 0; // Reset timeout so user space can continue waiting after new data arrives

                // NOTE: TCP window is now calculated automatically based on TCP buffer usage

                DEBUG(TEXT("[SocketReceive] Received %d bytes from socket %x"),BytesToCopy, SocketHandle);
                return BytesToCopy;
            } else {
                // No data available - check timeout
                if (Socket->ReceiveTimeout > 0) {
                    U32 CurrentTime = GetSystemTime();

                    // Initialize timeout start time on first call
                    if (Socket->ReceiveTimeoutStartTime == 0) {
                        Socket->ReceiveTimeoutStartTime = CurrentTime;
                    }

                    // Check if timeout exceeded
                    if ((CurrentTime - Socket->ReceiveTimeoutStartTime) >= Socket->ReceiveTimeout) {
                        Socket->ReceiveTimeoutStartTime = 0; // Reset for next operation
                        DEBUG(TEXT("[SocketReceive] Receive timeout (%u ms) exceeded for socket %x"), Socket->ReceiveTimeout, SocketHandle);
                        DEBUG(TEXT("[SocketReceive] User space may retry if the connection is still alive"));
                        return SOCKET_ERROR_TIMEOUT;
                    }
                }

                // No data available - check if connection is closed
                if (Socket->State == SOCKET_STATE_CLOSED) {
                    DEBUG(TEXT("[SocketReceive] Connection closed, returning EOF"));
                    return 0; // EOF
                }

                // No data available, would block
                return SOCKET_ERROR_WOULDBLOCK;
            }
        } else {
            ERROR(TEXT("[SocketReceive] Unsupported socket type for receive"));
            return SOCKET_ERROR_INVALID;
        }
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Send data to a specific address (UDP)
 *
 * This function sends data to a specific destination address without
 * establishing a connection. Currently not implemented.
 *
 * @param SocketHandle The socket descriptor to send data on
 * @param Buffer Pointer to the data to send
 * @param Length Number of bytes to send
 * @param Flags Send flags (currently unused)
 * @param DestinationAddress Destination address to send to
 * @param AddressLength Size of the destination address structure
 * @return Number of bytes sent on success, or negative error code on failure
 */
I32 SocketSendTo(U32 SocketHandle, const void* Buffer, U32 Length, U32 Flags,
                 LPSOCKET_ADDRESS DestinationAddress, U32 AddressLength) {
    UNUSED(SocketHandle);
    UNUSED(Buffer);
    UNUSED(Length);
    UNUSED(Flags);
    UNUSED(DestinationAddress);
    UNUSED(AddressLength);
    // TODO: Implement UDP sendto
    ERROR(TEXT("[SocketSendTo] SocketSendTo not implemented yet"));
    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Receive data from any address (UDP)
 *
 * This function receives data from any source address without requiring
 * an established connection. Currently not implemented.
 *
 * @param SocketHandle The socket descriptor to receive data from
 * @param Buffer Pointer to the buffer to store received data
 * @param Length Maximum number of bytes to receive
 * @param Flags Receive flags (currently unused)
 * @param SourceAddress Pointer to store the source address of received data
 * @param AddressLength Pointer to the size of the source address buffer
 * @return Number of bytes received on success, or negative error code on failure
 */
I32 SocketReceiveFrom(U32 SocketHandle, void* Buffer, U32 Length, U32 Flags,
                      LPSOCKET_ADDRESS SourceAddress, U32* AddressLength) {
    UNUSED(SocketHandle);
    UNUSED(Buffer);
    UNUSED(Length);
    UNUSED(Flags);
    UNUSED(SourceAddress);
    UNUSED(AddressLength);
    // TODO: Implement UDP recvfrom
    ERROR(TEXT("[SocketReceiveFrom] SocketReceiveFrom not implemented yet"));
    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief TCP receive callback function
 *
 * This function is called by the TCP layer when data is received on a
 * TCP connection. It buffers the data in the appropriate socket's receive buffer.
 *
 * @param TCPConnection The TCP connection ID that received data
 * @param Data Pointer to the received data
 * @param DataLength Number of bytes received
 */
void SocketTCPNotificationCallback(LPNOTIFICATION_DATA NotificationData, LPVOID UserData) {
    LPSOCKET Socket = (LPSOCKET)UserData;

    if (!Socket || !NotificationData) return;

    DEBUG(TEXT("[SocketTCPNotificationCallback] Socket %x received TCP event %u"), (U32)Socket, NotificationData->EventID);

    if (NotificationData->EventID == NOTIF_EVENT_TCP_CONNECTED) {
        DEBUG(TEXT("[SocketTCPNotificationCallback] TCP connection established, updating socket state"));
        Socket->State = SOCKET_STATE_CONNECTED;
    }
}

void SocketTCPReceiveCallback(LPTCP_CONNECTION TCPConnection, const U8* Data, U32 DataLength) {
    if (!Data || DataLength == 0) return;

    LPSOCKET Socket = (LPSOCKET)Kernel.Socket->First;
    while (Socket) {
        SAFE_USE(Socket) {
            if (Socket->TCPConnection == TCPConnection) {
                // Copy data to receive buffer using CircularBuffer
                U32 BytesToCopy = CircularBuffer_Write(&Socket->ReceiveBuffer, Data, DataLength);

                if (BytesToCopy > 0) {
                    Socket->PacketsReceived++;
                    DEBUG(TEXT("[SocketTCPReceiveCallback] Buffered %d bytes for socket %x"),BytesToCopy, Socket);
                } else {
                    WARNING(TEXT("[SocketTCPReceiveCallback] Receive buffer full for socket %x"),Socket);
                }

                // NOTE: TCP window is now calculated automatically based on TCP buffer usage

                break;
            }
            Socket = (LPSOCKET)Socket->Next;
        } else {
            break;
        }
    }
}

/************************************************************************/

/**
 * @brief Get socket option
 *
 * This function retrieves the value of a socket option.
 * Currently not implemented.
 *
 * @param SocketHandle The socket descriptor
 * @param Level The protocol level (SOL_SOCKET, IPPROTO_TCP, etc.)
 * @param OptionName The option name to retrieve
 * @param OptionValue Pointer to store the option value
 * @param OptionLength Pointer to the size of the option value buffer
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketGetOption(U32 SocketHandle, U32 Level, U32 OptionName, void* OptionValue, U32* OptionLength) {
    UNUSED(Level);
    UNUSED(OptionName);
    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    if (!OptionValue || !OptionLength) {
        ERROR(TEXT("[SocketGetOption] Invalid option value or length pointers"));
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, ID_SOCKET) {
        // TODO: Implement socket options
        ERROR(TEXT("[SocketGetOption] SocketGetOption not implemented yet"));
        return SOCKET_ERROR_INVALID;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Set socket option
 *
 * This function sets the value of a socket option.
 * Currently not implemented.
 *
 * @param SocketHandle The socket descriptor
 * @param Level The protocol level (SOL_SOCKET, IPPROTO_TCP, etc.)
 * @param OptionName The option name to set
 * @param OptionValue Pointer to the option value to set
 * @param OptionLength Size of the option value
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketSetOption(U32 SocketHandle, U32 Level, U32 OptionName, const void* OptionValue, U32 OptionLength) {
    UNUSED(Level);
    UNUSED(OptionName);
    UNUSED(OptionLength);
    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    if (!OptionValue) {
        ERROR(TEXT("[SocketSetOption] Invalid option value pointer"));
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, ID_SOCKET) {
        if (Level == SOL_SOCKET) {
            switch (OptionName) {
                case SO_RCVTIMEO: {
                    if (OptionLength != sizeof(U32)) {
                        ERROR(TEXT("[SocketSetOption] Invalid option length for SO_RCVTIMEO"));
                        return SOCKET_ERROR_INVALID;
                    }
                    U32 timeoutMs = *(const U32*)OptionValue;
                    Socket->ReceiveTimeout = timeoutMs;
                    DEBUG(TEXT("[SocketSetOption] Set SO_RCVTIMEO to %u ms for socket %x"), timeoutMs, SocketHandle);
                    return SOCKET_ERROR_NONE;
                }
                default:
                    ERROR(TEXT("[SocketSetOption] Unsupported socket option %u"), OptionName);
                    return SOCKET_ERROR_INVALID;
            }
        } else {
            ERROR(TEXT("[SocketSetOption] Unsupported option level %u"), Level);
            return SOCKET_ERROR_INVALID;
        }
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Get the remote address of a connected socket
 *
 * This function retrieves the remote address of a connected socket.
 * The socket must be in connected state.
 *
 * @param SocketHandle The socket descriptor
 * @param Address Pointer to store the remote address
 * @param AddressLength Pointer to the size of the address buffer
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketGetPeerName(U32 SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength) {
    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    if (!Address || !AddressLength) {
        DEBUG(TEXT("[SocketGetPeerName] Invalid address or length pointers"));
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, ID_SOCKET) {
        if (Socket->State != SOCKET_STATE_CONNECTED) {
            DEBUG(TEXT("[SocketGetPeerName] Socket %x not connected"), SocketHandle);
            return SOCKET_ERROR_NOTCONNECTED;
        }

        if (*AddressLength < sizeof(SOCKET_ADDRESS_INET)) {
            DEBUG(TEXT("[SocketGetPeerName] Address length too small"));
            return SOCKET_ERROR_INVALID;
        }

        SocketAddressInetToGeneric(&Socket->RemoteAddress, Address);
        *AddressLength = sizeof(SOCKET_ADDRESS_INET);

        return SOCKET_ERROR_NONE;
    }

    return SOCKET_ERROR_INVALID;
}

/************************************************************************/

/**
 * @brief Get the local address of a socket
 *
 * This function retrieves the local address that a socket is bound to.
 * The socket must be in bound state or higher.
 *
 * @param SocketHandle The socket descriptor
 * @param Address Pointer to store the local address
 * @param AddressLength Pointer to the size of the address buffer
 * @return SOCKET_ERROR_NONE on success, or error code on failure
 */
U32 SocketGetSocketName(U32 SocketHandle, LPSOCKET_ADDRESS Address, U32* AddressLength) {
    LPSOCKET Socket = (LPSOCKET)SocketHandle;

    if (!Address || !AddressLength) {
        ERROR(TEXT("[SocketGetSocketName] Invalid address or length pointers"));
        return SOCKET_ERROR_INVALID;
    }

    SAFE_USE_VALID_ID(Socket, ID_SOCKET) {
        if (Socket->State < SOCKET_STATE_BOUND) {
            ERROR(TEXT("[SocketGetSocketName] Socket %x not bound"), SocketHandle);
            return SOCKET_ERROR_NOTBOUND;
        }

        if (*AddressLength < sizeof(SOCKET_ADDRESS_INET)) {
            ERROR(TEXT("[SocketGetSocketName] Address length too small"));
            return SOCKET_ERROR_INVALID;
        }

        SocketAddressInetToGeneric(&Socket->LocalAddress, Address);
        *AddressLength = sizeof(SOCKET_ADDRESS_INET);

        return SOCKET_ERROR_NONE;
    }

    return SOCKET_ERROR_INVALID;
}
