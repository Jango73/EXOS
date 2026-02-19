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


    Byte size formatting helpers

\************************************************************************/

#include "utils/SizeFormat.h"
#include "CoreString.h"

/***************************************************************************/

#define SIZE_FORMAT_UNIT_COUNT 7

/***************************************************************************/

static const STR* const G_SizeFormatUnits[SIZE_FORMAT_UNIT_COUNT] = {
    TEXT("B"),
    TEXT("KB"),
    TEXT("MB"),
    TEXT("GB"),
    TEXT("TB"),
    TEXT("PB"),
    TEXT("EB")
};

/***************************************************************************/

/**
 * @brief Format a byte count using the largest common unit not exceeding 1024.
 * @param ByteCount Input size in bytes.
 * @param OutResult Output value + unit symbol.
 */
void SizeFormatBytes(U64 ByteCount, LPSIZE_FORMAT_RESULT OutResult) {
    UINT UnitIndex = 0;
    U64 Scaled = ByteCount;

    if (OutResult == NULL) {
        return;
    }

    while (UnitIndex + 1 < SIZE_FORMAT_UNIT_COUNT &&
           U64_Cmp(Scaled, U64_FromUINT(1024)) >= 0) {
        for (UINT ShiftIndex = 0; ShiftIndex < 10; ShiftIndex++) {
            Scaled = U64_ShiftRight1(Scaled);
        }
        UnitIndex++;
    }

    OutResult->Value = U64_ToU32_Clip(Scaled);
    OutResult->Unit = G_SizeFormatUnits[UnitIndex];
}

/***************************************************************************/

/**
 * @brief Format a byte count into a printable string with unit symbol.
 * @param ByteCount Input size in bytes.
 * @param OutText Output string buffer (must be large enough).
 */
void SizeFormatBytesText(U64 ByteCount, LPSTR OutText) {
    SIZE_FORMAT_RESULT Result;

    if (OutText == NULL) {
        return;
    }

    SizeFormatBytes(ByteCount, &Result);
    StringPrintFormat(OutText, TEXT("%u %s"), Result.Value, Result.Unit);
}

