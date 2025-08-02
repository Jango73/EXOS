
// NETWORK.H

/***************************************************************************\

  EXOS Kernel
  Copyright (c) 1999-2025 Jango73
  All rights reserved

\***************************************************************************/

#ifndef NETWORK_H_INCLUDED
#define NETWORK_H_INCLUDED

/***************************************************************************/

#define PROTOCOL_NONE 0x00000000
#define PROTOCOL_EXOS 0x00000001
#define PROTOCOL_TCP 0x00000002
#define PROTOCOL_IP 0x00000003
#define PROTOCOL_HTTP 0x00000004
#define PROTOCOL_FTP 0x00000005

/***************************************************************************/

typedef struct tag_IPADDRESS {
    U8 Data[4];
} IPADDRESS, *LPIPADDRESS;

/***************************************************************************/

#endif
