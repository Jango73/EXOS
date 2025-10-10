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


    Generic Circular Buffer Implementation

\************************************************************************/

#ifndef CIRCULARBUFFER_H_INCLUDED
#define CIRCULARBUFFER_H_INCLUDED

#include "Base.h"

/************************************************************************/

typedef struct tag_CIRCULAR_BUFFER {
    U8* Data;
    U8* InitialData;
    U8* AllocatedData;
    UINT Size;
    UINT InitialSize;
    UINT MaximumSize;
    UINT WriteOffset;
    UINT ReadOffset;
    UINT DataLength;
    BOOL Overflowed;
} CIRCULAR_BUFFER, *LPCIRCULAR_BUFFER;

/************************************************************************/

/**
 * @brief Initialize a circular buffer
 * @param Buffer The buffer structure to initialize
 * @param Data Pointer to the data array
 * @param Size Size of the data array
 */
void CircularBuffer_Initialize(LPCIRCULAR_BUFFER Buffer, U8* Data, U32 Size, U32 MaximumSize);

/**
 * @brief Write data to the circular buffer
 * @param Buffer The buffer to write to
 * @param Data Pointer to data to write
 * @param Length Number of bytes to write
 * @return Number of bytes actually written
 */
U32 CircularBuffer_Write(LPCIRCULAR_BUFFER Buffer, const U8* Data, U32 Length);

/**
 * @brief Read data from the circular buffer
 * @param Buffer The buffer to read from
 * @param Data Pointer to destination buffer
 * @param Length Number of bytes to read
 * @return Number of bytes actually read
 */
U32 CircularBuffer_Read(LPCIRCULAR_BUFFER Buffer, U8* Data, U32 Length);

/**
 * @brief Get the number of bytes available for reading
 * @param Buffer The buffer to check
 * @return Number of bytes available
 */
U32 CircularBuffer_GetAvailableData(LPCIRCULAR_BUFFER Buffer);

/**
 * @brief Get the number of bytes available for writing
 * @param Buffer The buffer to check
 * @return Number of bytes available for writing
 */
U32 CircularBuffer_GetAvailableSpace(LPCIRCULAR_BUFFER Buffer);

/**
 * @brief Reset the buffer to empty state
 * @param Buffer The buffer to reset
 */
void CircularBuffer_Reset(LPCIRCULAR_BUFFER Buffer);

/************************************************************************/

#endif // CIRCULARBUFFER_H_INCLUDED
