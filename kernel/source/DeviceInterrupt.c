/************************************************************************\

    EXOS Kernel

    Generic device interrupt top-half placeholder

\************************************************************************/

#include "DeviceInterrupt.h"

#include "Log.h"

/************************************************************************/

void DeviceInterruptHandler(void) {
    static U32 SpuriousCount = 0;

    if (SpuriousCount < 4U) {
        DEBUG(TEXT("[DeviceInterruptHandler] Interrupt received without ISR plumbing (count=%u)"), SpuriousCount);
    }

    SpuriousCount++;
}
