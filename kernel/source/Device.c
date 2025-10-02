
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

#include "../include/Device.h"
#include "../include/Heap.h"
#include "../include/ID.h"
#include "../include/Memory.h"
#include "../include/String.h"
#include "../include/Text.h"
#include "../include/Kernel.h"

/************************************************************************/

typedef struct tag_DEVICE_CONTEXT {
    LISTNODE_FIELDS
    U32 ContextID;
    LPVOID Context;
} DEVICE_CONTEXT, *LPDEVICE_CONTEXT;

/************************************************************************/

/**
 * @brief Get the default name for a device based on its type
 *
 * @param Name Buffer to receive the device name
 * @param Device Pointer to the device
 * @param DeviceType Type of device (DRIVER_TYPE_*)
 * @return TRUE if successful, FALSE otherwise
 */
BOOL GetDefaultDeviceName(LPSTR Name, LPDEVICE Device, U32 DeviceType) {
    STR Temp[12];
    LPLISTNODE Node;
    LPDEVICE CurrentDevice;
    U32 DeviceIndex = 0;

    if (Name == NULL || Device == NULL) return FALSE;

    // Count devices of the same type to find this device's index
    SAFE_USE(Kernel.PCIDevice) {
        for (Node = Kernel.PCIDevice->First; Node; Node = Node->Next) {
            CurrentDevice = (LPDEVICE)Node;
            SAFE_USE_VALID_ID(CurrentDevice, ID_PCIDEVICE) {
                SAFE_USE_VALID_ID(CurrentDevice->Driver, ID_DRIVER) {
                    if (CurrentDevice->Driver->Type == DeviceType) {
                        if (CurrentDevice == Device) break;
                        DeviceIndex++;
                    }
                }
            }
        }
    }

    // Select prefix based on device type
    switch (DeviceType) {
        case DRIVER_TYPE_NETWORK:
            StringCopy(Name, Text_Eth);
            break;
        default:
            StringCopy(Name, TEXT("dev"));
            break;
    }

    // Append device index
    U32ToString(DeviceIndex, Temp);
    StringConcat(Name, Temp);

    return TRUE;
}

/************************************************************************/

LPVOID GetDeviceContext(LPDEVICE Device, U32 ID) {
    LPDEVICE_CONTEXT DeviceContext;

    if (Device == NULL) return NULL;

    DeviceContext = (LPDEVICE_CONTEXT)Device->Contexts.First;
    while (DeviceContext != NULL) {
        if (DeviceContext->ContextID == ID) {
            return DeviceContext->Context;
        }
        DeviceContext = (LPDEVICE_CONTEXT)DeviceContext->Next;
    }

    return NULL;
}

/************************************************************************/

U32 SetDeviceContext(LPDEVICE Device, U32 ID, LPVOID Context) {
    LPDEVICE_CONTEXT DeviceContext;

    if (Device == NULL) return 0;

    DeviceContext = (LPDEVICE_CONTEXT)Device->Contexts.First;
    while (DeviceContext != NULL) {
        if (DeviceContext->ContextID == ID) {
            DeviceContext->Context = Context;
            return 1;
        }
        DeviceContext = (LPDEVICE_CONTEXT)DeviceContext->Next;
    }

    DeviceContext = (LPDEVICE_CONTEXT)KernelHeapAlloc(sizeof(DEVICE_CONTEXT));
    if (DeviceContext == NULL) return 0;

    DeviceContext->ID = ID_NONE;
    DeviceContext->References = 1;
    DeviceContext->Next = NULL;
    DeviceContext->Prev = NULL;
    DeviceContext->ContextID = ID;
    DeviceContext->Context = Context;

    ListAddTail(&Device->Contexts, DeviceContext);

    return 1;
}

/************************************************************************/

U32 RemoveDeviceContext(LPDEVICE Device, U32 ID) {
    LPDEVICE_CONTEXT DeviceContext;

    if (Device == NULL) return 0;

    DeviceContext = (LPDEVICE_CONTEXT)Device->Contexts.First;
    while (DeviceContext != NULL) {
        if (DeviceContext->ContextID == ID) {
            ListRemove(&Device->Contexts, DeviceContext);
            KernelHeapFree(DeviceContext);
            return 1;
        }
        DeviceContext = (LPDEVICE_CONTEXT)DeviceContext->Next;
    }

    return 0;
}
