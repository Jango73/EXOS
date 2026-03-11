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


    Busy-wait helper

\************************************************************************/

#include "utils/BusyWait.h"

/************************************************************************/

#define BUSY_WAIT_DEFAULT_LOOPS_PER_MILLISECOND 100000
#define BUSY_WAIT_ESTIMATED_CYCLES_PER_LOOP 3

/************************************************************************/

static UINT BusyWaitLoopsPerMillisecond = BUSY_WAIT_DEFAULT_LOOPS_PER_MILLISECOND;

/************************************************************************/

static void BusyWaitLoopAddEax(UINT Loops) {
#if defined(__EXOS_ARCH_X86_32__) || defined(__EXOS_ARCH_X86_64__)
    UINT LoopCounter = Loops;
    U32 Accumulator = 0;

    if (LoopCounter == 0) {
        return;
    }

    __asm__ __volatile__(
        "1:\n"
        "add $1, %%eax\n"
        "sub $1, %0\n"
        "jnz 1b\n"
        : "+r"(LoopCounter), "+a"(Accumulator)
        :
        : "cc");
#else
    volatile UINT Index;

    for (Index = 0; Index < Loops; Index++) {
    }
#endif
}

/************************************************************************/

/**
 * @brief Set busy-wait loops from a CPU base frequency estimate.
 *
 * @param FrequencyMHz Processor base frequency in MHz.
 */
void BusyWaitSetFrequencyMHz(U32 FrequencyMHz) {
    U64 Loops;

    if (FrequencyMHz == 0) {
        return;
    }

    Loops = U64_DIV_U32(U64_MUL_U32(FrequencyMHz, 1000), BUSY_WAIT_ESTIMATED_CYCLES_PER_LOOP, NULL);

    if (U64_Cmp(Loops, U64_FromU32(0)) == 0) {
        Loops = U64_FromU32(1);
    }

    if (U64_Cmp(Loops, U64_FromUINT(MAX_UINT)) > 0) {
        Loops = U64_FromUINT(MAX_UINT);
    }

    BusyWaitLoopsPerMillisecond = U64_ToUINT(Loops);
}

/************************************************************************/

/**
 * @brief Override busy-wait loops per millisecond.
 *
 * @param LoopsPerMillisecond Number of add-loop iterations per ms.
 */
void BusyWaitSetLoopsPerMillisecond(UINT LoopsPerMillisecond) {
    if (LoopsPerMillisecond == 0) {
        return;
    }

    BusyWaitLoopsPerMillisecond = LoopsPerMillisecond;
}

/************************************************************************/

/**
 * @brief Return the configured busy-wait loop density.
 *
 * @return Number of add-loop iterations per millisecond.
 */
UINT BusyWaitGetLoopsPerMillisecond(void) {
    return BusyWaitLoopsPerMillisecond;
}

/************************************************************************/

/**
 * @brief Busy-wait for a requested number of milliseconds.
 *
 * @param Milliseconds Delay duration in milliseconds.
 */
void BusyWaitMilliseconds(UINT Milliseconds) {
    UINT Index;

    for (Index = 0; Index < Milliseconds; Index++) {
        BusyWaitLoopAddEax(BusyWaitLoopsPerMillisecond);
    }
}
