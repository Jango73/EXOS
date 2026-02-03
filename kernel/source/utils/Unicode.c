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


    Unicode helpers

\************************************************************************/

#include "utils/Unicode.h"

/***************************************************************************/

#define UNICODE_REPLACEMENT_CODE_POINT 0x3F

/***************************************************************************/

/**
 * @brief Convert one ASCII code point to lowercase.
 *
 * @param CodePoint Input Unicode code point.
 * @return Lower-cased code point for ASCII A..Z, unchanged otherwise.
 */
static U32 UnicodeAsciiToLower(U32 CodePoint) {
    if (CodePoint >= 'A' && CodePoint <= 'Z') {
        return CodePoint + ('a' - 'A');
    }

    return CodePoint;
}

/***************************************************************************/

/**
 * @brief Encode one Unicode code point to UTF-8 bytes.
 *
 * @param CodePoint Unicode code point.
 * @param Output Output buffer.
 * @param OutputSize Output buffer size in bytes.
 * @param Written Output number of bytes written.
 * @return TRUE on success, FALSE on insufficient output space.
 */
static BOOL UnicodeEncodeUtf8(U32 CodePoint, LPSTR Output, UINT OutputSize, UINT* Written) {
    if (Output == NULL || Written == NULL) return FALSE;

    *Written = 0;

    if (CodePoint > 0x10FFFF) {
        CodePoint = UNICODE_REPLACEMENT_CODE_POINT;
    }

    if (CodePoint <= 0x7F) {
        if (OutputSize < 1) return FALSE;
        Output[0] = (STR)CodePoint;
        *Written = 1;
        return TRUE;
    }

    if (CodePoint <= 0x7FF) {
        if (OutputSize < 2) return FALSE;
        Output[0] = (STR)(0xC0 | ((CodePoint >> 6) & 0x1F));
        Output[1] = (STR)(0x80 | (CodePoint & 0x3F));
        *Written = 2;
        return TRUE;
    }

    if (CodePoint <= 0xFFFF) {
        if (OutputSize < 3) return FALSE;
        Output[0] = (STR)(0xE0 | ((CodePoint >> 12) & 0x0F));
        Output[1] = (STR)(0x80 | ((CodePoint >> 6) & 0x3F));
        Output[2] = (STR)(0x80 | (CodePoint & 0x3F));
        *Written = 3;
        return TRUE;
    }

    if (OutputSize < 4) return FALSE;
    Output[0] = (STR)(0xF0 | ((CodePoint >> 18) & 0x07));
    Output[1] = (STR)(0x80 | ((CodePoint >> 12) & 0x3F));
    Output[2] = (STR)(0x80 | ((CodePoint >> 6) & 0x3F));
    Output[3] = (STR)(0x80 | (CodePoint & 0x3F));
    *Written = 4;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Decode one UTF-16LE code point.
 *
 * The decoder consumes one or two UTF-16 code units. Invalid surrogate
 * sequences are replaced by the ASCII question mark code point.
 *
 * @param Input UTF-16LE code unit sequence.
 * @param InputUnits Number of UTF-16 code units in Input.
 * @param Index In/out code unit cursor.
 * @param CodePoint Decoded Unicode code point.
 * @return TRUE when one code point is produced, FALSE when Index is out of range.
 */
BOOL Utf16LeNextCodePoint(LPCUSTR Input, UINT InputUnits, UINT* Index, U32* CodePoint) {
    U16 Unit0;

    if (Input == NULL || Index == NULL || CodePoint == NULL) return FALSE;
    if (*Index >= InputUnits) return FALSE;

    Unit0 = Input[*Index];

    if (Unit0 >= 0xD800 && Unit0 <= 0xDBFF) {
        if ((*Index + 1) < InputUnits) {
            U16 Unit1 = Input[*Index + 1];
            if (Unit1 >= 0xDC00 && Unit1 <= 0xDFFF) {
                U32 High = (U32)(Unit0 - 0xD800);
                U32 Low = (U32)(Unit1 - 0xDC00);
                *CodePoint = 0x10000 + (High << 10) + Low;
                *Index += 2;
                return TRUE;
            }
        }

        *CodePoint = UNICODE_REPLACEMENT_CODE_POINT;
        *Index += 1;
        return TRUE;
    }

    if (Unit0 >= 0xDC00 && Unit0 <= 0xDFFF) {
        *CodePoint = UNICODE_REPLACEMENT_CODE_POINT;
        *Index += 1;
        return TRUE;
    }

    *CodePoint = (U32)Unit0;
    *Index += 1;
    return TRUE;
}

/***************************************************************************/

/**
 * @brief Convert UTF-16LE text to UTF-8.
 *
 * Invalid UTF-16 sequences are replaced by '?'.
 *
 * @param Input UTF-16LE code unit sequence.
 * @param InputUnits Number of UTF-16 code units in Input.
 * @param Output UTF-8 output buffer.
 * @param OutputSize Output buffer size in bytes.
 * @param OutputLength Optional output UTF-8 byte count (without null terminator).
 * @return TRUE on success, FALSE on invalid parameters or insufficient buffer size.
 */
BOOL Utf16LeToUtf8(LPCUSTR Input, UINT InputUnits, LPSTR Output, UINT OutputSize, UINT* OutputLength) {
    UINT InputIndex = 0;
    UINT OutputIndex = 0;

    if (Input == NULL || Output == NULL) return FALSE;
    if (OutputSize == 0) return FALSE;

    while (InputIndex < InputUnits) {
        U32 CodePoint = 0;
        UINT Written = 0;

        if (!Utf16LeNextCodePoint(Input, InputUnits, &InputIndex, &CodePoint)) {
            return FALSE;
        }

        if (!UnicodeEncodeUtf8(CodePoint, Output + OutputIndex, OutputSize - OutputIndex, &Written)) {
            return FALSE;
        }

        OutputIndex += Written;
    }

    if (OutputIndex >= OutputSize) return FALSE;
    Output[OutputIndex] = STR_NULL;

    if (OutputLength != NULL) {
        *OutputLength = OutputIndex;
    }

    return TRUE;
}

/***************************************************************************/

/**
 * @brief Compare two UTF-16LE strings using ASCII case-insensitive rules.
 *
 * Only ASCII A..Z are case-folded. Non-ASCII code points are compared as-is.
 *
 * @param Left Left UTF-16LE string.
 * @param LeftUnits Number of UTF-16 code units in Left.
 * @param Right Right UTF-16LE string.
 * @param RightUnits Number of UTF-16 code units in Right.
 * @return TRUE when equal under ASCII case-insensitive comparison, FALSE otherwise.
 */
BOOL Utf16LeCompareCaseInsensitiveAscii(LPCUSTR Left, UINT LeftUnits, LPCUSTR Right, UINT RightUnits) {
    UINT LeftIndex = 0;
    UINT RightIndex = 0;

    if (Left == NULL || Right == NULL) return FALSE;

    while (LeftIndex < LeftUnits && RightIndex < RightUnits) {
        U32 LeftCodePoint = 0;
        U32 RightCodePoint = 0;

        if (!Utf16LeNextCodePoint(Left, LeftUnits, &LeftIndex, &LeftCodePoint)) return FALSE;
        if (!Utf16LeNextCodePoint(Right, RightUnits, &RightIndex, &RightCodePoint)) return FALSE;

        if (UnicodeAsciiToLower(LeftCodePoint) != UnicodeAsciiToLower(RightCodePoint)) {
            return FALSE;
        }
    }

    return (LeftIndex == LeftUnits && RightIndex == RightUnits);
}

/***************************************************************************/
