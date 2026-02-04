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


    Minimal UEFI type definitions for the EXOS bootloader

\************************************************************************/

#ifndef UEFI_EFI_H_INCLUDED
#define UEFI_EFI_H_INCLUDED

/************************************************************************/

#include "Base.h"

/************************************************************************/

#if defined(__EXOS_ARCH_X86_64__)
    #define EFIAPI __attribute__((ms_abi))
#elif defined(__EXOS_ARCH_X86_32__)
    #define EFIAPI __attribute__((stdcall))
#else
    #error "Unsupported architecture for UEFI build"
#endif

/************************************************************************/
// Basic UEFI types

typedef void* EFI_HANDLE;
#if defined(__EXOS_ARCH_X86_64__)
    typedef U64 EFI_UINTN;
#else
    typedef U32 EFI_UINTN;
#endif
typedef EFI_UINTN EFI_STATUS;
typedef EFI_UINTN EFI_TPL;
typedef U16 CHAR16;
typedef U64 EFI_PHYSICAL_ADDRESS;
typedef U64 EFI_VIRTUAL_ADDRESS;
typedef void* EFI_EVENT;

/************************************************************************/

typedef struct PACKED tag_EFI_GUID {
    U32 Data1;
    U16 Data2;
    U16 Data3;
    U8 Data4[8];
} EFI_GUID;

/************************************************************************/

typedef struct PACKED tag_EFI_TABLE_HEADER {
    U64 Signature;
    U32 Revision;
    U32 HeaderSize;
    U32 CRC32;
    U32 Reserved;
} EFI_TABLE_HEADER;

/************************************************************************/
// Simple text output protocol

typedef struct tag_EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef EFI_STATUS(EFIAPI* EFI_TEXT_OUTPUT_STRING)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This,
    CHAR16* String);

struct tag_EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void* Reset;
    EFI_TEXT_OUTPUT_STRING OutputString;
    void* TestString;
    void* QueryMode;
    void* SetMode;
    void* SetAttribute;
    void* ClearScreen;
    void* SetCursorPosition;
    void* EnableCursor;
    void* Mode;
};

/************************************************************************/
// Boot services

typedef struct tag_EFI_MEMORY_DESCRIPTOR {
    U32 Type;
    U32 Pad;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    U64 NumberOfPages;
    U64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef EFI_STATUS(EFIAPI* EFI_ALLOCATE_PAGES)(
    EFI_UINTN Type,
    EFI_UINTN MemoryType,
    EFI_UINTN Pages,
    EFI_PHYSICAL_ADDRESS* Memory);

typedef EFI_STATUS(EFIAPI* EFI_FREE_PAGES)(
    EFI_PHYSICAL_ADDRESS Memory,
    EFI_UINTN Pages);

typedef EFI_STATUS(EFIAPI* EFI_GET_MEMORY_MAP)(
    EFI_UINTN* MemoryMapSize,
    EFI_MEMORY_DESCRIPTOR* MemoryMap,
    EFI_UINTN* MapKey,
    EFI_UINTN* DescriptorSize,
    EFI_UINTN* DescriptorVersion);

typedef EFI_STATUS(EFIAPI* EFI_ALLOCATE_POOL)(
    EFI_UINTN PoolType,
    EFI_UINTN Size,
    void** Buffer);

typedef EFI_STATUS(EFIAPI* EFI_FREE_POOL)(
    void* Buffer);

typedef EFI_STATUS(EFIAPI* EFI_HANDLE_PROTOCOL)(
    EFI_HANDLE Handle,
    EFI_GUID* Protocol,
    void** Interface);

typedef EFI_STATUS(EFIAPI* EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE ImageHandle,
    EFI_UINTN MapKey);

typedef EFI_STATUS(EFIAPI* EFI_LOCATE_PROTOCOL)(
    EFI_GUID* Protocol,
    void* Registration,
    void** Interface);

typedef struct tag_EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;
    void* RaiseTPL;
    void* RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    EFI_FREE_PAGES FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    void* CreateEvent;
    void* SetTimer;
    void* WaitForEvent;
    void* SignalEvent;
    void* CloseEvent;
    void* CheckEvent;
    void* InstallProtocolInterface;
    void* ReinstallProtocolInterface;
    void* UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL HandleProtocol;
    void* Reserved;
    void* RegisterProtocolNotify;
    void* LocateHandle;
    void* LocateDevicePath;
    void* InstallConfigurationTable;
    void* LoadImage;
    void* StartImage;
    void* Exit;
    void* UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void* GetNextMonotonicCount;
    void* Stall;
    void* SetWatchdogTimer;
    void* ConnectController;
    void* DisconnectController;
    void* OpenProtocol;
    void* CloseProtocol;
    void* OpenProtocolInformation;
    void* ProtocolsPerHandle;
    void* LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    void* InstallMultipleProtocolInterfaces;
    void* UninstallMultipleProtocolInterfaces;
    void* CalculateCRC32;
    void* CopyMem;
    void* SetMem;
    void* CreateEventEx;
} EFI_BOOT_SERVICES;

/************************************************************************/
// System table

typedef struct tag_EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    CHAR16* FirmwareVendor;
    U32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void* ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* StdErr;
    void* RuntimeServices;
    EFI_BOOT_SERVICES* BootServices;
    EFI_UINTN NumberOfTableEntries;
    void* ConfigurationTable;
} EFI_SYSTEM_TABLE;

/************************************************************************/
// Configuration table

typedef struct tag_EFI_CONFIGURATION_TABLE {
    EFI_GUID VendorGuid;
    void* VendorTable;
} EFI_CONFIGURATION_TABLE;

/************************************************************************/
// Simple file system protocol

typedef struct tag_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct tag_EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef EFI_STATUS(EFIAPI* EFI_OPEN_VOLUME)(
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* This,
    EFI_FILE_PROTOCOL** Root);

struct tag_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    U64 Revision;
    EFI_OPEN_VOLUME OpenVolume;
};

/************************************************************************/
// File protocol

typedef EFI_STATUS(EFIAPI* EFI_FILE_OPEN)(
    EFI_FILE_PROTOCOL* This,
    EFI_FILE_PROTOCOL** NewHandle,
    CHAR16* FileName,
    U64 OpenMode,
    U64 Attributes);

typedef EFI_STATUS(EFIAPI* EFI_FILE_CLOSE)(
    EFI_FILE_PROTOCOL* This);

typedef EFI_STATUS(EFIAPI* EFI_FILE_READ)(
    EFI_FILE_PROTOCOL* This,
    EFI_UINTN* BufferSize,
    void* Buffer);

typedef EFI_STATUS(EFIAPI* EFI_FILE_GET_INFO)(
    EFI_FILE_PROTOCOL* This,
    EFI_GUID* InformationType,
    EFI_UINTN* BufferSize,
    void* Buffer);

struct tag_EFI_FILE_PROTOCOL {
    U64 Revision;
    EFI_FILE_OPEN Open;
    EFI_FILE_CLOSE Close;
    void* Delete;
    EFI_FILE_READ Read;
    void* Write;
    void* GetPosition;
    void* SetPosition;
    EFI_FILE_GET_INFO GetInfo;
    void* SetInfo;
    void* Flush;
    void* OpenEx;
    void* ReadEx;
    void* WriteEx;
    void* FlushEx;
};

/************************************************************************/
// Loaded image protocol

typedef struct tag_EFI_LOADED_IMAGE_PROTOCOL {
    U32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE* SystemTable;
    EFI_HANDLE DeviceHandle;
    void* FilePath;
    void* Reserved;
    U32 LoadOptionsSize;
    void* LoadOptions;
    void* ImageBase;
    U64 ImageSize;
    U32 ImageCodeType;
    U32 ImageDataType;
    EFI_STATUS(EFIAPI* Unload)(EFI_HANDLE ImageHandle);
} EFI_LOADED_IMAGE_PROTOCOL;

/************************************************************************/
// EFI time

typedef struct PACKED tag_EFI_TIME {
    U16 Year;
    U8 Month;
    U8 Day;
    U8 Hour;
    U8 Minute;
    U8 Second;
    U8 Pad1;
    U32 Nanosecond;
    I16 TimeZone;
    U8 Daylight;
    U8 Pad2;
} EFI_TIME;

/************************************************************************/
// File information

typedef struct tag_EFI_FILE_INFO {
    U64 Size;
    U64 FileSize;
    U64 PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    U64 Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

/************************************************************************/
// Graphics Output Protocol (GOP)

typedef struct tag_EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef struct tag_EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;
typedef struct tag_EFI_GRAPHICS_OUTPUT_MODE_INFORMATION EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct tag_EFI_PIXEL_BITMASK {
    U32 RedMask;
    U32 GreenMask;
    U32 BlueMask;
    U32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor = 0,
    PixelBlueGreenRedReserved8BitPerColor = 1,
    PixelBitMask = 2,
    PixelBltOnly = 3,
    PixelFormatMax = 4
} EFI_GRAPHICS_PIXEL_FORMAT;

struct tag_EFI_GRAPHICS_OUTPUT_MODE_INFORMATION {
    U32 Version;
    U32 HorizontalResolution;
    U32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    U32 PixelsPerScanLine;
};

struct tag_EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE {
    U32 MaxMode;
    U32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    EFI_UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    EFI_UINTN FrameBufferSize;
};

typedef EFI_STATUS(EFIAPI* EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL* This,
    U32 ModeNumber,
    EFI_UINTN* SizeOfInfo,
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION** Info);

typedef EFI_STATUS(EFIAPI* EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL* This,
    U32 ModeNumber);

typedef EFI_STATUS(EFIAPI* EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL* This,
    void* BltBuffer,
    EFI_UINTN BltOperation,
    EFI_UINTN SourceX,
    EFI_UINTN SourceY,
    EFI_UINTN DestinationX,
    EFI_UINTN DestinationY,
    EFI_UINTN Width,
    EFI_UINTN Height,
    EFI_UINTN Delta);

struct tag_EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE SetMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
};

/************************************************************************/
// Simple Network Protocol (SNP)

#define EFI_SIMPLE_NETWORK_MAX_MCAST_FILTER_CNT 16

typedef struct tag_EFI_SIMPLE_NETWORK_PROTOCOL EFI_SIMPLE_NETWORK_PROTOCOL;
typedef struct tag_EFI_SIMPLE_NETWORK_MODE EFI_SIMPLE_NETWORK_MODE;

typedef struct tag_EFI_MAC_ADDRESS {
    U8 Addr[32];
} EFI_MAC_ADDRESS;

typedef struct tag_EFI_IPv4_ADDRESS {
    U8 Addr[4];
} EFI_IPv4_ADDRESS;

typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_START)(
    EFI_SIMPLE_NETWORK_PROTOCOL* This);

typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_STOP)(
    EFI_SIMPLE_NETWORK_PROTOCOL* This);

typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_INITIALIZE)(
    EFI_SIMPLE_NETWORK_PROTOCOL* This,
    EFI_UINTN ExtraRxBufferSize,
    EFI_UINTN ExtraTxBufferSize);

typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_SHUTDOWN)(
    EFI_SIMPLE_NETWORK_PROTOCOL* This);

typedef EFI_STATUS(EFIAPI* EFI_SIMPLE_NETWORK_TRANSMIT)(
    EFI_SIMPLE_NETWORK_PROTOCOL* This,
    EFI_UINTN HeaderSize,
    EFI_UINTN BufferSize,
    void* Buffer,
    EFI_MAC_ADDRESS* SrcAddr,
    EFI_MAC_ADDRESS* DestAddr,
    U16* Protocol);

struct tag_EFI_SIMPLE_NETWORK_MODE {
    U32 State;
    U32 HwAddressSize;
    U32 MediaHeaderSize;
    U32 MaxPacketSize;
    U32 NvRamSize;
    U32 NvRamAccessSize;
    U32 ReceiveFilterMask;
    U32 ReceiveFilterSetting;
    U32 MaxMCastFilterCount;
    U32 MCastFilterCount;
    EFI_MAC_ADDRESS MCastFilter[EFI_SIMPLE_NETWORK_MAX_MCAST_FILTER_CNT];
    EFI_MAC_ADDRESS CurrentAddress;
    EFI_MAC_ADDRESS BroadcastAddress;
    EFI_MAC_ADDRESS PermanentAddress;
    U8 IfType;
    BOOL MacAddressChangeable;
    BOOL MultipleTxSupported;
    BOOL MediaPresentSupported;
    BOOL MediaPresent;
};

struct tag_EFI_SIMPLE_NETWORK_PROTOCOL {
    U64 Revision;
    EFI_SIMPLE_NETWORK_START Start;
    EFI_SIMPLE_NETWORK_STOP Stop;
    EFI_SIMPLE_NETWORK_INITIALIZE Initialize;
    void* Reset;
    EFI_SIMPLE_NETWORK_SHUTDOWN Shutdown;
    void* ReceiveFilters;
    void* StationAddress;
    void* Statistics;
    void* MCastIpToMac;
    void* NvData;
    void* GetStatus;
    EFI_SIMPLE_NETWORK_TRANSMIT Transmit;
    void* Receive;
    EFI_EVENT WaitForPacket;
    EFI_SIMPLE_NETWORK_MODE* Mode;
};

/************************************************************************/
// Status codes

#if defined(__EXOS_ARCH_X86_64__)
    #define EFI_STATUS_ERROR_MASK 0x8000000000000000ull
#else
    #define EFI_STATUS_ERROR_MASK 0x80000000u
#endif

#define EFIERR(a) (EFI_STATUS_ERROR_MASK | (EFI_STATUS)(a))

#define EFI_SUCCESS 0
#define EFI_INVALID_PARAMETER EFIERR(2)
#define EFI_BUFFER_TOO_SMALL EFIERR(5)

/************************************************************************/
// Allocate types

#define EFI_ALLOCATE_ANY_PAGES 0
#define EFI_ALLOCATE_MAX_ADDRESS 1
#define EFI_ALLOCATE_ADDRESS 2

/************************************************************************/
// Memory types

#define EfiReservedMemoryType 0
#define EfiLoaderCode 1
#define EfiLoaderData 2
#define EfiBootServicesCode 3
#define EfiBootServicesData 4
#define EfiRuntimeServicesCode 5
#define EfiRuntimeServicesData 6
#define EfiConventionalMemory 7
#define EfiUnusableMemory 8
#define EfiACPIReclaimMemory 9
#define EfiACPIMemoryNVS 10
#define EfiMemoryMappedIO 11
#define EfiMemoryMappedIOPortSpace 12
#define EfiPalCode 13
#define EfiPersistentMemory 14

/************************************************************************/
// File open modes

#define EFI_FILE_MODE_READ 0x0000000000000001ULL

/************************************************************************/

#define EFI_PAGE_SIZE 4096u

#endif  // UEFI_EFI_H_INCLUDED
