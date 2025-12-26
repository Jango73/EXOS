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


    Driver Enumeration

\************************************************************************/

#ifndef DRIVER_ENUM_H_INCLUDED
#define DRIVER_ENUM_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "User.h"

/***************************************************************************/

#pragma pack(push, 1)

/***************************************************************************/
// Defines

#define DRIVER_ENUM_DATA_MAX 64
#define DRIVER_ENUM_MAX_DOMAINS 8

#define ENUM_DOMAIN_PCI_DEVICE 0x00000001
#define ENUM_DOMAIN_AHCI_PORT 0x00000002
#define ENUM_DOMAIN_ATA_DEVICE 0x00000003
#define ENUM_DOMAIN_XHCI_PORT 0x00000004

/***************************************************************************/
// Typedefs

typedef void* DRIVER_ENUM_PROVIDER;

typedef struct tag_DRIVER_ENUM_QUERY {
    ABI_HEADER Header;
    UINT Domain;
    UINT Flags;
    UINT Index;
} DRIVER_ENUM_QUERY, *LPDRIVER_ENUM_QUERY;

typedef struct tag_DRIVER_ENUM_ITEM {
    ABI_HEADER Header;
    UINT Domain;
    UINT Index;
    UINT DataSize;
    U8 Data[DRIVER_ENUM_DATA_MAX];
} DRIVER_ENUM_ITEM, *LPDRIVER_ENUM_ITEM;

typedef struct tag_DRIVER_ENUM_NEXT {
    ABI_HEADER Header;
    LPDRIVER_ENUM_QUERY Query;
    LPDRIVER_ENUM_ITEM Item;
} DRIVER_ENUM_NEXT, *LPDRIVER_ENUM_NEXT;

typedef struct tag_DRIVER_ENUM_PRETTY {
    ABI_HEADER Header;
    const DRIVER_ENUM_QUERY* Query;
    const DRIVER_ENUM_ITEM* Item;
    LPSTR Buffer;
    UINT BufferSize;
} DRIVER_ENUM_PRETTY, *LPDRIVER_ENUM_PRETTY;

typedef struct tag_DRIVER_ENUM_PCI_DEVICE {
    U8 Bus;
    U8 Dev;
    U8 Func;
    U16 VendorID;
    U16 DeviceID;
    U8 BaseClass;
    U8 SubClass;
    U8 ProgIF;
    U8 Revision;
} DRIVER_ENUM_PCI_DEVICE, *LPDRIVER_ENUM_PCI_DEVICE;

typedef struct tag_DRIVER_ENUM_AHCI_PORT {
    UINT PortNumber;
    UINT PortImplemented;
    UINT Ssts;
    UINT Sig;
} DRIVER_ENUM_AHCI_PORT, *LPDRIVER_ENUM_AHCI_PORT;

typedef struct tag_DRIVER_ENUM_ATA_DEVICE {
    UINT IOPort;
    UINT Drive;
    UINT IRQ;
    UINT Cylinders;
    UINT Heads;
    UINT SectorsPerTrack;
} DRIVER_ENUM_ATA_DEVICE, *LPDRIVER_ENUM_ATA_DEVICE;

typedef struct tag_DRIVER_ENUM_XHCI_PORT {
    U8 Bus;
    U8 Dev;
    U8 Func;
    U8 PortNumber;
    UINT PortStatus;
    UINT SpeedId;
    UINT Connected;
    UINT Enabled;
} DRIVER_ENUM_XHCI_PORT, *LPDRIVER_ENUM_XHCI_PORT;

/***************************************************************************/
// External symbols

UINT KernelEnumGetProvider(const DRIVER_ENUM_QUERY* Query, UINT ProviderIndex, DRIVER_ENUM_PROVIDER* ProviderOut);
UINT KernelEnumNext(DRIVER_ENUM_PROVIDER Provider, DRIVER_ENUM_QUERY* Query, DRIVER_ENUM_ITEM* Item);
UINT KernelEnumPretty(DRIVER_ENUM_PROVIDER Provider, const DRIVER_ENUM_QUERY* Query, const DRIVER_ENUM_ITEM* Item,
                      LPSTR Buffer, UINT BufferSize);

/***************************************************************************/

#pragma pack(pop)

/***************************************************************************/

#endif  // DRIVER_ENUM_H_INCLUDED
