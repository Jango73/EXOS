
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


    Network Checksum Utilities

\************************************************************************/

#include "utils/NetworkChecksum.h"
#include "Endianness.h"
#include "Log.h"

/************************************************************************/

/**
 * @brief Accumulates data into checksum calculation without finalization.
 *
 * This function allows for incremental checksum calculation by accumulating
 * data into a running sum without performing the final complement operation.
 * Use NetworkChecksum_Finalize to complete the checksum calculation.
 *
 * @param Data Pointer to data to checksum in network byte order
 * @param Length Length of data in bytes
 * @param Accumulator Previous accumulator value (0 for first call)
 * @return Updated accumulator value
 */
U32 NetworkChecksum_Calculate_Accumulate(const U8* Data, U32 Length, U32 Accumulator) {
    U32 Sum = Accumulator;
    U32 i;

    for (i = 0; i < Length / 2; i++) {
        U16 Word = (Data[i * 2] << 8) | Data[i * 2 + 1];
        Sum += Word;
    }

    if (i * 2 < Length) {
        Sum += Data[i * 2] << 8;
    }

    return Sum;
}

/************************************************************************/

/**
 * @brief Finalizes checksum calculation from accumulator.
 *
 * This function completes the checksum calculation by handling carry bits
 * and performing the one's complement operation.
 *
 * @param Accumulator The accumulated sum from NetworkChecksum_Calculate_Accumulate
 * @return 16-bit checksum in network byte order
 */
U16 NetworkChecksum_Finalize(U32 Accumulator) {
    U32 Sum = Accumulator;
    U16 Checksum;

    while (Sum >> 16) {
        Sum = (Sum & 0xFFFF) + (Sum >> 16);
    }

    Checksum = (~Sum) & MAX_U16;

    return Htons(Checksum);
}

/************************************************************************/

/**
 * @brief Calculates the standard Internet checksum.
 *
 * This function implements the standard 16-bit one's complement checksum
 * used by IPv4, TCP, UDP, and other Internet protocols.
 *
 * @param Data Pointer to data to checksum in network byte order
 * @param Length Length of data in bytes
 * @return 16-bit checksum in network byte order
 */
U16 NetworkChecksum_Calculate(const U8* Data, U32 Length) {
    U32 Accumulator = NetworkChecksum_Calculate_Accumulate(Data, Length, 0);
    return NetworkChecksum_Finalize(Accumulator);
}
