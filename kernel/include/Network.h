
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved


    Generic Network Driver Interface

\***************************************************************************/

#ifndef NETWORK_H_INCLUDED
#define NETWORK_H_INCLUDED

/***************************************************************************/

#include "PCI.h"

typedef struct tag_NETWORKINFO NETWORKINFO, *LPNETWORKINFO;

typedef void (*NT_RXCB)(const U8 *Frame, U32 Length);

#define PROTOCOL_NONE 0x00000000
#define PROTOCOL_EXOS 0x00000001
#define PROTOCOL_TCP 0x00000002
#define PROTOCOL_IP 0x00000003
#define PROTOCOL_HTTP 0x00000004
#define PROTOCOL_FTP 0x00000005

/***************************************************************************/
/* Generic Network Driver Function IDs                                     */
/* All network drivers must implement these IDs                            */

#define DF_NT_RESET (DF_FIRSTFUNC + 0x00)   /* Reset the adapter */
#define DF_NT_GETINFO (DF_FIRSTFUNC + 0x01) /* Get device information */
#define DF_NT_SEND (DF_FIRSTFUNC + 0x02)    /* Send frame (param=ptr, param2=len) */
#define DF_NT_POLL (DF_FIRSTFUNC + 0x03)    /* Poll RX ring */
#define DF_NT_SETRXCB (DF_FIRSTFUNC + 0x04) /* Set RX callback */

/***************************************************************************/
/* Generic Network Driver Error Codes                                      */

#define DF_ERROR_NT_TX_FAIL (DF_ERROR_FIRST + 0x00) /* Transmission failed */
#define DF_ERROR_NT_RX_FAIL (DF_ERROR_FIRST + 0x01) /* Reception failed */
#define DF_ERROR_NT_NO_LINK (DF_ERROR_FIRST + 0x02) /* Link down */

/***************************************************************************/

typedef struct tag_IPADDRESS {
    U8 Data[4];
} IPADDRESS, *LPIPADDRESS;

/***************************************************************************/

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
} NETWORKSETRXCB, *LPNETWORKSETRXCB;

typedef struct tag_NETWORKSEND {
    LPPCI_DEVICE Device;
    const U8 *Data;
    U32 Length;
} NETWORKSEND, *LPNETWORKSEND;

typedef struct tag_NETWORKPOLL {
    LPPCI_DEVICE Device;
} NETWORKPOLL, *LPNETWORKPOLL;

/***************************************************************************/

#endif /* NETWORK_H_INCLUDED */
