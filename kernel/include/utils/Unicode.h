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

#ifndef UNICODE_H_INCLUDED
#define UNICODE_H_INCLUDED

/***************************************************************************/

#include "Base.h"

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
BOOL Utf16LeNextCodePoint(LPCUSTR Input, UINT InputUnits, UINT* Index, U32* CodePoint);

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
BOOL Utf16LeToUtf8(LPCUSTR Input, UINT InputUnits, LPSTR Output, UINT OutputSize, UINT* OutputLength);

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
BOOL Utf16LeCompareCaseInsensitiveAscii(LPCUSTR Left, UINT LeftUnits, LPCUSTR Right, UINT RightUnits);

/***************************************************************************/

#endif  // UNICODE_H_INCLUDED
