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

#include "../include/CircularBuffer.h"
#include "../include/Memory.h"
#include "../include/String.h"

/************************************************************************/

/**
 * @brief Initialize a circular buffer
 */
void CircularBuffer_Initialize(LPCIRCULAR_BUFFER Buffer, U8* Data, U32 Size) {
    if (!Buffer || !Data || Size == 0) {
        return;
    }

    Buffer->Data = Data;
    Buffer->Size = Size;
    Buffer->WriteOffset = 0;
    Buffer->ReadOffset = 0;
    Buffer->DataLength = 0;
}

/************************************************************************/

/**
 * @brief Write data to the circular buffer
 */
U32 CircularBuffer_Write(LPCIRCULAR_BUFFER Buffer, const U8* Data, U32 Length) {
    if (!Buffer || !Data || Length == 0) {
        return 0;
    }

    // Calculate available space
    U32 AvailableSpace = Buffer->Size - Buffer->DataLength;
    U32 BytesToWrite = (Length < AvailableSpace) ? Length : AvailableSpace;

    if (BytesToWrite == 0) {
        return 0;
    }

    // Calculate actual write position in circular buffer
    U32 WritePos = Buffer->WriteOffset % Buffer->Size;
    U32 SpaceToEnd = Buffer->Size - WritePos;

    if (BytesToWrite <= SpaceToEnd) {
        // Data fits without wrapping
        MemoryCopy(&Buffer->Data[WritePos], Data, BytesToWrite);
    } else {
        // Need to wrap around
        MemoryCopy(&Buffer->Data[WritePos], Data, SpaceToEnd);
        MemoryCopy(&Buffer->Data[0], Data + SpaceToEnd, BytesToWrite - SpaceToEnd);
    }

    Buffer->WriteOffset += BytesToWrite;
    Buffer->DataLength += BytesToWrite;

    return BytesToWrite;
}

/************************************************************************/

/**
 * @brief Read data from the circular buffer
 */
U32 CircularBuffer_Read(LPCIRCULAR_BUFFER Buffer, U8* Data, U32 Length) {
    if (!Buffer || !Data || Length == 0) {
        return 0;
    }

    // Calculate available data
    U32 BytesToRead = (Length < Buffer->DataLength) ? Length : Buffer->DataLength;

    if (BytesToRead == 0) {
        return 0;
    }

    // Calculate actual read position in circular buffer
    U32 ReadPos = Buffer->ReadOffset % Buffer->Size;
    U32 DataToEnd = Buffer->Size - ReadPos;

    if (BytesToRead <= DataToEnd) {
        // Data fits without wrapping
        MemoryCopy(Data, &Buffer->Data[ReadPos], BytesToRead);
    } else {
        // Need to wrap around
        MemoryCopy(Data, &Buffer->Data[ReadPos], DataToEnd);
        MemoryCopy(Data + DataToEnd, &Buffer->Data[0], BytesToRead - DataToEnd);
    }

    Buffer->ReadOffset += BytesToRead;
    Buffer->DataLength -= BytesToRead;

    // Reset offsets if buffer is empty (optimization)
    if (Buffer->DataLength == 0) {
        Buffer->ReadOffset = 0;
        Buffer->WriteOffset = 0;
    }

    return BytesToRead;
}

/************************************************************************/

/**
 * @brief Get the number of bytes available for reading
 */
U32 CircularBuffer_GetAvailableData(LPCIRCULAR_BUFFER Buffer) {
    if (!Buffer) {
        return 0;
    }
    return Buffer->DataLength;
}

/************************************************************************/

/**
 * @brief Get the number of bytes available for writing
 */
U32 CircularBuffer_GetAvailableSpace(LPCIRCULAR_BUFFER Buffer) {
    if (!Buffer) {
        return 0;
    }
    return Buffer->Size - Buffer->DataLength;
}

/************************************************************************/

/**
 * @brief Reset the buffer to empty state
 */
void CircularBuffer_Reset(LPCIRCULAR_BUFFER Buffer) {
    if (!Buffer) {
        return;
    }

    Buffer->WriteOffset = 0;
    Buffer->ReadOffset = 0;
    Buffer->DataLength = 0;
}