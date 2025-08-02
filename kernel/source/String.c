
/***************************************************************************\

    EXOS Kernel
    Copyright (c) 1999-2025 Jango73
    All rights reserved

\***************************************************************************/

#include "String.h"

#include "Base.h"

/***************************************************************************/

BOOL IsAlpha(STR Char) {
    if ((Char >= 'a' && Char <= 'z') || (Char >= 'A' && Char <= 'Z'))
        return TRUE;

    return FALSE;
}

/***************************************************************************/

BOOL IsNumeric(STR Char) {
    if (Char >= '0' && Char <= '9') return TRUE;

    return FALSE;
}

/***************************************************************************/

BOOL IsAlphaNumeric(STR Char) {
    if (IsAlpha(Char) || IsNumeric(Char)) return TRUE;

    return FALSE;
}

/***************************************************************************/

STR CharToLower(STR Char) {
    if (Char >= 'A' && Char <= 'Z') Char = 'a' + (Char - 'A');
    return Char;
}

/***************************************************************************/

STR CharToUpper(STR Char) {
    if (Char >= 'a' && Char <= 'z') Char = 'A' + (Char - 'a');
    return Char;
}

/***************************************************************************/

BOOL StringEmpty(LPCSTR Src) {
    if (Src) {
        return Src[0] == STR_NULL;
    }

    return TRUE;
}

/***************************************************************************/

U32 StringLength(LPCSTR Src) {
    U32 Index = 0;
    U32 Size = 0;

    if (Src) {
        for (Index = 0; Index < MAX_U32; Index++) {
            if (*Src == STR_NULL) break;
            Src++;
            Size++;
        }
    }

    return Size;
}

/***************************************************************************/

void StringCopy(LPSTR Dst, LPCSTR Src) {
    U32 Index;

    if (Dst && Src) {
        for (Index = 0; Index < MAX_U32; Index++) {
            Dst[Index] = Src[Index];
            if (Src[Index] == STR_NULL) break;
        }
    }
}

/***************************************************************************/

void StringCopyNum(LPSTR Dst, LPCSTR Src, U32 Len) {
    U32 Index;

    if (Dst && Src) {
        for (Index = 0; Index < Len; Index++) {
            Dst[Index] = Src[Index];
        }
    }
}

/***************************************************************************/

void StringConcat(LPSTR Dst, LPCSTR Src) {
    LPSTR DstPtr = NULL;
    LPCSTR SrcPtr = NULL;
    U32 Index = 0;

    if (Dst && Src) {
        SrcPtr = Src;
        DstPtr = Dst + StringLength(Dst);
        StringCopy(DstPtr, SrcPtr);
    }
}

/***************************************************************************/

I32 StringCompare(LPCSTR Text1, LPCSTR Text2) {
    REGISTER I8 Result;

    while (1) {
        Result = *Text1 - *Text2;
        if (Result != 0 || *Text1 == STR_NULL) break;
        Text1++;
        Text2++;
    }

    return (I32)Result;
}

/***************************************************************************/

I32 StringCompareNC(LPCSTR Text1, LPCSTR Text2) {
    REGISTER I8 Result;

    while (1) {
        Result = CharToLower(*Text1) - CharToLower(*Text2);
        if (Result != 0 || *Text1 == STR_NULL) break;
        Text1++;
        Text2++;
    }

    return (I32)Result;
}

/***************************************************************************/

LPSTR StringToLower(LPSTR Src) {
    LPSTR SrcPtr = Src;

    if (SrcPtr) {
        while (*SrcPtr) {
            *SrcPtr = CharToLower(*SrcPtr);
            SrcPtr++;
        }
    }

    return Src;
}

/***************************************************************************/

LPSTR StringToUpper(LPSTR Src) {
    LPSTR SrcPtr = Src;

    if (SrcPtr) {
        while (*SrcPtr) {
            *SrcPtr = CharToUpper(*SrcPtr);
            SrcPtr++;
        }
    }

    return Src;
}

/***************************************************************************/

LPSTR StringFindChar(LPCSTR Text, STR Char) {
    for (; *Text != Char; Text++) {
        if (*Text == STR_NULL) return NULL;
    }

    return (LPSTR)Text;
}

/***************************************************************************/

LPSTR StringFindCharR(LPCSTR Text, STR Char) {
    LPCSTR Ptr = Text + StringLength(Text);

    do {
        if (*Ptr == Char) return (LPSTR)Ptr;
    } while (--Ptr >= Text);

    return NULL;
}

/***************************************************************************/

void StringInvert(LPSTR Text) {
    STR Temp[256];
    U32 Length = StringLength(Text);
    U32 Index1 = 0;
    U32 Index2 = Length - 1;

    for (Index1 = 0; Index1 < Length;) {
        Temp[Index1++] = Text[Index2--];
    }

    Temp[Index1] = STR_NULL;

    StringCopy(Text, Temp);
}

/***************************************************************************/

/*
void MemorySet (LPVOID Pointer, U8 Value, U32 Size)
{
  REGISTER U32 Index = 0;
  REGISTER U8* Bytes = (U8*) Pointer;

  for (Index = 0; Index < Size; Index++)
  {
    Bytes[Index] = Value;
  }
}
*/

/***************************************************************************/

/*
void MemoryCopy (LPVOID Dst, LPVOID Src, U32 Size)
{
  REGISTER U32 Index = 0;
  REGISTER U8* DstPtr = (U8*) Dst;
  REGISTER U8* SrcPtr = (U8*) Src;

  for (Index = 0; Index < Size; Index++)
  {
    DstPtr[Index] = SrcPtr[Index];
  }
}
*/

/***************************************************************************/

void U32ToString(U32 Number, LPSTR Text) {
    U32 Index = 0;

    if (Number == 0) {
        Text[0] = '0';
        Text[1] = STR_NULL;
        return;
    }

    while (Number) {
        Text[Index++] = (STR)'0' + (Number % 10);
        Number /= 10;
    }

    Text[Index] = STR_NULL;

    StringInvert(Text);
}

/***************************************************************************/

static STR HexDigitLo[] = "0123456789abcdef";
static STR HexDigitHi[] = "0123456789ABCDEF";

/***************************************************************************/

#define U32_NUM_BITS 32
#define U32_DIGIT_BITS 4
#define U32_NUM_DIGITS (U32_NUM_BITS / U32_DIGIT_BITS)

void U32ToHexString(U32 Number, LPSTR Text) {
    U32 Index = 0;
    U32 Value = 0;
    U32 Shift = U32_NUM_DIGITS - 1;

    if (Text == NULL) return;

    for (Index = 0; Index < U32_NUM_DIGITS; Index++) {
        Value = (Number >> (Shift * U32_DIGIT_BITS)) & 0xF;
        Text[Index] = HexDigitHi[Value];
        Shift--;
    }

    Text[Index] = STR_NULL;
}

/***************************************************************************/

U32 HexStringToU32(LPCSTR Text) {
    U32 c, d, Length, Value, Temp, Shift;

    if (Text[0] != '0') return 0;
    if (Text[1] != 'x' && Text[1] != 'X') return 0;

    Text += 2;
    Length = StringLength(Text);
    if (Length == 0) return 0;

    for (c = 0, Value = 0, Shift = 4 * (Length - 1); c < Length; c++) {
        U32 FoundDigit = 0;
        for (d = 0; d < 16; d++) {
            if (Text[c] == HexDigitLo[d]) {
                Temp = d;
                FoundDigit = 1;
                break;
            }
            if (Text[c] == HexDigitHi[d]) {
                Temp = d;
                FoundDigit = 1;
                break;
            }
        }
        if (FoundDigit == 0) return 0;
        Value += (Temp << Shift);
        Shift -= 4;
    }

    return Value;
}

/***************************************************************************/

I32 StringToI32(LPCSTR Text) {
    I32 Value = 0;
    U32 Index = 0;
    U32 Power = 1;
    STR Data = 0;

    if (Text[0] == STR_NULL) return 0;

    Index = StringLength(Text - 1);

    while (1) {
        Data = Text[Index];
        if (IsNumeric(Data) == 0) return 0;
        Value += (Data - (STR)'0') * Power;
        Power *= 10;
        if (Index == 0) break;
        Index--;
    }

    return Value;
}

/***************************************************************************/

U32 StringToU32(LPCSTR Text) {
    U32 Value = 0;
    U32 Index = 0;
    U32 Power = 1;
    STR Data = 0;

    if (Text[0] == STR_NULL) return 0;

    if (Text[0] == '0' && Text[1] == 'x') return HexStringToU32(Text);
    if (Text[0] == '0' && Text[1] == 'X') return HexStringToU32(Text);

    Index = StringLength(Text) - 1;

    while (1) {
        Data = Text[Index];
        if (IsNumeric(Data) == 0) break;
        Value += (Data - (STR)'0') * Power;
        Power *= 10;
        if (Index == 0) break;
        Index--;
    }

    return Value;
}

/***************************************************************************/

#define DoDiv(n, base)                \
    ({                                \
        int __res;                    \
        __res = ((U32)n) % (U32)Base; \
        n = ((U32)n) / (U32)Base;     \
        __res;                        \
    })

LPSTR NumberToString(LPSTR Text, I32 Number, I32 Base, I32 Size, I32 Precision,
                     I32 Type) {
    STR c, Sign, Temp[66];
    LPCSTR Digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    INT i;

    if (Type & PF_LARGE) Digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    if (Type & PF_LEFT) Type &= ~PF_ZEROPAD;

    if (Base < 2 || Base > 36) return NULL;

    c = (Type & PF_ZEROPAD) ? '0' : ' ';
    Sign = 0;

    if (Type & PF_SIGN) {
        if (Number < 0) {
            Sign = '-';
            Number = -Number;
            Size--;
        } else if (Type & PF_PLUS) {
            Sign = '+';
            Size--;
        } else if (Type & PF_SPACE) {
            Sign = ' ';
            Size--;
        }
    }

    if (Type & PF_SPECIAL) {
        if (Base == 16)
            Size -= 2;
        else if (Base == 8)
            Size--;
    }

    i = 0;

    if (Number == 0)
        Temp[i++] = '0';
    else
        while (Number != 0) Temp[i++] = Digits[DoDiv(Number, Base)];

    if (i > Precision) Precision = i;

    Size -= Precision;

    if (!(Type & (PF_ZEROPAD | PF_LEFT))) {
        while (Size-- > 0) *Text++ = ' ';
    }

    if (Sign) *Text++ = Sign;

    if (Type & PF_SPECIAL) {
        if (Base == 8)
            *Text++ = '0';
        else if (Base == 16) {
            *Text++ = '0';
            *Text++ = Digits[33];
        }
    }

    if (!(Type & PF_LEFT)) {
        while (Size-- > 0) *Text++ = c;
    }

    while (i < Precision--) *Text++ = '0';
    while (i-- > 0) *Text++ = Temp[i];
    while (Size-- > 0) *Text++ = STR_SPACE;

    *Text++ = STR_NULL;

    return Text;
}

/***************************************************************************/
