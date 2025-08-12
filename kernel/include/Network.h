
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved


    Generic Network Driver Interface

\***************************************************************************/

#ifndef NETWORK_H_INCLUDED
#define NETWORK_H_INCLUDED

/***************************************************************************/

#define PROTOCOL_NONE    0x00000000
#define PROTOCOL_EXOS    0x00000001
#define PROTOCOL_TCP     0x00000002
#define PROTOCOL_IP      0x00000003
#define PROTOCOL_HTTP    0x00000004
#define PROTOCOL_FTP     0x00000005

/***************************************************************************/
/* Generic Network Driver Function IDs                                     */
/* All network drivers must implement these IDs                            */

#define DF_NT_RESET          (DF_FIRSTFUNC + 0x00) /* Reset the adapter */
#define DF_NT_GETMAC         (DF_FIRSTFUNC + 0x01) /* Get MAC address (out: U8[6]) */
#define DF_NT_SEND           (DF_FIRSTFUNC + 0x02) /* Send frame (param=ptr, param2=len) */
#define DF_NT_POLL           (DF_FIRSTFUNC + 0x03) /* Poll RX ring */
#define DF_NT_SETRXCB        (DF_FIRSTFUNC + 0x04) /* Set RX callback */

/***************************************************************************/
/* Generic Network Driver Error Codes                                      */

#define DF_ERROR_NT_TX_FAIL  (DF_ERROR_FIRST + 0x00) /* Transmission failed */
#define DF_ERROR_NT_RX_FAIL  (DF_ERROR_FIRST + 0x01) /* Reception failed */
#define DF_ERROR_NT_NO_LINK  (DF_ERROR_FIRST + 0x02) /* Link down */

/***************************************************************************/

typedef struct tag_IPADDRESS {
    U8 Data[4];
} IPADDRESS, *LPIPADDRESS;

/***************************************************************************/

#endif /* NETWORK_H_INCLUDED */
