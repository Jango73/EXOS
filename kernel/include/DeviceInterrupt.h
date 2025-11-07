/************************************************************************\

    EXOS Kernel

    Generic device interrupt entry points

\************************************************************************/

#ifndef DEVICE_INTERRUPT_H_INCLUDED
#define DEVICE_INTERRUPT_H_INCLUDED

/***************************************************************************/

#include "Base.h"
#include "Device.h"
#include "User.h"

/***************************************************************************/

#define DEVICE_INTERRUPT_VECTOR_BASE 48
#define DEVICE_INTERRUPT_VECTOR_COUNT 8
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

static inline U8 GetDeviceInterruptVector(U8 Slot) {
    if (Slot >= DEVICE_INTERRUPT_VECTOR_COUNT) {
        Slot = DEVICE_INTERRUPT_VECTOR_COUNT - 1U;
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
