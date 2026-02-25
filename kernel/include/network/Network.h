
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
#include "User.h"

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

typedef struct tag_IPADDRESS {
    U8 Data[4];
} IPADDRESS, *LPIPADDRESS;

/************************************************************************/

typedef struct tag_NETWORKRESET {
    LPPCI_DEVICE Device;
} NETWORKRESET, *LPNETWORKRESET;

typedef struct tag_NETWORKGETINFO {
    LPPCI_DEVICE Device;
    LPNETWORKINFO Info;
} NETWORKGETINFO, *LPNETWORKGETINFO;

typedef struct tag_NETWORKSETRXCB {
    LPPCI_DEVICE Device;
    NT_RXCB Callback;
    LPVOID UserData;
} NETWORKSETRXCB, *LPNETWORKSETRXCB;

typedef struct tag_NETWORKSEND {
    LPPCI_DEVICE Device;
    const U8 *Data;
    U32 Length;
} NETWORKSEND, *LPNETWORKSEND;

typedef struct tag_NETWORKPOLL {
    LPPCI_DEVICE Device;
} NETWORKPOLL, *LPNETWORKPOLL;

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

#endif  // NETWORK_H_INCLUDED
