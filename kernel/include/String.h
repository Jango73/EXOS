
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#ifndef STRING_H_INCLUDED
#define STRING_H_INCLUDED

/***************************************************************************/

#include "Base.h"

/***************************************************************************/
// Flags for format printing

#define PF_ZEROPAD 1  /* pad with zero */
#define PF_SIGN 2     /* unsigned/signed long */
#define PF_PLUS 4     /* show plus */
#define PF_SPACE 8    /* space if plus */
#define PF_LEFT 16    /* left justified */
#define PF_SPECIAL 32 /* 0x for hex, 0 for octal*/
#define PF_LARGE 64   /* use 'ABCDEF' instead of 'abcdef' */

/***************************************************************************/

BOOL IsAlpha(STR);
BOOL IsNumeric(STR);
BOOL IsAlphaNumeric(STR);
STR CharToLower(STR);
STR CharToUpper(STR);
BOOL StringEmpty(LPCSTR);
U32 StringLength(LPCSTR);
void StringCopy(LPSTR, LPCSTR);
void StringCopyNum(LPSTR, LPCSTR, U32);
void StringConcat(LPSTR, LPCSTR);
I32 StringCompare(LPCSTR, LPCSTR);
I32 StringCompareNC(LPCSTR, LPCSTR);
LPSTR StringToLower(LPSTR);
LPSTR StringToUpper(LPSTR);
LPSTR StringFindChar(LPCSTR, STR);
LPSTR StringFindCharR(LPCSTR, STR);
void StringInvert(LPSTR);
void MemorySet(LPVOID, U32, U32);
void MemoryCopy(LPVOID, LPVOID, U32);

/***************************************************************************/

void U32ToString(U32, LPSTR);
void U32ToHexString(U32, LPSTR);
U32 HexStringToU32(LPCSTR);
I32 StringToI32(LPCSTR);
U32 StringToU32(LPCSTR);

/***************************************************************************/

LPSTR NumberToString(LPSTR Text, I32 Number, I32 Base, I32 Size, I32 Precision, I32 Type);

/***************************************************************************/

#endif
