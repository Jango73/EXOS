/************************************************************************\

    EXOS Kernel

    Network interrupt top-half placeholder

\************************************************************************/

#include "network/NetworkInterrupt.h"

#include "Log.h"

/************************************************************************/

void NetworkInterruptHandler(void) {
    static U32 SpuriousCount = 0;

    if (SpuriousCount < 4U) {
        DEBUG(TEXT("[NetworkInterruptHandler] Interrupt received without ISR plumbing (count=%u)"), SpuriousCount);
    }

    SpuriousCount++;
}
