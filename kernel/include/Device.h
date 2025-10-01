
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


    Device

\************************************************************************/

#ifndef DEVICE_H_INCLUDED
#define DEVICE_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Driver.h"
#include "List.h"
#include "Mutex.h"

/***************************************************************************/

#define DEVICE_FIELDS       \
    LISTNODE_FIELDS         \
    MUTEX Mutex;            \
    LPDRIVER Driver;        \
    LIST Contexts;          \
    STR Name[MAX_FS_LOGICAL_NAME];

typedef struct tag_DEVICE {
    DEVICE_FIELDS
} DEVICE, *LPDEVICE;

/***************************************************************************/

BOOL GetDefaultDeviceName(LPSTR Name, LPDEVICE Device, U32 DeviceType);
LPVOID GetDeviceContext(LPDEVICE Device, U32 ID);
U32 SetDeviceContext(LPDEVICE Device, U32 ID, LPVOID Context);
U32 RemoveDeviceContext(LPDEVICE Device, U32 ID);

/***************************************************************************/

#endif  // DEVICE_H_INCLUDED
