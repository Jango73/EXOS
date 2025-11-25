
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


    Network Utilities

\************************************************************************/

#include "network/Network.h"

#include "Log.h"
#include "Memory.h"

/************************************************************************/

/**
 * @brief Send a raw Ethernet frame through a network device.
 *
 * @param Device Target network device.
 * @param Data Pointer to frame buffer.
 * @param Length Frame length in bytes.
 * @return 1 on success, 0 otherwise.
 */
INT Network_SendRawFrame(LPDEVICE Device, const U8 *Data, U32 Length) {
    NETWORKSEND Send;
    INT Result = 0;

    if (Device == NULL) return 0;

    if (Data == NULL || Length == 0) {
        DEBUG(TEXT("[Network_SendRawFrame] Invalid Data or Length: Data=%x Length=%u"), (UINT)(LPVOID)Data,
              Length);
        return 0;
    }

    LockMutex(&(Device->Mutex), INFINITY);

    Send.Device = (LPPCI_DEVICE)Device;
    Send.Data = Data;
    Send.Length = Length;
    SAFE_USE_VALID_ID(Device, KOID_PCIDEVICE) {
        SAFE_USE_VALID_ID(((LPPCI_DEVICE)Device)->Driver, KOID_DRIVER) {
            Result =
                (((LPPCI_DEVICE)Device)->Driver->Command(DF_NT_SEND, (UINT)(LPVOID)&Send) == DF_RET_SUCCESS) ? 1 : 0;
        }
    }

    UnlockMutex(&(Device->Mutex));
    return Result;
}

/************************************************************************/
