
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

    Address Resolution Protocol (ARP)

\***************************************************************************/

#include "../include/ARP.h"
#include "../include/Log.h"
#include "../include/Network.h"

/************************************************************************/
// Cache Configuration

#define ARP_CACHE_SIZE               32U
#define ARP_ENTRY_TTL_TICKS         600U /* ~10 minutes if ARP_Tick is called each 1s */
#define ARP_PROBE_INTERVAL_TICKS      3U /* pacing for repeated requests */

/************************************************************************/
// Types

typedef struct ArpCacheEntryTag {
	U32	IPv4_Be;          /* IPv4 address (big-endian) */
	U8	MacAddress[6];
	U32	TimeToLive;       /* in ticks */
	U8	IsValid;
	U8	IsProbing;        /* request already sent recently */
} ArpCacheEntry, *LPArpCacheEntry;

typedef struct ArpContextTag {
	LPVOID	NetworkDevice;
	DRVFUNC	NetworkCommand;

	U8		LocalMacAddress[6];
	U32		LocalIPv4_Be;

	ArpCacheEntry	Cache[ARP_CACHE_SIZE];
} ArpContext, *LPArpContext;

static ArpContext GlobalArp;

/************************************************************************/
// Utilities

static int MacIsBroadcast(const U8 Mac[6]) {
	return (Mac[0]==0xFF && Mac[1]==0xFF && Mac[2]==0xFF && Mac[3]==0xFF && Mac[4]==0xFF && Mac[5]==0xFF);
}

/************************************************************************/

static void MacCopy(U8 Destination[6], const U8 Source[6]) {
	MemoryCopy(Destination, Source, 6);
}

/************************************************************************/

static int MacEqual(const U8 A[6], const U8 B[6]) {
	U32 Index; for (Index=0; Index<6; Index++) if (A[Index]!=B[Index]) return 0; return 1;
}

/************************************************************************/

static LPArpCacheEntry ArpLookup(U32 IPv4_Be) {
	U32 Index;
	for (Index=0; Index<ARP_CACHE_SIZE; Index++) {
		if (GlobalArp.Cache[Index].IsValid && GlobalArp.Cache[Index].IPv4_Be == IPv4_Be) return &GlobalArp.Cache[Index];
	}
	return NULL;
}

/************************************************************************/

static LPArpCacheEntry ArpAllocateSlot(U32 IPv4_Be) {
	U32 Index, Victim = 0;

	/* Find an empty slot */
	for (Index=0; Index<ARP_CACHE_SIZE; Index++) {
		if (!GlobalArp.Cache[Index].IsValid) { Victim = Index; break; }
	}

	/* Otherwise evict the entry with the smallest TTL */
	if (Index==ARP_CACHE_SIZE) {
		U32 MinTtl = 0xFFFFFFFFU;
		for (Index=0; Index<ARP_CACHE_SIZE; Index++) {
			if (GlobalArp.Cache[Index].TimeToLive < MinTtl) {
				MinTtl = GlobalArp.Cache[Index].TimeToLive;
				Victim = Index;
			}
		}
	}

	GlobalArp.Cache[Victim].IPv4_Be = IPv4_Be;
	GlobalArp.Cache[Victim].IsValid = 0;
	GlobalArp.Cache[Victim].IsProbing = 0;
	GlobalArp.Cache[Victim].TimeToLive = 0;
	return &GlobalArp.Cache[Victim];
}

static void ArpCacheUpdate(U32 IPv4_Be, const U8 MacAddress[6]) {
	LPArpCacheEntry Entry = ArpLookup(IPv4_Be);
	if (!Entry) Entry = ArpAllocateSlot(IPv4_Be);

	MacCopy(Entry->MacAddress, MacAddress);
	Entry->IsValid = 1;
	Entry->IsProbing = 0;
	Entry->TimeToLive = ARP_ENTRY_TTL_TICKS;
}

/************************************************************************/
// Transmission

static int ArpSendFrame(const U8* Data, U32 Length) {
	NETWORKSEND Send;
	Send.Device = GlobalArp.NetworkDevice;
	Send.Data = Data;
	Send.Length = Length;
	return (GlobalArp.NetworkCommand(DF_NT_SEND, (U32)(LPVOID)&Send) == DF_ERROR_SUCCESS) ? 1 : 0;
}

/************************************************************************/

static int ArpSendRequest(U32 TargetIPv4_Be) {
	U8 Buffer[sizeof(EthernetHeader) + sizeof(ArpPacket)];
	LPEthernetHeader Ethernet = (LPEthernetHeader)Buffer;
	LPArpPacket Packet = (LPArpPacket)(Buffer + sizeof(EthernetHeader));

	/* Ethernet header */
	U8 BroadcastMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
	MacCopy(Ethernet->Destination, BroadcastMac);
	MacCopy(Ethernet->Source, GlobalArp.LocalMacAddress);
	Ethernet->EtherType = Htons(ETHERTYPE_ARP);

	/* ARP payload */
	Packet->HardwareType = Htons(ARP_HTYPE_ETH);
	Packet->ProtocolType = Htons(ARP_PTYPE_IPV4);
	Packet->HardwareLength = ARP_HLEN_ETH;
	Packet->ProtocolLength = ARP_PLEN_IPV4;
	Packet->Operation = Htons(ARP_OP_REQUEST);

	MacCopy(Packet->SenderHardwareAddress, GlobalArp.LocalMacAddress);
	Packet->SenderProtocolAddress = GlobalArp.LocalIPv4_Be;

	MemorySet(Packet->TargetHardwareAddress, 0, 6);
	Packet->TargetProtocolAddress = TargetIPv4_Be;

	return ArpSendFrame(Buffer, (U32)sizeof(Buffer));
}

/************************************************************************/

static int ArpSendReply(const U8 DestinationMac[6], U32 DestinationIPv4_Be) {
	U8 Buffer[sizeof(EthernetHeader) + sizeof(ArpPacket)];
	LPEthernetHeader Ethernet = (LPEthernetHeader)Buffer;
	LPArpPacket Packet = (LPArpPacket)(Buffer + sizeof(EthernetHeader));

	MacCopy(Ethernet->Destination, DestinationMac);
	MacCopy(Ethernet->Source, GlobalArp.LocalMacAddress);
	Ethernet->EtherType = Htons(ETHERTYPE_ARP);

	Packet->HardwareType = Htons(ARP_HTYPE_ETH);
	Packet->ProtocolType = Htons(ARP_PTYPE_IPV4);
	Packet->HardwareLength = ARP_HLEN_ETH;
	Packet->ProtocolLength = ARP_PLEN_IPV4;
	Packet->Operation = Htons(ARP_OP_REPLY);

	/* Sender = us, Target = original requester */
	MacCopy(Packet->SenderHardwareAddress, GlobalArp.LocalMacAddress);
	Packet->SenderProtocolAddress = GlobalArp.LocalIPv4_Be;

	MacCopy(Packet->TargetHardwareAddress, DestinationMac);
	Packet->TargetProtocolAddress = DestinationIPv4_Be;

	return ArpSendFrame(Buffer, (U32)sizeof(Buffer));
}

/************************************************************************/
// Receive path

static void ArpHandlePacket(const ArpPacket* Packet) {
	U16 HardwareType = Ntohs(Packet->HardwareType);
	U16 ProtocolType = Ntohs(Packet->ProtocolType);
	U16 Operation = Ntohs(Packet->Operation);

	if (HardwareType != ARP_HTYPE_ETH) return;
	if (ProtocolType != ARP_PTYPE_IPV4) return;
	if (Packet->HardwareLength != 6 || Packet->ProtocolLength != 4) return;

	/* Update cache from sender */
	ArpCacheUpdate(Packet->SenderProtocolAddress, Packet->SenderHardwareAddress);

	/* If request targets our IP, send a reply */
	if (Operation == ARP_OP_REQUEST && Packet->TargetProtocolAddress == GlobalArp.LocalIPv4_Be) {
		ArpSendReply(Packet->SenderHardwareAddress, Packet->SenderProtocolAddress);
	}
	/* For replies: cache already updated */
}

/************************************************************************/

void ARP_OnEthernetFrame(const U8* Frame, U32 Length) {
	if (Length < sizeof(EthernetHeader)) return;

	const LPEthernetHeader Ethernet = (const LPEthernetHeader)Frame;
	U16 EtherType = Ntohs(Ethernet->EtherType);
	if (EtherType != ETHERTYPE_ARP) return;

	if (Length < (U32)(sizeof(EthernetHeader) + sizeof(ArpPacket))) return;

	const LPArpPacket Packet = (const LPArpPacket)(Frame + sizeof(EthernetHeader));
	ArpHandlePacket(Packet);
}

/************************************************************************/
// Public API

void ARP_Initialize(LPVOID NetworkDevice, DRVFUNC NetworkCommand, U32 LocalIPv4_Be) {
	U32 Index;

	GlobalArp.NetworkDevice = NetworkDevice;
	GlobalArp.NetworkCommand = NetworkCommand;
	GlobalArp.LocalIPv4_Be = LocalIPv4_Be;

	for (Index=0; Index<ARP_CACHE_SIZE; Index++) {
		GlobalArp.Cache[Index].IsValid = 0;
		GlobalArp.Cache[Index].IsProbing = 0;
		GlobalArp.Cache[Index].TimeToLive = 0;
		GlobalArp.Cache[Index].IPv4_Be = 0;
	}

	/* Query local MAC through DF_NT_GETINFO */
	NETWORKGETINFO GetInfo;
	NETWORKINFO Info;
	MemorySet(&GetInfo, 0, sizeof(GetInfo));
	MemorySet(&Info, 0, sizeof(Info));
	GetInfo.Device = GlobalArp.NetworkDevice;
	GetInfo.Info = &Info;

	if (GlobalArp.NetworkCommand(DF_NT_GETINFO, (U32)(LPVOID)&GetInfo) == DF_ERROR_SUCCESS) {
		GlobalArp.LocalMacAddress[0] = Info.MAC[0];
		GlobalArp.LocalMacAddress[1] = Info.MAC[1];
		GlobalArp.LocalMacAddress[2] = Info.MAC[2];
		GlobalArp.LocalMacAddress[3] = Info.MAC[3];
		GlobalArp.LocalMacAddress[4] = Info.MAC[4];
		GlobalArp.LocalMacAddress[5] = Info.MAC[5];
	}

	/* Register RX callback */
	NETWORKSETRXCB SetRxCallback;
	SetRxCallback.Device = GlobalArp.NetworkDevice;
	SetRxCallback.Callback = ARP_OnEthernetFrame;
	GlobalArp.NetworkCommand(DF_NT_SETRXCB, (U32)(LPVOID)&SetRxCallback);
}

/************************************************************************/

void ARP_Tick(void) {
	U32 Index;
	for (Index=0; Index<ARP_CACHE_SIZE; Index++) {
		LPArpCacheEntry Entry = &GlobalArp.Cache[Index];
		if (!Entry->IsValid) continue;
		if (Entry->TimeToLive) Entry->TimeToLive--;
		if (Entry->TimeToLive == 0) {
			Entry->IsValid = 0;
			Entry->IsProbing = 0;
		}
	}
}

/************************************************************************/

int ARP_Resolve(U32 TargetIPv4_Be, U8 OutMacAddress[6]) {
	LPArpCacheEntry Entry = ArpLookup(TargetIPv4_Be);
	if (Entry && Entry->IsValid) {
		MacCopy(OutMacAddress, Entry->MacAddress);
		return 1;
	}

	if (!Entry) Entry = ArpAllocateSlot(TargetIPv4_Be);
	if (!Entry->IsProbing) {
		ArpSendRequest(TargetIPv4_Be);
		Entry->IsProbing = 1;
		Entry->TimeToLive = ARP_PROBE_INTERVAL_TICKS;
	}
	return 0;
}

/************************************************************************/

void ARP_DumpCache(void) {
	U32 Index;
	for (Index=0; Index<ARP_CACHE_SIZE; Index++) {
		LPArpCacheEntry Entry = &GlobalArp.Cache[Index];
		if (!Entry->IsValid) continue;

		U32 HostOrder = Ntohl(Entry->IPv4_Be);
		KernelLogText(LOG_DEBUG, TEXT("[ARP] %u.%u.%u.%u -> %02X:%02X:%02X:%02X:%02X:%02X ttl=%u"),
			(U32)((HostOrder >> 24) & 0xFF),
			(U32)((HostOrder >> 16) & 0xFF),
			(U32)((HostOrder >>  8) & 0xFF),
			(U32)((HostOrder >>  0) & 0xFF),
			Entry->MacAddress[0], Entry->MacAddress[1], Entry->MacAddress[2],
			Entry->MacAddress[3], Entry->MacAddress[4], Entry->MacAddress[5],
			Entry->TimeToLive);
	}
}
