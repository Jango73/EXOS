
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


    USB Mass Storage

\************************************************************************/

#ifndef USBMASSSTORAGE_H_INCLUDED
#define USBMASSSTORAGE_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "Driver.h"
#include "List.h"

/************************************************************************/

#pragma pack(push, 1)

/************************************************************************/

typedef struct tag_USB_MASS_STORAGE_DEVICE USB_MASS_STORAGE_DEVICE, *LPUSB_MASS_STORAGE_DEVICE;

typedef struct tag_USB_STORAGE_ENTRY {
    LISTNODE_FIELDS
    LPUSB_MASS_STORAGE_DEVICE Device;
    U8 Address;
    U16 VendorId;
    U16 ProductId;
    UINT BlockCount;
    UINT BlockSize;
    BOOL Present;
} USB_STORAGE_ENTRY, *LPUSB_STORAGE_ENTRY;

/************************************************************************/

LPDRIVER USBMassStorageGetDriver(void);
LPCSTR UsbEnumErrorToString(U8 Code);

/************************************************************************/

#pragma pack(pop)

#endif  // USBMASSSTORAGE_H_INCLUDED
