
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


    Network

\************************************************************************/

#ifndef NETWORK_H_INCLUDED
#define NETWORK_H_INCLUDED

/************************************************************************/

#include "drivers/bus/PCI.h"
#include "Device.h"
#include "user/User.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

typedef void (*NT_RXCB)(const U8 *Frame, U32 Length, LPVOID UserData);

#define PROTOCOL_NONE 0x00000000
#define PROTOCOL_EXOS 0x00000001
#define PROTOCOL_TCP 0x00000002
#define PROTOCOL_IP 0x00000003
#define PROTOCOL_HTTP 0x00000004
#define PROTOCOL_FTP 0x00000005

/************************************************************************/
// Generic Network Driver Function IDs
// All network drivers must implement these IDs

#define DF_NT_RESET (DF_FIRST_FUNCTION + 0x00)      /* Reset the adapter */
#define DF_NT_GETINFO (DF_FIRST_FUNCTION + 0x01)    /* Get device information */
#define DF_NT_SEND (DF_FIRST_FUNCTION + 0x02)       /* Send frame (param=ptr, param2=len) */
#define DF_NT_POLL (DF_FIRST_FUNCTION + 0x03)       /* Poll RX ring */
#define DF_NT_SETRXCB (DF_FIRST_FUNCTION + 0x04)    /* Set RX callback */

/************************************************************************/
// Generic Network Driver Error Codes

#define DF_RETURN_NT_TX_FAIL (DF_RETURN_FIRST + 0x00) /* Transmission failed */
#define DF_RETURN_NT_RX_FAIL (DF_RETURN_FIRST + 0x01) /* Reception failed */
#define DF_RETURN_NT_NO_LINK (DF_RETURN_FIRST + 0x02) /* Link down */

/************************************************************************/

typedef struct tag_NETWORK_RESET {
    LPPCI_DEVICE Device;
} NETWORK_RESET, *LPNETWORK_RESET;

typedef struct tag_NETWORK_GET_INFO {
    LPPCI_DEVICE Device;
    LPNETWORK_INFO Info;
} NETWORK_GET_INFO, *LPNETWORK_GET_INFO;

typedef struct tag_NETWORK_SET_RX_CB {
    LPPCI_DEVICE Device;
    NT_RXCB Callback;
    LPVOID UserData;
} NETWORK_SET_RX_CB, *LPNETWORK_SET_RX_CB;

typedef struct tag_NETWORK_SEND {
    LPPCI_DEVICE Device;
    const U8 *Data;
    U32 Length;
} NETWORK_SEND, *LPNETWORK_SEND;

typedef struct tag_NETWORK_POLL {
    LPPCI_DEVICE Device;
} NETWORK_POLL, *LPNETWORK_POLL;

/************************************************************************/

/**
 * @brief Send a raw Ethernet frame through a network device.
 *
 * @param Device Target network device.
 * @param Data Pointer to frame buffer.
 * @param Length Frame length in bytes.
 * @return 1 on success, 0 otherwise.
 */
INT Network_SendRawFrame(LPDEVICE Device, const U8 *Data, U32 Length);

/************************************************************************/

#pragma pack(pop)

#endif  // NETWORK_H_INCLUDED
