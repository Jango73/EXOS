
/************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

    Address Resolution Protocol (ARP)

\************************************************************************/

#ifndef ARP_H_INCLUDED
#define ARP_H_INCLUDED

#include "../include/Base.h"
#include "../include/Driver.h"
#include "../include/Network.h"
#include "../include/String.h"
#include "Endianness.h"

/************************************************************************/
// EtherTypes
#define ETHERTYPE_IPV4 0x0800
#define ETHERTYPE_ARP  0x0806

// ARP constants
#define ARP_HTYPE_ETH  0x0001
#define ARP_PTYPE_IPV4 0x0800
#define ARP_HLEN_ETH   6
#define ARP_PLEN_IPV4  4
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

/************************************************************************/

typedef struct __attribute__((packed)) EthernetHeaderTag {
	U8	Destination[6];
	U8	Source[6];
	U16	EtherType; // Big-endian on the wire
} EthernetHeader, *LPEthernetHeader;

typedef struct __attribute__((packed)) ArpPacketTag {
	U16	HardwareType;    // 1 = Ethernet (be)
	U16	ProtocolType;    /// 0x0800 = IPv4 (be)
	U8	HardwareLength;  // 6
	U8	ProtocolLength;  // 4
	U16	Operation;       // 1 = request, 2 = reply (be)

	U8	SenderHardwareAddress[6]; // MAC
	U32	SenderProtocolAddress;    // IPv4 (be)

	U8	TargetHardwareAddress[6]; // MAC
	U32	TargetProtocolAddress;    // IPv4 (be)
} ArpPacket, *LPArpPacket;

// Public API
void ARP_Initialize(LPVOID NetworkDevice, DRVFUNC NetworkCommand, U32 LocalIPv4_Be);
void ARP_Tick(void); /* Call periodically (e.g., each 1s) to age the cache. */

// Resolve returns 1 if the MAC is known (OutMacAddress filled), 0 otherwise.
// If unknown, it triggers an ARP Request (paced).
int ARP_Resolve(U32 TargetIPv4_Be, U8 OutMacAddress[6]);

// Debug helper
void ARP_DumpCache(void);

// RX entry point (registered as DF_NT_SETRXCB). You don't call this manually.
void ARP_OnEthernetFrame(const U8* Frame, U32 Length);

#endif	// ARP_H_INCLUDED
