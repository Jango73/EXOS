
/************************************************************************\

    EXOS UEFI Bootloader
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


    UEFI UDP logger

\************************************************************************/

#include "uefi/uefi-log-udp.h"
#include "CoreString.h"

/************************************************************************/

#ifndef UEFI_LOG_UDP_DEST_IP_0
    #define UEFI_LOG_UDP_DEST_IP_0 192
#endif
#ifndef UEFI_LOG_UDP_DEST_IP_1
    #define UEFI_LOG_UDP_DEST_IP_1 168
#endif
#ifndef UEFI_LOG_UDP_DEST_IP_2
    #define UEFI_LOG_UDP_DEST_IP_2 50
#endif
#ifndef UEFI_LOG_UDP_DEST_IP_3
    #define UEFI_LOG_UDP_DEST_IP_3 1
#endif

#ifndef UEFI_LOG_UDP_SOURCE_IP_0
    #define UEFI_LOG_UDP_SOURCE_IP_0 192
#endif
#ifndef UEFI_LOG_UDP_SOURCE_IP_1
    #define UEFI_LOG_UDP_SOURCE_IP_1 168
#endif
#ifndef UEFI_LOG_UDP_SOURCE_IP_2
    #define UEFI_LOG_UDP_SOURCE_IP_2 50
#endif
#ifndef UEFI_LOG_UDP_SOURCE_IP_3
    #define UEFI_LOG_UDP_SOURCE_IP_3 2
#endif

#ifndef UEFI_LOG_UDP_DEST_PORT
    #define UEFI_LOG_UDP_DEST_PORT 18194
#endif
#ifndef UEFI_LOG_UDP_SOURCE_PORT
    #define UEFI_LOG_UDP_SOURCE_PORT 18195
#endif

/************************************************************************/

#define EFI_SIMPLE_NETWORK_STOPPED_STATE 0
#define EFI_SIMPLE_NETWORK_STARTED_STATE 1
#define EFI_SIMPLE_NETWORK_INITIALIZED_STATE 2

#define UEFI_LOG_UDP_MAX_PAYLOAD 512
#define UEFI_LOG_UDP_MAX_FRAME 1536

/************************************************************************/

typedef struct UEFI_UDP_LOG_CONTEXT {
    EFI_SIMPLE_NETWORK_PROTOCOL* SimpleNetwork;
    BOOL IsEnabled;
    BOOL BootServicesExited;
    U32 InitFlags;
    U16 Sequence;
} UEFI_UDP_LOG_CONTEXT;

/************************************************************************/

static UEFI_UDP_LOG_CONTEXT UefiUdpLogContext = {
    .SimpleNetwork = NULL,
    .IsEnabled = FALSE,
    .BootServicesExited = FALSE,
    .InitFlags = 0u,
    .Sequence = 1
};

/************************************************************************/

static U16 BootUefiUdpSwap16(U16 Value);
static U16 BootUefiUdpInternetChecksum(const U8* Buffer, UINT Length);
static UINT BootUefiUdpStringLength(LPCSTR Text);
static UINT BootUefiUdpMin(UINT Left, UINT Right);
static UINT BootUefiUdpComposeFrame(U8* Frame, UINT FrameCapacity, LPCSTR Payload, UINT PayloadLength);
static void BootUefiUdpSendTextChunk(LPCSTR Chunk, UINT ChunkLength);

/************************************************************************/

/**
 * @brief Swap bytes of a 16-bit value.
 *
 * @param Value 16-bit host-order value.
 * @return Big-endian representation stored as U16.
 */
static U16 BootUefiUdpSwap16(U16 Value) {
    return (U16)((Value << 8) | (Value >> 8));
}

/************************************************************************/

/**
 * @brief Compute standard internet checksum.
 *
 * @param Buffer Pointer to data buffer.
 * @param Length Buffer length in bytes.
 * @return 16-bit checksum.
 */
static U16 BootUefiUdpInternetChecksum(const U8* Buffer, UINT Length) {
    U32 Sum = 0;
    UINT Index = 0;

    while (Index + 1u < Length) {
        U16 Word = (U16)(((U16)Buffer[Index] << 8) | (U16)Buffer[Index + 1u]);
        Sum += Word;
        if (Sum > 0xFFFFu) {
            Sum = (Sum & 0xFFFFu) + (Sum >> 16);
        }
        Index += 2u;
    }

    if (Index < Length) {
        U16 Word = (U16)((U16)Buffer[Index] << 8);
        Sum += Word;
        if (Sum > 0xFFFFu) {
            Sum = (Sum & 0xFFFFu) + (Sum >> 16);
        }
    }

    while (Sum > 0xFFFFu) {
        Sum = (Sum & 0xFFFFu) + (Sum >> 16);
    }

    return (U16)(~Sum);
}

/************************************************************************/

/**
 * @brief Return string length without stdlib dependencies.
 *
 * @param Text Null-terminated string.
 * @return String length in bytes.
 */
static UINT BootUefiUdpStringLength(LPCSTR Text) {
    UINT Length = 0u;
    if (Text == NULL) {
        return 0u;
    }

    while (Text[Length] != 0u) {
        Length++;
    }

    return Length;
}

/************************************************************************/

/**
 * @brief Return the minimum of two unsigned values.
 *
 * @param Left First value.
 * @param Right Second value.
 * @return Minimum value.
 */
static UINT BootUefiUdpMin(UINT Left, UINT Right) {
    if (Left < Right) {
        return Left;
    }
    return Right;
}

/************************************************************************/

/**
 * @brief Compose an Ethernet+IPv4+UDP packet carrying ASCII payload.
 *
 * @param Frame Output frame buffer.
 * @param FrameCapacity Frame buffer capacity in bytes.
 * @param Payload Payload pointer.
 * @param PayloadLength Payload length in bytes.
 * @return Frame length in bytes, or zero on failure.
 */
static UINT BootUefiUdpComposeFrame(U8* Frame, UINT FrameCapacity, LPCSTR Payload, UINT PayloadLength) {
    if (Frame == NULL || Payload == NULL || PayloadLength == 0u) {
        return 0u;
    }

    const UINT EthernetHeaderSize = 14u;
    const UINT IpHeaderSize = 20u;
    const UINT UdpHeaderSize = 8u;
    const UINT HeadersLength = EthernetHeaderSize + IpHeaderSize + UdpHeaderSize;
    const UINT IpPayloadLength = UdpHeaderSize + PayloadLength;
    const UINT PacketLength = HeadersLength + PayloadLength;
    UINT FrameLength = PacketLength;

    if (PacketLength > FrameCapacity) {
        return 0u;
    }

    if (FrameLength < 60u) {
        FrameLength = 60u;
    }

    MemorySet(Frame, 0, FrameLength);

    // Ethernet header.
    Frame[0] = 0xFFu;
    Frame[1] = 0xFFu;
    Frame[2] = 0xFFu;
    Frame[3] = 0xFFu;
    Frame[4] = 0xFFu;
    Frame[5] = 0xFFu;

    Frame[6] = UefiUdpLogContext.SimpleNetwork->Mode->CurrentAddress.Addr[0];
    Frame[7] = UefiUdpLogContext.SimpleNetwork->Mode->CurrentAddress.Addr[1];
    Frame[8] = UefiUdpLogContext.SimpleNetwork->Mode->CurrentAddress.Addr[2];
    Frame[9] = UefiUdpLogContext.SimpleNetwork->Mode->CurrentAddress.Addr[3];
    Frame[10] = UefiUdpLogContext.SimpleNetwork->Mode->CurrentAddress.Addr[4];
    Frame[11] = UefiUdpLogContext.SimpleNetwork->Mode->CurrentAddress.Addr[5];

    Frame[12] = 0x08u;
    Frame[13] = 0x00u;

    // IPv4 header.
    U8* Ip = Frame + EthernetHeaderSize;
    Ip[0] = 0x45u;
    Ip[1] = 0x00u;
    U16 TotalLength = BootUefiUdpSwap16((U16)(IpHeaderSize + IpPayloadLength));
    MemoryCopy(Ip + 2, &TotalLength, sizeof(TotalLength));
    U16 Identification = BootUefiUdpSwap16(UefiUdpLogContext.Sequence++);
    MemoryCopy(Ip + 4, &Identification, sizeof(Identification));
    Ip[6] = 0x40u;
    Ip[7] = 0x00u;
    Ip[8] = 64u;
    Ip[9] = 17u;  // UDP
    Ip[10] = 0u;
    Ip[11] = 0u;
    Ip[12] = UEFI_LOG_UDP_SOURCE_IP_0;
    Ip[13] = UEFI_LOG_UDP_SOURCE_IP_1;
    Ip[14] = UEFI_LOG_UDP_SOURCE_IP_2;
    Ip[15] = UEFI_LOG_UDP_SOURCE_IP_3;
    Ip[16] = UEFI_LOG_UDP_DEST_IP_0;
    Ip[17] = UEFI_LOG_UDP_DEST_IP_1;
    Ip[18] = UEFI_LOG_UDP_DEST_IP_2;
    Ip[19] = UEFI_LOG_UDP_DEST_IP_3;
    U16 HeaderChecksum = BootUefiUdpInternetChecksum(Ip, IpHeaderSize);
    U16 NetworkChecksum = BootUefiUdpSwap16(HeaderChecksum);
    MemoryCopy(Ip + 10, &NetworkChecksum, sizeof(NetworkChecksum));

    // UDP header.
    U8* Udp = Ip + IpHeaderSize;
    U16 SourcePort = BootUefiUdpSwap16(UEFI_LOG_UDP_SOURCE_PORT);
    U16 DestinationPort = BootUefiUdpSwap16(UEFI_LOG_UDP_DEST_PORT);
    U16 UdpLength = BootUefiUdpSwap16((U16)IpPayloadLength);
    U16 UdpChecksum = 0u;
    MemoryCopy(Udp + 0, &SourcePort, sizeof(SourcePort));
    MemoryCopy(Udp + 2, &DestinationPort, sizeof(DestinationPort));
    MemoryCopy(Udp + 4, &UdpLength, sizeof(UdpLength));
    MemoryCopy(Udp + 6, &UdpChecksum, sizeof(UdpChecksum));

    MemoryCopy(Udp + UdpHeaderSize, Payload, PayloadLength);
    return FrameLength;
}

/************************************************************************/

/**
 * @brief Send one UDP datagram chunk through SNP.
 *
 * @param Chunk Pointer to payload bytes.
 * @param ChunkLength Payload length in bytes.
 */
static void BootUefiUdpSendTextChunk(LPCSTR Chunk, UINT ChunkLength) {
    if (UefiUdpLogContext.IsEnabled == FALSE || UefiUdpLogContext.SimpleNetwork == NULL) {
        return;
    }

    U8 Frame[UEFI_LOG_UDP_MAX_FRAME];
    UINT FrameLength = BootUefiUdpComposeFrame(Frame, sizeof(Frame), Chunk, ChunkLength);
    if (FrameLength == 0u) {
        return;
    }

    UefiUdpLogContext.SimpleNetwork->Transmit(
        UefiUdpLogContext.SimpleNetwork,
        0u,
        FrameLength,
        Frame,
        NULL,
        NULL,
        NULL);
}

/************************************************************************/

/**
 * @brief Initialize SNP transport for UDP logging.
 *
 * @param BootServices UEFI boot services table.
 */
void BootUefiUdpLogInitialize(EFI_BOOT_SERVICES* BootServices) {
    if (BootServices == NULL) {
        return;
    }

    if (UefiUdpLogContext.IsEnabled == TRUE) {
        UefiUdpLogContext.InitFlags |= UEFI_UDP_INIT_FLAG_ENABLED;
        return;
    }

    UefiUdpLogContext.InitFlags = 0u;

    EFI_GUID SimpleNetworkGuid = {
        0xA19832B9u, 0xAC25u, 0x11D3u,
        {0x9A, 0x2D, 0x00, 0x98, 0x27, 0x3F, 0xC1, 0x4D}
    };

    EFI_SIMPLE_NETWORK_PROTOCOL* SimpleNetwork = NULL;
    EFI_STATUS Status = BootServices->LocateProtocol(
        &SimpleNetworkGuid,
        NULL,
        (void**)&SimpleNetwork);
    if (Status != EFI_SUCCESS || SimpleNetwork == NULL || SimpleNetwork->Mode == NULL) {
        return;
    }
    UefiUdpLogContext.InitFlags |= UEFI_UDP_INIT_FLAG_LOCATE_OK;

    if (SimpleNetwork->Mode->State == EFI_SIMPLE_NETWORK_STOPPED_STATE) {
        Status = SimpleNetwork->Start(SimpleNetwork);
        if (Status != EFI_SUCCESS) {
            return;
        }
        UefiUdpLogContext.InitFlags |= UEFI_UDP_INIT_FLAG_START_OK;
    } else {
        UefiUdpLogContext.InitFlags |= UEFI_UDP_INIT_FLAG_START_OK;
    }

    if (SimpleNetwork->Mode->State == EFI_SIMPLE_NETWORK_STARTED_STATE) {
        Status = SimpleNetwork->Initialize(SimpleNetwork, 0u, 0u);
        if (Status != EFI_SUCCESS) {
            return;
        }
        UefiUdpLogContext.InitFlags |= UEFI_UDP_INIT_FLAG_INITIALIZE_OK;
    } else if (SimpleNetwork->Mode->State == EFI_SIMPLE_NETWORK_INITIALIZED_STATE) {
        UefiUdpLogContext.InitFlags |= UEFI_UDP_INIT_FLAG_INITIALIZE_OK;
    }

    if (SimpleNetwork->Mode->State != EFI_SIMPLE_NETWORK_INITIALIZED_STATE) {
        return;
    }

    UefiUdpLogContext.SimpleNetwork = SimpleNetwork;
    UefiUdpLogContext.BootServicesExited = FALSE;
    UefiUdpLogContext.IsEnabled = TRUE;
    UefiUdpLogContext.InitFlags |= UEFI_UDP_INIT_FLAG_ENABLED;
}

/************************************************************************/

/**
 * @brief Disable UDP logging after ExitBootServices.
 */
void BootUefiUdpLogNotifyExitBootServices(void) {
    UefiUdpLogContext.BootServicesExited = TRUE;
    UefiUdpLogContext.IsEnabled = FALSE;
}

/************************************************************************/

/**
 * @brief Write one text line over UDP in best-effort mode.
 *
 * @param Text Null-terminated text.
 */
void BootUefiUdpLogWrite(LPCSTR Text) {
    if (Text == NULL || UefiUdpLogContext.IsEnabled == FALSE || UefiUdpLogContext.BootServicesExited == TRUE) {
        return;
    }

    UINT Length = BootUefiUdpStringLength(Text);
    UINT Offset = 0u;
    while (Offset < Length) {
        UINT ChunkLength = BootUefiUdpMin(UEFI_LOG_UDP_MAX_PAYLOAD, Length - Offset);
        BootUefiUdpSendTextChunk(Text + Offset, ChunkLength);
        Offset += ChunkLength;
    }
}

/************************************************************************/

/**
 * @brief Return UDP logger initialization flags.
 *
 * @return Bit field composed from UEFI_UDP_INIT_FLAG_*.
 */
U32 BootUefiUdpLogGetInitFlags(void) {
    return UefiUdpLogContext.InitFlags;
}
