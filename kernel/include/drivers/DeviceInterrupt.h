
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


    Device interrupt entry points

\************************************************************************/

#ifndef DEVICE_INTERRUPT_H_INCLUDED
#define DEVICE_INTERRUPT_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Device.h"
#include "User.h"

/***************************************************************************/

#define DEVICE_INTERRUPT_VECTOR_BASE 48
#define DEVICE_INTERRUPT_VECTOR_MAX 32
#define DEVICE_INTERRUPT_VECTOR_DEFAULT DEVICE_INTERRUPT_VECTOR_MAX
#define DEVICE_INTERRUPT_INVALID_SLOT 0xFFU
#define DF_DEV_ENABLE_INTERRUPT (DF_FIRSTFUNC + 0xF0)
#define DF_DEV_DISABLE_INTERRUPT (DF_FIRSTFUNC + 0xF1)

/***************************************************************************/

typedef BOOL (*DEVICE_INTERRUPT_ISR)(LPDEVICE Device, LPVOID Context);
typedef void (*DEVICE_INTERRUPT_BOTTOM_HALF)(LPDEVICE Device, LPVOID Context);
typedef void (*DEVICE_INTERRUPT_POLL)(LPDEVICE Device, LPVOID Context);

typedef struct tag_DEVICE_INTERRUPT_CONFIG {
    LPDEVICE Device;
    U8 LegacyIRQ;
    U8 TargetCPU;
    U8 VectorSlot;
    BOOL InterruptEnabled;
} DEVICE_INTERRUPT_CONFIG, *LPDEVICE_INTERRUPT_CONFIG;

typedef struct tag_DEVICE_INTERRUPT_REGISTRATION {
    LPDEVICE Device;
    U8 LegacyIRQ;
    U8 TargetCPU;
    DEVICE_INTERRUPT_ISR InterruptHandler;
    DEVICE_INTERRUPT_BOTTOM_HALF DeferredCallback;
    DEVICE_INTERRUPT_POLL PollCallback;
    LPVOID Context;
    LPCSTR Name;
} DEVICE_INTERRUPT_REGISTRATION, *LPDEVICE_INTERRUPT_REGISTRATION;

/***************************************************************************/

U8 DeviceInterruptGetSlotCount(void);

/***************************************************************************/

static inline U8 GetDeviceInterruptVector(U8 Slot) {
    U8 SlotCount = DeviceInterruptGetSlotCount();
    if (SlotCount == 0U) {
        SlotCount = 1U;
    }
    if (Slot >= SlotCount) {
        Slot = (U8)(SlotCount - 1U);
    }

    return (U8)(DEVICE_INTERRUPT_VECTOR_BASE + Slot);
}

/***************************************************************************/

void InitializeDeviceInterrupts(void);
BOOL DeviceInterruptRegister(const DEVICE_INTERRUPT_REGISTRATION *Registration, U8 *AssignedSlot);
BOOL DeviceInterruptUnregister(U8 Slot);
void DeviceInterruptHandler(U8 Slot);
BOOL DeviceInterruptSlotIsEnabled(U8 Slot);

/***************************************************************************/

#endif // DEVICE_INTERRUPT_H_INCLUDED
