
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


    String

\************************************************************************/

#ifndef STRING_H_INCLUDED
#define STRING_H_INCLUDED

/************************************************************************/

#include "Base.h"
#include "VarArg.h"

/************************************************************************/
// Flags for format printing

#define PF_ZEROPAD 1   // pad with zero
#define PF_SIGN 2      // unsigned/signed long
#define PF_PLUS 4      // show plus
#define PF_SPACE 8     // space if plus
#define PF_LEFT 16     // left justified
#define PF_SPECIAL 32  // 0x for hex, 0 for octal
#define PF_LARGE 64    // use 'ABCDEF' instead of 'abcdef'

/************************************************************************/

#define STRING_EMPTY(a) (a == NULL || StringEmpty(a) == TRUE)

/************************************************************************/

BOOL IsAlpha(STR);
BOOL IsNumeric(STR);
BOOL IsAlphaNumeric(STR);
STR CharToLower(STR);
STR CharToUpper(STR);
BOOL StringEmpty(LPCSTR);
U32 StringLength(LPCSTR);
void StringClear(LPSTR Str);                                // Clears Str
void StringCopy(LPSTR Dst, LPCSTR Src);                     // Copies Src to Dst
void StringCopyLimit(LPSTR Dst, LPCSTR Src, U32 MaxLength); // Copies Src to Dst, limiting length to Length
void StringCopyNum(LPSTR Dst, LPCSTR Src, U32 Length);      // Copies Src to Dst using Length
void StringConcat(LPSTR Dst, LPCSTR Src);                   // Concatenates Src to Dst
I32 StringCompare(LPCSTR, LPCSTR);                          // Compares two strings WITH case sensitivity
I32 StringCompareNC(LPCSTR, LPCSTR);                        // Compares with strings NO case sensitivity
LPSTR StringToLower(LPSTR);
LPSTR StringToUpper(LPSTR);
LPSTR StringFindChar(LPCSTR, STR);
LPSTR StringFindCharR(LPCSTR, STR);
void StringInvert(LPSTR);
void U32ToString(U32, LPSTR);
void U32ToHexString(U32, LPSTR);
U32 HexStringToU32(LPCSTR);
I32 StringToI32(LPCSTR);
U32 StringToU32(LPCSTR);
LPSTR NumberToString(
    LPSTR Text, unsigned long long Number, I32 Base, I32 Size, I32 Precision, I32 Type, BOOL IsNegative);
void StringPrintFormatArgs(LPSTR Destination, LPCSTR Format, VarArgList Args);
void StringPrintFormat(LPSTR Destination, LPCSTR Format, ...);
U32 ParseIPAddress(LPCSTR ipStr);

/************************************************************************/
// Functions in System.asm

void MemorySet(LPVOID Destination, U32 What, U32 Size);
void MemoryCopy(LPVOID Destination, LPCVOID Source, U32 Size);
void MemoryMove(LPVOID Destination, LPCVOID Source, U32 Size);
I32 MemoryCompare(LPCVOID First, LPCVOID Second, U32 Size);

/************************************************************************/

#endif  // STRING_H_INCLUDED
