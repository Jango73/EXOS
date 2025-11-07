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
#define DF_DEV_ENABLE_INTERRUPT (DF_FIRSTFUNC + 0xF0)
#define DF_DEV_DISABLE_INTERRUPT (DF_FIRSTFUNC + 0xF1)

/***************************************************************************/

typedef struct tag_DEVICE_INTERRUPT_CONFIG {
    LPDEVICE Device;
    U8 LegacyIRQ;
    U8 TargetCPU;
    U8 VectorSlot;
} DEVICE_INTERRUPT_CONFIG, *LPDEVICE_INTERRUPT_CONFIG;

/***************************************************************************/

static inline U8 GetDeviceInterruptVector(U8 Slot) {
    if (Slot >= DEVICE_INTERRUPT_VECTOR_COUNT) {
        Slot = DEVICE_INTERRUPT_VECTOR_COUNT - 1U;
    }

    return (U8)(DEVICE_INTERRUPT_VECTOR_BASE + Slot);
}

/***************************************************************************/

void DeviceInterruptHandler(void);

/***************************************************************************/

#endif // DEVICE_INTERRUPT_H_INCLUDED
